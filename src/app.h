#pragma once

#include "image.h"
#include "viewport.h"
#include "adjustments.h"
#include "ai_processor.h"
#include "crop_tool.h"
#include <string>
#include <vector>
#include <deque>
#include <thread>
#include <atomic>

enum class ToolMode {
    Pan,
    Crop
};

// Central application state. Owns all subsystems and image data.
class App {
public:
    App();
    ~App();

    // ── File I/O ────────────────────────────────────────────────────────
    bool openImage(const std::string& path);
    bool saveImage(const std::string& path);

    // ── Image pipeline ──────────────────────────────────────────────────
    // The "working" image is the base; adjustments are applied on top
    // to produce the display image.
    void rebuildDisplay();          // re-apply current adjustments
    void bakeAdjustments();         // flatten adjustments into working image
    void commitWorkingImage(const Image& img); // set new base (pushes undo)

    // ── Undo / Redo ─────────────────────────────────────────────────────
    void pushUndo();
    void undo();
    void redo();
    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    // ── AI operations (run on background thread, push undo on completion)
    void aiUpscale(int scale);
    void aiDenoise(float strength);
    void aiSharpen(float amount);
    void aiFaceRestore();
    void aiEnhance();

    // Call every frame to check if a background AI task has finished
    void pollAI();

    // ── Crop ────────────────────────────────────────────────────────────
    void applyCrop();

    // ── Public state ────────────────────────────────────────────────────
    Image            workingImage;   // base image (pre-adjustments)
    Image            displayImage;   // after adjustments (what is shown)
    Viewport         viewport;
    AdjustmentParams adjustments;
    AIProcessor      aiProcessor;
    CropTool         cropTool;
    ToolMode         toolMode = ToolMode::Pan;

    std::string      filePath;
    std::string      statusMsg;
    bool             imageLoaded = false;
    bool             adjustmentsDirty = true;
    std::atomic<float> aiProgress{0.0f};
    bool             aiRunning  = false;

    static constexpr int MAX_UNDO = 20;

private:
    std::deque<Image>  undoStack_;
    std::deque<Image>  redoStack_;

    // Background AI thread state
    std::thread      aiThread_;
    Image            aiResult_;
    std::string      aiStatus_;
    std::atomic<bool> aiDone_{false};
};
