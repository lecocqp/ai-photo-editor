#include "ai_processor.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <vector>

#if HAS_ONNXRUNTIME && defined(_WIN32)
// Forward-declare the DirectML provider entry point (from onnxruntime.dll)
extern "C" OrtStatus* __stdcall OrtSessionOptionsAppendExecutionProvider_DML(
    OrtSessionOptions* options, int device_id);
#endif

namespace fs = std::filesystem;

// ── Helpers ─────────────────────────────────────────────────────────────────

static inline float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

static float luminance(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// ── Constructor / Destructor ────────────────────────────────────────────────

AIProcessor::AIProcessor() {
#if HAS_ONNXRUNTIME
    env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "ai-photo-editor");

    // CPU session options
    cpuOpts_ = std::make_unique<Ort::SessionOptions>();
    cpuOpts_->SetIntraOpNumThreads(4);
    cpuOpts_->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    // GPU session options (DirectML)
    gpuOpts_ = std::make_unique<Ort::SessionOptions>();
    gpuOpts_->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef _WIN32
    {
        OrtStatus* dmlStatus = OrtSessionOptionsAppendExecutionProvider_DML(
            static_cast<OrtSessionOptions*>(*gpuOpts_), 0);
        if (dmlStatus == nullptr) {
            gpuAvailable_ = true;
            printf("DirectML GPU provider available\n");
        } else {
            const char* msg = Ort::GetApi().GetErrorMessage(dmlStatus);
            printf("DirectML not available: %s\n", msg);
            Ort::GetApi().ReleaseStatus(dmlStatus);
            gpuAvailable_ = false;
        }
    }
#endif

    // Print all available providers
    auto providers = Ort::GetAvailableProviders();
    printf("ONNX Runtime providers:");
    for (const auto& p : providers) printf(" %s", p.c_str());
    printf("\n");
#endif
}

AIProcessor::~AIProcessor() = default;

void AIProcessor::setUseGPU(bool enabled) {
#if HAS_ONNXRUNTIME
    bool newVal = enabled && gpuAvailable_;
    if (newVal != useGPU_) {
        sessions_.clear(); // invalidate cached sessions for old provider
        useGPU_ = newVal;
    }
    printf("Execution provider: %s\n", useGPU_ ? "DirectML (GPU)" : "CPU");
#endif
}

bool AIProcessor::hasModel(const std::string& name) const {
    return fs::exists(fs::path(modelDir_) / (name + ".onnx"));
}

// ── ONNX Runtime inference ──────────────────────────────────────────────────

#if HAS_ONNXRUNTIME

Ort::Session& AIProcessor::getSession(const std::string& modelName) {
    std::string key = modelName + (useGPU_ ? ":gpu" : ":cpu");
    auto it = sessions_.find(key);
    if (it != sessions_.end()) return *it->second;

    std::string modelPath = (fs::path(modelDir_) / (modelName + ".onnx")).string();
    printf("Loading model: %s (%s)\n", modelPath.c_str(), useGPU_ ? "GPU" : "CPU");

    auto makeSession = [&](Ort::SessionOptions& opts) {
#ifdef _WIN32
        std::wstring wpath(modelPath.begin(), modelPath.end());
        return std::make_unique<Ort::Session>(*env_, wpath.c_str(), opts);
#else
        return std::make_unique<Ort::Session>(*env_, modelPath.c_str(), opts);
#endif
    };

    std::unique_ptr<Ort::Session> session;
    try {
        session = makeSession(activeOpts());
    } catch (const Ort::Exception& e) {
        if (useGPU_) {
            // Some models use ops unsupported by DirectML — fall back to CPU
            printf("GPU session failed for '%s': %s\nFalling back to CPU for this model.\n",
                   modelName.c_str(), e.what());
            std::string cpuKey = modelName + ":cpu";
            auto it2 = sessions_.find(cpuKey);
            if (it2 != sessions_.end()) return *it2->second;
            session = makeSession(*cpuOpts_);
            auto& ref = *session;
            sessions_[cpuKey] = std::move(session);
            return ref;
        }
        throw;
    }

    auto& ref = *session;
    sessions_[key] = std::move(session);
    return ref;
}

