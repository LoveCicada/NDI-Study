#include "Dx11VideoRenderer.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace {

const char* kVertexShader = R"(
struct VS_IN { float2 pos : POSITION; float2 uv : TEXCOORD0; };
struct VS_OUT { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VS_OUT main(VS_IN input) {
    VS_OUT o;
    o.pos = float4(input.pos, 0.0, 1.0);
    o.uv = input.uv;
    return o;
}
)";

const char* kPixelShaderBgra = R"(
Texture2D tex : register(t0);
SamplerState samp : register(s0);
float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target {
    return tex.Sample(samp, uv);
}
)";

const char* kPixelShaderUyvy = R"(
Texture2D tex : register(t0);
SamplerState samp : register(s0);
float3 yuvToRgb(float y, float u, float v) {
    float r = y + 1.402 * v;
    float g = y - 0.344136 * u - 0.714136 * v;
    float b = y + 1.772 * u;
    return saturate(float3(r, g, b));
}
float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target {
    float4 packed = tex.Sample(samp, uv);
    float y = packed.r;
    float u = packed.g - 0.5;
    float v = packed.b - 0.5;
    return float4(yuvToRgb(y, u, v), 1.0);
}
)";

const char* kPixelShaderChecker = R"(
cbuffer CheckerParams : register(b0) {
    float2 viewportSize;
    float2 pad;
};
float4 main(float4 pos : SV_POSITION) : SV_Target {
    const float tile = 16.0;
    int cx = int(pos.x / tile);
    int cy = int(pos.y / tile);
    bool dark = ((cx + cy) & 1) == 0;
    float3 light = float3(0.82, 0.82, 0.82);
    float3 darkColor = float3(0.45, 0.45, 0.45);
    return float4(dark ? darkColor : light, 1.0);
}
)";

bool isBgraFourCC(NDIlib_FourCC_video_type_e fourCC) {
    return fourCC == NDIlib_FourCC_type_BGRA || fourCC == NDIlib_FourCC_type_BGRX;
}

bool hasAlphaChannel(NDIlib_FourCC_video_type_e fourCC) {
    return fourCC == NDIlib_FourCC_type_BGRA;
}

} // namespace

Dx11VideoRenderer::Dx11VideoRenderer() = default;

Dx11VideoRenderer::~Dx11VideoRenderer() {
    clear();
}

void Dx11VideoRenderer::setAlphaCheckerBackground(bool enabled) {
    alphaCheckerBackground_ = enabled;
}

void Dx11VideoRenderer::setPreviewAlphaScale(float scale) {
    previewAlphaScale_ = std::clamp(scale, 0.f, 1.f);
}

bool Dx11VideoRenderer::initialize(void* windowHandle, int width, int height) {
    return createDevice(windowHandle, width, height) && createShaders();
}

bool Dx11VideoRenderer::createDevice(void* windowHandle, int width, int height) {
    clientWidth_ = width;
    clientHeight_ = height;

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = static_cast<UINT>(width);
    scd.BufferDesc.Height = static_cast<UINT>(height);
    scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = static_cast<HWND>(windowHandle);
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL level{};
    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
        D3D11_SDK_VERSION, &scd, swapChain_.GetAddressOf(),
        device_.GetAddressOf(), &level, context_.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<ID3D11Texture2D> backBuffer;
    swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuffer.GetAddressOf()));
    device_->CreateRenderTargetView(backBuffer.Get(), nullptr, rtv_.GetAddressOf());
    return true;
}

