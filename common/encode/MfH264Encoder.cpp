#include "MfH264Encoder.h"

#include "GpuBgraToNv12.h"

#include <codecapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <wmcodecdsp.h>

#include <cstring>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")

struct MfH264Encoder::Impl {
    IMFTransform* transform = nullptr;
    IMFMediaType* inputType = nullptr;
    IMFMediaType* outputType = nullptr;
    IMFDXGIDeviceManager* dxgiManager = nullptr;
    DWORD inputStreamId = 0;
    DWORD outputStreamId = 0;
    bool comInitialized = false;
    UINT dxgiResetToken = 0;
};

MfH264Encoder::MfH264Encoder()
    : impl_(std::make_unique<Impl>())
    , bgraToNv12_(std::make_unique<GpuBgraToNv12>()) {}

MfH264Encoder::~MfH264Encoder() {
    close();
}

bool MfH264Encoder::open(int width, int height, int frameRateN, int frameRateD, uint32_t bitrate,
                           ID3D11Device* d3dDevice) {
    close();
    width_ = width;
    height_ = height;
    frameRateN_ = frameRateN;
    frameRateD_ = frameRateD;
    frameIndex_ = 0;
    d3dDevice_ = d3dDevice;

    if (!impl_->comInitialized) {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
            return false;
        }
        impl_->comInitialized = true;
    }
    if (FAILED(MFStartup(MF_VERSION))) {
        return false;
    }

    if (d3dDevice_ &&
        bgraToNv12_->open(d3dDevice_.Get(), width_, height_) &&
        createGpuEncoder(width, height, frameRateN, frameRateD, bitrate)) {
        gpuPath_ = true;
        open_ = true;
        return true;
    }

    bgraToNv12_->close();
    d3dDevice_.Reset();
    gpuPath_ = false;
    if (!createCpuEncoder(width, height, frameRateN, frameRateD, bitrate)) {
        close();
        return false;
    }
    open_ = true;
    return true;
}

void MfH264Encoder::close() {
    bgraToNv12_->close();
    d3dDevice_.Reset();
    gpuPath_ = false;

    if (impl_->dxgiManager) {
        impl_->dxgiManager->Release();
        impl_->dxgiManager = nullptr;
    }
    if (impl_->outputType) {
        impl_->outputType->Release();
        impl_->outputType = nullptr;
    }
    if (impl_->inputType) {
        impl_->inputType->Release();
        impl_->inputType = nullptr;
    }
    if (impl_->transform) {
        impl_->transform->Release();
        impl_->transform = nullptr;
    }
    if (open_) {
        MFShutdown();
    }
    open_ = false;
}

bool MfH264Encoder::createCpuEncoder(int width, int height, int frameRateN, int frameRateD, uint32_t bitrate) {
    MFT_REGISTER_TYPE_INFO inputInfo = {MFMediaType_Video, MFVideoFormat_RGB32};
    MFT_REGISTER_TYPE_INFO outputInfo = {MFMediaType_Video, MFVideoFormat_H264};

    IMFActivate** activates = nullptr;
    UINT32 count = 0;
    if (FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
                         &inputInfo, &outputInfo, &activates, &count)) ||
        count == 0) {
        if (FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_SYNCMFT,
                             &inputInfo, &outputInfo, &activates, &count)) ||
            count == 0) {
            return false;
        }
    }

    if (FAILED(activates[0]->ActivateObject(IID_PPV_ARGS(&impl_->transform)))) {
        for (UINT32 i = 0; i < count; ++i) {
            activates[i]->Release();
        }
        CoTaskMemFree(activates);
        return false;
    }
    for (UINT32 i = 0; i < count; ++i) {
        activates[i]->Release();
    }
    CoTaskMemFree(activates);

    DWORD inputIds[1] = {0};
    DWORD outputIds[1] = {0};
    if (SUCCEEDED(impl_->transform->GetStreamIDs(1, inputIds, 1, outputIds))) {
        impl_->inputStreamId = inputIds[0];
        impl_->outputStreamId = outputIds[0];
    }

    if (FAILED(MFCreateMediaType(&impl_->inputType))) {
        return false;
    }
    impl_->inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    impl_->inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    MFSetAttributeSize(impl_->inputType, MF_MT_FRAME_SIZE, static_cast<UINT32>(width), static_cast<UINT32>(height));
    MFSetAttributeRatio(impl_->inputType, MF_MT_FRAME_RATE, static_cast<UINT32>(frameRateN),
                        static_cast<UINT32>(frameRateD));
    impl_->inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (FAILED(impl_->transform->SetInputType(impl_->inputStreamId, impl_->inputType, 0))) {
        return false;
    }

    if (FAILED(MFCreateMediaType(&impl_->outputType))) {
        return false;
    }
    impl_->outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    impl_->outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    MFSetAttributeSize(impl_->outputType, MF_MT_FRAME_SIZE, static_cast<UINT32>(width), static_cast<UINT32>(height));
    MFSetAttributeRatio(impl_->outputType, MF_MT_FRAME_RATE, static_cast<UINT32>(frameRateN),
                        static_cast<UINT32>(frameRateD));
    impl_->outputType->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
    impl_->outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (FAILED(impl_->transform->SetOutputType(impl_->outputStreamId, impl_->outputType, 0))) {
        return false;
    }

    impl_->transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    impl_->transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    impl_->transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    return true;
}

