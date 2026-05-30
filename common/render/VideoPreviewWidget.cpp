#include "VideoPreviewWidget.h"

#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>

VideoPreviewWidget::VideoPreviewWidget(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NativeWindow);
    setMinimumSize(640, 360);

    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() {
        if (hasPending_) {
            drawPending();
        }
    });
    timer->start(16);
}

VideoPreviewWidget::~VideoPreviewWidget() = default;

void VideoPreviewWidget::ensureRenderer() {
    if (renderer_ || !internalWinId()) {
        return;
    }
    renderer_ = std::make_unique<Dx11VideoRenderer>();
    renderer_->initialize(reinterpret_cast<void*>(winId()), width(), height());
}

void VideoPreviewWidget::submitFrame(const uint8_t* data, int width, int height, int stride,
                                     NDIlib_FourCC_video_type_e fourCC) {
    if (!data || width <= 0 || height <= 0) {
        return;
    }
    int rowStride = stride;
    if (rowStride <= 0) {
        rowStride = (fourCC == NDIlib_FourCC_type_BGRA || fourCC == NDIlib_FourCC_type_BGRX)
            ? width * 4
            : width * 2;
    }
    const size_t size = static_cast<size_t>(rowStride) * static_cast<size_t>(height);
    std::lock_guard<std::mutex> lock(frameMutex_);
    pending_.assign(data, data + size);
    pendingWidth_ = width;
    pendingHeight_ = height;
    pendingStride_ = rowStride;
    pendingFourCC_ = fourCC;
    hasPending_ = true;
}

void VideoPreviewWidget::drawPending() {
    ensureRenderer();
    if (!renderer_) {
        return;
    }

    std::vector<uint8_t> local;
    int w = 0, h = 0, stride = 0;
    NDIlib_FourCC_video_type_e fourCC = NDIlib_FourCC_type_UYVY;
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        if (!hasPending_) {
            return;
        }
        local.swap(pending_);
        w = pendingWidth_;
        h = pendingHeight_;
        stride = pendingStride_;
        fourCC = pendingFourCC_;
        hasPending_ = false;
    }

    renderer_->renderFrame(local.data(), w, h, stride, fourCC);
    renderer_->present();
}

void VideoPreviewWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    ensureRenderer();
}

void VideoPreviewWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    ensureRenderer();
    if (renderer_) {
        renderer_->resize(width(), height());
    }
}

void VideoPreviewWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    drawPending();
}