bool Dx11VideoRenderer::createShaders() {
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBgraBlob;
    ComPtr<ID3DBlob> psUyvyBlob;
    ComPtr<ID3DBlob> psCheckerBlob;
    ComPtr<ID3DBlob> err;

    if (FAILED(D3DCompile(kVertexShader, std::strlen(kVertexShader), nullptr, nullptr, nullptr,
                          "main", "vs_4_0", 0, 0, vsBlob.GetAddressOf(), err.GetAddressOf()))) {
        return false;
    }
    if (FAILED(D3DCompile(kPixelShaderBgra, std::strlen(kPixelShaderBgra), nullptr, nullptr, nullptr,
                          "main", "ps_4_0", 0, 0, psBgraBlob.GetAddressOf(), err.GetAddressOf()))) {
        return false;
    }
    if (FAILED(D3DCompile(kPixelShaderUyvy, std::strlen(kPixelShaderUyvy), nullptr, nullptr, nullptr,
                          "main", "ps_4_0", 0, 0, psUyvyBlob.GetAddressOf(), err.GetAddressOf()))) {
        return false;
    }
    if (FAILED(D3DCompile(kPixelShaderChecker, std::strlen(kPixelShaderChecker), nullptr, nullptr, nullptr,
                          "main", "ps_4_0", 0, 0, psCheckerBlob.GetAddressOf(), err.GetAddressOf()))) {
        return false;
    }

    device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                nullptr, vs_.GetAddressOf());
    device_->CreatePixelShader(psBgraBlob->GetBufferPointer(), psBgraBlob->GetBufferSize(),
                               nullptr, psBgra_.GetAddressOf());
    device_->CreatePixelShader(psUyvyBlob->GetBufferPointer(), psUyvyBlob->GetBufferSize(),
                               nullptr, psUyvy_.GetAddressOf());
    device_->CreatePixelShader(psCheckerBlob->GetBufferPointer(), psCheckerBlob->GetBufferSize(),
                               nullptr, psChecker_.GetAddressOf());

    const D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    device_->CreateInputLayout(layoutDesc, 2, vsBlob->GetBufferPointer(),
                             vsBlob->GetBufferSize(), layout_.GetAddressOf());

    struct Vertex {
        float x, y, u, v;
    };
    const Vertex verts[] = {
        {-1.f, 1.f, 0.f, 0.f},
        {1.f, 1.f, 1.f, 0.f},
        {-1.f, -1.f, 0.f, 1.f},
        {1.f, -1.f, 1.f, 1.f},
    };
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(verts);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = verts;
    device_->CreateBuffer(&bd, &init, vb_.GetAddressOf());

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    device_->CreateSamplerState(&sd, sampler_.GetAddressOf());

    D3D11_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device_->CreateBlendState(&blendDesc, alphaBlendState_.GetAddressOf());

    D3D11_BLEND_DESC opaqueDesc{};
    opaqueDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device_->CreateBlendState(&opaqueDesc, opaqueBlendState_.GetAddressOf());
    return true;
}

void Dx11VideoRenderer::resize(int width, int height) {
    if (!swapChain_ || width <= 0 || height <= 0) {
        return;
    }
    clientWidth_ = width;
    clientHeight_ = height;
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    rtv_.Reset();
    swapChain_->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height),
                              DXGI_FORMAT_UNKNOWN, 0);
    ComPtr<ID3D11Texture2D> backBuffer;
    swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuffer.GetAddressOf()));
    device_->CreateRenderTargetView(backBuffer.Get(), nullptr, rtv_.GetAddressOf());
}

