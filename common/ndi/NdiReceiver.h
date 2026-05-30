#pragma once

#include <Processing.NDI.Lib.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class NdiRecvColorFormatChoice {
    Fastest,
    Best,
    UYVY_BGRA,
    BGRX_BGRA,
};

struct NdiReceiverConfig {
    std::string receiverName = "NDIReceiver Demo";
    NdiRecvColorFormatChoice colorFormat = NdiRecvColorFormatChoice::Fastest;
    NDIlib_recv_bandwidth_e bandwidth = NDIlib_recv_bandwidth_highest;
    bool allowVideoFields = true;
    bool useFrameSync = false;
    bool enableHardwareDecode = true;
    bool enableVideo = true;
    bool enableAudio = true;
};

struct NdiReceiverStats {
    int64_t videoFrames = 0;
    int64_t audioFrames = 0;
    int64_t droppedVideo = 0;
    int64_t droppedAudio = 0;
};

struct NdiVideoFrameData {
    std::vector<uint8_t> buffer;
    int width = 0;
    int height = 0;
    int stride = 0;
    NDIlib_FourCC_video_type_e fourCC = NDIlib_FourCC_type_UYVY;
    int frameRateN = 60000;
    int frameRateD = 1001;
};

struct NdiAudioFrameData {
    std::vector<float> samples;
    int sampleRate = 48000;
    int channels = 2;
    int sampleCount = 0;
};

class NdiReceiver {
public:
    using VideoCallback = std::function<void(const NdiVideoFrameData&)>;
    using AudioCallback = std::function<void(const NdiAudioFrameData&)>;

    NdiReceiver();
    ~NdiReceiver();

    NdiReceiver(const NdiReceiver&) = delete;
    NdiReceiver& operator=(const NdiReceiver&) = delete;

    bool create(const NdiReceiverConfig& config);
    void destroy();

    bool connectToSource(const NDIlib_source_t& source);
    void disconnect();

    bool start(VideoCallback videoCb, AudioCallback audioCb);
    void stop();

    bool isRunning() const { return running_.load(); }
    NdiReceiverStats stats() const;

    void setConfig(const NdiReceiverConfig& config) { config_ = config; }
    const NdiReceiverConfig& config() const { return config_; }

private:
    static NDIlib_recv_color_format_e mapColorFormat(NdiRecvColorFormatChoice choice);
    void recvLoop();
    void processVideoFrame(const NDIlib_video_frame_v2_t& frame);
    void processAudioFrame(const NDIlib_audio_frame_v3_t& frame);
    void applyHardwareDecodeHint();

    NDIlib_recv_instance_t receiver_ = nullptr;
    NDIlib_framesync_instance_t frameSync_ = nullptr;
    NdiReceiverConfig config_;
    VideoCallback videoCallback_;
    AudioCallback audioCallback_;

    std::thread recvThread_;
    std::atomic<bool> running_{false};

    mutable std::mutex statsMutex_;
    NdiReceiverStats stats_;
};
