#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

// Float32 RGBA image buffer backed by OpenImageIO for I/O
class Image {
public:
    Image() = default;
    Image(int w, int h);
    Image(int w, int h, const std::vector<float>& data);

    bool load(const std::string& path);
    bool save(const std::string& path) const;

    Image clone() const;
    Image resized(int newW, int newH) const;
    Image cropped(int x, int y, int cropW, int cropH) const;

    // Pixel access (RGBA, 0-1 float range)
    void  getPixel(int x, int y, float rgba[4]) const;
    void  setPixel(int x, int y, const float rgba[4]);

    float*       data()       { return pixels_.data(); }
    const float* data() const { return pixels_.data(); }

    int   width()    const { return width_; }
    int   height()   const { return height_; }
    int   channels() const { return 4; }
    bool  empty()    const { return pixels_.empty(); }

    size_t pixelCount() const { return static_cast<size_t>(width_) * height_; }
    size_t dataSize()   const { return pixels_.size(); }

    const std::string& filePath() const { return filePath_; }

private:
    int width_  = 0;
    int height_ = 0;
    std::vector<float> pixels_; // RGBA interleaved, row-major
    std::string filePath_;
};