bool AIProcessor::runModel(Ort::Session& session,
                           const float* inputCHW, int inC, int inH, int inW,
                           std::vector<float>& output,
                           int& outC, int& outH, int& outW,
                           std::string& status) {
    try {
        Ort::AllocatorWithDefaultOptions alloc;

        // Find the actual image input. Old ONNX models (pre-opset 9) list all
        // initializers (weights) as graph inputs, so index 0 may be a weight
        // tensor. We look for the first 4D input whose channel dim matches inC
        // and spatial dims are >= 32 (i.e. looks like an image, not a kernel).
        int imageInputIdx = 0;
        size_t numInputs = session.GetInputCount();
        for (size_t i = 0; i < numInputs; ++i) {
            try {
                auto shape = session.GetInputTypeInfo(i)
                                 .GetTensorTypeAndShapeInfo().GetShape();
                if (shape.size() == 4) {
                    int64_t c = shape[1], h = shape[2], w = shape[3];
                    bool chanOk = (c < 0 || c == inC);
                    bool spatOk = (h < 0 || h >= 32) && (w < 0 || w >= 32);
                    if (chanOk && spatOk) {
                        imageInputIdx = static_cast<int>(i);
                        break;
                    }
                }
            } catch (...) {}
        }

        // Input info
        auto inputName = session.GetInputNameAllocated(imageInputIdx, alloc);

        // Build input tensor  (N=1, C, H, W)
        std::vector<int64_t> inDims = {1, inC, inH, inW};
        size_t inputSize = static_cast<size_t>(inC) * inH * inW;
        auto memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memInfo, const_cast<float*>(inputCHW), inputSize, inDims.data(), inDims.size());

        // Output info
        auto outputName = session.GetOutputNameAllocated(0, alloc);

        const char* inNames[]  = {inputName.get()};
        const char* outNames[] = {outputName.get()};

        // Run
        auto results = session.Run(Ort::RunOptions{nullptr},
                                   inNames, &inputTensor, 1,
                                   outNames, 1);

        // Extract output
        auto& outTensor = results[0];
        auto outShape = outTensor.GetTensorTypeAndShapeInfo().GetShape();
        outC = (outShape.size() > 1) ? static_cast<int>(outShape[1]) : 3;
        outH = (outShape.size() > 2) ? static_cast<int>(outShape[2]) : inH;
        outW = (outShape.size() > 3) ? static_cast<int>(outShape[3]) : inW;

        size_t outSize = static_cast<size_t>(outC) * outH * outW;
        const float* outData = outTensor.GetTensorData<float>();
        output.assign(outData, outData + outSize);
        return true;

    } catch (const Ort::Exception& e) {
        status = std::string("ONNX error: ") + e.what();
        return false;
    }
}

bool AIProcessor::getModelInputSize(const std::string& modelName, int& h, int& w) {
    if (!hasModel(modelName)) return false;

    try {
        Ort::Session& session = getSession(modelName);
        auto shape = session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        // shape is [N, C, H, W]
        if (shape.size() >= 4 && shape[2] > 0 && shape[3] > 0) {
            h = static_cast<int>(shape[2]);
            w = static_cast<int>(shape[3]);
            return true;
        }
    } catch (...) {}
    return false;
}

#endif // HAS_ONNXRUNTIME

// ── Helper: convert RGBA (HWC) → RGB (CHW) for model input ─────────────────

static std::vector<float> rgbaToCHW(const Image& img) {
    int w = img.width(), h = img.height();
    size_t npix = img.pixelCount();
    std::vector<float> chw(3 * npix);
    const float* d = img.data();
    for (size_t i = 0; i < npix; ++i) {
        chw[0 * npix + i] = d[i * 4 + 0]; // R
        chw[1 * npix + i] = d[i * 4 + 1]; // G
        chw[2 * npix + i] = d[i * 4 + 2]; // B
    }
    return chw;
}

// Convert CHW (3 channels) → RGBA Image
static Image chwToImage(const std::vector<float>& chw, int w, int h) {
    size_t npix = static_cast<size_t>(w) * h;
    std::vector<float> rgba(npix * 4);
    for (size_t i = 0; i < npix; ++i) {
        rgba[i * 4 + 0] = clamp01(chw[0 * npix + i]);
        rgba[i * 4 + 1] = clamp01(chw[1 * npix + i]);
        rgba[i * 4 + 2] = clamp01(chw[2 * npix + i]);
        rgba[i * 4 + 3] = 1.0f;
    }
    return Image(w, h, rgba);
}

// ── Public API: Upscale ─────────────────────────────────────────────────────

