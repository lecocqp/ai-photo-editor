#include "adjustments.h"
#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

// ── Parallel helper ──────────────────────────────────────────────────────────
// Splits [0, n) into hardware_concurrency chunks and runs fn(begin, end) on
// each chunk in parallel. Falls back to single-threaded for tiny images.
template<typename F>
static void parallelFor(size_t n, F fn) {
    unsigned int nThreads = std::max(1u, std::thread::hardware_concurrency());
    if (n < 8192 || nThreads <= 1) { fn(0, n); return; }

    std::vector<std::thread> threads;
    threads.reserve(nThreads);
    size_t chunk = (n + nThreads - 1) / nThreads;

    for (unsigned int t = 0; t < nThreads; ++t) {
        size_t s = t * chunk;
        size_t e = std::min(s + chunk, n);
        if (s >= n) break;
        threads.emplace_back([=, &fn]() { fn(s, e); });
    }
    for (auto& th : threads) th.join();
}

// ── Helpers ─────────────────────────────────────────────────────────────────

static inline float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

static inline float luminance(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// Convert RGB → HSL
static void rgbToHsl(float r, float g, float b, float& h, float& s, float& l) {
    float mx = std::max({r, g, b});
    float mn = std::min({r, g, b});
    l = (mx + mn) * 0.5f;
    if (mx == mn) { h = s = 0.0f; return; }
    float d = mx - mn;
    s = (l > 0.5f) ? d / (2.0f - mx - mn) : d / (mx + mn);
    if      (mx == r) h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (mx == g) h = (b - r) / d + 2.0f;
    else              h = (r - g) / d + 4.0f;
    h *= 60.0f;
}

static float hue2rgb(float p, float q, float t) {
    if (t < 0) t += 360.0f;
    if (t > 360.0f) t -= 360.0f;
    if (t < 60.0f)  return p + (q - p) * t / 60.0f;
    if (t < 180.0f) return q;
    if (t < 240.0f) return p + (q - p) * (240.0f - t) / 60.0f;
    return p;
}

// Convert HSL → RGB
static void hslToRgb(float h, float s, float l, float& r, float& g, float& b) {
    if (s == 0.0f) { r = g = b = l; return; }
    float q = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;
    r = hue2rgb(p, q, h + 120.0f);
    g = hue2rgb(p, q, h);
    b = hue2rgb(p, q, h - 120.0f);
}

// ── Gaussian blur (separable, for clarity / unsharp mask) ───────────────────

static std::vector<float> makeGaussianKernel(int radius) {
    std::vector<float> k(radius * 2 + 1);
    float sigma = radius / 3.0f;
    float sum = 0;
    for (int i = -radius; i <= radius; ++i) {
        k[i + radius] = std::exp(-(i * i) / (2.0f * sigma * sigma));
        sum += k[i + radius];
    }
    for (auto& v : k) v /= sum;
    return k;
}

// Blur a single-channel buffer (w×h) in place.
static void gaussianBlur(std::vector<float>& buf, int w, int h, int radius) {
    if (radius <= 0) return;
    auto kernel = makeGaussianKernel(radius);
    std::vector<float> tmp(buf.size());

    // Horizontal pass — each row is independent
    parallelFor(static_cast<size_t>(h), [&](size_t y0, size_t y1) {
        for (size_t y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x) {
                float sum = 0;
                for (int k = -radius; k <= radius; ++k) {
                    int sx = std::max(0, std::min(w - 1, x + k));
                    sum += buf[y * w + sx] * kernel[k + radius];
                }
                tmp[y * w + x] = sum;
            }
        }
    });

    // Vertical pass — each output row is independent
    parallelFor(static_cast<size_t>(h), [&](size_t y0, size_t y1) {
        for (size_t y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x) {
                float sum = 0;
                for (int k = -radius; k <= radius; ++k) {
                    int sy = std::max(0, std::min(h - 1, static_cast<int>(y) + k));
                    sum += tmp[static_cast<size_t>(sy) * w + x] * kernel[k + radius];
                }
                buf[y * w + x] = sum;
            }
        }
    });
}

// ── AdjustmentParams ────────────────────────────────────────────────────────

bool AdjustmentParams::isDefault() const {
    return exposure == 0 && contrast == 0 && brightness == 0 &&
           highlights == 0 && shadows == 0 && whites == 0 && blacks == 0 &&
           gamma == 1.0f && saturation == 0 && vibrance == 0 &&
           temperature == 0 && tint == 0 && hueShift == 0 &&
           clarity == 0 && sharpen == 0;
}

void AdjustmentParams::reset() { *this = AdjustmentParams{}; }

// ── Main apply function ─────────────────────────────────────────────────────

