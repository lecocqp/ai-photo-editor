#pragma once

class App;
struct ImDrawList;

namespace ui {

// Call once per frame inside the ImGui NewFrame/Render block.
void drawAll(App& app);

// Individual panels (called by drawAll)
void drawMenuBar(App& app);
void drawToolbar(App& app);
void drawAdjustmentsPanel(App& app);
void drawAIPanel(App& app);
void drawViewport(App& app);
void drawStatusBar(App& app);
void drawCropOverlay(App& app, ImDrawList* drawList);


} // namespace ui