Image AIProcessor::upscale(const Image& src, int scale, std::string& status,
                           ProgressCallback progress) {
    if (src.empty()) { status = "No image"; return {}; }
    if (scale < 2) scale = 2;
    if (scale > 4) scale = 4;

#if HAS_ONNXRUNTIME
    std::string modelName = (scale == 4) ? "realesrgan-x4" : "realesrgan-x2";
    if (hasModel(modelName)) {
        if (progress) progress(0.05f);

        // Query model's expected input size (fixed-size models like 128x128)
        int modelH = 0, modelW = 0;
        bool fixedSize = getModelInputSize(modelName, modelH, modelW);
        int tileSize = fixedSize ? modelH : 512;
        printf("Model tile size: %dx%d (fixed=%d)\n", tileSize, tileSize, fixedSize);

        int inW = src.width(), inH = src.height();
        int newW = inW * scale, newH = inH * scale;
        Image result(newW, newH);  // initialized to 0
        size_t outPixels = static_cast<size_t>(newW) * newH;
        std::vector<float> weights(outPixels, 0.0f);

        // Overlapping tiles with feathered blending to hide seams
        int overlap = std::min(tileSize / 4, 16);  // overlap in input pixels
        int step = tileSize - overlap;
        if (step < 1) step = 1;

        int tilesX = (inW <= tileSize) ? 1 : (inW - overlap + step - 1) / step;
        int tilesY = (inH <= tileSize) ? 1 : (inH - overlap + step - 1) / step;
        int totalTiles = tilesX * tilesY;
        int tileIdx = 0;
        int tilesOK = 0;
        std::string lastError;
        int overlapOut = overlap * scale;  // overlap in output pixels

        Ort::Session& session = getSession(modelName);

        for (int ty = 0; ty < tilesY; ++ty) {
            for (int tx = 0; tx < tilesX; ++tx) {
                // Clamp tile position so it doesn't exceed image bounds
                int x0 = std::min(tx * step, std::max(0, inW - tileSize));
                int y0 = std::min(ty * step, std::max(0, inH - tileSize));
                int tw = std::min(tileSize, inW - x0);
                int th = std::min(tileSize, inH - y0);

                Image tile = src.cropped(x0, y0, tw, th);

                // Pad edge tiles to tileSize x tileSize if needed
                if (fixedSize && (tw < tileSize || th < tileSize)) {
                    Image padded(tileSize, tileSize);
                    for (int row = 0; row < th; ++row)
                        for (int col = 0; col < tw; ++col) {
                            float px[4];
                            tile.getPixel(col, row, px);
                            padded.setPixel(col, row, px);
                        }
                    tile = std::move(padded);
                }

                auto tileCHW = rgbaToCHW(tile);
                std::vector<float> outCHW;
                int outC, outH, outW;
                if (runModel(session, tileCHW.data(), 3,
                             tile.height(), tile.width(),
                             outCHW, outC, outH, outW, status)) {
                    Image outTile = chwToImage(outCHW, outW, outH);
                    int validW = tw * scale;
                    int validH = th * scale;
                    int dx = x0 * scale, dy = y0 * scale;
                    float* rd = result.data();

                    for (int row = 0; row < validH && dy + row < newH; ++row) {
                        // Vertical blend weight: ramp at overlap edges
                        float wy = 1.0f;
                        if (row < overlapOut && y0 > 0)
                            wy = static_cast<float>(row) / overlapOut;
                        if (row >= validH - overlapOut && y0 + tileSize < inH)
                            wy = std::min(wy, static_cast<float>(validH - 1 - row) / overlapOut);

                        for (int col = 0; col < validW && dx + col < newW; ++col) {
                            // Horizontal blend weight
                            float wx = 1.0f;
                            if (col < overlapOut && x0 > 0)
                                wx = static_cast<float>(col) / overlapOut;
                            if (col >= validW - overlapOut && x0 + tileSize < inW)
                                wx = std::min(wx, static_cast<float>(validW - 1 - col) / overlapOut);

                            float w = wx * wy;
                            float px[4];
                            outTile.getPixel(col, row, px);
                            size_t idx = static_cast<size_t>(dy + row) * newW + (dx + col);
                            rd[idx * 4 + 0] += px[0] * w;
                            rd[idx * 4 + 1] += px[1] * w;
                            rd[idx * 4 + 2] += px[2] * w;
                            weights[idx] += w;
                        }
                    }
                    tilesOK++;
                } else {
                    printf("Tile %d/%d failed: %s\n", tileIdx + 1, totalTiles, status.c_str());
                    lastError = status;
                }
                tileIdx++;
                if (progress) progress(0.05f + 0.90f * static_cast<float>(tileIdx) / totalTiles);
            }
        }

        // Normalize accumulated pixels by weight
        if (tilesOK > 0) {
            float* rd = result.data();
            for (size_t i = 0; i < outPixels; ++i) {
                if (weights[i] > 0.0f) {
                    rd[i * 4 + 0] /= weights[i];
                    rd[i * 4 + 1] /= weights[i];
                    rd[i * 4 + 2] /= weights[i];
                    rd[i * 4 + 3] = 1.0f;
                }
            }
            if (progress) progress(1.0f);
        }

        if (tilesOK == 0) {
            status = "AI upscale failed (" + lastError + "), using classical";
            printf("All %d tiles failed, falling back to classical upscale\n", totalTiles);
        } else {
            status = "Upscaled " + std::to_string(scale) + "x with Real-ESRGAN (" +
                     std::to_string(tilesOK) + "/" + std::to_string(totalTiles) + " tiles)";
            return result;
        }
    }
    status = "Model not found, using classical upscale";
#else
    status = "ONNX Runtime not available, using classical upscale";
#endif
    if (progress) progress(0.5f);
    auto result = classicalUpscale(src, scale);
    if (progress) progress(1.0f);
    return result;
}