bool MfH264Encoder::createGpuEncoder(int width, int height, int frameRateN, int frameRateD, uint32_t bitrate) {
    if (!d3dDevice_) {
        return false;
    }

    MFT_REGISTER_TYPE_INFO inputInfo = {MFMediaType_Video, MFVideoFormat_NV12};
    MFT_REGISTER_TYPE_INFO outputInfo = {MFMediaType_Video, MFVideoFormat_H264};

    IMFActivate** activates = nullptr;
    UINT32 count = 0;
    if (FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
                         &inputInfo, &outputInfo, &activates, &count)) ||
        count == 0) {
        return false;
    }

    bool created = false;
    for (UINT32 i = 0; i < count && !created; ++i) {
        if (impl_->transform) {
            impl_->transform->Release();
            impl_->transform = nullptr;
        }

        if (FAILED(activates[i]->ActivateObject(IID_PPV_ARGS(&impl_->transform)))) {
            continue;
        }

        IMFAttributes* attributes = nullptr;
        if (FAILED(impl_->transform->GetAttributes(&attributes))) {
            continue;
        }

        UINT32 d3dAware = 0;
        if (FAILED(attributes->GetUINT32(MF_SA_D3D11_AWARE, &d3dAware)) || d3dAware == 0) {
            attributes->Release();
            continue;
        }
        attributes->SetUINT32(CODECAPI_AVEncCommonLowLatency, TRUE);
        attributes->Release();

        if (FAILED(MFCreateDXGIDeviceManager(&impl_->dxgiResetToken, &impl_->dxgiManager))) {
            continue;
        }
        if (FAILED(impl_->dxgiManager->ResetDevice(d3dDevice_.Get(), impl_->dxgiResetToken))) {
            continue;
        }
        if (FAILED(impl_->transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                                                    reinterpret_cast<ULONG_PTR>(impl_->dxgiManager)))) {
            continue;
        }

        DWORD inputIds[1] = {0};
        DWORD outputIds[1] = {0};
        if (SUCCEEDED(impl_->transform->GetStreamIDs(1, inputIds, 1, outputIds))) {
            impl_->inputStreamId = inputIds[0];
            impl_->outputStreamId = outputIds[0];
        }

        if (impl_->inputType) {
            impl_->inputType->Release();
            impl_->inputType = nullptr;
        }
        if (FAILED(MFCreateMediaType(&impl_->inputType))) {
            continue;
        }
        impl_->inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        impl_->inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        MFSetAttributeSize(impl_->inputType, MF_MT_FRAME_SIZE, static_cast<UINT32>(width), static_cast<UINT32>(height));
        MFSetAttributeRatio(impl_->inputType, MF_MT_FRAME_RATE, static_cast<UINT32>(frameRateN),
                            static_cast<UINT32>(frameRateD));
        impl_->inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (FAILED(impl_->transform->SetInputType(impl_->inputStreamId, impl_->inputType, 0))) {
            continue;
        }

        if (impl_->outputType) {
            impl_->outputType->Release();
            impl_->outputType = nullptr;
        }
        if (FAILED(MFCreateMediaType(&impl_->outputType))) {
            continue;
        }
        impl_->outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        impl_->outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        MFSetAttributeSize(impl_->outputType, MF_MT_FRAME_SIZE, static_cast<UINT32>(width),
                           static_cast<UINT32>(height));
        MFSetAttributeRatio(impl_->outputType, MF_MT_FRAME_RATE, static_cast<UINT32>(frameRateN),
                            static_cast<UINT32>(frameRateD));
        impl_->outputType->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
        impl_->outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (FAILED(impl_->transform->SetOutputType(impl_->outputStreamId, impl_->outputType, 0))) {
            continue;
        }

        impl_->transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        impl_->transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        impl_->transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        created = true;
    }

    for (UINT32 i = 0; i < count; ++i) {
        activates[i]->Release();
    }
    CoTaskMemFree(activates);
    return created;
}

