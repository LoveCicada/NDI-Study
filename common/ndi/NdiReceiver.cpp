#include "NdiReceiver.h"

#include <chrono>
#include <cstring>

namespace {

std::vector<uint8_t> copyVideoBuffer(const NDIlib_video_frame_v2_t& frame) {
    if (!frame.p_data || frame.xres <= 0 || frame.yres <= 0) {
        return {};
    }
    const int stride = frame.line_stride_in_bytes > 0
        ? frame.line_stride_in_bytes
        : frame.xres * 4;
    const size_t size = static_cast<size_t>(stride) * static_cast<size_t>(frame.yres);
    std::vector<uint8_t> out(size);
    std::memcpy(out.data(), frame.p_data, size);
    return out;
}

std::vector<float> copyAudioBuffer(const NDIlib_audio_frame_v3_t& frame) {
    if (!frame.p_data || frame.no_samples <= 0 || frame.no_channels <= 0) {
        return {};
    }
    const size_t count = static_cast<size_t>(frame.no_samples) * static_cast<size_t>(frame.no_channels);
    std::vector<float> out(count);
    for (int ch = 0; ch < frame.no_channels; ++ch) {
        const float* src = reinterpret_cast<const float*>(
            reinterpret_cast<const uint8_t*>(frame.p_data) + frame.channel_stride_in_bytes * ch);
        for (int i = 0; i < frame.no_samples; ++i) {
            out[static_cast<size_t>(i) * frame.no_channels + ch] = src[i];
        }
    }
    return out;
}

} // namespace

NdiReceiver::NdiReceiver() = default;

NdiReceiver::~NdiReceiver() {
    stop();
    destroy();
}

NDIlib_recv_color_format_e NdiReceiver::mapColorFormat(NdiRecvColorFormatChoice choice) {
    switch (choice) {
    case NdiRecvColorFormatChoice::Best:
        return NDIlib_recv_color_format_best;
    case NdiRecvColorFormatChoice::UYVY_BGRA:
        return NDIlib_recv_color_format_UYVY_BGRA;
    case NdiRecvColorFormatChoice::BGRX_BGRA:
        return NDIlib_recv_color_format_BGRX_BGRA;
    case NdiRecvColorFormatChoice::Fastest:
    default:
        return NDIlib_recv_color_format_fastest;
    }
}

bool NdiReceiver::create(const NdiReceiverConfig& config) {
    destroy();
    config_ = config;

    NDIlib_recv_create_v3_t desc{};
    desc.color_format = mapColorFormat(config_.colorFormat);
    desc.bandwidth = config_.bandwidth;
    desc.allow_video_fields = config_.allowVideoFields;
    desc.p_ndi_recv_name = config_.receiverName.c_str();

    receiver_ = NDIlib_recv_create_v3(&desc);
    if (!receiver_) {
        return false;
    }

    if (config_.useFrameSync) {
        frameSync_ = NDIlib_framesync_create(receiver_);
    }

    applyHardwareDecodeHint();
    return true;
}

void NdiReceiver::destroy() {
    stop();
    if (frameSync_) {
        NDIlib_framesync_destroy(frameSync_);
        frameSync_ = nullptr;
    }
    if (receiver_) {
        NDIlib_recv_destroy(receiver_);
        receiver_ = nullptr;
    }
}

void NdiReceiver::applyHardwareDecodeHint() {
    if (!receiver_ || !config_.enableHardwareDecode) {
        return;
    }
    NDIlib_metadata_frame_t meta{};
    meta.p_data = const_cast<char*>(
        "<ndi_video_codec hardware=\"true\"/>");
    NDIlib_recv_add_connection_metadata(receiver_, &meta);
}

bool NdiReceiver::connectToSource(const NDIlib_source_t& source) {
    if (!receiver_) {
        return false;
    }
    NDIlib_recv_connect(receiver_, &source);
    return true;
}

void NdiReceiver::disconnect() {
    if (receiver_) {
        NDIlib_recv_connect(receiver_, nullptr);
    }
}

bool NdiReceiver::start(VideoCallback videoCb, AudioCallback audioCb) {
    if (!receiver_ || running_.load()) {
        return false;
    }
    videoCallback_ = std::move(videoCb);
    audioCallback_ = std::move(audioCb);
    running_.store(true);
    recvThread_ = std::thread(&NdiReceiver::recvLoop, this);
    return true;
}

void NdiReceiver::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (recvThread_.joinable()) {
        recvThread_.join();
    }
}

NdiReceiverStats NdiReceiver::stats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    NdiReceiverStats s = stats_;
    if (receiver_) {
        NDIlib_recv_performance_t total{};
        NDIlib_recv_performance_t dropped{};
        NDIlib_recv_get_performance(receiver_, &total, &dropped);
        s.droppedVideo = dropped.video_frames;
        s.droppedAudio = dropped.audio_frames;
    }
    return s;
}

void NdiReceiver::processVideoFrame(const NDIlib_video_frame_v2_t& frame) {
    if (!config_.enableVideo || !videoCallback_) {
        return;
    }
    NdiVideoFrameData data;
    data.buffer = copyVideoBuffer(frame);
    data.width = frame.xres;
    data.height = frame.yres;
    data.stride = frame.line_stride_in_bytes;
    data.fourCC = frame.FourCC;
    data.frameRateN = frame.frame_rate_N;
    data.frameRateD = frame.frame_rate_D;
    videoCallback_(data);

    std::lock_guard<std::mutex> lock(statsMutex_);
    ++stats_.videoFrames;
}

void NdiReceiver::processAudioFrame(const NDIlib_audio_frame_v3_t& frame) {
    if (!config_.enableAudio || !audioCallback_) {
        return;
    }
    NdiAudioFrameData data;
    data.samples = copyAudioBuffer(frame);
    data.sampleRate = frame.sample_rate;
    data.channels = frame.no_channels;
    data.sampleCount = frame.no_samples;
    audioCallback_(data);

    std::lock_guard<std::mutex> lock(statsMutex_);
    ++stats_.audioFrames;
}

void NdiReceiver::recvLoop() {
    while (running_.load()) {
        if (frameSync_ && config_.useFrameSync) {
            NDIlib_video_frame_v2_t video{};
            NDIlib_framesync_capture_video(frameSync_, &video, NDIlib_frame_format_type_progressive);
            if (video.p_data) {
                processVideoFrame(video);
                NDIlib_framesync_free_video(frameSync_, &video);
            }

            if (config_.enableAudio && audioCallback_) {
                NDIlib_audio_frame_v3_t audio{};
                NDIlib_framesync_capture_audio_v2(frameSync_, &audio, 48000, 2, 960);
                if (audio.p_data) {
                    processAudioFrame(audio);
                    NDIlib_framesync_free_audio_v2(frameSync_, &audio);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        NDIlib_video_frame_v2_t video{};
        NDIlib_audio_frame_v3_t audio{};
        const auto type = NDIlib_recv_capture_v3(
            receiver_,
            config_.enableVideo ? &video : nullptr,
            config_.enableAudio ? &audio : nullptr,
            nullptr,
            250);

        switch (type) {
        case NDIlib_frame_type_video:
            processVideoFrame(video);
            NDIlib_recv_free_video_v2(receiver_, &video);
            break;
        case NDIlib_frame_type_audio:
            processAudioFrame(audio);
            NDIlib_recv_free_audio_v3(receiver_, &audio);
            break;
        case NDIlib_frame_type_status_change:
            break;
        default:
            break;
        }
    }
}
