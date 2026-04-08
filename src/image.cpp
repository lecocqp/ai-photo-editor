#include "image.h"

#include <OpenImageIO/imageio.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace OIIO = OIIO_NAMESPACE;

Image::Image(int w, int h)
    : width_(w), height_(h), pixels_(static_cast<size_t>(w) * h * 4, 0.0f)
{}

Image::Image(int w, int h, const std::vector<float>& data)
    : width_(w), height_(h), pixels_(data)
{}

bool Image::load(const std::string& path) {
    OIIO::ImageBuf srcBuf(path);
    if (!srcBuf.read()) return false;

    // Apply EXIF orientation (rotates portrait photos correctly)
    OIIO::ImageBuf oriented = OIIO::ImageBufAlgo::reorient(srcBuf);

    const OIIO::ImageSpec& spec = oriented.spec();
    width_  = spec.width;
    height_ = spec.height;
    int srcChannels = spec.nchannels;

    // Read pixels as float with the source channel count
    std::vector<float> tmp(static_cast<size_t>(width_) * height_ * srcChannels);
    oriented.get_pixels(OIIO::ROI::All(), OIIO::TypeFloat, tmp.data());

    // Convert to RGBA
    pixels_.resize(static_cast<size_t>(width_) * height_ * 4);
    size_t npix = pixelCount();
    for (size_t i = 0; i < npix; ++i) {
        const float* src = &tmp[i * srcChannels];
        float* dst = &pixels_[i * 4];
        dst[0] = src[0];
        dst[1] = (srcChannels > 1) ? src[1] : src[0];
        dst[2] = (srcChannels > 2) ? src[2] : src[0];
        dst[3] = (srcChannels > 3) ? src[3] : 1.0f;
    }

    filePath_ = path;
    return true;
}

bool Image::save(const std::string& path) const {
    if (empty()) return false;

    // Determine output channels from extension
    int outChannels = 4;
    std::string ext = path.substr(path.find_last_of('.') + 1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(c));
    if (ext == "jpg" || ext == "jpeg" || ext == "bmp") {
        outChannels = 3; // no alpha support
    }

    OIIO::ImageSpec spec(width_, height_, outChannels, OIIO::TypeFloat);
    auto out = OIIO::ImageOutput::create(path);
    if (!out) return false;

    if (!out->open(path, spec)) return false;

    if (outChannels == 4) {
        out->write_image(OIIO::TypeFloat, pixels_.data());
    } else {
        // Strip alpha
        std::vector<float> rgb(static_cast<size_t>(width_) * height_ * 3);
        size_t npix = pixelCount();
        for (size_t i = 0; i < npix; ++i) {
            rgb[i * 3 + 0] = pixels_[i * 4 + 0];
            rgb[i * 3 + 1] = pixels_[i * 4 + 1];
            rgb[i * 3 + 2] = pixels_[i * 4 + 2];
        }
        out->write_image(OIIO::TypeFloat, rgb.data());
    }

    out->close();
    return true;
}

Image Image::clone() const {
    Image img;
    img.width_  = width_;
    img.height_ = height_;
    img.pixels_ = pixels_;
    img.filePath_ = filePath_;
    return img;
}

Image Image::resized(int newW, int newH) const {
    if (empty() || newW <= 0 || newH <= 0) return {};

    OIIO::ImageSpec srcSpec(width_, height_, 4, OIIO::TypeFloat);
    OIIO::ImageBuf srcBuf(srcSpec, const_cast<float*>(pixels_.data()));

    OIIO::ImageSpec dstSpec(newW, newH, 4, OIIO::TypeFloat);
    OIIO::ImageBuf dstBuf(dstSpec);

    OIIO::ImageBufAlgo::resize(dstBuf, srcBuf);

    Image result(newW, newH);
    std::memcpy(result.data(), dstBuf.localpixels(), result.dataSize() * sizeof(float));
    return result;
}

Image Image::cropped(int x, int y, int cropW, int cropH) const {
    if (empty()) return {};

    x = std::max(0, std::min(x, width_ - 1));
    y = std::max(0, std::min(y, height_ - 1));
    cropW = std::min(cropW, width_ - x);
    cropH = std::min(cropH, height_ - y);
    if (cropW <= 0 || cropH <= 0) return {};

    Image result(cropW, cropH);
    for (int row = 0; row < cropH; ++row) {
        const float* src = &pixels_[((y + row) * width_ + x) * 4];
        float* dst = &result.pixels_[row * cropW * 4];
        std::memcpy(dst, src, cropW * 4 * sizeof(float));
    }
    return result;
}

void Image::getPixel(int x, int y, float rgba[4]) const {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) {
        rgba[0] = rgba[1] = rgba[2] = 0.0f;
        rgba[3] = 1.0f;
        return;
    }
    size_t idx = (static_cast<size_t>(y) * width_ + x) * 4;
    rgba[0] = pixels_[idx + 0];
    rgba[1] = pixels_[idx + 1];
    rgba[2] = pixels_[idx + 2];
    rgba[3] = pixels_[idx + 3];
}

void Image::setPixel(int x, int y, const float rgba[4]) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    size_t idx = (static_cast<size_t>(y) * width_ + x) * 4;
    pixels_[idx + 0] = rgba[0];
    pixels_[idx + 1] = rgba[1];
    pixels_[idx + 2] = rgba[2];
    pixels_[idx + 3] = rgba[3];
}
