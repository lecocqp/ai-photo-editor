#pragma once

#include "image.h"
#include <cstdint>

// Manages an OpenGL texture and a 2D camera (pan + zoom) for displaying
// an image in the center viewport area.
class Viewport {
public:
    Viewport();
    ~Viewport();

    // Upload the image pixel data to the GPU texture.
    void uploadImage(const Image& img);

    // Call when a new image is loaded to fit it inside the viewport rect.
    void fitToView(float viewW, float viewH);

    // ── Interaction ─────────────────────────────────────────────────────
    void pan(float dx, float dy);
    void zoom(float delta, float pivotX, float pivotY);
    void setZoom(float z);

    // ── Coordinate transforms ───────────────────────────────────────────
    // Screen (viewport-relative) → image pixel coords
    void screenToImage(float sx, float sy, float& ix, float& iy) const;
    // Image pixel → screen coords
    void imageToScreen(float ix, float iy, float& sx, float& sy) const;

    // ── Rendering ───────────────────────────────────────────────────────
    // Set the viewport rectangle (in screen pixels) for coordinate transforms.
    void setViewRect(float viewX, float viewY, float viewW, float viewH);

    // ── Accessors ───────────────────────────────────────────────────────
    uint32_t textureId() const { return texId_; }
    float    zoomLevel() const { return zoom_; }
    float    panX()      const { return panX_; }
    float    panY()      const { return panY_; }
    int      imgWidth()  const { return imgW_; }
    int      imgHeight() const { return imgH_; }
    bool     hasImage()  const { return texId_ != 0; }
    float    viewW()     const { return viewW_; }
    float    viewH()     const { return viewH_; }

private:
    void ensureShader();

    uint32_t texId_  = 0;
    uint32_t vao_    = 0;
    uint32_t vbo_    = 0;
    uint32_t shader_ = 0;

    int imgW_ = 0;   // actual full-res image dimensions
    int imgH_ = 0;
    int texW_ = 0;   // texture dimensions (may be downscaled for GPU limits)
    int texH_ = 0;

    float zoom_ = 1.0f;
    float panX_ = 0.0f; // in image pixels
    float panY_ = 0.0f;

    float viewX_ = 0, viewY_ = 0, viewW_ = 1, viewH_ = 1;
};
