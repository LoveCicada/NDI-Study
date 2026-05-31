#include "SdlAudioPlayer.h"

#ifdef NDI_STUDY_HAS_SDL2
#include <SDL.h>
#endif

#include <algorithm>
#include <cstring>

namespace {

void downmixToChannels(const float* interleaved, int inChannels, int frames, int outChannels,
                       std::vector<float>& out) {
    out.resize(static_cast<size_t>(frames) * static_cast<size_t>(outChannels));
    if (inChannels <= 1) {
        for (int i = 0; i < frames; ++i) {
            const float sample = interleaved[static_cast<size_t>(i) * std::max(1, inChannels)];
            for (int ch = 0; ch < outChannels; ++ch) {
                out[static_cast<size_t>(i) * outChannels + ch] = sample;
            }
        }
        return;
    }

    for (int i = 0; i < frames; ++i) {
        const float* frame = interleaved + static_cast<size_t>(i) * static_cast<size_t>(inChannels);
        for (int ch = 0; ch < outChannels; ++ch) {
            out[static_cast<size_t>(i) * outChannels + ch] = frame[std::min(ch, inChannels - 1)];
        }
    }
}

} // namespace

SdlAudioPlayer::SdlAudioPlayer() = default;

SdlAudioPlayer::~SdlAudioPlayer() {
    close();
}

bool SdlAudioPlayer::open(int sampleRate, int channels) {
    close();
    sampleRate_ = sampleRate;
    channels_ = channels;

#ifdef NDI_STUDY_HAS_SDL2
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        return false;
    }
    SDL_AudioSpec desired{};
    desired.freq = sampleRate;
    desired.format = AUDIO_F32;
    desired.channels = static_cast<Uint8>(channels);
    desired.samples = 960;
    desired.callback = [](void* userdata, Uint8* stream, int len) {
        auto* self = static_cast<SdlAudioPlayer*>(userdata);
        std::lock_guard<std::mutex> lock(self->mutex_);
        const int floatsNeeded = len / static_cast<int>(sizeof(float));
        float* out = reinterpret_cast<float*>(stream);
        const int available = static_cast<int>(self->pending_.size());
        const int toCopy = std::min(floatsNeeded, available);
        if (toCopy > 0) {
            std::memcpy(out, self->pending_.data(), static_cast<size_t>(toCopy) * sizeof(float));
            self->pending_.erase(self->pending_.begin(), self->pending_.begin() + toCopy);
        }
        if (toCopy < floatsNeeded) {
            std::memset(out + toCopy, 0, static_cast<size_t>(floatsNeeded - toCopy) * sizeof(float));
        }
    };
    desired.userdata = this;

    if (SDL_OpenAudio(&desired, nullptr) != 0) {
        return false;
    }
    SDL_PauseAudio(0);
    open_ = true;
    return true;
#else
    (void)sampleRate;
    (void)channels;
    return false;
#endif
}

bool SdlAudioPlayer::ensureOpen(int sampleRate, int channels) {
    if (open_ && sampleRate_ == sampleRate && channels_ == channels) {
        return true;
    }
    return open(sampleRate, channels);
}

void SdlAudioPlayer::close() {
#ifdef NDI_STUDY_HAS_SDL2
    if (open_) {
        SDL_CloseAudio();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
#endif
    open_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.clear();
}

void SdlAudioPlayer::clearPending() {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.clear();
}

void SdlAudioPlayer::queue(const float* interleaved, int sampleRate, int channels, int frames) {
    if (!open_ || !interleaved || frames <= 0 || channels <= 0) {
        return;
    }
    (void)sampleRate;

    std::vector<float> normalized;
    const float* src = interleaved;
    int srcChannels = channels;
    if (channels != channels_) {
        downmixToChannels(interleaved, channels, frames, channels_, normalized);
        src = normalized.data();
        srcChannels = channels_;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const size_t count = static_cast<size_t>(frames) * static_cast<size_t>(srcChannels);
    const size_t maxQueue = static_cast<size_t>(sampleRate_ * channels_) / 2;
    if (pending_.size() + count > maxQueue) {
        const size_t target = maxQueue > count ? maxQueue - count : 0;
        if (pending_.size() > target) {
            pending_.erase(pending_.begin(),
                           pending_.begin() + static_cast<std::ptrdiff_t>(pending_.size() - target));
        }
    }
    pending_.insert(pending_.end(), src, src + count);
}
