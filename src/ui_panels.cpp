#include "ui_panels.h"
#include "app.h"
#include "imgui.h"
#include "ImGuiFileDialog.h"
#include <filesystem>
#include <string>
#include <algorithm>

namespace fs = std::filesystem;

// ── Slider helper: label + reset button ─────────────────────────────────────
static bool SliderWithReset(const char* label, float* v, float lo, float hi,
                            float resetVal = 0.0f, const char* fmt = "%.2f") {
    bool changed = ImGui::SliderFloat(label, v, lo, hi, fmt);
    ImGui::SameLine();
    std::string btnId = std::string("R##") + label;
    if (ImGui::SmallButton(btnId.c_str())) {
        *v = resetVal;
        changed = true;
    }
    return changed;
}

// ═══════════════════════════════════════════════════════════════════════════
// drawAll
// ═══════════════════════════════════════════════════════════════════════════

void ui::drawAll(App& app) {
    drawMenuBar(app);

    // Dockspace-like layout using fixed windows
    float menuH = ImGui::GetFrameHeight();
    float statusH = ImGui::GetFrameHeight() + 8;
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    float toolbarW = 60;
    float panelW   = 280;
    float viewY    = menuH;
    float viewH    = displaySize.y - menuH - statusH;

    // Left toolbar
    ImGui::SetNextWindowPos(ImVec2(0, viewY));
    ImGui::SetNextWindowSize(ImVec2(toolbarW, viewH));
    drawToolbar(app);

    // Right panel
    ImGui::SetNextWindowPos(ImVec2(displaySize.x - panelW, viewY));
    ImGui::SetNextWindowSize(ImVec2(panelW, viewH));
    ImGui::Begin("Properties", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse);
    drawAdjustmentsPanel(app);
    ImGui::Separator();
    drawAIPanel(app);
    ImGui::End();

    // Center viewport
    float vpX = toolbarW;
    float vpW = displaySize.x - toolbarW - panelW;
    ImGui::SetNextWindowPos(ImVec2(vpX, viewY));
    ImGui::SetNextWindowSize(ImVec2(vpW, viewH));
    drawViewport(app);

    // Status bar
    ImGui::SetNextWindowPos(ImVec2(0, displaySize.y - statusH));
    ImGui::SetNextWindowSize(ImVec2(displaySize.x, statusH));
    drawStatusBar(app);

    // File dialogs
    ImVec2 dlgMin(600, 400);
    ImVec2 dlgMax(FLT_MAX, FLT_MAX);

    if (IGFD::FileDialog::Instance()->Display("OpenImageDlg",
            ImGuiWindowFlags_NoCollapse, dlgMin, dlgMax)) {
        if (IGFD::FileDialog::Instance()->IsOk()) {
            std::string path = IGFD::FileDialog::Instance()->GetFilePathName();
            if (app.openImage(path)) {
                ImVec2 ds = ImGui::GetIO().DisplaySize;
                app.viewport.fitToView(ds.x - 340, ds.y - 60);
            }
        }
        IGFD::FileDialog::Instance()->Close();
    }

    if (IGFD::FileDialog::Instance()->Display("SaveImageDlg",
            ImGuiWindowFlags_NoCollapse, dlgMin, dlgMax)) {
        if (IGFD::FileDialog::Instance()->IsOk()) {
            std::string path = IGFD::FileDialog::Instance()->GetFilePathName();
            app.saveImage(path);
        }
        IGFD::FileDialog::Instance()->Close();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Menu Bar
// ═══════════════════════════════════════════════════════════════════════════

void ui::drawMenuBar(App& app) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                IGFD::FileDialogConfig cfg;
                cfg.path  = ".";
                cfg.flags = ImGuiFileDialogFlags_Modal;
                IGFD::FileDialog::Instance()->OpenDialog(
                    "OpenImageDlg", "Open Image",
                    "Image files{.png,.jpg,.jpeg,.bmp,.tif,.tiff,.exr},.*", cfg);
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+S", false, app.imageLoaded)) {
                IGFD::FileDialogConfig cfg;
                cfg.path     = app.filePath.empty() ? "."
                    : fs::path(app.filePath).parent_path().string();
                cfg.fileName = app.filePath.empty() ? ""
                    : fs::path(app.filePath).filename().string();
                cfg.flags = ImGuiFileDialogFlags_Modal
                          | ImGuiFileDialogFlags_ConfirmOverwrite;
                IGFD::FileDialog::Instance()->OpenDialog(
                    "SaveImageDlg", "Save Image As",
                    "PNG{.png},JPEG{.jpg,.jpeg},TIFF{.tif,.tiff},EXR{.exr},BMP{.bmp}", cfg);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Alt+F4")) {
                // Handled in main loop via glfwSetWindowShouldClose
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, app.canUndo()))
                app.undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, app.canRedo()))
                app.redo();
            ImGui::Separator();
            if (ImGui::MenuItem("Bake Adjustments", nullptr, false,
                                app.imageLoaded && !app.adjustments.isDefault()))
                app.bakeAdjustments();
            if (ImGui::MenuItem("Reset Adjustments", nullptr, false,
                                app.imageLoaded)) {
                app.adjustments.reset();
                app.adjustmentsDirty = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Fit to Window", "F"))
                app.viewport.fitToView(
                    ImGui::GetIO().DisplaySize.x - 340,
                    ImGui::GetIO().DisplaySize.y - 60);
            if (ImGui::MenuItem("Zoom 100%", "1"))
                app.viewport.setZoom(1.0f);
            if (ImGui::MenuItem("Zoom 200%", "2"))
                app.viewport.setZoom(2.0f);
            if (ImGui::MenuItem("Zoom 50%"))
                app.viewport.setZoom(0.5f);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Toolbar
// ═══════════════════════════════════════════════════════════════════════════

void ui::drawToolbar(App& app) {
    ImGui::Begin("Tools", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    ImGui::Text("Tools");
    ImGui::Separator();

    bool isPan  = (app.toolMode == ToolMode::Pan);
    bool isCrop = (app.toolMode == ToolMode::Crop);

    if (isPan) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1));
    if (ImGui::Button("Pan", ImVec2(44, 32)))
        app.toolMode = ToolMode::Pan;
    if (isPan) ImGui::PopStyleColor();

    if (isCrop) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1));
    if (ImGui::Button("Crop", ImVec2(44, 32)))
        app.toolMode = ToolMode::Crop;
    if (isCrop) ImGui::PopStyleColor();

    if (app.toolMode == ToolMode::Crop && app.cropTool.hasCrop()) {
        ImGui::Separator();
        // Aspect ratio selector
        const char* aspects[] = {"Free", "1:1", "4:3", "16:9", "3:2"};
        int curAspect = static_cast<int>(app.cropTool.aspectRatio());
        if (ImGui::Combo("##AR", &curAspect, aspects, 5)) {
            app.cropTool.setAspectRatio(static_cast<AspectRatio>(curAspect));
        }
        if (ImGui::Button("Apply", ImVec2(44, 24))) {
            app.applyCrop();
        }
        if (ImGui::Button("Cancel", ImVec2(44, 24))) {
            app.cropTool.reset();
        }
    }

    ImGui::End();
}

