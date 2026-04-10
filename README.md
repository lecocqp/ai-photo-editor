# AI Photo Editor

 APE (AI Photo Editor) is a lightweight photo editor built in C++ with a focus on AI-powered photo enhancements. It supports basic image adjustments as well as advanced operations like upscaling and face restoration using ONNX Runtime with DirectML for GPU acceleration. 

This project is being developed mainly for educational purposes to explore how AI image processing models can be integrated in C++ into a desktop application. This project also serves as a playground to see how far Claude can assist in code generation and project structuring for a non-trivial application.


## Features

- **Image adjustments:** exposure, contrast, brightness, highlights, shadows, whites, blacks, gamma, saturation, vibrance, temperature, tint, hue shift, clarity, and sharpening
- **AI-processing operations** (via ONNX Runtime + DirectML):
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

- `models/` and `libs/` are excluded from the repo (large binaries). AI features require you to supply the ONNX Runtime and model files separately. See README.txt in models folder for details.
- This is an exploratory/learning project, not production software.