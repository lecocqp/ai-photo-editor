#include "crop_tool.h"
#include <algorithm>
#include <cmath>

void CropTool::begin(float imgX, float imgY) {
    active_  = true;
    hasRect_ = true;
    startX_  = imgX;
    startY_  = imgY;
    x_ = imgX; y_ = imgY;
    w_ = 0; h_ = 0;
}

void CropTool::update(float imgX, float imgY) {
    if (!active_) return;
    x_ = std::min(startX_, imgX);
    y_ = std::min(startY_, imgY);
    w_ = std::abs(imgX - startX_);
    h_ = std::abs(imgY - startY_);
    enforceAspect();
}

void CropTool::end() {
    active_ = false;
    if (w_ < 2 || h_ < 2) hasRect_ = false; // too small
}

void CropTool::beginHandleDrag(int handleIdx, float imgX, float imgY) {
    dragHandle_ = handleIdx;
    dragStartX_ = imgX;
    dragStartY_ = imgY;
    origX_ = x_; origY_ = y_; origW_ = w_; origH_ = h_;
    active_ = true;
}

void CropTool::updateHandleDrag(float imgX, float imgY) {
    if (dragHandle_ < 0) return;

    float dx = imgX - dragStartX_;
    float dy = imgY - dragStartY_;

    // Corners: 0=TL, 1=TR, 2=BR, 3=BL
    // Edges:   4=T,  5=R,  6=B,  7=L
    switch (dragHandle_) {
        case 0: // TL
            x_ = origX_ + dx; y_ = origY_ + dy;
            w_ = origW_ - dx; h_ = origH_ - dy;
            break;
        case 1: // TR
            y_ = origY_ + dy;
            w_ = origW_ + dx; h_ = origH_ - dy;
            break;
        case 2: // BR
            w_ = origW_ + dx; h_ = origH_ + dy;
            break;
        case 3: // BL
            x_ = origX_ + dx;
            w_ = origW_ - dx; h_ = origH_ + dy;
            break;
        case 4: // T
            y_ = origY_ + dy; h_ = origH_ - dy;
            break;
        case 5: // R
            w_ = origW_ + dx;
            break;
        case 6: // B
            h_ = origH_ + dy;
            break;
        case 7: // L
            x_ = origX_ + dx; w_ = origW_ - dx;
            break;
    }

    // Prevent negative size
    if (w_ < 1) { w_ = 1; }
    if (h_ < 1) { h_ = 1; }

    enforceAspect();
}

int CropTool::hitTestHandle(float imgX, float imgY, float r) const {
    if (!hasRect_) return -1;

    // Handle positions: corners then edge midpoints
    float hx[8] = { x_, x_+w_, x_+w_, x_,     x_+w_/2, x_+w_, x_+w_/2, x_ };
    float hy[8] = { y_, y_,    y_+h_, y_+h_,   y_,      y_+h_/2, y_+h_, y_+h_/2 };

    for (int i = 0; i < 8; ++i) {
        float dx = imgX - hx[i];
        float dy = imgY - hy[i];
        if (dx*dx + dy*dy <= r*r) return i;
    }
    return -1;
}

Image CropTool::apply(const Image& src) const {
    if (!hasRect_ || w_ < 1 || h_ < 1) return src.clone();
    int cx = static_cast<int>(std::round(x_));
    int cy = static_cast<int>(std::round(y_));
    int cw = static_cast<int>(std::round(w_));
    int ch = static_cast<int>(std::round(h_));
    return src.cropped(cx, cy, cw, ch);
}

void CropTool::reset() {
    active_  = false;
    hasRect_ = false;
    dragHandle_ = -1;
    x_ = y_ = w_ = h_ = 0;
}

void CropTool::enforceAspect() {
    if (aspectRatio_ == AspectRatio::Free) return;

    float ratio = 1.0f;
    switch (aspectRatio_) {
        case AspectRatio::Square:   ratio = 1.0f;       break;
        case AspectRatio::Ratio4_3: ratio = 4.0f/3.0f;  break;
        case AspectRatio::Ratio16_9:ratio = 16.0f/9.0f; break;
        case AspectRatio::Ratio3_2: ratio = 3.0f/2.0f;  break;
        default: return;
    }

    // Adjust height to match width * ratio
    float desiredH = w_ / ratio;
    h_ = desiredH;
}