bool Dx11VideoRenderer::ensureTexture(int width, int height, DXGI_FORMAT format) {
    if (texture_ && texWidth_ == width && texHeight_ == height && texFormat_ == format) {
        return true;
    }
    texture_.Reset();
    srv_.Reset();
    texWidth_ = width;
    texHeight_ = height;
    texFormat_ = format;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    if (FAILED(device_->CreateTexture2D(&desc, nullptr, texture_.GetAddressOf()))) {
        return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    return SUCCEEDED(device_->CreateShaderResourceView(texture_.Get(), &srvDesc, srv_.GetAddressOf()));
}

void Dx11VideoRenderer::uploadBgra(const uint8_t* data, int width, int height, int stride) {
    const int rowStride = stride > 0 ? stride : width * 4;
    const uint8_t* uploadData = data;
    if (previewAlphaScale_ < 0.999f && data) {
        const size_t total = static_cast<size_t>(rowStride) * static_cast<size_t>(height);
        scaledBgraBuffer_.resize(total);
        for (int y = 0; y < height; ++y) {
            const uint8_t* src = data + static_cast<size_t>(y) * rowStride;
            uint8_t* dst = scaledBgraBuffer_.data() + static_cast<size_t>(y) * rowStride;
            for (int x = 0; x < width; ++x) {
                dst[x * 4 + 0] = src[x * 4 + 0];
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4 + 2];
                dst[x * 4 + 3] = static_cast<uint8_t>(static_cast<float>(src[x * 4 + 3]) * previewAlphaScale_);
            }
        }
        uploadData = scaledBgraBuffer_.data();
    }

    if (!ensureTexture(width, height, DXGI_FORMAT_B8G8R8A8_UNORM)) {
        return;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(texture_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    const int rowBytes = width * 4;
    for (int y = 0; y < height; ++y) {
        std::memcpy(static_cast<uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
                    uploadData + static_cast<size_t>(y) * rowStride, rowBytes);
    }
    context_->Unmap(texture_.Get(), 0);
}

void Dx11VideoRenderer::uploadUyvy(const uint8_t* data, int width, int height, int stride) {
    if (!ensureTexture(width, height, DXGI_FORMAT_R8G8B8A8_UNORM)) {
        return;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(texture_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    for (int y = 0; y < height; ++y) {
        auto* dst = static_cast<uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
        const uint8_t* src = data + static_cast<size_t>(y) * stride;
        for (int x = 0; x < width; x += 2) {
            const uint8_t u0 = src[0];
            const uint8_t y0 = src[1];
            const uint8_t v0 = src[2];
            const uint8_t y1 = src[3];
            dst[x * 4 + 0] = y0;
            dst[x * 4 + 1] = u0;
            dst[x * 4 + 2] = v0;
            dst[x * 4 + 3] = 255;
            dst[(x + 1) * 4 + 0] = y1;
            dst[(x + 1) * 4 + 1] = u0;
            dst[(x + 1) * 4 + 2] = v0;
            dst[(x + 1) * 4 + 3] = 255;
            src += 4;
        }
    }
    context_->Unmap(texture_.Get(), 0);
}

void Dx11VideoRenderer::drawFullscreenQuad(ID3D11PixelShader* pixelShader) {
    const float blendFactor[4] = {0.f, 0.f, 0.f, 0.f};
    context_->OMSetBlendState(opaqueBlendState_.Get(), blendFactor, 0xFFFFFFFF);

    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(clientWidth_);
    vp.Height = static_cast<float>(clientHeight_);
    vp.MaxDepth = 1.f;
    context_->RSSetViewports(1, &vp);

    const UINT strideVb = 16;
    const UINT offset = 0;
    context_->IASetInputLayout(layout_.Get());
    context_->IASetVertexBuffers(0, 1, vb_.GetAddressOf(), &strideVb, &offset);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context_->VSSetShader(vs_.Get(), nullptr, 0);
    context_->PSSetShader(pixelShader, nullptr, 0);
    context_->Draw(4, 0);
}

void Dx11VideoRenderer::drawCheckerBackground() {
    context_->PSSetShaderResources(0, 0, nullptr);
    drawFullscreenQuad(psChecker_.Get());
}

void Dx11VideoRenderer::renderCheckerboardOnly() {
    if (!context_ || !rtv_ || !alphaCheckerBackground_) {
        return;
    }

    const float clearColor[] = {0.1f, 0.1f, 0.12f, 1.f};
    context_->ClearRenderTargetView(rtv_.Get(), clearColor);
    context_->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);
    drawCheckerBackground();
}

void Dx11VideoRenderer::renderFrame(const uint8_t* data, int width, int height, int stride,
                                    NDIlib_FourCC_video_type_e fourCC) {
    if (!data || !context_ || !rtv_) {
        return;
    }

    const bool bgra = isBgraFourCC(fourCC);
    if (bgra) {
        uploadBgra(data, width, height, stride);
    } else {
        uploadUyvy(data, width, height, stride > 0 ? stride : width * 2);
    }

    const bool useAlphaBlend = alphaCheckerBackground_
        && (hasAlphaChannel(fourCC) || previewAlphaScale_ < 0.999f);

    const float clearColor[] = {0.1f, 0.1f, 0.12f, 1.f};
    context_->ClearRenderTargetView(rtv_.Get(), clearColor);
    context_->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);

    if (alphaCheckerBackground_) {
        drawCheckerBackground();
    }

    const float blendFactor[4] = {0.f, 0.f, 0.f, 0.f};
    context_->OMSetBlendState(useAlphaBlend ? alphaBlendState_.Get() : opaqueBlendState_.Get(),
                              blendFactor, 0xFFFFFFFF);

    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(clientWidth_);
    vp.Height = static_cast<float>(clientHeight_);
    vp.MaxDepth = 1.f;
    context_->RSSetViewports(1, &vp);

    const UINT strideVb = 16;
    const UINT offset = 0;
    context_->IASetInputLayout(layout_.Get());
    context_->IASetVertexBuffers(0, 1, vb_.GetAddressOf(), &strideVb, &offset);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context_->VSSetShader(vs_.Get(), nullptr, 0);
    context_->PSSetShader(bgra ? psBgra_.Get() : psUyvy_.Get(), nullptr, 0);
    context_->PSSetShaderResources(0, 1, srv_.GetAddressOf());
    context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    context_->Draw(4, 0);

    context_->OMSetBlendState(opaqueBlendState_.Get(), blendFactor, 0xFFFFFFFF);
    context_->PSSetShaderResources(0, 0, nullptr);
}

void Dx11VideoRenderer::present() {
    if (swapChain_) {
        swapChain_->Present(0, 0);
    }
}

void Dx11VideoRenderer::clear() {
    alphaBlendState_.Reset();
    opaqueBlendState_.Reset();
    sampler_.Reset();
    vb_.Reset();
    layout_.Reset();
    psChecker_.Reset();
    psUyvy_.Reset();
    psBgra_.Reset();
    vs_.Reset();
    srv_.Reset();
    texture_.Reset();
    rtv_.Reset();
    swapChain_.Reset();
    context_.Reset();
    device_.Reset();
}
