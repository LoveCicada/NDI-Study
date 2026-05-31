#include "VideoFormatConvert.h"

#include <algorithm>
#include <cstring>

NDIlib_FourCC_video_type_e sendColorFormatToFourCC(NdiSendColorFormatChoice choice) {
    switch (choice) {
    case NdiSendColorFormatChoice::BGRX:
        return NDIlib_FourCC_type_BGRX;
    case NdiSendColorFormatChoice::UYVY:
        return NDIlib_FourCC_type_UYVY;
    case NdiSendColorFormatChoice::UYVA:
        return NDIlib_FourCC_type_UYVA;
    case NdiSendColorFormatChoice::BGRA:
    default:
        return NDIlib_FourCC_type_BGRA;
    }
}

std::string sendColorFormatName(NdiSendColorFormatChoice choice) {
    switch (choice) {
    case NdiSendColorFormatChoice::BGRX:
        return "BGRX";
    case NdiSendColorFormatChoice::UYVY:
        return "UYVY";
    case NdiSendColorFormatChoice::UYVA:
        return "UYVA";
    case NdiSendColorFormatChoice::BGRA:
    default:
        return "BGRA";
    }
}

size_t uyvaBufferSize(int width, int height, int uyvyStride) {
    const int rowStride = uyvyStride > 0 ? uyvyStride : width * 2;
    return static_cast<size_t>(rowStride) * static_cast<size_t>(height)
        + static_cast<size_t>(rowStride / 2) * static_cast<size_t>(height);
}

void uyvyToRgb(uint8_t y, uint8_t u, uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b) {
    const float Y = y / 255.f;
    const float U = u / 255.f - 0.5f;
    const float V = v / 255.f - 0.5f;
    r = static_cast<uint8_t>(std::min(255.f, (Y + 1.402f * V) * 255.f));
    g = static_cast<uint8_t>(std::min(255.f, (Y - 0.344136f * U - 0.714136f * V) * 255.f));
    b = static_cast<uint8_t>(std::min(255.f, (Y + 1.772f * U) * 255.f));
}

void rgbToUyvy(uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1, uint8_t g1, uint8_t b1,
               uint8_t& u, uint8_t& y0, uint8_t& v, uint8_t& y1) {
    const auto clampByte = [](float x) {
        return static_cast<uint8_t>(std::clamp(x, 0.f, 255.f));
    };
    const float rf0 = r0 / 255.f;
    const float gf0 = g0 / 255.f;
    const float bf0 = b0 / 255.f;
    const float rf1 = r1 / 255.f;
    const float gf1 = g1 / 255.f;
    const float bf1 = b1 / 255.f;

    const float yf0 = 0.299f * rf0 + 0.587f * gf0 + 0.114f * bf0;
    const float yf1 = 0.299f * rf1 + 0.587f * gf1 + 0.114f * bf1;
    const float uf = -0.169f * rf0 - 0.331f * gf0 + 0.500f * bf0 + 0.5f;
    const float vf = 0.500f * rf0 - 0.419f * gf0 - 0.081f * bf0 + 0.5f;

    y0 = clampByte(yf0 * 255.f);
    y1 = clampByte(yf1 * 255.f);
    u = clampByte(uf * 255.f);
    v = clampByte(vf * 255.f);
}

namespace {

void copyBgraPlane(std::vector<uint8_t>& out, int width, int height, int bgraStride,
                   const uint8_t* bgra, bool forceOpaqueAlpha) {
    const int rowStride = bgraStride > 0 ? bgraStride : width * 4;
    const size_t size = static_cast<size_t>(rowStride) * static_cast<size_t>(height);
    out.resize(size);
    for (int y = 0; y < height; ++y) {
        const uint8_t* src = bgra + static_cast<size_t>(y) * rowStride;
        uint8_t* dst = out.data() + static_cast<size_t>(y) * rowStride;
        if (!forceOpaqueAlpha) {
            std::memcpy(dst, src, static_cast<size_t>(width) * 4);
            continue;
        }
        for (int x = 0; x < width; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 0];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 2];
            dst[x * 4 + 3] = 255;
        }
    }
}

void convertBgraToUyvyPlane(std::vector<uint8_t>& out, int width, int height, int bgraStride,
                            const uint8_t* bgra) {
    const int srcStride = bgraStride > 0 ? bgraStride : width * 4;
    const int dstStride = width * 2;
    out.assign(static_cast<size_t>(dstStride) * static_cast<size_t>(height), 0);
    for (int y = 0; y < height; ++y) {
        const uint8_t* srcRow = bgra + static_cast<size_t>(y) * srcStride;
        uint8_t* dstRow = out.data() + static_cast<size_t>(y) * dstStride;
        for (int x = 0; x < width; x += 2) {
            const uint8_t b0 = srcRow[x * 4 + 0];
            const uint8_t g0 = srcRow[x * 4 + 1];
            const uint8_t r0 = srcRow[x * 4 + 2];
            uint8_t b1 = b0;
            uint8_t g1 = g0;
            uint8_t r1 = r0;
            if (x + 1 < width) {
                b1 = srcRow[(x + 1) * 4 + 0];
                g1 = srcRow[(x + 1) * 4 + 1];
                r1 = srcRow[(x + 1) * 4 + 2];
            }
            uint8_t u = 0;
            uint8_t y0 = 0;
            uint8_t v = 0;
            uint8_t y1 = 0;
            rgbToUyvy(r0, g0, b0, r1, g1, b1, u, y0, v, y1);
            dstRow[x * 2 + 0] = u;
            dstRow[x * 2 + 1] = y0;
            dstRow[x * 2 + 2] = v;
            dstRow[x * 2 + 3] = y1;
        }
    }
}

