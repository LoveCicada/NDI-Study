#pragma once

#include <cstdint>
#include <vector>

class AlphaTestPattern {
public:
    void setSize(int width, int height);
    void setAlphaScale(float scale);

    void fillFrame(std::vector<uint8_t>& bgra, int& width, int& height, int& stride,
                   uint64_t frameIndex) const;

private:
    int width_ = 1280;
    int height_ = 720;
    float alphaScale_ = 1.f;
};
