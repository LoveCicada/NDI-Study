#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class WasapiLoopbackCapture {
public:
    using AudioCallback = std::function<void(const float* interleaved, int sampleRate, int channels, int frames)>;

    WasapiLoopbackCapture();
    ~WasapiLoopbackCapture();

    WasapiLoopbackCapture(const WasapiLoopbackCapture&) = delete;
    WasapiLoopbackCapture& operator=(const WasapiLoopbackCapture&) = delete;

    static std::vector<uint32_t> findProcessIdsByExeName(const std::wstring& exeName);

    void setExcludeProcessIds(std::vector<uint32_t> processIds);
    void setExcludeProcessNames(const std::vector<std::wstring>& exeNames);

    bool start(AudioCallback callback, int sampleRate = 48000, int channels = 2);
    void stop();
    bool isRunning() const { return running_.load(); }
    bool usedProcessExclude() const { return usedProcessExclude_.load(); }

private:
    void captureLoop(std::promise<bool> initPromise);
    bool initWasapiDevices();
    bool initClassicLoopback();
    bool initProcessLoopbackExclude(uint32_t processId);
    void releaseWasapiDevices();
    void refreshExcludeProcessIds();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    AudioCallback callback_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> usedProcessExclude_{false};
    int targetSampleRate_ = 48000;
    int targetChannels_ = 2;
    std::vector<uint32_t> excludeProcessIds_;
    std::vector<std::wstring> excludeProcessNames_;
};