// ── Public API: Denoise ─────────────────────────────────────────────────────

Image AIProcessor::denoise(const Image& src, float strength, std::string& status,
                           ProgressCallback progress) {
    if (src.empty()) { status = "No image"; return {}; }

#if HAS_ONNXRUNTIME
    if (hasModel("denoise")) {
        if (progress) progress(0.1f);
        auto chw = rgbaToCHW(src);
        std::vector<float> outCHW;
        int outC, outH, outW;
        Ort::Session& session = getSession("denoise");
        if (runModel(session, chw.data(), 3, src.height(), src.width(),
                     outCHW, outC, outH, outW, status)) {
            if (progress) progress(0.9f);
            // Blend with original based on strength
            Image result = chwToImage(outCHW, outW, outH);
            float* rd = result.data();
            const float* sd = src.data();
            for (size_t i = 0; i < src.dataSize(); ++i) {
                rd[i] = sd[i] + (rd[i] - sd[i]) * strength;
            }
            if (progress) progress(1.0f);
            status = "Denoised with AI model";
            return result;
        }
    }
    status = "Denoise model not found, using classical denoise";
#else
    status = "ONNX Runtime not available, using classical denoise";
#endif
    if (progress) progress(0.5f);
    auto result = classicalDenoise(src, strength);
    if (progress) progress(1.0f);
    return result;
}

// ── Public API: Sharpen ─────────────────────────────────────────────────────

Image AIProcessor::sharpen(const Image& src, float amount, std::string& status,
                           ProgressCallback progress) {
    if (src.empty()) { status = "No image"; return {}; }

    // Sharpening works well with classical methods; use AI only if model exists
#if HAS_ONNXRUNTIME
    if (hasModel("sharpen")) {
        if (progress) progress(0.1f);
        auto chw = rgbaToCHW(src);
        std::vector<float> outCHW;
        int outC, outH, outW;
        Ort::Session& session = getSession("sharpen");
        if (runModel(session, chw.data(), 3, src.height(), src.width(),
                     outCHW, outC, outH, outW, status)) {
            Image result = chwToImage(outCHW, outW, outH);
            float* rd = result.data();
            const float* sd = src.data();
            for (size_t i = 0; i < src.dataSize(); ++i)
                rd[i] = sd[i] + (rd[i] - sd[i]) * amount;
            if (progress) progress(1.0f);
            status = "Sharpened with AI model";
            return result;
        }
    }
#endif
    status = "Using unsharp-mask sharpening";
    if (progress) progress(0.5f);
    auto result = classicalSharpen(src, amount);
    if (progress) progress(1.0f);
    return result;
}

// ── Public API: Face Restore ────────────────────────────────────────────────

