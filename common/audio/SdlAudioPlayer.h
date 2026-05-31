#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

class SdlAudioPlayer {
public:
    SdlAudioPlayer();
    ~SdlAudioPlayer();

    SdlAudioPlayer(const SdlAudioPlayer&) = delete;
    SdlAudioPlayer& operator=(const SdlAudioPlayer&) = delete;

    bool open(int sampleRate = 48000, int channels = 2);
    bool ensureOpen(int sampleRate, int channels);
    void close();
    bool isOpen() const { return open_; }
    int sampleRate() const { return sampleRate_; }
    int channels() const { return channels_; }

    void queue(const float* interleaved, int sampleRate, int channels, int frames);
    void clearPending();

private:
    bool open_ = false;
    int sampleRate_ = 48000;
    int channels_ = 2;
    std::mutex mutex_;
    std::vector<float> pending_;
};
