# AI Photo Editor

An exploratory desktop photo editor built in C++ as an exercise in AI-assisted development using the [Claude](https://claude.ai) coding assistant. The goal was to prototype a non-trivial native application — from scratch — with Claude driving the implementation.

## About

This project explores how far an AI coding assistant can take a real, multi-subsystem C++ application. Everything from the image pipeline and UI layout to the AI inference integration was written collaboratively with Claude Code.

## Features

- **Image adjustments** — exposure, contrast, brightness, highlights, shadows, whites, blacks, gamma, saturation, vibrance, temperature, tint, hue shift, clarity, and sharpening
- **AI-powered operations** (via ONNX Runtime + DirectML):
  - Upscaling (Real-ESRGAN)
  - Face restoration (GFPGAN)
  - Denoise, sharpen, and auto-enhance
- **Crop tool** with interactive viewport manipulation
- **Undo / redo** (up to 20 levels)
- **Pan & zoom** viewport
- Immediate-mode UI built with [Dear ImGui](https://github.com/ocornut/imgui)

## Tech Stack

| Component | Library |
|---|---|
| UI | Dear ImGui 1.91.8 + GLFW + OpenGL 3 |
| Image I/O | OpenImageIO |
| AI inference | ONNX Runtime with DirectML (GPU) |
| Build | CMake + vcpkg |
| Language | C++17 |

## Building

### Prerequisites

- Windows 10/11 (DirectML is Windows-only)
- CMake 3.20+
- [vcpkg](https://vcpkg.io) with the manifest mode (handled automatically)
- ONNX Runtime DirectML package in `libs/onnxruntime-directml/` (optional — AI features are disabled gracefully if absent)
- AI model files in `models/` (not included in the repo due to size)

### Steps

```bash
cmake --preset <your-preset>   # see CMakePresets.json
cmake --build build
```

## Notes

- `models/` and `libs/` are excluded from the repo (large binaries). AI features require you to supply the ONNX Runtime and model files separately.
- This is an exploratory/learning project, not production software.