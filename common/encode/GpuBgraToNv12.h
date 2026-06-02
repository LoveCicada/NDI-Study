#pragma once

#include <d3d11.h>
#include <wrl/client.h>

class GpuBgraToNv12 {
public:
    GpuBgraToNv12();
    ~GpuBgraToNv12();

    GpuBgraToNv12(const GpuBgraToNv12&) = delete;
    GpuBgraToNv12& operator=(const GpuBgraToNv12&) = delete;

    bool open(ID3D11Device* device, int width, int height);
    void close();
    bool isOpen() const { return videoProcessor_ != nullptr; }

    // Returns internal NV12 texture; valid until the next convert() call.
    ID3D11Texture2D* convert(ID3D11Texture2D* bgraTexture);

private:
    bool createResources(int width, int height);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> videoDevice_;
    Microsoft::WRL::ComPtr<ID3D11VideoContext> videoContext_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> videoProcessorEnum_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> videoProcessor_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> nv12Texture_;

    int width_ = 0;
    int height_ = 0;
};
