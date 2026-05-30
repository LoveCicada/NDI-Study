#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

class WasapiLoopbackCapture {
public:
    using AudioCallback = std::function<void(const float* interleaved, int sampleRate, int channels, int frames)>;

    WasapiLoopbackCapture();
    ~WasapiLoopbackCapture();

    WasapiLoopbackCapture(const WasapiLoopbackCapture&) = delete;
    WasapiLoopbackCapture& operator=(const WasapiLoopbackCapture&) = delete;

    bool start(AudioCallback callback, int sampleRate = 48000, int channels = 2);
    void stop();
    bool isRunning() const { return running_.load(); }

private:
    void captureLoop();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    AudioCallback callback_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    int targetSampleRate_ = 48000;
    int targetChannels_ = 2;
};
