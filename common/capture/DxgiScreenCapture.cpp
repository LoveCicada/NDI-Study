#include "DxgiScreenCapture.h"

#include <windows.h>

#include <cstring>

using Microsoft::WRL::ComPtr;

DxgiScreenCapture::DxgiScreenCapture() = default;

DxgiScreenCapture::~DxgiScreenCapture() {
    close();
}

std::vector<DxgiOutputInfo> DxgiScreenCapture::listOutputs() {
    std::vector<DxgiOutputInfo> outputs;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                  reinterpret_cast<void**>(factory.GetAddressOf())))) {
        return outputs;
    }

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT ai = 0; factory->EnumAdapters1(ai, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++ai) {
        ComPtr<IDXGIOutput> output;
        for (UINT oi = 0; adapter->EnumOutputs(oi, output.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++oi) {
            DXGI_OUTPUT_DESC desc{};
            output->GetDesc(&desc);

            DxgiOutputInfo info;
            info.index = static_cast<int>(outputs.size());
            info.width = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
            info.height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;

            const int len = WideCharToMultiByte(CP_UTF8, 0, desc.DeviceName, -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                std::string name(static_cast<size_t>(len - 1), '\0');
                WideCharToMultiByte(CP_UTF8, 0, desc.DeviceName, -1, name.data(), len, nullptr, nullptr);
                info.name = std::move(name);
            } else {
                info.name = "Output " + std::to_string(info.index);
            }
            outputs.push_back(std::move(info));
        }
    }
    return outputs;
}

bool DxgiScreenCapture::open(int outputIndex) {
    close();
    outputIndex_ = outputIndex;
    return initDuplication(outputIndex);
}

void DxgiScreenCapture::close() {
    gpuPool_.Reset();
    staging_.Reset();
    duplication_.Reset();
    context_.Reset();
    device_.Reset();
    width_ = 0;
    height_ = 0;
}

bool DxgiScreenCapture::initDuplication(int outputIndex) {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                  reinterpret_cast<void**>(factory.GetAddressOf())))) {
        return false;
    }

    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIOutput> targetOutput;
    int current = 0;

    for (UINT ai = 0; factory->EnumAdapters1(ai, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++ai) {
        ComPtr<IDXGIOutput> output;
        for (UINT oi = 0; adapter->EnumOutputs(oi, output.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++oi) {
            if (current == outputIndex) {
                targetOutput = output;
                break;
            }
            ++current;
        }
        if (targetOutput) {
            break;
        }
    }

    if (!targetOutput) {
        return false;
    }

    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL level{};
    const UINT deviceFlags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    if (FAILED(D3D11CreateDevice(
            adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, deviceFlags, levels, 1,
            D3D11_SDK_VERSION, device_.GetAddressOf(), &level, context_.GetAddressOf()))) {
        return false;
    }

    ComPtr<IDXGIOutput1> output1;
    if (FAILED(targetOutput.As(&output1))) {
        return false;
    }

    if (FAILED(output1->DuplicateOutput(device_.Get(), duplication_.GetAddressOf()))) {
        return false;
    }

    DXGI_OUTDUPL_DESC dupDesc{};
    duplication_->GetDesc(&dupDesc);
    width_ = static_cast<int>(dupDesc.ModeDesc.Width);
    height_ = static_cast<int>(dupDesc.ModeDesc.Height);
    recreateGpuPoolTexture();
    return gpuPool_ != nullptr;
}

void DxgiScreenCapture::recreateGpuPoolTexture() {
    gpuPool_.Reset();
    if (!device_ || width_ <= 0 || height_ <= 0) {
        return;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width_);
    desc.Height = static_cast<UINT>(height_);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    device_->CreateTexture2D(&desc, nullptr, gpuPool_.GetAddressOf());
}

void DxgiScreenCapture::recreateStagingTexture() {
    staging_.Reset();
    if (!device_ || width_ <= 0 || height_ <= 0) {
        return;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width_);
    desc.Height = static_cast<UINT>(height_);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    device_->CreateTexture2D(&desc, nullptr, staging_.GetAddressOf());
}

bool DxgiScreenCapture::captureFrame(CapturedFrame& out, uint32_t timeoutMs) {
    if (!duplication_ || !context_) {
        return false;
    }

    if (!staging_) {
        recreateStagingTexture();
    }
    if (!staging_) {
        return false;
    }

    ComPtr<IDXGIResource> resource;
    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    const HRESULT hr = duplication_->AcquireNextFrame(timeoutMs, &frameInfo, resource.GetAddressOf());
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return false;
    }
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            close();
            initDuplication(outputIndex_);
        }
        return false;
    }

    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(resource.As(&texture))) {
        duplication_->ReleaseFrame();
        return false;
    }

    context_->CopyResource(staging_.Get(), texture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        duplication_->ReleaseFrame();
        return false;
    }

    out.width = width_;
    out.height = height_;
    out.stride = static_cast<int>(mapped.RowPitch);
    out.bgra.resize(static_cast<size_t>(mapped.RowPitch) * static_cast<size_t>(height_));
    for (int y = 0; y < height_; ++y) {
        std::memcpy(out.bgra.data() + static_cast<size_t>(y) * mapped.RowPitch,
                    static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
                    mapped.RowPitch);
    }

    context_->Unmap(staging_.Get(), 0);
    duplication_->ReleaseFrame();
    return true;
}

bool DxgiScreenCapture::captureGpuFrame(CapturedGpuFrame& out, uint32_t timeoutMs) {
    if (!duplication_ || !gpuPool_ || !context_) {
        return false;
    }

    ComPtr<IDXGIResource> resource;
    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    const HRESULT hr = duplication_->AcquireNextFrame(timeoutMs, &frameInfo, resource.GetAddressOf());
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return false;
    }
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            close();
            initDuplication(outputIndex_);
        }
        return false;
    }

    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(resource.As(&texture))) {
        duplication_->ReleaseFrame();
        return false;
    }

    context_->CopyResource(gpuPool_.Get(), texture.Get());
    duplication_->ReleaseFrame();

    out.texture = gpuPool_.Get();
    out.width = width_;
    out.height = height_;
    return true;
}