void convertBgraToUyvaBuffer(std::vector<uint8_t>& out, int width, int height, int bgraStride,
                             const uint8_t* bgra) {
    const int srcStride = bgraStride > 0 ? bgraStride : width * 4;
    const int uyvyStride = width * 2;
    const size_t total = uyvaBufferSize(width, height, uyvyStride);
    out.assign(total, 0);

    uint8_t* uyvyBase = out.data();
    uint8_t* alphaBase = out.data() + static_cast<size_t>(uyvyStride) * static_cast<size_t>(height);

    for (int y = 0; y < height; ++y) {
        const uint8_t* srcRow = bgra + static_cast<size_t>(y) * srcStride;
        uint8_t* uyvyRow = uyvyBase + static_cast<size_t>(y) * uyvyStride;
        uint8_t* alphaRow = alphaBase + static_cast<size_t>(y) * width;
        for (int x = 0; x < width; x += 2) {
            const uint8_t b0 = srcRow[x * 4 + 0];
            const uint8_t g0 = srcRow[x * 4 + 1];
            const uint8_t r0 = srcRow[x * 4 + 2];
            alphaRow[x] = srcRow[x * 4 + 3];
            uint8_t b1 = b0;
            uint8_t g1 = g0;
            uint8_t r1 = r0;
            if (x + 1 < width) {
                b1 = srcRow[(x + 1) * 4 + 0];
                g1 = srcRow[(x + 1) * 4 + 1];
                r1 = srcRow[(x + 1) * 4 + 2];
                alphaRow[x + 1] = srcRow[(x + 1) * 4 + 3];
            }
            uint8_t u = 0;
            uint8_t y0 = 0;
            uint8_t v = 0;
            uint8_t y1 = 0;
            rgbToUyvy(r0, g0, b0, r1, g1, b1, u, y0, v, y1);
            uyvyRow[x * 2 + 0] = u;
            uyvyRow[x * 2 + 1] = y0;
            uyvyRow[x * 2 + 2] = v;
            uyvyRow[x * 2 + 3] = y1;
        }
    }
}

} // namespace

bool packBgraForSend(std::vector<uint8_t>& out, int& outStride, NDIlib_FourCC_video_type_e& outFourCC,
                     const uint8_t* bgra, int width, int height, int bgraStride,
                     NdiSendColorFormatChoice format) {
    if (!bgra || width <= 0 || height <= 0) {
        return false;
    }

    switch (format) {
    case NdiSendColorFormatChoice::BGRX:
        copyBgraPlane(out, width, height, bgraStride, bgra, true);
        outStride = width * 4;
        outFourCC = NDIlib_FourCC_type_BGRX;
        return true;
    case NdiSendColorFormatChoice::UYVY:
        convertBgraToUyvyPlane(out, width, height, bgraStride, bgra);
        outStride = width * 2;
        outFourCC = NDIlib_FourCC_type_UYVY;
        return true;
    case NdiSendColorFormatChoice::UYVA:
        convertBgraToUyvaBuffer(out, width, height, bgraStride, bgra);
        outStride = width * 2;
        outFourCC = NDIlib_FourCC_type_UYVA;
        return true;
    case NdiSendColorFormatChoice::BGRA:
    default:
        copyBgraPlane(out, width, height, bgraStride, bgra, false);
        outStride = bgraStride > 0 ? bgraStride : width * 4;
        outFourCC = NDIlib_FourCC_type_BGRA;
        return true;
    }
}

bool convertUyvaFrameToBgra(std::vector<uint8_t>& outBgra, int& outStride, int width, int height,
                            int uyvyStride, const uint8_t* uyvaData) {
    if (!uyvaData || width <= 0 || height <= 0) {
        return false;
    }

    const int rowUyvyStride = uyvyStride > 0 ? uyvyStride : width * 2;
    outStride = width * 4;
    outBgra.assign(static_cast<size_t>(outStride) * static_cast<size_t>(height), 0);

    const uint8_t* uyvy = uyvaData;
    const uint8_t* alphaPlane = uyvaData + static_cast<size_t>(rowUyvyStride) * static_cast<size_t>(height);

    for (int y = 0; y < height; ++y) {
        const uint8_t* uyvyRow = uyvy + static_cast<size_t>(y) * rowUyvyStride;
        const uint8_t* alphaRow = alphaPlane + static_cast<size_t>(y) * width;
        uint8_t* dstRow = outBgra.data() + static_cast<size_t>(y) * outStride;
        for (int x = 0; x < width; x += 2) {
            const uint8_t u = uyvyRow[0];
            const uint8_t y0 = uyvyRow[1];
            const uint8_t v = uyvyRow[2];
            const uint8_t y1 = uyvyRow[3];
            uyvyRow += 4;

            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            uyvyToRgb(y0, u, v, r, g, b);
            dstRow[x * 4 + 0] = b;
            dstRow[x * 4 + 1] = g;
            dstRow[x * 4 + 2] = r;
            dstRow[x * 4 + 3] = alphaRow[x];

            if (x + 1 < width) {
                uyvyToRgb(y1, u, v, r, g, b);
                dstRow[(x + 1) * 4 + 0] = b;
                dstRow[(x + 1) * 4 + 1] = g;
                dstRow[(x + 1) * 4 + 2] = r;
                dstRow[(x + 1) * 4 + 3] = alphaRow[x + 1];
            }
        }
    }
    return true;
}
