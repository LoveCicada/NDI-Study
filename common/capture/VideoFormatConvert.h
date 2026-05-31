#pragma once

#include <Processing.NDI.Lib.h>

#include <cstdint>
#include <string>
#include <vector>

enum class NdiSendColorFormatChoice {
    BGRA,
    BGRX,
    UYVY,
    UYVA,
};

NDIlib_FourCC_video_type_e sendColorFormatToFourCC(NdiSendColorFormatChoice choice);
std::string sendColorFormatName(NdiSendColorFormatChoice choice);

void uyvyToRgb(uint8_t y, uint8_t u, uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b);
void rgbToUyvy(uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1, uint8_t g1, uint8_t b1,
               uint8_t& u, uint8_t& y0, uint8_t& v, uint8_t& y1);

bool packBgraForSend(std::vector<uint8_t>& out, int& outStride, NDIlib_FourCC_video_type_e& outFourCC,
                     const uint8_t* bgra, int width, int height, int bgraStride,
                     NdiSendColorFormatChoice format);

bool convertUyvaFrameToBgra(std::vector<uint8_t>& outBgra, int& outStride, int width, int height,
                            int uyvyStride, const uint8_t* uyvaData);

size_t uyvaBufferSize(int width, int height, int uyvyStride);
