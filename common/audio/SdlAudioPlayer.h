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
    void close();
    bool isOpen() const { return open_; }

    void queue(const float* interleaved, int sampleRate, int channels, int frames);

private:
    bool open_ = false;
    int sampleRate_ = 48000;
    int channels_ = 2;
    std::mutex mutex_;
    std::vector<float> pending_;
};
