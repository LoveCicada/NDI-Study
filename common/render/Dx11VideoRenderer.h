#pragma once

#include <Processing.NDI.Lib.h>

#include <cstdint>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <vector>

class Dx11VideoRenderer {
public:
    Dx11VideoRenderer();
    ~Dx11VideoRenderer();

    Dx11VideoRenderer(const Dx11VideoRenderer&) = delete;
    Dx11VideoRenderer& operator=(const Dx11VideoRenderer&) = delete;

    bool initialize(void* windowHandle, int width, int height);
    void resize(int width, int height);
    void renderFrame(const uint8_t* data, int width, int height, int stride,
                     NDIlib_FourCC_video_type_e fourCC);
    void present();
    void clear();

    ID3D11Device* device() const { return device_.Get(); }

private:
    bool createDevice(void* windowHandle, int width, int height);
    bool createShaders();
    bool ensureTexture(int width, int height, DXGI_FORMAT format);
    void uploadUyvy(const uint8_t* data, int width, int height, int stride);
    void uploadBgra(const uint8_t* data, int width, int height, int stride);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psBgra_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psUyvy_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vb_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;

    int clientWidth_ = 0;
    int clientHeight_ = 0;
    int texWidth_ = 0;
    int texHeight_ = 0;
    DXGI_FORMAT texFormat_ = DXGI_FORMAT_B8G8R8A8_UNORM;
};
