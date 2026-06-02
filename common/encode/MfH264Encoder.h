#pragma once

#include <cstdint>
#include <d3d11.h>
#include <functional>
#include <memory>
#include <vector>
#include <wrl/client.h>

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

    bool open(int width, int height, int frameRateN, int frameRateD, uint32_t bitrate,
              ID3D11Device* d3dDevice = nullptr);
    void close();
    bool isOpen() const { return open_; }
    bool usesGpuPath() const { return gpuPath_; }

    bool encodeBGRA(const uint8_t* data, int stride, int64_t timestamp100ns);
    bool encodeGpuBgraTexture(ID3D11Texture2D* bgraTexture, int64_t timestamp100ns);

    void setCallback(FrameCallback cb) { callback_ = std::move(cb); }

private:
    bool createCpuEncoder(int width, int height, int frameRateN, int frameRateD, uint32_t bitrate);
    bool createGpuEncoder(int width, int height, int frameRateN, int frameRateD, uint32_t bitrate);
    bool processOutput();
    bool encodeNv12Texture(ID3D11Texture2D* nv12Texture, int64_t timestamp100ns);

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::unique_ptr<class GpuBgraToNv12> bgraToNv12_;

    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    bool open_ = false;
    bool gpuPath_ = false;
    FrameCallback callback_;
    int width_ = 0;
    int height_ = 0;
    int frameRateN_ = 60000;
    int frameRateD_ = 1001;
    int64_t frameIndex_ = 0;
};
