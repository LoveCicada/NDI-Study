#include "GpuBgraToNv12.h"

using Microsoft::WRL::ComPtr;

namespace {

int alignEven(int value) {
    return (value + 1) & ~1;
}

} // namespace

GpuBgraToNv12::GpuBgraToNv12() = default;

GpuBgraToNv12::~GpuBgraToNv12() {
    close();
}

bool GpuBgraToNv12::open(ID3D11Device* device, int width, int height) {
    close();
    if (!device || width <= 0 || height <= 0) {
        return false;
    }

    device_ = device;
    device_->GetImmediateContext(context_.GetAddressOf());
    if (!context_) {
        close();
        return false;
    }

    if (FAILED(device_.As(&videoDevice_))) {
        close();
        return false;
    }
    if (FAILED(context_.As(&videoContext_))) {
        close();
        return false;
    }

    width_ = alignEven(width);
    height_ = alignEven(height);
    if (!createResources(width_, height_)) {
        close();
        return false;
    }
    return true;
}

void GpuBgraToNv12::close() {
    nv12Texture_.Reset();
    videoProcessor_.Reset();
    videoProcessorEnum_.Reset();
    videoContext_.Reset();
    videoDevice_.Reset();
    context_.Reset();
    device_.Reset();
    width_ = 0;
    height_ = 0;
}

bool GpuBgraToNv12::createResources(int width, int height) {
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc{};
    contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    contentDesc.InputWidth = static_cast<UINT>(width);
    contentDesc.InputHeight = static_cast<UINT>(height);
    contentDesc.OutputWidth = static_cast<UINT>(width);
    contentDesc.OutputHeight = static_cast<UINT>(height);
    contentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    if (FAILED(videoDevice_->CreateVideoProcessorEnumerator(&contentDesc, videoProcessorEnum_.GetAddressOf()))) {
        return false;
    }
    if (FAILED(videoDevice_->CreateVideoProcessor(videoProcessorEnum_.Get(), 0,
                                                  videoProcessor_.GetAddressOf()))) {
        return false;
    }

    D3D11_TEXTURE2D_DESC nv12Desc{};
    nv12Desc.Width = static_cast<UINT>(width);
    nv12Desc.Height = static_cast<UINT>(height);
    nv12Desc.MipLevels = 1;
    nv12Desc.ArraySize = 1;
    nv12Desc.Format = DXGI_FORMAT_NV12;
    nv12Desc.SampleDesc.Count = 1;
    nv12Desc.Usage = D3D11_USAGE_DEFAULT;
    nv12Desc.BindFlags = D3D11_BIND_RENDER_TARGET;

    if (FAILED(device_->CreateTexture2D(&nv12Desc, nullptr, nv12Texture_.GetAddressOf()))) {
        return false;
    }

    videoContext_->VideoProcessorSetStreamFrameFormat(videoProcessor_.Get(), 0,
                                                      D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    videoContext_->VideoProcessorSetStreamOutputRate(videoProcessor_.Get(), 0, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL,
                                                     TRUE, nullptr);
    videoContext_->VideoProcessorSetOutputTargetRect(videoProcessor_.Get(), TRUE, nullptr);
    videoContext_->VideoProcessorSetOutputBackgroundColor(videoProcessor_.Get(), TRUE, nullptr);

    RECT srcRect{0, 0, width, height};
    videoContext_->VideoProcessorSetStreamSourceRect(videoProcessor_.Get(), 0, TRUE, &srcRect);
    videoContext_->VideoProcessorSetStreamDestRect(videoProcessor_.Get(), 0, TRUE, &srcRect);
    return true;
}

ID3D11Texture2D* GpuBgraToNv12::convert(ID3D11Texture2D* bgraTexture) {
    if (!videoProcessor_ || !bgraTexture) {
        return nullptr;
    }

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc{};
    inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inputViewDesc.Texture2D.MipSlice = 0;
    inputViewDesc.Texture2D.ArraySlice = 0;

    ComPtr<ID3D11VideoProcessorInputView> inputView;
    if (FAILED(videoDevice_->CreateVideoProcessorInputView(bgraTexture, videoProcessorEnum_.Get(),
                                                           &inputViewDesc, inputView.GetAddressOf()))) {
        return nullptr;
    }

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc{};
    outputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    outputViewDesc.Texture2D.MipSlice = 0;

    ComPtr<ID3D11VideoProcessorOutputView> outputView;
    if (FAILED(videoDevice_->CreateVideoProcessorOutputView(nv12Texture_.Get(), videoProcessorEnum_.Get(),
                                                            &outputViewDesc, outputView.GetAddressOf()))) {
        return nullptr;
    }

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = inputView.Get();

    if (FAILED(videoContext_->VideoProcessorBlt(videoProcessor_.Get(), outputView.Get(), 0, 1, &stream))) {
        return nullptr;
    }

    return nv12Texture_.Get();
}
