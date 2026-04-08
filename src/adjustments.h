#pragma once

#include "image.h"

// All adjustable parameters for color / light correction.
// Default values are "no change".
struct AdjustmentParams {
    // Light
    float exposure   = 0.0f;  // -5 … +5  (EV stops)
    float contrast   = 0.0f;  // -1 … +1
    float brightness = 0.0f;  // -1 … +1
    float highlights = 0.0f;  // -1 … +1
    float shadows    = 0.0f;  // -1 … +1
    float whites     = 0.0f;  // -1 … +1
    float blacks     = 0.0f;  // -1 … +1
    float gamma      = 1.0f;  //  0.1 … 3.0

    // Color
    float saturation  = 0.0f;  // -1 … +1
    float vibrance    = 0.0f;  // -1 … +1
    float temperature = 0.0f;  // -1 … +1  (cool ← → warm)
    float tint        = 0.0f;  // -1 … +1  (green ← → magenta)
    float hueShift    = 0.0f;  // -180 … +180 degrees

    // Detail
    float clarity   = 0.0f;   // -1 … +1  (local contrast)
    float sharpen   = 0.0f;   // 0 … +2   (unsharp mask amount)

    bool isDefault() const;
    void reset();
};

// Apply all adjustments to `src`, writing the result into `dst`.
// `dst` must already be allocated with the same dimensions as `src`.
void applyAdjustments(const Image& src, Image& dst, const AdjustmentParams& params);
