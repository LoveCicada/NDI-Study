#pragma once

#include "Dx11VideoRenderer.h"

#include <Processing.NDI.Lib.h>

#include <QWidget>
#include <memory>
#include <mutex>

class VideoPreviewWidget : public QWidget {
    Q_OBJECT

public:
    explicit VideoPreviewWidget(QWidget* parent = nullptr);
    ~VideoPreviewWidget() override;

    void submitFrame(const uint8_t* data, int width, int height, int stride,
                     NDIlib_FourCC_video_type_e fourCC);

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void ensureRenderer();
    void drawPending();

    std::unique_ptr<Dx11VideoRenderer> renderer_;
    std::mutex frameMutex_;
    std::vector<uint8_t> pending_;
    int pendingWidth_ = 0;
    int pendingHeight_ = 0;
    int pendingStride_ = 0;
    NDIlib_FourCC_video_type_e pendingFourCC_ = NDIlib_FourCC_type_UYVY;
    bool hasPending_ = false;
};
