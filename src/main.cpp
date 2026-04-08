#include "app.h"
#include "ui_panels.h"

#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <cstdio>
#include <string>

// ── GLFW error callback ─────────────────────────────────────────────────────

static void glfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// ── Keyboard shortcut handling ──────────────────────────────────────────────

static void handleShortcuts(GLFWwindow* window, App& app) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) return; // ImGui has focus

    bool ctrl = io.KeyCtrl;

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
        // Trigger open dialog — handled in UI
        // We send a synthetic "show open" by directly opening the image
        // if a path is on the clipboard, or show dialog
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        app.undo();
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
        app.redo();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F)) {
        ImVec2 ds = io.DisplaySize;
        app.viewport.fitToView(ds.x - 340, ds.y - 60);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_1)) {
        app.viewport.setZoom(1.0f);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_2)) {
        app.viewport.setZoom(2.0f);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (app.toolMode == ToolMode::Crop) {
            app.cropTool.reset();
            app.toolMode = ToolMode::Pan;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        if (app.toolMode == ToolMode::Crop && app.cropTool.hasCrop()) {
            app.applyCrop();
            app.toolMode = ToolMode::Pan;
        }
    }
}

// ── Drag-and-drop support ───────────────────────────────────────────────────

static App* g_app = nullptr;

static void dropCallback(GLFWwindow* window, int count, const char** paths) {
    if (count > 0 && g_app) {
        if (g_app->openImage(paths[0])) {
            int w, h;
            glfwGetWindowSize(window, &w, &h);
            g_app->viewport.fitToView(
                static_cast<float>(w) - 340,
                static_cast<float>(h) - 60);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    // ── GLFW Init ───────────────────────────────────────────────────────
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) return 1;

    // Request OpenGL 2.1 (maximum compatibility)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

    GLFWwindow* window = glfwCreateWindow(1280, 800, "AI Photo Editor", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    // ── ImGui Init ──────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // NavEnableKeyboard is intentionally omitted: it causes WantCaptureKeyboard
    // to be true even when no widget is focused, blocking all keyboard shortcuts.

    // Style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 0.0f;
    style.FrameRounding     = 3.0f;
    style.GrabRounding      = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.WindowBorderSize  = 0.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
    style.Colors[ImGuiCol_Header]   = ImVec4(0.20f, 0.22f, 0.27f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.30f, 0.38f, 1.0f);
    style.Colors[ImGuiCol_Button]   = ImVec4(0.22f, 0.24f, 0.30f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.34f, 0.42f, 1.0f);
    style.Colors[ImGuiCol_FrameBg]  = ImVec4(0.16f, 0.17f, 0.20f, 1.0f);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 120");

    // ── Application ─────────────────────────────────────────────────────
    App app;
    g_app = &app;
    glfwSetDropCallback(window, dropCallback);

    // Open file from command line argument
    if (argc > 1) {
        if (app.openImage(argv[1])) {
            app.viewport.fitToView(1280 - 340, 800 - 60);
        }
    }

    // ── Main Loop ───────────────────────────────────────────────────────
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.pollAI();
        handleShortcuts(window, app);
        ui::drawAll(app);

        // Rendering
        ImGui::Render();
        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Draw the image viewport (OpenGL immediate mode, behind ImGui)
        // This is handled inside ui::drawViewport via app.viewport.render()

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // ── Cleanup ─────────────────────────────────────────────────────────
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