Image AIProcessor::faceRestore(const Image& src, std::string& status,
                               ProgressCallback progress) {
    if (src.empty()) { status = "No image"; return {}; }

#if HAS_ONNXRUNTIME
    // Try GFPGAN or CodeFormer model
    std::string modelName = hasModel("gfpgan") ? "gfpgan" :
                            hasModel("codeformer") ? "codeformer" : "";
    if (!modelName.empty()) {
        if (progress) progress(0.1f);

        // Face restoration models typically expect 512x512 input
        int targetSize = 512;
        Image resized = src.resized(targetSize, targetSize);
        auto chw = rgbaToCHW(resized);

        std::vector<float> outCHW;
        int outC, outH, outW;
        Ort::Session& session = getSession(modelName);
        if (runModel(session, chw.data(), 3, targetSize, targetSize,
                     outCHW, outC, outH, outW, status)) {
            Image restored = chwToImage(outCHW, outW, outH);
            // Resize back to original dimensions
            if (outW != src.width() || outH != src.height()) {
                restored = restored.resized(src.width(), src.height());
            }
            if (progress) progress(1.0f);
            status = "Face restored with " + modelName;
            return restored;
        }
        // runModel set status to the actual ONNX error; keep it visible
        status = modelName + " failed: " + status;
    } else {
        status = "Face restoration model not found (expected gfpgan.onnx or codeformer.onnx)";
    }
#else
    status = "ONNX Runtime not available, using classical face restore";
#endif
    if (progress) progress(0.5f);
    auto result = classicalFaceRestore(src);
    if (progress) progress(1.0f);
    return result;
}

// ── Public API: Enhance ─────────────────────────────────────────────────────

Image AIProcessor::enhance(const Image& src, std::string& status,
                           ProgressCallback progress) {
    if (src.empty()) { status = "No image"; return {}; }

#if HAS_ONNXRUNTIME
    if (hasModel("enhance")) {
        if (progress) progress(0.1f);
        auto chw = rgbaToCHW(src);
        std::vector<float> outCHW;
        int outC, outH, outW;
        Ort::Session& session = getSession("enhance");
        if (runModel(session, chw.data(), 3, src.height(), src.width(),
                     outCHW, outC, outH, outW, status)) {
            if (progress) progress(1.0f);
            status = "Enhanced with AI model";
            return chwToImage(outCHW, outW, outH);
        }
    }
    status = "Enhance model not found, using classical method";
#else
    status = "ONNX Runtime not available, using classical enhance";
#endif
    if (progress) progress(0.5f);
    auto result = classicalEnhance(src);
    if (progress) progress(1.0f);
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Classical fallbacks
// ═══════════════════════════════════════════════════════════════════════════

// ── Classical Upscale (Lanczos via OIIO) ────────────────────────────────────

Image AIProcessor::classicalUpscale(const Image& src, int scale) {
    return src.resized(src.width() * scale, src.height() * scale);
}

// ── Classical Denoise (bilateral filter) ────────────────────────────────────

Image AIProcessor::classicalDenoise(const Image& src, float strength) {
    int w = src.width(), h = src.height();
    Image result(w, h);
    const float* sd = src.data();
    float* rd = result.data();

    int radius = std::max(1, static_cast<int>(strength * 5));
    float sigmaSpace = radius / 2.0f;
    float sigmaColor = 0.1f + strength * 0.3f;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t ci = (static_cast<size_t>(y) * w + x) * 4;
            float cr = sd[ci], cg = sd[ci+1], cb = sd[ci+2];
            float sumR = 0, sumG = 0, sumB = 0, sumW = 0;

            for (int ky = -radius; ky <= radius; ++ky) {
                for (int kx = -radius; kx <= radius; ++kx) {
                    int nx = std::max(0, std::min(w-1, x + kx));
                    int ny = std::max(0, std::min(h-1, y + ky));
                    size_t ni = (static_cast<size_t>(ny) * w + nx) * 4;
                    float nr = sd[ni], ng = sd[ni+1], nb = sd[ni+2];

                    float ds = static_cast<float>(kx*kx + ky*ky);
                    float dc = (cr-nr)*(cr-nr) + (cg-ng)*(cg-ng) + (cb-nb)*(cb-nb);
                    float weight = std::exp(-ds / (2*sigmaSpace*sigmaSpace)
                                           -dc / (2*sigmaColor*sigmaColor));
                    sumR += nr * weight;
                    sumG += ng * weight;
                    sumB += nb * weight;
                    sumW += weight;
                }
            }
            rd[ci+0] = sumR / sumW;
            rd[ci+1] = sumG / sumW;
            rd[ci+2] = sumB / sumW;
            rd[ci+3] = sd[ci+3];
        }
    }
    return result;
}

