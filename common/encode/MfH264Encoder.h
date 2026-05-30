#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct EncodedH264Frame {
    std::vector<uint8_t> data;
    int64_t timestamp100ns = 0;
    bool keyFrame = false;
};

class MfH264Encoder {
public:
    using FrameCallback = std::function<void(const EncodedH264Frame&)>;

    MfH264Encoder();
    ~MfH264Encoder();

    MfH264Encoder(const MfH264Encoder&) = delete;
    MfH264Encoder& operator=(const MfH264Encoder&) = delete;

    bool open(int width, int height, int frameRateN, int frameRateD, uint32_t bitrate);
    void close();
    bool isOpen() const { return open_; }

    bool encodeBGRA(const uint8_t* data, int stride, int64_t timestamp100ns);
    void setCallback(FrameCallback cb) { callback_ = std::move(cb); }

private:
    bool createEncoder(int width, int height, int frameRateN, int frameRateD, uint32_t bitrate);
    bool processOutput();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool open_ = false;
    FrameCallback callback_;
    int width_ = 0;
    int height_ = 0;
    int64_t frameIndex_ = 0;
};
