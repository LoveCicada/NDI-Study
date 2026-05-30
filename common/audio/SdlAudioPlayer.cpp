#include "SdlAudioPlayer.h"

#ifdef NDI_STUDY_HAS_SDL2
#include <SDL.h>
#endif

#include <algorithm>
#include <cstring>

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

void SdlAudioPlayer::queue(const float* interleaved, int sampleRate, int channels, int frames) {
    if (!open_ || !interleaved || frames <= 0) {
        return;
    }
    (void)sampleRate;
    (void)channels;
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t count = static_cast<size_t>(frames) * static_cast<size_t>(channels_);
    pending_.insert(pending_.end(), interleaved, interleaved + count);
    if (pending_.size() > static_cast<size_t>(sampleRate_ * channels_ * 2)) {
        pending_.erase(pending_.begin(),
                       pending_.begin() + static_cast<std::ptrdiff_t>(pending_.size() / 2));
    }
}
