#pragma once

#include <cstdint>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <string>
#include <vector>

struct DxgiOutputInfo {
    int index = 0;
    std::string name;
    int width = 0;
    int height = 0;
};

struct CapturedFrame {
    std::vector<uint8_t> bgra;
    int width = 0;
    int height = 0;
    int stride = 0;
};

struct CapturedGpuFrame {
    ID3D11Texture2D* texture = nullptr;
    int width = 0;
    int height = 0;
};

class DxgiScreenCapture {
public:
    DxgiScreenCapture();
    ~DxgiScreenCapture();

    DxgiScreenCapture(const DxgiScreenCapture&) = delete;
    DxgiScreenCapture& operator=(const DxgiScreenCapture&) = delete;

    static std::vector<DxgiOutputInfo> listOutputs();

    bool open(int outputIndex);
    void close();
    bool isOpen() const { return duplication_ != nullptr; }

    bool captureFrame(CapturedFrame& out, uint32_t timeoutMs = 100);

    // GPU-only capture: copies duplication texture to an internal pool; no CPU readback.
    bool captureGpuFrame(CapturedGpuFrame& out, uint32_t timeoutMs = 100);

    ID3D11Device* device() const { return device_.Get(); }
    ID3D11DeviceContext* context() const { return context_.Get(); }

    int width() const { return width_; }
    int height() const { return height_; }

private:
    bool initDuplication(int outputIndex);
    void recreateStagingTexture();
    void recreateGpuPoolTexture();

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> gpuPool_;

    int width_ = 0;
    int height_ = 0;
    int outputIndex_ = 0;
};