// ── Classical Sharpen (unsharp mask) ────────────────────────────────────────

Image AIProcessor::classicalSharpen(const Image& src, float amount) {
    int w = src.width(), h = src.height();
    size_t npix = src.pixelCount();
    const float* sd = src.data();

    // Build per-channel blur
    std::vector<float> blurR(npix), blurG(npix), blurB(npix);
    for (size_t i = 0; i < npix; ++i) {
        blurR[i] = sd[i*4+0];
        blurG[i] = sd[i*4+1];
        blurB[i] = sd[i*4+2];
    }
    // Simple 3×3 box blur (fast)
    auto boxBlur = [&](std::vector<float>& ch) {
        std::vector<float> tmp(npix);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                float s = 0; int n = 0;
                for (int ky = -1; ky <= 1; ++ky)
                    for (int kx = -1; kx <= 1; ++kx) {
                        int nx = std::max(0, std::min(w-1, x+kx));
                        int ny = std::max(0, std::min(h-1, y+ky));
                        s += ch[ny*w+nx]; ++n;
                    }
                tmp[y*w+x] = s / n;
            }
        ch = tmp;
    };
    boxBlur(blurR); boxBlur(blurG); boxBlur(blurB);

    Image result(w, h);
    float* rd = result.data();
    for (size_t i = 0; i < npix; ++i) {
        rd[i*4+0] = clamp01(sd[i*4+0] + (sd[i*4+0] - blurR[i]) * amount);
        rd[i*4+1] = clamp01(sd[i*4+1] + (sd[i*4+1] - blurG[i]) * amount);
        rd[i*4+2] = clamp01(sd[i*4+2] + (sd[i*4+2] - blurB[i]) * amount);
        rd[i*4+3] = sd[i*4+3];
    }
    return result;
}

// ── Classical Face Restore (bilateral smooth + detail sharpen) ──────────────

Image AIProcessor::classicalFaceRestore(const Image& src) {
    // Step 1: Smooth skin tones (bilateral)
    Image smoothed = classicalDenoise(src, 0.7f);
    // Step 2: Sharpen details
    Image sharpened = classicalSharpen(smoothed, 0.5f);
    return sharpened;
}

// ── Classical Enhance (auto levels + contrast + saturation) ─────────────────

Image AIProcessor::classicalEnhance(const Image& src) {
    int w = src.width(), h = src.height();
    size_t npix = src.pixelCount();
    const float* sd = src.data();

    // Find min/max luminance for auto levels
    float minL = 1.0f, maxL = 0.0f;
    for (size_t i = 0; i < npix; ++i) {
        float l = luminance(sd[i*4], sd[i*4+1], sd[i*4+2]);
        minL = std::min(minL, l);
        maxL = std::max(maxL, l);
    }

    float range = maxL - minL;
    if (range < 0.01f) range = 1.0f; // avoid division by zero

    Image result(w, h);
    float* rd = result.data();

    for (size_t i = 0; i < npix; ++i) {
        float r = sd[i*4+0], g = sd[i*4+1], b = sd[i*4+2];

        // Auto levels: stretch to 0–1
        r = (r - minL) / range;
        g = (g - minL) / range;
        b = (b - minL) / range;

        // Slight contrast boost
        float contrast = 1.15f;
        r = (r - 0.5f) * contrast + 0.5f;
        g = (g - 0.5f) * contrast + 0.5f;
        b = (b - 0.5f) * contrast + 0.5f;

        // Slight saturation boost
        float l = luminance(r, g, b);
        float satBoost = 1.2f;
        r = l + (r - l) * satBoost;
        g = l + (g - l) * satBoost;
        b = l + (b - l) * satBoost;

        // Gentle S-curve for pop
        auto sCurve = [](float v) {
            v = std::max(0.0f, std::min(1.0f, v));
            return v * v * (3.0f - 2.0f * v); // smoothstep
        };
        r = sCurve(r);
        g = sCurve(g);
        b = sCurve(b);

        rd[i*4+0] = clamp01(r);
        rd[i*4+1] = clamp01(g);
        rd[i*4+2] = clamp01(b);
        rd[i*4+3] = sd[i*4+3];
    }
    return result;
}
