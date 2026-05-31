#pragma once

#include "VideoFormatConvert.h"

#include <Processing.NDI.Lib.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

enum class NdiSendMode {
    HighBandwidth,
    HxH264,
};

struct NdiSenderConfig {
    std::string ndiName = "NDISender Demo";
    std::string groups;
    bool clockVideo = true;
    bool clockAudio = false;
    NdiSendMode mode = NdiSendMode::HighBandwidth;
    bool enableVideo = true;
    bool enableAudio = true;
    float hxBitrateMultiplier = 1.0f;
    NdiSendColorFormatChoice colorFormat = NdiSendColorFormatChoice::BGRA;
};

class NdiSender {
public:
    NdiSender();
    ~NdiSender();

    NdiSender(const NdiSender&) = delete;
    NdiSender& operator=(const NdiSender&) = delete;

    bool create(const NdiSenderConfig& config);
    void destroy();

    bool isActive() const { return sender_ != nullptr; }
    const NdiSenderConfig& config() const { return config_; }

    bool sendVideo(const uint8_t* bgra, int width, int height, int bgraStride,
                   int frameRateN, int frameRateD, NdiSendColorFormatChoice format);
    bool sendVideoBGRA(const uint8_t* data, int width, int height, int stride,
                       int frameRateN, int frameRateD);
    bool sendVideoH264(const uint8_t* data, size_t size, int width, int height,
                       int frameRateN, int frameRateD, int64_t timecode100ns);
    bool sendAudio(const float* interleaved, int sampleRate, int channels, int samples);
    void flushVideoAsync();

    int getTargetBitrate(int width, int height, int frameRateN, int frameRateD) const;

private:
    NDIlib_send_instance_t sender_ = nullptr;
    NdiSenderConfig config_;
    std::vector<float> audioBuffer_;
    std::vector<uint8_t> videoSendBuffers_[2];
    int videoSendBufferIndex_ = 0;
};
