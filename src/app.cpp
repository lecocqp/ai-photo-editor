#include "app.h"
#include <filesystem>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

// Get the directory containing the executable
static fs::path getExeDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
#else
    return fs::canonical("/proc/self/exe").parent_path();
#endif
}

App::App() {
    // Search for models/ relative to the executable (handles build/Debug/ layout)
    fs::path exeDir = getExeDir();
    for (auto dir = exeDir; dir.has_parent_path() && dir != dir.root_path(); dir = dir.parent_path()) {
        fs::path candidate = dir / "models";
        if (fs::exists(candidate) && fs::is_directory(candidate)) {
            aiProcessor.setModelDir(candidate.string());
            break;
        }
    }
}

// ── File I/O ────────────────────────────────────────────────────────────────

bool App::openImage(const std::string& path) {
    Image img;
    if (!img.load(path)) {
        statusMsg = "Failed to open: " + path;
        return false;
    }

    workingImage = std::move(img);
    filePath     = path;
    imageLoaded  = true;

    // Reset state
    adjustments.reset();
    cropTool.reset();
    undoStack_.clear();
    redoStack_.clear();
    adjustmentsDirty = true;

    rebuildDisplay();
    viewport.uploadImage(displayImage);

    statusMsg = "Opened: " + fs::path(path).filename().string() +
                " (" + std::to_string(workingImage.width()) + "x" +
                std::to_string(workingImage.height()) + ")";
    return true;
}

bool App::saveImage(const std::string& path) {
    rebuildDisplay();
    if (!displayImage.save(path)) {
        statusMsg = "Failed to save: " + path;
        return false;
    }
    statusMsg = "Saved: " + fs::path(path).filename().string();
    return true;
}

// ── Image pipeline ──────────────────────────────────────────────────────────

void App::rebuildDisplay() {
    if (workingImage.empty()) return;

    if (adjustments.isDefault()) {
        displayImage = workingImage.clone();
    } else {
        displayImage = Image(workingImage.width(), workingImage.height());
        applyAdjustments(workingImage, displayImage, adjustments);
    }
    adjustmentsDirty = false;
}

void App::bakeAdjustments() {
    if (adjustments.isDefault()) return;
    pushUndo();
    rebuildDisplay();
    workingImage = displayImage.clone();
    adjustments.reset();
    adjustmentsDirty = false;
    statusMsg = "Adjustments baked into image";
}

void App::commitWorkingImage(const Image& img) {
    pushUndo();
    workingImage = img.clone();
    adjustmentsDirty = true;
    rebuildDisplay();
    viewport.uploadImage(displayImage);
    viewport.fitToView(viewport.viewW(), viewport.viewH());
}

// ── Undo / Redo ─────────────────────────────────────────────────────────────

void App::pushUndo() {
    undoStack_.push_back(workingImage.clone());
    if (static_cast<int>(undoStack_.size()) > MAX_UNDO) {
        undoStack_.pop_front();
    }
    redoStack_.clear(); // new action invalidates redo
}

void App::undo() {
    if (undoStack_.empty()) return;
    redoStack_.push_back(workingImage.clone());
    workingImage = std::move(undoStack_.back());
    undoStack_.pop_back();
    adjustmentsDirty = true;
    rebuildDisplay();
    viewport.uploadImage(displayImage);
    statusMsg = "Undo";
}

void App::redo() {
    if (redoStack_.empty()) return;
    undoStack_.push_back(workingImage.clone());
    workingImage = std::move(redoStack_.back());
    redoStack_.pop_back();
    adjustmentsDirty = true;
    rebuildDisplay();
    viewport.uploadImage(displayImage);
    statusMsg = "Redo";
}

// ── AI operations (background-threaded) ─────────────────────────────────────

App::~App() {
    if (aiThread_.joinable()) aiThread_.join();
}

