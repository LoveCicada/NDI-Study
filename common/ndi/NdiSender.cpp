#include "NdiSender.h"

#include <Processing.NDI.Advanced.h>

#include <cstring>

NdiSender::NdiSender() = default;

NdiSender::~NdiSender() {
    destroy();
}

bool NdiSender::create(const NdiSenderConfig& config) {
    destroy();
    config_ = config;

    NDIlib_send_create_t desc{};
    desc.p_ndi_name = config_.ndiName.c_str();
    desc.p_groups = config_.groups.empty() ? nullptr : config_.groups.c_str();
    desc.clock_video = config_.clockVideo;
    desc.clock_audio = config_.clockAudio;

    sender_ = NDIlib_send_create(&desc);
    return sender_ != nullptr;
}

void NdiSender::destroy() {
    if (sender_) {
        NDIlib_send_send_video_async_v2(sender_, nullptr);
        NDIlib_send_destroy(sender_);
        sender_ = nullptr;
    }
}

bool NdiSender::sendVideoBGRA(const uint8_t* data, int width, int height, int stride,
                              int frameRateN, int frameRateD) {
    if (!sender_ || !config_.enableVideo || !data) {
        return false;
    }

    const int rowStride = stride > 0 ? stride : width * 4;
    const size_t size = static_cast<size_t>(rowStride) * static_cast<size_t>(height);
    videoSendBufferIndex_ = 1 - videoSendBufferIndex_;
    auto& owned = videoSendBuffers_[videoSendBufferIndex_];
    owned.resize(size);
    for (int y = 0; y < height; ++y) {
        std::memcpy(owned.data() + static_cast<size_t>(y) * rowStride,
                    data + static_cast<size_t>(y) * rowStride,
                    static_cast<size_t>(width) * 4);
    }

    NDIlib_video_frame_v2_t frame{};
    frame.xres = width;
    frame.yres = height;
    frame.FourCC = NDIlib_FourCC_type_BGRA;
    frame.frame_rate_N = frameRateN;
    frame.frame_rate_D = frameRateD;
    frame.picture_aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
    frame.frame_format_type = NDIlib_frame_format_type_progressive;
    frame.line_stride_in_bytes = rowStride;
    frame.p_data = owned.data();

    NDIlib_send_send_video_async_v2(sender_, &frame);
    return true;
}

bool NdiSender::sendVideoH264(const uint8_t* data, size_t size, int width, int height,
                              int frameRateN, int frameRateD, int64_t timecode100ns) {
    if (!sender_ || !config_.enableVideo || !data || size == 0) {
        return false;
    }

    NDIlib_video_frame_v2_t frame{};
    frame.xres = width;
    frame.yres = height;
    frame.FourCC = static_cast<NDIlib_FourCC_video_type_e>(NDIlib_FourCC_type_H264_highest_bandwidth);
    frame.frame_rate_N = frameRateN;
    frame.frame_rate_D = frameRateD;
    frame.picture_aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
    frame.frame_format_type = NDIlib_frame_format_type_progressive;
    frame.timecode = timecode100ns;
    frame.line_stride_in_bytes = static_cast<int>(size);
    frame.p_data = const_cast<uint8_t*>(data);

    NDIlib_send_send_video_async_v2(sender_, &frame);
    return true;
}

bool NdiSender::sendAudio(const float* interleaved, int sampleRate, int channels, int samples) {
    if (!sender_ || !config_.enableAudio || !interleaved || samples <= 0) {
        return false;
    }

    const size_t total = static_cast<size_t>(samples) * static_cast<size_t>(channels);
    audioBuffer_.assign(interleaved, interleaved + total);

    NDIlib_audio_frame_v3_t frame{};
    frame.sample_rate = sampleRate;
    frame.no_channels = channels;
    frame.no_samples = samples;
    frame.FourCC = NDIlib_FourCC_audio_type_FLTP;
    frame.p_data = reinterpret_cast<uint8_t*>(audioBuffer_.data());
    frame.channel_stride_in_bytes = static_cast<int>(sizeof(float) * static_cast<size_t>(samples));

    NDIlib_send_send_audio_v3(sender_, &frame);
    return true;
}

void NdiSender::flushVideoAsync() {
    if (sender_) {
        NDIlib_send_send_video_async_v2(sender_, nullptr);
    }
}

int NdiSender::getTargetBitrate(int width, int height, int frameRateN, int frameRateD) const {
    if (!sender_) {
        return 0;
    }
    NDIlib_video_frame_v2_t frame{};
    frame.xres = width;
    frame.yres = height;
    frame.frame_rate_N = frameRateN;
    frame.frame_rate_D = frameRateD;
    frame.FourCC = static_cast<NDIlib_FourCC_video_type_e>(NDIlib_FourCC_type_H264_highest_bandwidth);
    frame.frame_format_type = NDIlib_frame_format_type_progressive;
    return NDIlib_send_get_target_bit_rate(sender_, &frame);
}