// ═══════════════════════════════════════════════════════════════════════════
// Adjustments Panel
// ═══════════════════════════════════════════════════════════════════════════

void ui::drawAdjustmentsPanel(App& app) {
    if (!ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    // Reserve space for label text + reset button + margin so nothing gets clipped
    float resetBtnW = ImGui::CalcTextSize("R").x + ImGui::GetStyle().FramePadding.x * 2 + 4;
    float labelW = ImGui::CalcTextSize("Temperature").x + 8; // widest label + spacing
    float margin = 8.0f;
    ImGui::PushItemWidth(-(labelW + resetBtnW + margin));

    AdjustmentParams& adj = app.adjustments;
    bool changed = false;

    changed |= SliderWithReset("Exposure",   &adj.exposure,   -5.0f, 5.0f);
    changed |= SliderWithReset("Contrast",   &adj.contrast,   -1.0f, 1.0f);
    changed |= SliderWithReset("Brightness", &adj.brightness, -1.0f, 1.0f);
    changed |= SliderWithReset("Highlights", &adj.highlights, -1.0f, 1.0f);
    changed |= SliderWithReset("Shadows",    &adj.shadows,    -1.0f, 1.0f);
    changed |= SliderWithReset("Whites",     &adj.whites,     -1.0f, 1.0f);
    changed |= SliderWithReset("Blacks",     &adj.blacks,     -1.0f, 1.0f);
    changed |= SliderWithReset("Gamma",      &adj.gamma,       0.1f, 3.0f, 1.0f);

    if (ImGui::CollapsingHeader("Color", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= SliderWithReset("Saturation",  &adj.saturation,  -1.0f, 1.0f);
        changed |= SliderWithReset("Vibrance",    &adj.vibrance,    -1.0f, 1.0f);
        changed |= SliderWithReset("Temperature", &adj.temperature, -1.0f, 1.0f);
        changed |= SliderWithReset("Tint",        &adj.tint,        -1.0f, 1.0f);
        changed |= SliderWithReset("Hue Shift",   &adj.hueShift,    -180.0f, 180.0f);
    }

    if (ImGui::CollapsingHeader("Detail", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= SliderWithReset("Clarity", &adj.clarity, -1.0f, 1.0f);
        changed |= SliderWithReset("Sharpen", &adj.sharpen,  0.0f, 2.0f);
    }

    ImGui::PopItemWidth();

    if (changed) {
        app.adjustmentsDirty = true;
    }

    if (ImGui::Button("Reset All")) {
        adj.reset();
        app.adjustmentsDirty = true;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// AI Panel
// ═══════════════════════════════════════════════════════════════════════════

void ui::drawAIPanel(App& app) {
    if (!ImGui::CollapsingHeader("AI Tools", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    // GPU toggle
    if (app.aiProcessor.gpuAvailable()) {
        bool gpu = app.aiProcessor.usingGPU();
        if (ImGui::Checkbox("GPU (DirectML)", &gpu)) {
            app.aiProcessor.setUseGPU(gpu);
        }
    }

    bool disabled = !app.imageLoaded || app.aiRunning;
    if (disabled) ImGui::BeginDisabled();

    // Upscale
    static int upscaleFactor = 2;
    ImGui::Text("Upscale");
    ImGui::SameLine();
    ImGui::RadioButton("2x", &upscaleFactor, 2);
    ImGui::SameLine();
    ImGui::RadioButton("4x", &upscaleFactor, 4);
    ImGui::SameLine();
    if (ImGui::Button("Go##up")) app.aiUpscale(upscaleFactor);

    // Reserve space for "Go" button + label + margin
    float goBtnW = ImGui::CalcTextSize("Go").x + ImGui::GetStyle().FramePadding.x * 2 + 4;
    float aiLabelW = ImGui::CalcTextSize("AI Sharpen").x + 8;
    float aiMargin = 8.0f;
    ImGui::PushItemWidth(-(aiLabelW + goBtnW + aiMargin));

    // Denoise
    static float denoiseStr = 0.5f;
    ImGui::SliderFloat("Denoise##str", &denoiseStr, 0.1f, 1.0f);
    ImGui::SameLine();
    if (ImGui::Button("Go##dn")) app.aiDenoise(denoiseStr);

    // Sharpen
    static float sharpenAmt = 1.0f;
    ImGui::SliderFloat("AI Sharpen##amt", &sharpenAmt, 0.1f, 3.0f);
    ImGui::SameLine();
    if (ImGui::Button("Go##sh")) app.aiSharpen(sharpenAmt);

    ImGui::PopItemWidth();

    // Face Restore
    if (ImGui::Button("Face Restore")) app.aiFaceRestore();

    // Enhance
    ImGui::SameLine();
    if (ImGui::Button("Auto Enhance")) app.aiEnhance();

    if (disabled) ImGui::EndDisabled();

    // Progress bar
    if (app.aiRunning) {
        float p = app.aiProgress.load();
        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "%d%%", static_cast<int>(p * 100));
        ImGui::ProgressBar(p, ImVec2(-1, 0), overlay);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Viewport
// ═══════════════════════════════════════════════════════════════════════════

void ui::drawViewport(App& app) {
    ImGuiWindowFlags vpFlags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::Begin("Viewport", nullptr, vpFlags);

    ImVec2 pos  = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    float vx = pos.x, vy = pos.y, vw = size.x, vh = size.y;

    // Handle rebuilt display
    if (app.imageLoaded && app.adjustmentsDirty) {
        app.rebuildDisplay();
        app.viewport.uploadImage(app.displayImage);
    }

    // Set viewport rect for coordinate transforms
    app.viewport.setViewRect(vx, vy, vw, vh);

    // Render the image via ImGui draw list (so it's drawn at the right time)
    if (app.viewport.hasImage()) {
        float x0s, y0s, x1s, y1s;
        app.viewport.imageToScreen(0.0f, 0.0f, x0s, y0s);
        app.viewport.imageToScreen(
            static_cast<float>(app.viewport.imgWidth()),
            static_cast<float>(app.viewport.imgHeight()), x1s, y1s);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddImage(
            (ImTextureID)(uintptr_t)app.viewport.textureId(),
            ImVec2(x0s, y0s), ImVec2(x1s, y1s));
    }

    // ── Input handling ──────────────────────────────────────────────────
    if (ImGui::IsWindowHovered()) {
        ImGuiIO& io = ImGui::GetIO();

        // Zoom with scroll wheel
        if (io.MouseWheel != 0) {
            app.viewport.zoom(io.MouseWheel, io.MousePos.x, io.MousePos.y);
        }

        if (app.toolMode == ToolMode::Pan) {
            // Pan with middle mouse or left mouse drag
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 delta = io.MouseDelta;
                app.viewport.pan(delta.x, delta.y);
            }
        }
        else if (app.toolMode == ToolMode::Crop && app.imageLoaded) {
            float imgX, imgY;
            app.viewport.screenToImage(io.MousePos.x, io.MousePos.y, imgX, imgY);

            float handleR = 8.0f / app.viewport.zoomLevel(); // handle radius in image space

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                int handle = app.cropTool.hitTestHandle(imgX, imgY, handleR);
                if (handle >= 0) {
                    app.cropTool.beginHandleDrag(handle, imgX, imgY);
                } else {
                    app.cropTool.begin(imgX, imgY);
                }
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                if (app.cropTool.isActive()) {
                    if (app.cropTool.hitTestHandle(imgX, imgY, handleR) >= 0 ||
                        app.cropTool.isActive()) {
                        app.cropTool.updateHandleDrag(imgX, imgY);
                    }
                    app.cropTool.update(imgX, imgY);
                }
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                app.cropTool.end();
            }

            // Middle mouse still pans
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                ImVec2 delta = io.MouseDelta;
                app.viewport.pan(delta.x, delta.y);
            }
        }
    }

    // Draw crop overlay
    if (app.cropTool.hasCrop()) {
        drawCropOverlay(app, ImGui::GetWindowDrawList());
    }

    // Show message if no image
    if (!app.imageLoaded) {
        ImVec2 center(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
        const char* msg = "Open an image (Ctrl+O)";
        ImVec2 textSize = ImGui::CalcTextSize(msg);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
            IM_COL32(180, 180, 180, 255), msg);
    }

    ImGui::End();
}

// ═══════════════════════════════════════════════════════════════════════════
// Crop Overlay
// ═══════════════════════════════════════════════════════════════════════════

void ui::drawCropOverlay(App& app, ImDrawList* dl) {
    if (!app.cropTool.hasCrop()) return;

    float cx = app.cropTool.x(), cy = app.cropTool.y();
    float cw = app.cropTool.width(), ch = app.cropTool.height();

    // Convert crop rect corners to screen space
    float s0x, s0y, s1x, s1y;
    app.viewport.imageToScreen(cx, cy, s0x, s0y);
    app.viewport.imageToScreen(cx + cw, cy + ch, s1x, s1y);

    // Dim area outside crop
    ImU32 dimColor = IM_COL32(0, 0, 0, 120);
    float vx = 0, vy = 0;
    ImVec2 ds = ImGui::GetIO().DisplaySize;

    // Top
    dl->AddRectFilled(ImVec2(vx, vy), ImVec2(ds.x, s0y), dimColor);
    // Bottom
    dl->AddRectFilled(ImVec2(vx, s1y), ImVec2(ds.x, ds.y), dimColor);
    // Left
    dl->AddRectFilled(ImVec2(vx, s0y), ImVec2(s0x, s1y), dimColor);
    // Right
    dl->AddRectFilled(ImVec2(s1x, s0y), ImVec2(ds.x, s1y), dimColor);

    // Crop border
    dl->AddRect(ImVec2(s0x, s0y), ImVec2(s1x, s1y),
                IM_COL32(255, 255, 255, 220), 0, 0, 2.0f);

    // Rule-of-thirds grid
    ImU32 gridColor = IM_COL32(255, 255, 255, 80);
    float tw = (s1x - s0x) / 3.0f, th = (s1y - s0y) / 3.0f;
    for (int i = 1; i <= 2; ++i) {
        dl->AddLine(ImVec2(s0x + tw * i, s0y), ImVec2(s0x + tw * i, s1y), gridColor);
        dl->AddLine(ImVec2(s0x, s0y + th * i), ImVec2(s1x, s0y + th * i), gridColor);
    }

    // Handles (corner squares)
    float hs = 5.0f;
    ImU32 handleColor = IM_COL32(255, 255, 255, 255);
    auto drawHandle = [&](float sx, float sy) {
        dl->AddRectFilled(ImVec2(sx - hs, sy - hs), ImVec2(sx + hs, sy + hs),
                          handleColor);
    };
    drawHandle(s0x, s0y);
    drawHandle(s1x, s0y);
    drawHandle(s1x, s1y);
    drawHandle(s0x, s1y);
    // Edge midpoints
    drawHandle((s0x+s1x)*0.5f, s0y);
    drawHandle(s1x, (s0y+s1y)*0.5f);
    drawHandle((s0x+s1x)*0.5f, s1y);
    drawHandle(s0x, (s0y+s1y)*0.5f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Status Bar
// ═══════════════════════════════════════════════════════════════════════════

void ui::drawStatusBar(App& app) {
    ImGui::Begin("##status", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoScrollbar);

    // Left: status message
    ImGui::Text("%s", app.statusMsg.c_str());

    // Right: image info + zoom
    if (app.imageLoaded) {
        char info[256];
        std::snprintf(info, sizeof(info), "%dx%d | %.0f%%",
                      app.workingImage.width(), app.workingImage.height(),
                      app.viewport.zoomLevel() * 100.0f);
        float textW = ImGui::CalcTextSize(info).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - textW - 16);
        ImGui::Text("%s", info);
    }

    ImGui::End();
}

