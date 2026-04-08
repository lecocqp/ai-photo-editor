#pragma once

#include "image.h"
#include <string>
#include <functional>
#include <memory>
#include <unordered_map>

#if HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

// Callback: progress 0.0–1.0, return false to cancel
using ProgressCallback = std::function<bool(float)>;

class AIProcessor {
public:
    AIProcessor();
    ~AIProcessor();

    // Set the directory containing .onnx model files
    void setModelDir(const std::string& dir) { modelDir_ = dir; }

    // GPU execution provider control
    void setUseGPU(bool enabled);
    bool gpuAvailable() const { return gpuAvailable_; }
    bool usingGPU() const { return useGPU_; }

    // ── AI Operations ───────────────────────────────────────────────────
    // Each returns a new image (or empty image on failure).
    // `status` receives a human-readable result message.

    Image upscale(const Image& src, int scale, std::string& status,
                  ProgressCallback progress = nullptr);

    Image denoise(const Image& src, float strength, std::string& status,
                  ProgressCallback progress = nullptr);

    Image sharpen(const Image& src, float amount, std::string& status,
                  ProgressCallback progress = nullptr);

    Image faceRestore(const Image& src, std::string& status,
                      ProgressCallback progress = nullptr);

    Image enhance(const Image& src, std::string& status,
                  ProgressCallback progress = nullptr);

    bool hasModel(const std::string& name) const;

private:
    std::string modelDir_ = "models";
    bool useGPU_ = false;
    bool gpuAvailable_ = false;

#if HAS_ONNXRUNTIME
    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::SessionOptions> cpuOpts_;
    std::unique_ptr<Ort::SessionOptions> gpuOpts_;

    Ort::SessionOptions& activeOpts() { return useGPU_ ? *gpuOpts_ : *cpuOpts_; }

    // Session cache (keyed by "modelName:cpu" or "modelName:gpu")
    std::unordered_map<std::string, std::unique_ptr<Ort::Session>> sessions_;
    Ort::Session& getSession(const std::string& modelName);

    // Run inference on a cached session
    bool runModel(Ort::Session& session,
                  const float* inputCHW, int inC, int inH, int inW,
                  std::vector<float>& output, int& outC, int& outH, int& outW,
                  std::string& status);

    // Query a model's expected fixed input H and W (returns false if dynamic)
    bool getModelInputSize(const std::string& modelName, int& h, int& w);
#endif

    // ── Classical fallbacks (no ONNX needed) ────────────────────────────
    Image classicalUpscale(const Image& src, int scale);
    Image classicalDenoise(const Image& src, float strength);
    Image classicalSharpen(const Image& src, float amount);
    Image classicalFaceRestore(const Image& src);
    Image classicalEnhance(const Image& src);
};