static std::vector<uint8_t> copySample(IMFSample* sample) {
    IMFMediaBuffer* buffer = nullptr;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) {
        return {};
    }
    BYTE* data = nullptr;
    DWORD maxLen = 0;
    DWORD curLen = 0;
    if (FAILED(buffer->Lock(&data, &maxLen, &curLen))) {
        buffer->Release();
        return {};
    }
    std::vector<uint8_t> out(data, data + curLen);
    buffer->Unlock();
    buffer->Release();
    return out;
}

bool MfH264Encoder::processOutput() {
    MFT_OUTPUT_DATA_BUFFER outBuf{};
    DWORD status = 0;
    IMFSample* sample = nullptr;
    if (FAILED(MFCreateSample(&sample))) {
        return false;
    }
    IMFMediaBuffer* mediaBuffer = nullptr;
    if (FAILED(MFCreateMemoryBuffer(4 * 1024 * 1024, &mediaBuffer))) {
        sample->Release();
        return false;
    }
    sample->AddBuffer(mediaBuffer);
    mediaBuffer->Release();

    outBuf.pSample = sample;
    const HRESULT hr = impl_->transform->ProcessOutput(0, 1, &outBuf, &status);
    if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
        sample->Release();
        return true;
    }
    if (FAILED(hr)) {
        sample->Release();
        return false;
    }

    EncodedH264Frame frame;
    frame.data = copySample(sample);
    frame.timestamp100ns = frameIndex_ * 10000000LL * static_cast<int64_t>(frameRateD_) /
                           static_cast<int64_t>(frameRateN_);
    frame.keyFrame = true;
    sample->Release();

    if (callback_ && !frame.data.empty()) {
        callback_(frame);
    }
    return true;
}

bool MfH264Encoder::encodeNv12Texture(ID3D11Texture2D* nv12Texture, int64_t /*timestamp100ns*/) {
    if (!open_ || !gpuPath_ || !impl_->transform || !nv12Texture) {
        return false;
    }

    IMFSample* sample = nullptr;
    if (FAILED(MFCreateSample(&sample))) {
        return false;
    }

    IMFMediaBuffer* buffer = nullptr;
    if (FAILED(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), nv12Texture, 0, FALSE, &buffer))) {
        sample->Release();
        return false;
    }
    sample->AddBuffer(buffer);
    buffer->Release();

    const int64_t duration = 10000000LL * frameRateD_ / frameRateN_;
    const int64_t sampleTime = frameIndex_ * duration;
    sample->SetSampleTime(sampleTime);
    sample->SetSampleDuration(duration);

    if (FAILED(impl_->transform->ProcessInput(impl_->inputStreamId, sample, 0))) {
        sample->Release();
        return false;
    }
    sample->Release();
    ++frameIndex_;
    return processOutput();
}

bool MfH264Encoder::encodeGpuBgraTexture(ID3D11Texture2D* bgraTexture, int64_t timestamp100ns) {
    if (!gpuPath_ || !bgraToNv12_->isOpen()) {
        return false;
    }

    ID3D11Texture2D* nv12 = bgraToNv12_->convert(bgraTexture);
    if (!nv12) {
        return false;
    }
    return encodeNv12Texture(nv12, timestamp100ns);
}

bool MfH264Encoder::encodeBGRA(const uint8_t* data, int stride, int64_t /*timestamp100ns*/) {
    if (!open_ || gpuPath_ || !impl_->transform || !data) {
        return false;
    }

    IMFSample* sample = nullptr;
    if (FAILED(MFCreateSample(&sample))) {
        return false;
    }
    IMFMediaBuffer* buffer = nullptr;
    const DWORD bufferSize = static_cast<DWORD>(stride * height_);
    if (FAILED(MFCreateMemoryBuffer(bufferSize, &buffer))) {
        sample->Release();
        return false;
    }
    BYTE* dst = nullptr;
    if (FAILED(buffer->Lock(&dst, nullptr, nullptr))) {
        buffer->Release();
        sample->Release();
        return false;
    }
    std::memcpy(dst, data, bufferSize);
    buffer->Unlock();
    buffer->SetCurrentLength(bufferSize);
    sample->AddBuffer(buffer);
    buffer->Release();

    const int64_t duration = 10000000LL * frameRateD_ / frameRateN_;
    sample->SetSampleTime(frameIndex_ * duration);
    sample->SetSampleDuration(duration);

    if (FAILED(impl_->transform->ProcessInput(impl_->inputStreamId, sample, 0))) {
        sample->Release();
        return false;
    }
    sample->Release();
    ++frameIndex_;
    return processOutput();
}
