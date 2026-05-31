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
#include <audioclientactivationparams.h>
#include <ksmedia.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <future>
#include <memory>
#include <vector>

#pragma comment(lib, "ole32.lib")

namespace {

constexpr float kSilenceGatePeak = 1e-4f;

void downmixInterleavedToStereo(const float* in, int inChannels, int frames, std::vector<float>& stereo) {
    stereo.resize(static_cast<size_t>(frames) * 2);
    if (inChannels <= 1) {
        for (int i = 0; i < frames; ++i) {
            const float sample = in[static_cast<size_t>(i) * std::max(1, inChannels)];
            stereo[static_cast<size_t>(i) * 2] = sample;
            stereo[static_cast<size_t>(i) * 2 + 1] = sample;
        }
        return;
    }

    for (int i = 0; i < frames; ++i) {
        const float* frame = in + static_cast<size_t>(i) * static_cast<size_t>(inChannels);
        stereo[static_cast<size_t>(i) * 2] = frame[0];
        stereo[static_cast<size_t>(i) * 2 + 1] = frame[1];
    }
}

void resampleStereoLinear(const std::vector<float>& inStereo, int inFrames, int inRate,
                          std::vector<float>& outStereo, int outRate, int& outFrames) {
    if (inRate <= 0 || outRate <= 0 || inFrames <= 0) {
        outStereo.clear();
        outFrames = 0;
        return;
    }

    if (inRate == outRate) {
        outStereo = inStereo;
        outFrames = inFrames;
        return;
    }

    const double ratio = static_cast<double>(inRate) / static_cast<double>(outRate);
    outFrames = std::max(1, static_cast<int>(std::lround(static_cast<double>(inFrames) / ratio)));
    outStereo.assign(static_cast<size_t>(outFrames) * 2, 0.f);

    for (int i = 0; i < outFrames; ++i) {
        const double srcPos = static_cast<double>(i) * ratio;
        const int i0 = std::min(static_cast<int>(srcPos), inFrames - 1);
        const int i1 = std::min(i0 + 1, inFrames - 1);
        const float t = static_cast<float>(srcPos - static_cast<double>(i0));
        for (int ch = 0; ch < 2; ++ch) {
            const float s0 = inStereo[static_cast<size_t>(i0) * 2 + ch];
            const float s1 = inStereo[static_cast<size_t>(i1) * 2 + ch];
            outStereo[static_cast<size_t>(i) * 2 + ch] = s0 + (s1 - s0) * t;
        }
    }
}

void applySilenceGate(std::vector<float>& stereo) {
    float peak = 0.f;
    for (float sample : stereo) {
        peak = std::max(peak, std::abs(sample));
    }
    if (peak < kSilenceGatePeak) {
        std::fill(stereo.begin(), stereo.end(), 0.f);
    }
}

class ActivateCompletionHandler final : public IActivateAudioInterfaceCompletionHandler {
public:
    ActivateCompletionHandler(HANDLE doneEvent, IAudioClient** outClient)
        : doneEvent_(doneEvent)
        , outClient_(outClient) {}

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&refCount_);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG refs = InterlockedDecrement(&refCount_);
        if (refs == 0) {
            delete this;
        }
        return refs;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) {
            return E_POINTER;
        }
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
            *ppvObject = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override {
        HRESULT activateResult = E_FAIL;
        IUnknown* punk = nullptr;
        if (operation) {
            operation->GetActivateResult(&activateResult, &punk);
        }

        if (SUCCEEDED(activateResult) && punk && outClient_) {
            activateResult = punk->QueryInterface(__uuidof(IAudioClient),
                                                  reinterpret_cast<void**>(outClient_));
            punk->Release();
        } else if (punk) {
            punk->Release();
        }

        activateResult_ = activateResult;
        if (doneEvent_) {
            SetEvent(doneEvent_);
        }
        return S_OK;
    }

    HRESULT activateResult() const { return activateResult_; }

private:
    LONG refCount_ = 1;
    HANDLE doneEvent_ = nullptr;
    IAudioClient** outClient_ = nullptr;
    HRESULT activateResult_ = E_FAIL;
};

} // namespace

struct WasapiLoopbackCapture::Impl {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    WAVEFORMATEX* format = nullptr;
};

WasapiLoopbackCapture::WasapiLoopbackCapture()
    : impl_(std::make_unique<Impl>()) {}

WasapiLoopbackCapture::~WasapiLoopbackCapture() {
    stop();
}

