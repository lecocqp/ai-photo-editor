#include "viewport.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#include <GL/gl.h>

// OpenGL 1.2+ / 3.0+ constants not in Windows' gl.h
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif

Viewport::Viewport() {}

Viewport::~Viewport() {
    if (texId_) glDeleteTextures(1, &texId_);
}

void Viewport::uploadImage(const Image& img) {
    if (img.empty()) return;

    imgW_ = img.width();
    imgH_ = img.height();

    if (!texId_) glGenTextures(1, &texId_);

    glBindTexture(GL_TEXTURE_2D, texId_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 imgW_, imgH_, 0, GL_RGBA, GL_FLOAT, img.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Viewport::fitToView(float viewW, float viewH) {
    if (imgW_ <= 0 || imgH_ <= 0) return;
    float scaleX = viewW / static_cast<float>(imgW_);
    float scaleY = viewH / static_cast<float>(imgH_);
    zoom_ = std::min(scaleX, scaleY) * 0.95f; // slight margin
    panX_ = 0.0f;
    panY_ = 0.0f;
}

void Viewport::pan(float dx, float dy) {
    panX_ += dx / zoom_;
    panY_ += dy / zoom_;
}

void Viewport::zoom(float delta, float pivotX, float pivotY) {
    float oldZoom = zoom_;
    zoom_ *= (delta > 0) ? 1.1f : (1.0f / 1.1f);
    zoom_ = std::max(0.01f, std::min(zoom_, 100.0f));

    // Adjust pan so the pivot point stays under the mouse
    float imgPivotX, imgPivotY;
    // Use old zoom for the conversion
    float tmpZoom = zoom_;
    zoom_ = oldZoom;
    screenToImage(pivotX, pivotY, imgPivotX, imgPivotY);
    zoom_ = tmpZoom;

    float newScreenX, newScreenY;
    imageToScreen(imgPivotX, imgPivotY, newScreenX, newScreenY);
    panX_ += (pivotX - newScreenX) / zoom_;
    panY_ += (pivotY - newScreenY) / zoom_;
}

void Viewport::setZoom(float z) {
    zoom_ = std::max(0.01f, std::min(z, 100.0f));
}

void Viewport::screenToImage(float sx, float sy, float& ix, float& iy) const {
    float cx = viewX_ + viewW_ * 0.5f;
    float cy = viewY_ + viewH_ * 0.5f;
    ix = (sx - cx) / zoom_ - panX_ + imgW_ * 0.5f;
    iy = (sy - cy) / zoom_ - panY_ + imgH_ * 0.5f;
}

void Viewport::imageToScreen(float ix, float iy, float& sx, float& sy) const {
    float cx = viewX_ + viewW_ * 0.5f;
    float cy = viewY_ + viewH_ * 0.5f;
    sx = (ix - imgW_ * 0.5f + panX_) * zoom_ + cx;
    sy = (iy - imgH_ * 0.5f + panY_) * zoom_ + cy;
}

void Viewport::setViewRect(float viewX, float viewY, float viewW, float viewH) {
    viewX_ = viewX;
    viewY_ = viewY;
    viewW_ = viewW;
    viewH_ = viewH;
}
