#pragma once

#include "Dx11VideoRenderer.h"

#include <Processing.NDI.Lib.h>

#include <QWidget>
#include <atomic>
#include <memory>
#include <mutex>

class VideoPreviewWidget : public QWidget {
    Q_OBJECT

public:
    explicit VideoPreviewWidget(QWidget* parent = nullptr);
    ~VideoPreviewWidget() override;

    void submitFrame(const uint8_t* frameData, int width, int height, int stride,
                     NDIlib_FourCC_video_type_e fourCC);
    void setAlphaCheckerBackground(bool enabled);
    void setPreviewAlphaScale(float scale);
    bool saveCurrentFramePng(const QString& path) const;
    bool hasBgraFrame() const;
    NDIlib_FourCC_video_type_e lastFourCC() const;

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void ensureRenderer();
    void onRenderTick();
    void markRenderDirty();

    std::unique_ptr<Dx11VideoRenderer> renderer_;
    mutable std::mutex frameMutex_;
    std::vector<uint8_t> displayFrame_;
    int displayWidth_ = 0;
    int displayHeight_ = 0;
    int displayStride_ = 0;
    NDIlib_FourCC_video_type_e displayFourCC_ = NDIlib_FourCC_type_UYVY;
    bool hasFrame_ = false;
    std::atomic<bool> frameUpdated_{false};
    std::atomic<bool> renderDirty_{false};
    bool alphaCheckerBackground_ = false;
    float previewAlphaScale_ = 1.f;
};