std::vector<uint32_t> WasapiLoopbackCapture::findProcessIdsByExeName(const std::wstring& exeName) {
    std::vector<uint32_t> pids;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return pids;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, exeName.c_str()) == 0) {
                pids.push_back(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return pids;
}

void WasapiLoopbackCapture::setExcludeProcessIds(std::vector<uint32_t> processIds) {
    excludeProcessIds_ = std::move(processIds);
}

void WasapiLoopbackCapture::setExcludeProcessNames(const std::vector<std::wstring>& exeNames) {
    excludeProcessNames_ = exeNames;
    excludeProcessIds_.clear();
    for (const auto& name : exeNames) {
        const auto found = findProcessIdsByExeName(name);
        excludeProcessIds_.insert(excludeProcessIds_.end(), found.begin(), found.end());
    }
}

void WasapiLoopbackCapture::refreshExcludeProcessIds() {
    excludeProcessIds_.clear();
    for (const auto& name : excludeProcessNames_) {
        const auto found = findProcessIdsByExeName(name);
        excludeProcessIds_.insert(excludeProcessIds_.end(), found.begin(), found.end());
    }
}

bool WasapiLoopbackCapture::initProcessLoopbackExclude(uint32_t processId) {
    if (processId == 0) {
        return false;
    }

    HANDLE doneEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!doneEvent) {
        return false;
    }

    IAudioClient* activatedClient = nullptr;
    ActivateCompletionHandler* handler = new ActivateCompletionHandler(doneEvent, &activatedClient);

    AUDIOCLIENT_ACTIVATION_PARAMS activationParams{};
    activationParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    activationParams.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
    activationParams.ProcessLoopbackParams.TargetProcessId = processId;

    PROPVARIANT activateParams{};
    activateParams.vt = VT_BLOB;
    activateParams.blob.cbSize = sizeof(activationParams);
    activateParams.blob.pBlobData = reinterpret_cast<BYTE*>(&activationParams);

    IActivateAudioInterfaceAsyncOperation* asyncOp = nullptr;
    const HRESULT asyncHr = ActivateAudioInterfaceAsync(
        VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
        __uuidof(IAudioClient),
        &activateParams,
        handler,
        &asyncOp);

    if (FAILED(asyncHr)) {
        handler->Release();
        CloseHandle(doneEvent);
        return false;
    }

    WaitForSingleObject(doneEvent, 5000);
    CloseHandle(doneEvent);

    const HRESULT activateHr = handler->activateResult();
    handler->Release();
    if (asyncOp) {
        asyncOp->Release();
    }

    if (FAILED(activateHr) || !activatedClient) {
        if (activatedClient) {
            activatedClient->Release();
        }
        return false;
    }

    impl_->client = activatedClient;
    if (FAILED(impl_->client->GetMixFormat(&impl_->format))) {
        return false;
    }

    const REFERENCE_TIME bufferDuration = 10000000;
    if (FAILED(impl_->client->Initialize(
            AUDCLNT_SHAREMODE_SHARED, 0, bufferDuration, 0, impl_->format, nullptr))) {
        return false;
    }

    if (FAILED(impl_->client->GetService(__uuidof(IAudioCaptureClient),
                                         reinterpret_cast<void**>(&impl_->capture)))) {
        return false;
    }

    if (FAILED(impl_->client->Start())) {
        return false;
    }

    usedProcessExclude_.store(true);
    return true;
}

bool WasapiLoopbackCapture::initClassicLoopback() {
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

    const REFERENCE_TIME bufferDuration = 10000000;
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

    usedProcessExclude_.store(false);
    return true;
}

bool WasapiLoopbackCapture::initWasapiDevices() {
    usedProcessExclude_.store(false);

    const int maxAttempts = excludeProcessNames_.empty() ? 1 : 25;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        refreshExcludeProcessIds();
        for (const uint32_t pid : excludeProcessIds_) {
            if (initProcessLoopbackExclude(pid)) {
                return true;
            }
        }
        if (attempt + 1 < maxAttempts) {
            Sleep(100);
        }
    }

    return initClassicLoopback();
}

void WasapiLoopbackCapture::releaseWasapiDevices() {
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

bool WasapiLoopbackCapture::start(AudioCallback callback, int sampleRate, int channels) {
    stop();
    callback_ = std::move(callback);
    targetSampleRate_ = sampleRate;
    targetChannels_ = channels;

    std::promise<bool> initPromise;
    auto initFuture = initPromise.get_future();
    running_.store(true);
    thread_ = std::thread([this, init = std::move(initPromise)]() mutable {
        captureLoop(std::move(init));
    });

    const bool ok = initFuture.get();
    if (!ok) {
        running_.store(false);
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    return ok;
}

void WasapiLoopbackCapture::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void WasapiLoopbackCapture::captureLoop(std::promise<bool> initPromise) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        initPromise.set_value(false);
        return;
    }

    const bool initialized = initWasapiDevices();
    initPromise.set_value(initialized);
    if (!initialized) {
        releaseWasapiDevices();
        CoUninitialize();
        return;
    }

    const int channels = impl_->format ? impl_->format->nChannels : targetChannels_;
    const int sampleRate = impl_->format ? static_cast<int>(impl_->format->nSamplesPerSec) : targetSampleRate_;
    std::vector<float> convertBuffer;
    std::vector<float> stereoBuffer;
    std::vector<float> outputBuffer;
    int outputFrames = 0;

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

        convertBuffer.resize(static_cast<size_t>(numFrames) * static_cast<size_t>(channels));
        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
            std::fill(convertBuffer.begin(), convertBuffer.end(), 0.f);
        } else if (data) {
            const bool isFloat = impl_->format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                (impl_->format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                 reinterpret_cast<WAVEFORMATEXTENSIBLE*>(impl_->format)->SubFormat ==
                     KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);

            if (isFloat) {
                std::memcpy(convertBuffer.data(), data, convertBuffer.size() * sizeof(float));
            } else {
                const auto* pcm = reinterpret_cast<const int16_t*>(data);
                for (UINT32 i = 0; i < numFrames * static_cast<UINT32>(channels); ++i) {
                    convertBuffer[i] = pcm[i] / 32768.f;
                }
            }
        } else {
            std::fill(convertBuffer.begin(), convertBuffer.end(), 0.f);
        }

        downmixInterleavedToStereo(convertBuffer.data(), channels, static_cast<int>(numFrames), stereoBuffer);
        applySilenceGate(stereoBuffer);
        resampleStereoLinear(stereoBuffer, static_cast<int>(numFrames), sampleRate,
                             outputBuffer, targetSampleRate_, outputFrames);

        if (callback_ && outputFrames > 0) {
            callback_(outputBuffer.data(), targetSampleRate_, targetChannels_, outputFrames);
        }

        impl_->capture->ReleaseBuffer(numFrames);
    }

    if (impl_->client) {
        impl_->client->Stop();
    }
    releaseWasapiDevices();
    CoUninitialize();
}
