#include "AlphaTestPattern.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

void writePixel(uint8_t* dst, uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
    dst[0] = b;
    dst[1] = g;
    dst[2] = r;
    dst[3] = a;
}

bool inCircle(int x, int y, int cx, int cy, int radius) {
    const int dx = x - cx;
    const int dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

bool inRect(int x, int y, int left, int top, int right, int bottom) {
    return x >= left && x < right && y >= top && y < bottom;
}

} // namespace

void AlphaTestPattern::setSize(int width, int height) {
    width_ = std::max(64, width);
    height_ = std::max(64, height);
}

void AlphaTestPattern::setAlphaScale(float scale) {
    alphaScale_ = std::clamp(scale, 0.f, 1.f);
}

void AlphaTestPattern::fillFrame(std::vector<uint8_t>& bgra, int& width, int& height, int& stride,
                                 uint64_t frameIndex) const {
    width = width_;
    height = height_;
    stride = width_ * 4;
    bgra.assign(static_cast<size_t>(stride) * static_cast<size_t>(height_), 0);

    const int cx = width_ / 2;
    const int cy = height_ / 2;
    const float phase = static_cast<float>(frameIndex % 120) / 120.f;
    const int movingRadius = std::min(width_, height_) / 6;
    const int movingCx = cx + static_cast<int>(std::cos(phase * 6.28318f) * (width_ / 4));
    const int movingCy = cy + static_cast<int>(std::sin(phase * 6.28318f) * (height_ / 5));

    const int bar = std::max(24, std::min(width_, height_) / 24);

    for (int y = 0; y < height_; ++y) {
        uint8_t* row = bgra.data() + static_cast<size_t>(y) * stride;
        for (int x = 0; x < width_; ++x) {
            uint8_t* px = row + x * 4;

            if (inRect(x, y, 0, 0, width_, bar) || inRect(x, y, 0, height_ - bar, width_, height_) ||
                inRect(x, y, 0, 0, bar, height_) || inRect(x, y, width_ - bar, 0, width_, height_)) {
                writePixel(px, 30, 180, 255, 255);
                continue;
            }

            if (inCircle(x, y, cx, cy, std::min(width_, height_) / 3)) {
                const uint8_t r = static_cast<uint8_t>(40 + (x * 120 / std::max(1, width_)));
                const uint8_t g = static_cast<uint8_t>(180 + (y * 60 / std::max(1, height_)));
                writePixel(px, 220, g, r, 128);
                continue;
            }

            if (inRect(x, y, cx - width_ / 8, cy - height_ / 8, cx + width_ / 8, cy + height_ / 8)) {
                writePixel(px, 60, 200, 80, 255);
                continue;
            }

            if (inCircle(x, y, movingCx, movingCy, movingRadius)) {
                writePixel(px, 255, 80, 80, 200);
                continue;
            }

            writePixel(px, 0, 0, 0, 0);
        }
    }

    if (alphaScale_ >= 0.999f) {
        return;
    }

    const size_t total = static_cast<size_t>(stride) * static_cast<size_t>(height_);
    for (size_t i = 3; i < total; i += 4) {
        bgra[i] = static_cast<uint8_t>(static_cast<float>(bgra[i]) * alphaScale_);
    }
}
