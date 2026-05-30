#include "VideoPreviewWidget.h"

#include <QImage>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>

#include <algorithm>
#include <cstring>

VideoPreviewWidget::VideoPreviewWidget(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setMinimumSize(640, 360);
    setStyleSheet(QStringLiteral("background-color: #1a1a1f;"));

    renderTimer_ = new QTimer(this);
    renderTimer_->setInterval(33);
    connect(renderTimer_, &QTimer::timeout, this, &VideoPreviewWidget::onRenderTick);
    renderTimer_->start();

    resizeDebounceTimer_ = new QTimer(this);
    resizeDebounceTimer_->setSingleShot(true);
    resizeDebounceTimer_->setInterval(100);
    connect(resizeDebounceTimer_, &QTimer::timeout, this, &VideoPreviewWidget::applyDebouncedResize);
}

VideoPreviewWidget::~VideoPreviewWidget() = default;

void VideoPreviewWidget::markRenderDirty() {
    renderDirty_.store(true);
}

void VideoPreviewWidget::setAlphaCheckerBackground(bool enabled) {
    alphaCheckerBackground_ = enabled;
    if (renderer_) {
        renderer_->setAlphaCheckerBackground(enabled);
    }
    markRenderDirty();
}

void VideoPreviewWidget::setPreviewAlphaScale(float scale) {
    previewAlphaScale_ = std::clamp(scale, 0.f, 1.f);
    if (renderer_) {
        renderer_->setPreviewAlphaScale(previewAlphaScale_);
    }
    markRenderDirty();
}

bool VideoPreviewWidget::hasBgraFrame() const {
    std::lock_guard<std::mutex> lock(frameMutex_);
    return hasFrame_ && displayFourCC_ == NDIlib_FourCC_type_BGRA && !displayFrame_.empty();
}

NDIlib_FourCC_video_type_e VideoPreviewWidget::lastFourCC() const {
    std::lock_guard<std::mutex> lock(frameMutex_);
    return displayFourCC_;
}

bool VideoPreviewWidget::saveCurrentFramePng(const QString& path) const {
    std::vector<uint8_t> local;
    int width = 0;
    int height = 0;
    int stride = 0;
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        if (!hasFrame_ || displayFrame_.empty() || displayFourCC_ != NDIlib_FourCC_type_BGRA) {
            return false;
        }
        local = displayFrame_;
        width = displayWidth_;
        height = displayHeight_;
        stride = displayStride_;
    }

    if (stride <= 0) {
        stride = width * 4;
    }

    QImage image(width, height, QImage::Format_ARGB32);
    for (int y = 0; y < height; ++y) {
        std::memcpy(image.scanLine(y), local.data() + static_cast<size_t>(y) * stride,
                    static_cast<size_t>(width) * 4);
    }
    return image.save(path, "PNG");
}

void VideoPreviewWidget::ensureRenderer() {
    if (renderer_ || !internalWinId()) {
        return;
    }
    renderer_ = std::make_unique<Dx11VideoRenderer>();
    renderer_->setAlphaCheckerBackground(alphaCheckerBackground_);
    renderer_->setPreviewAlphaScale(previewAlphaScale_);
    renderer_->initialize(reinterpret_cast<void*>(winId()), width(), height());
}

void VideoPreviewWidget::submitFrame(const uint8_t* frameData, int width, int height, int stride,
                                     NDIlib_FourCC_video_type_e fourCC) {
    if (!frameData || width <= 0 || height <= 0) {
        return;
    }
    int rowStride = stride;
    if (rowStride <= 0) {
        rowStride = (fourCC == NDIlib_FourCC_type_BGRA || fourCC == NDIlib_FourCC_type_BGRX)
            ? width * 4
            : width * 2;
    }
    const size_t size = static_cast<size_t>(rowStride) * static_cast<size_t>(height);
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        if (displayFrame_.size() != size) {
            displayFrame_.resize(size);
        }
        std::memcpy(displayFrame_.data(), frameData, size);
        displayWidth_ = width;
        displayHeight_ = height;
        displayStride_ = rowStride;
        displayFourCC_ = fourCC;
        hasFrame_ = true;
    }
    frameUpdated_.store(true);
}

void VideoPreviewWidget::onRenderTick() {
    const bool needRender = frameUpdated_.exchange(false) || renderDirty_.exchange(false);
    if (!needRender) {
        return;
    }

    bool hasFrame = false;
    std::vector<uint8_t> local;
    int w = 0;
    int h = 0;
    int stride = 0;
    NDIlib_FourCC_video_type_e fourCC = NDIlib_FourCC_type_UYVY;
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        hasFrame = hasFrame_;
        if (!hasFrame && !alphaCheckerBackground_) {
            return;
        }
        if (hasFrame) {
            local = displayFrame_;
            w = displayWidth_;
            h = displayHeight_;
            stride = displayStride_;
            fourCC = displayFourCC_;
        }
    }

    ensureRenderer();
    if (!renderer_) {
        return;
    }

    renderer_->setAlphaCheckerBackground(alphaCheckerBackground_);
    renderer_->setPreviewAlphaScale(previewAlphaScale_);

    if (!hasFrame) {
        if (alphaCheckerBackground_) {
            renderer_->renderCheckerboardOnly();
            renderer_->present();
        }
        return;
    }

    renderer_->renderFrame(local.data(), w, h, stride, fourCC);
    renderer_->present();
}

void VideoPreviewWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
}

void VideoPreviewWidget::applyDebouncedResize() {
    if (!renderer_) {
        return;
    }
    renderer_->resize(pendingResizeWidth_, pendingResizeHeight_);
    markRenderDirty();
}

void VideoPreviewWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    pendingResizeWidth_ = width();
    pendingResizeHeight_ = height();
    if (!renderer_) {
        return;
    }
    resizeDebounceTimer_->start();
}