void applyAdjustments(const Image& src, Image& dst, const AdjustmentParams& p) {
    int w = src.width();
    int h = src.height();
    size_t npix = src.pixelCount();
    const float* sData = src.data();
    float* dData = dst.data();

    // Build a luminance map for clarity (local contrast)
    std::vector<float> lumMap;
    std::vector<float> lumBlur;
    bool needClarity = (p.clarity != 0.0f);
    if (needClarity) {
        lumMap.resize(npix);
        for (size_t i = 0; i < npix; ++i) {
            lumMap[i] = luminance(sData[i*4], sData[i*4+1], sData[i*4+2]);
        }
        lumBlur = lumMap;
        int radius = std::max(1, std::min(w, h) / 50);
        gaussianBlur(lumBlur, w, h, radius);
    }

    // Build blurred luminance for unsharp-mask sharpening
    std::vector<float> sharpBlur;
    bool needSharpen = (p.sharpen > 0.0f);
    if (needSharpen) {
        if (lumMap.empty()) {
            lumMap.resize(npix);
            for (size_t i = 0; i < npix; ++i)
                lumMap[i] = luminance(sData[i*4], sData[i*4+1], sData[i*4+2]);
        }
        sharpBlur = lumMap;
        gaussianBlur(sharpBlur, w, h, 2);
    }

    float exposureMul = std::pow(2.0f, p.exposure);
    float contrastFactor = 1.0f + p.contrast;

    parallelFor(npix, [&](size_t i0, size_t i1) {
    for (size_t i = i0; i < i1; ++i) {
        float r = sData[i * 4 + 0];
        float g = sData[i * 4 + 1];
        float b = sData[i * 4 + 2];
        float a = sData[i * 4 + 3];

        // ── Exposure ──
        r *= exposureMul;
        g *= exposureMul;
        b *= exposureMul;

        // ── Brightness ──
        r += p.brightness;
        g += p.brightness;
        b += p.brightness;

        // ── Contrast ──
        r = (r - 0.5f) * contrastFactor + 0.5f;
        g = (g - 0.5f) * contrastFactor + 0.5f;
        b = (b - 0.5f) * contrastFactor + 0.5f;

        // ── Highlights / Shadows ──
        float lum = luminance(r, g, b);
        if (p.highlights != 0.0f) {
            float mask = clamp01(lum);          // highlights affect bright areas
            mask = mask * mask;                  // softer falloff
            float shift = p.highlights * mask * -0.5f;
            r += shift; g += shift; b += shift;
        }
        if (p.shadows != 0.0f) {
            float mask = clamp01(1.0f - lum);   // shadows affect dark areas
            mask = mask * mask;
            float shift = p.shadows * mask * 0.5f;
            r += shift; g += shift; b += shift;
        }

        // ── Whites / Blacks (toe / shoulder) ──
        if (p.whites != 0.0f) {
            float mask = clamp01(lum);
            mask = mask * mask * mask;
            float shift = p.whites * mask * 0.3f;
            r += shift; g += shift; b += shift;
        }
        if (p.blacks != 0.0f) {
            float mask = clamp01(1.0f - lum);
            mask = mask * mask * mask;
            float shift = p.blacks * mask * 0.3f;
            r += shift; g += shift; b += shift;
        }

        // ── Gamma ──
        if (p.gamma != 1.0f) {
            float invG = 1.0f / p.gamma;
            r = std::pow(std::max(0.0f, r), invG);
            g = std::pow(std::max(0.0f, g), invG);
            b = std::pow(std::max(0.0f, b), invG);
        }

        // ── Temperature (warm / cool) ──
        if (p.temperature != 0.0f) {
            r += p.temperature * 0.1f;
            b -= p.temperature * 0.1f;
        }

        // ── Tint (green / magenta) ──
        if (p.tint != 0.0f) {
            g += p.tint * 0.1f;
        }

        // ── Saturation ──
        if (p.saturation != 0.0f) {
            float l = luminance(r, g, b);
            float factor = 1.0f + p.saturation;
            r = l + (r - l) * factor;
            g = l + (g - l) * factor;
            b = l + (b - l) * factor;
        }

        // ── Vibrance (smart saturation) ──
        if (p.vibrance != 0.0f) {
            float l = luminance(r, g, b);
            float maxC = std::max({r, g, b});
            float minC = std::min({r, g, b});
            float curSat = (maxC > 0.0f) ? (maxC - minC) / maxC : 0.0f;
            float factor = 1.0f + p.vibrance * (1.0f - curSat);
            r = l + (r - l) * factor;
            g = l + (g - l) * factor;
            b = l + (b - l) * factor;
        }

        // ── Hue shift ──
        if (p.hueShift != 0.0f) {
            float hh, ss, ll;
            rgbToHsl(clamp01(r), clamp01(g), clamp01(b), hh, ss, ll);
            hh += p.hueShift;
            if (hh < 0) hh += 360.0f;
            if (hh >= 360.0f) hh -= 360.0f;
            hslToRgb(hh, ss, ll, r, g, b);
        }

        // ── Clarity (local contrast) ──
        if (needClarity) {
            float detail = lumMap[i] - lumBlur[i];
            float boost = detail * p.clarity * 2.0f;
            r += boost; g += boost; b += boost;
        }

        // ── Sharpening (unsharp mask) ──
        if (needSharpen) {
            float detail = lumMap[i] - sharpBlur[i];
            float boost = detail * p.sharpen;
            r += boost; g += boost; b += boost;
        }

        // ── Final clamp ──
        dData[i * 4 + 0] = clamp01(r);
        dData[i * 4 + 1] = clamp01(g);
        dData[i * 4 + 2] = clamp01(b);
        dData[i * 4 + 3] = a;
    }
    }); // parallelFor
}