void App::pollAI() {
    if (!aiRunning || !aiDone_.load()) return;

    aiThread_.join();
    if (!aiResult_.empty()) {
        commitWorkingImage(aiResult_);
    }
    aiResult_ = Image();
    statusMsg  = aiStatus_;
    aiRunning  = false;
    aiProgress = 0.0f;
    aiDone_    = false;
}

void App::aiUpscale(int scale) {
    if (!imageLoaded || aiRunning) return;
    if (!adjustments.isDefault()) bakeAdjustments();

    aiRunning  = true;
    aiProgress = 0.0f;
    aiDone_    = false;
    statusMsg  = "Upscaling...";

    // Copy the working image for the thread (thread must not touch UI state)
    Image input = workingImage.clone();
    aiThread_ = std::thread([this, input = std::move(input), scale]() mutable {
        std::string status;
        aiResult_ = aiProcessor.upscale(input, scale, status,
            [this](float p) { aiProgress.store(p); return true; });
        aiStatus_ = status;
        aiDone_.store(true);
    });
}

void App::aiDenoise(float strength) {
    if (!imageLoaded || aiRunning) return;
    if (!adjustments.isDefault()) bakeAdjustments();

    aiRunning  = true;
    aiProgress = 0.0f;
    aiDone_    = false;
    statusMsg  = "Denoising...";

    Image input = workingImage.clone();
    aiThread_ = std::thread([this, input = std::move(input), strength]() mutable {
        std::string status;
        aiResult_ = aiProcessor.denoise(input, strength, status,
            [this](float p) { aiProgress.store(p); return true; });
        aiStatus_ = status;
        aiDone_.store(true);
    });
}

void App::aiSharpen(float amount) {
    if (!imageLoaded || aiRunning) return;
    if (!adjustments.isDefault()) bakeAdjustments();

    aiRunning  = true;
    aiProgress = 0.0f;
    aiDone_    = false;
    statusMsg  = "Sharpening...";

    Image input = workingImage.clone();
    aiThread_ = std::thread([this, input = std::move(input), amount]() mutable {
        std::string status;
        aiResult_ = aiProcessor.sharpen(input, amount, status,
            [this](float p) { aiProgress.store(p); return true; });
        aiStatus_ = status;
        aiDone_.store(true);
    });
}

void App::aiFaceRestore() {
    if (!imageLoaded || aiRunning) return;
    if (!adjustments.isDefault()) bakeAdjustments();

    aiRunning  = true;
    aiProgress = 0.0f;
    aiDone_    = false;
    statusMsg  = "Restoring faces...";

    Image input = workingImage.clone();
    aiThread_ = std::thread([this, input = std::move(input)]() mutable {
        std::string status;
        aiResult_ = aiProcessor.faceRestore(input, status,
            [this](float p) { aiProgress.store(p); return true; });
        aiStatus_ = status;
        aiDone_.store(true);
    });
}

void App::aiEnhance() {
    if (!imageLoaded || aiRunning) return;
    if (!adjustments.isDefault()) bakeAdjustments();

    aiRunning  = true;
    aiProgress = 0.0f;
    aiDone_    = false;
    statusMsg  = "Enhancing...";

    Image input = workingImage.clone();
    aiThread_ = std::thread([this, input = std::move(input)]() mutable {
        std::string status;
        aiResult_ = aiProcessor.enhance(input, status,
            [this](float p) { aiProgress.store(p); return true; });
        aiStatus_ = status;
        aiDone_.store(true);
    });
}

// ── Crop ────────────────────────────────────────────────────────────────────

void App::applyCrop() {
    if (!cropTool.hasCrop()) return;
    Image cropped = cropTool.apply(workingImage);
    if (!cropped.empty()) {
        commitWorkingImage(cropped);
        cropTool.reset();
        statusMsg = "Crop applied (" + std::to_string(workingImage.width()) +
                    "x" + std::to_string(workingImage.height()) + ")";
    }
}
