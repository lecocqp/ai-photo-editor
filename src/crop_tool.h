#pragma once

#include "image.h"

enum class AspectRatio {
    Free, Square, Ratio4_3, Ratio16_9, Ratio3_2
};

// Interactive crop tool that tracks a selection rectangle in image space.
class CropTool {
public:
    CropTool() = default;

    // Start a new selection at image coordinates
    void begin(float imgX, float imgY);
    // Update the selection endpoint
    void update(float imgX, float imgY);
    // Finish the selection
    void end();

    // Drag a handle (0–7: corners TL,TR,BR,BL then edges T,R,B,L)
    void beginHandleDrag(int handleIdx, float imgX, float imgY);
    void updateHandleDrag(float imgX, float imgY);

    // Hit-test handles, returns index or -1
    int hitTestHandle(float imgX, float imgY, float handleRadius) const;

    // Apply the crop to the image, returns cropped result
    Image apply(const Image& src) const;

    // Reset / cancel
    void reset();

    // State
    bool  isActive()   const { return active_; }
    bool  hasCrop()    const { return hasRect_; }
    float x()          const { return x_; }
    float y()          const { return y_; }
    float width()      const { return w_; }
    float height()     const { return h_; }

    void  setAspectRatio(AspectRatio ar) { aspectRatio_ = ar; }
    AspectRatio aspectRatio() const { return aspectRatio_; }

private:
    void enforceAspect();
    void clampToImage(int imgW, int imgH);

    bool  active_  = false;
    bool  hasRect_ = false;
    float x_ = 0, y_ = 0, w_ = 0, h_ = 0;
    float startX_ = 0, startY_ = 0;

    // Handle dragging state
    int   dragHandle_ = -1;
    float dragStartX_ = 0, dragStartY_ = 0;
    float origX_ = 0, origY_ = 0, origW_ = 0, origH_ = 0;

    AspectRatio aspectRatio_ = AspectRatio::Free;
};
