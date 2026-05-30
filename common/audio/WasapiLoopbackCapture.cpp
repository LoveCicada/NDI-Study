#include "WasapiLoopbackCapture.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ksmedia.h>

#include <cstring>
#include <memory>
#include <vector>

#pragma comment(lib, "ole32.lib")

struct WasapiLoopbackCapture::Impl {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    WAVEFORMATEX* format = nullptr;
    bool comInitialized = false;
};

WasapiLoopbackCapture::WasapiLoopbackCapture()
    : impl_(std::make_unique<Impl>()) {}

WasapiLoopbackCapture::~WasapiLoopbackCapture() {
    stop();
}

bool WasapiLoopbackCapture::start(AudioCallback callback, int sampleRate, int channels) {
    stop();
    callback_ = std::move(callback);
    targetSampleRate_ = sampleRate;
    targetChannels_ = channels;

    if (!impl_->comInitialized) {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
            return false;
        }
        impl_->comInitialized = true;
    }

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&impl_->enumerator)))) {
        return false;
    }

    if (FAILED(impl_->enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &impl_->device))) {
        return false;
    }

    if (FAILED(impl_->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                      reinterpret_cast<void**>(&impl_->client)))) {
        return false;
    }

    if (FAILED(impl_->client->GetMixFormat(&impl_->format))) {
        return false;
    }

    REFERENCE_TIME bufferDuration = 10000000;
    if (FAILED(impl_->client->Initialize(
            AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
            bufferDuration, 0, impl_->format, nullptr))) {
        return false;
    }

    if (FAILED(impl_->client->GetService(__uuidof(IAudioCaptureClient),
                                         reinterpret_cast<void**>(&impl_->capture)))) {
        return false;
    }

    if (FAILED(impl_->client->Start())) {
        return false;
    }

    running_.store(true);
    thread_ = std::thread(&WasapiLoopbackCapture::captureLoop, this);
    return true;
}

void WasapiLoopbackCapture::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (impl_->client) {
        impl_->client->Stop();
    }
    if (impl_->capture) {
        impl_->capture->Release();
        impl_->capture = nullptr;
    }
    if (impl_->client) {
        impl_->client->Release();
        impl_->client = nullptr;
    }
    if (impl_->format) {
        CoTaskMemFree(impl_->format);
        impl_->format = nullptr;
    }
    if (impl_->device) {
        impl_->device->Release();
        impl_->device = nullptr;
    }
    if (impl_->enumerator) {
        impl_->enumerator->Release();
        impl_->enumerator = nullptr;
    }
}

void WasapiLoopbackCapture::captureLoop() {
    const int channels = impl_->format ? impl_->format->nChannels : targetChannels_;
    const int sampleRate = impl_->format ? static_cast<int>(impl_->format->nSamplesPerSec) : targetSampleRate_;
    std::vector<float> convertBuffer;

    while (running_.load()) {
        if (!impl_->capture) {
            break;
        }

        UINT32 packetLength = 0;
        if (FAILED(impl_->capture->GetNextPacketSize(&packetLength)) || packetLength == 0) {
            Sleep(5);
            continue;
        }

        BYTE* data = nullptr;
        UINT32 numFrames = 0;
        DWORD flags = 0;
        if (FAILED(impl_->capture->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr))) {
            break;
        }

        if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data && callback_) {
            const bool isFloat = impl_->format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                (impl_->format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                 reinterpret_cast<WAVEFORMATEXTENSIBLE*>(impl_->format)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);

            convertBuffer.resize(static_cast<size_t>(numFrames) * static_cast<size_t>(channels));
            if (isFloat) {
                std::memcpy(convertBuffer.data(), data, convertBuffer.size() * sizeof(float));
            } else {
                const auto* pcm = reinterpret_cast<const int16_t*>(data);
                for (UINT32 i = 0; i < numFrames * static_cast<UINT32>(channels); ++i) {
                    convertBuffer[i] = pcm[i] / 32768.f;
                }
            }
            callback_(convertBuffer.data(), sampleRate, channels, static_cast<int>(numFrames));
        }

        impl_->capture->ReleaseBuffer(numFrames);
    }
}
