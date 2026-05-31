#include "MainWindow.h"

#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSplitter>
#include <QVBoxLayout>

#include <memory>
#include <algorithm>

namespace {

void sampleAlphaRange(const NdiVideoFrameData& frame, int& alphaMin, int& alphaMax) {
    alphaMin = 255;
    alphaMax = 0;
    if (frame.fourCC != NDIlib_FourCC_type_BGRA || frame.width <= 0 || frame.height <= 0) {
        alphaMin = -1;
        alphaMax = -1;
        return;
    }
    const int stride = frame.stride > 0 ? frame.stride : frame.width * 4;
    const int stepX = std::max(1, frame.width / 64);
    const int stepY = std::max(1, frame.height / 64);
    for (int y = 0; y < frame.height; y += stepY) {
        const uint8_t* row = frame.buffer.data() + static_cast<size_t>(y) * stride;
        for (int x = 0; x < frame.width; x += stepX) {
            const uint8_t a = row[x * 4 + 3];
            alphaMin = std::min(alphaMin, static_cast<int>(a));
            alphaMax = std::max(alphaMax, static_cast<int>(a));
        }
    }
}

} // namespace

MainWindow::MainWindow(NdiContext& ndiContext, QWidget* parent)
    : QMainWindow(parent)
    , ndiContext_(ndiContext) {
    if (!ndiContext_.isValid()) {
        QMessageBox::critical(this, tr("NDI"), tr("NDIlib_initialize 失败"));
    }
    finder_ = std::make_unique<NdiFinder>();
    receiver_ = std::make_unique<NdiReceiver>();
    audioPlayer_ = std::make_unique<SdlAudioPlayer>();
    setupUi();
    sourceRefreshThread_ = std::thread([this]() { sourceRefreshWorker(); });
    QTimer::singleShot(0, this, [this]() { requestSourceRefresh(2000); });
}

MainWindow::~MainWindow() {
    shutdown_.store(true);
    refreshCv_.notify_one();
    onStopReceive();
    onDisconnect();
    if (sourceRefreshThread_.joinable()) {
        sourceRefreshThread_.join();
    }
}

void MainWindow::setupUi() {
    setWindowTitle(tr("NDIReceiver Demo"));
    resize(1100, 700);

    auto* central = new QWidget(this);
    auto* rootLayout = new QHBoxLayout(central);

    auto* controlPanel = new QWidget(central);
    auto* controlLayout = new QVBoxLayout(controlPanel);
    controlPanel->setMaximumWidth(380);

    auto* connGroup = new QGroupBox(tr("连接"), controlPanel);
    auto* connLayout = new QFormLayout(connGroup);
    sourceCombo_ = new QComboBox(connGroup);
    receiverNameEdit_ = new QLineEdit("NDIReceiver Demo", connGroup);
    refreshBtn_ = new QPushButton(tr("刷新源"), connGroup);
    connectBtn_ = new QPushButton(tr("连接"), connGroup);
    disconnectBtn_ = new QPushButton(tr("断开"), connGroup);
    connLayout->addRow(tr("NDI 源"), sourceCombo_);
    connLayout->addRow(tr("接收器名称"), receiverNameEdit_);
    connLayout->addRow(refreshBtn_);
    connLayout->addRow(connectBtn_);
    connLayout->addRow(disconnectBtn_);
    sourceStatusLabel_ = new QLabel(tr("源数量: -"), connGroup);
    sourceStatusLabel_->setWordWrap(true);
    connLayout->addRow(sourceStatusLabel_);

    auto* videoGroup = new QGroupBox(tr("视频"), controlPanel);
    auto* videoLayout = new QFormLayout(videoGroup);
    enableVideoCheck_ = new QCheckBox(tr("启用视频"), videoGroup);
    enableVideoCheck_->setChecked(true);
    colorFormatCombo_ = new QComboBox(videoGroup);
    colorFormatCombo_->addItem(tr("Fastest (低延时)"), static_cast<int>(NdiRecvColorFormatChoice::Fastest));
    colorFormatCombo_->addItem(tr("Best (高质量)"), static_cast<int>(NdiRecvColorFormatChoice::Best));
    colorFormatCombo_->addItem("UYVY/BGRA", static_cast<int>(NdiRecvColorFormatChoice::UYVY_BGRA));
    colorFormatCombo_->addItem("BGRX/BGRA", static_cast<int>(NdiRecvColorFormatChoice::BGRX_BGRA));
    alphaPresetBtn_ = new QPushButton(tr("Alpha 测试预设"), videoGroup);
    alphaPresetBtn_->setToolTip(tr("选择 Fastest 色彩格式、关闭硬件解码，并启用透明检测背景"));
    alphaCheckerCheck_ = new QCheckBox(tr("启用透明检测背景（棋盘格）"), videoGroup);
    previewAlphaSlider_ = new QSlider(Qt::Horizontal, videoGroup);
    previewAlphaSlider_->setRange(0, 100);
    previewAlphaSlider_->setValue(100);
    previewAlphaSlider_->setToolTip(tr("仅影响预览混合显示，不改变 NDI 原始帧；调低可验证棋盘格叠加是否正常"));
    previewAlphaValueLabel_ = new QLabel(tr("100%"), videoGroup);
    previewAlphaValueLabel_->setMinimumWidth(40);
    auto* previewAlphaRow = new QWidget(videoGroup);
    auto* previewAlphaLayout = new QHBoxLayout(previewAlphaRow);
    previewAlphaLayout->setContentsMargins(0, 0, 0, 0);
    previewAlphaLayout->addWidget(previewAlphaSlider_, 1);
    previewAlphaLayout->addWidget(previewAlphaValueLabel_);
    savePngBtn_ = new QPushButton(tr("保存当前帧 PNG..."), videoGroup);
    bandwidthCombo_ = new QComboBox(videoGroup);
    bandwidthCombo_->addItem(tr("最高带宽"), static_cast<int>(NDIlib_recv_bandwidth_highest));
    bandwidthCombo_->addItem(tr("较低带宽"), static_cast<int>(NDIlib_recv_bandwidth_lowest));
    allowFieldsCheck_ = new QCheckBox(tr("允许场视频"), videoGroup);
    allowFieldsCheck_->setChecked(true);
    frameSyncCheck_ = new QCheckBox(tr("Frame Sync (平滑播放)"), videoGroup);
    hwDecodeCheck_ = new QCheckBox(tr("硬件解码"), videoGroup);
    hwDecodeCheck_->setChecked(true);
    videoLayout->addRow(enableVideoCheck_);
    videoLayout->addRow(tr("色彩格式"), colorFormatCombo_);
    videoLayout->addRow(alphaPresetBtn_);
    videoLayout->addRow(alphaCheckerCheck_);
    videoLayout->addRow(tr("预览 Alpha 倍增"), previewAlphaRow);
    videoLayout->addRow(savePngBtn_);
    videoLayout->addRow(tr("带宽"), bandwidthCombo_);
    videoLayout->addRow(allowFieldsCheck_);
    videoLayout->addRow(frameSyncCheck_);
    videoLayout->addRow(hwDecodeCheck_);

    auto* audioGroup = new QGroupBox(tr("音频"), controlPanel);
    auto* audioLayout = new QFormLayout(audioGroup);
    enableAudioCheck_ = new QCheckBox(tr("启用音频"), audioGroup);
    enableAudioCheck_->setChecked(true);
    audioLayout->addRow(enableAudioCheck_);

    startBtn_ = new QPushButton(tr("开始接收"), controlPanel);
    stopBtn_ = new QPushButton(tr("停止接收"), controlPanel);
    statsLabel_ = new QLabel(tr("统计: -"), controlPanel);
    statsLabel_->setWordWrap(true);

    controlLayout->addWidget(connGroup);
    controlLayout->addWidget(videoGroup);
    controlLayout->addWidget(audioGroup);
    controlLayout->addWidget(startBtn_);
    controlLayout->addWidget(stopBtn_);
    controlLayout->addWidget(statsLabel_);
    controlLayout->addStretch();

    preview_ = new VideoPreviewWidget(central);

    rootLayout->addWidget(controlPanel);
    rootLayout->addWidget(preview_, 1);
    setCentralWidget(central);

    statsTimer_ = new QTimer(this);
    connect(statsTimer_, &QTimer::timeout, this, &MainWindow::updateStats);
    refreshTimer_ = new QTimer(this);
    connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::onAutoRefreshSources);
    refreshTimer_->start(2000);
    connect(refreshBtn_, &QPushButton::clicked, this, &MainWindow::onRefreshSources);
    connect(connectBtn_, &QPushButton::clicked, this, &MainWindow::onConnect);
    connect(disconnectBtn_, &QPushButton::clicked, this, &MainWindow::onDisconnect);
    connect(startBtn_, &QPushButton::clicked, this, &MainWindow::onStartReceive);
    connect(stopBtn_, &QPushButton::clicked, this, &MainWindow::onStopReceive);
    connect(alphaPresetBtn_, &QPushButton::clicked, this, &MainWindow::onAlphaTestPreset);
    connect(savePngBtn_, &QPushButton::clicked, this, &MainWindow::onSaveFramePng);
    connect(alphaCheckerCheck_, &QCheckBox::toggled, this, &MainWindow::onAlphaCheckerToggled);
    connect(previewAlphaSlider_, &QSlider::valueChanged, this, &MainWindow::onPreviewAlphaChanged);
}

QString MainWindow::fourCcToString(NDIlib_FourCC_video_type_e fourCC) {
    switch (fourCC) {
    case NDIlib_FourCC_type_BGRA:
        return QStringLiteral("BGRA");
    case NDIlib_FourCC_type_BGRX:
        return QStringLiteral("BGRX");
    case NDIlib_FourCC_type_UYVY:
        return QStringLiteral("UYVY");
    case NDIlib_FourCC_type_UYVA:
        return QStringLiteral("UYVA");
    default:
        return QString("0x%1").arg(static_cast<uint32_t>(fourCC), 8, 16, QChar('0'));
    }
}

NdiReceiverConfig MainWindow::buildConfig() const {
    NdiReceiverConfig cfg;
    cfg.receiverName = receiverNameEdit_->text().toStdString();
    cfg.colorFormat = static_cast<NdiRecvColorFormatChoice>(colorFormatCombo_->currentData().toInt());
    cfg.bandwidth = static_cast<NDIlib_recv_bandwidth_e>(bandwidthCombo_->currentData().toInt());
    cfg.allowVideoFields = allowFieldsCheck_->isChecked();
    cfg.useFrameSync = frameSyncCheck_->isChecked();
    cfg.enableHardwareDecode = hwDecodeCheck_->isChecked();
    cfg.enableVideo = enableVideoCheck_->isChecked();
    cfg.enableAudio = enableAudioCheck_->isChecked();
    return cfg;
}

void MainWindow::onAlphaTestPreset() {
    const int idx = colorFormatCombo_->findData(static_cast<int>(NdiRecvColorFormatChoice::Fastest));
    if (idx >= 0) {
        colorFormatCombo_->setCurrentIndex(idx);
    }
    hwDecodeCheck_->setChecked(false);
    frameSyncCheck_->setChecked(false);
    allowFieldsCheck_->setChecked(true);
    alphaCheckerCheck_->setChecked(true);
    previewAlphaSlider_->setValue(100);
    previewAlphaValueLabel_->setText(tr("100%"));
    preview_->setAlphaCheckerBackground(true);
    preview_->setPreviewAlphaScale(1.f);
}

void MainWindow::onAlphaCheckerToggled(bool enabled) {
    preview_->setAlphaCheckerBackground(enabled);
}

void MainWindow::onPreviewAlphaChanged(int value) {
    previewAlphaValueLabel_->setText(tr("%1%").arg(value));
    preview_->setPreviewAlphaScale(static_cast<float>(value) / 100.f);
}

void MainWindow::onSaveFramePng() {
    if (!preview_->hasBgraFrame()) {
        QMessageBox::warning(
            this, tr("保存 PNG"),
            tr("当前帧不可用。请使用 BGRX/BGRA 色彩格式接收带 Alpha 的源，并确保正在接收视频。"));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, tr("保存当前帧"), QString(), tr("PNG 图像 (*.png)"));
    if (path.isEmpty()) {
        return;
    }

    if (!preview_->saveCurrentFramePng(path)) {
        QMessageBox::critical(this, tr("保存 PNG"), tr("保存失败"));
        return;
    }
    QMessageBox::information(this, tr("保存 PNG"), tr("已保存到:\n%1").arg(path));
}

void MainWindow::requestSourceRefresh(uint32_t waitMs) {
    if (receiver_->isRunning()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(refreshRequestMutex_);
        pendingRefreshWaitMs_ = waitMs;
        refreshPending_.store(true);
    }
    refreshCv_.notify_one();
}

void MainWindow::sourceRefreshWorker() {
    while (!shutdown_.load()) {
        uint32_t waitMs = 250;
        {
            std::unique_lock<std::mutex> lock(refreshRequestMutex_);
            refreshCv_.wait(lock, [this]() {
                return shutdown_.load() || refreshPending_.load();
            });
            if (shutdown_.load()) {
                break;
            }
            waitMs = pendingRefreshWaitMs_;
            refreshPending_.store(false);
        }

        if (receiver_->isRunning()) {
            continue;
        }

        sourceRefreshInProgress_.store(true);
        std::vector<NdiSourceInfo> result;
        {
            std::lock_guard<std::mutex> lock(finderMutex_);
            if (finder_) {
                result = finder_->refresh(waitMs);
            }
        }

        if (shutdown_.load()) {
            break;
        }

        QMetaObject::invokeMethod(
            this,
            [this, result = std::move(result)]() mutable {
                applyRefreshedSources(std::move(result));
            },
            Qt::QueuedConnection);
    }
}

void MainWindow::onRefreshSources() {
    requestSourceRefresh(2000);
}

void MainWindow::onAutoRefreshSources() {
    if (sourceRefreshInProgress_.load()) {
        return;
    }
    requestSourceRefresh(250);
}

void MainWindow::applyRefreshedSources(std::vector<NdiSourceInfo> sources) {
    sourceRefreshInProgress_.store(false);
    if (shutdown_.load() || receiver_->isRunning()) {
        return;
    }
    sources_ = std::move(sources);
    const int prevIndex = sourceCombo_->currentIndex();
    const QString prevText = prevIndex >= 0 ? sourceCombo_->currentText() : QString();
    sourceCombo_->clear();
    for (const auto& s : sources_) {
        sourceCombo_->addItem(QString::fromStdString(s.name));
    }
    if (!prevText.isEmpty()) {
        const int restored = sourceCombo_->findText(prevText);
        if (restored >= 0) {
            sourceCombo_->setCurrentIndex(restored);
        }
    }
    const QString version = ndiContext_.version() ? QString::fromUtf8(ndiContext_.version()) : QString();
    if (sources_.empty()) {
        sourceStatusLabel_->setText(
            tr("源数量: 0\n请先启动 NDISender 或 NDI Tools 测试源，然后点击刷新。\nNDI: %1").arg(version));
    } else {
        sourceStatusLabel_->setText(
            tr("源数量: %1（每 2 秒自动刷新）\nNDI: %2").arg(sources_.size()).arg(version));
    }
}

void MainWindow::onConnect() {
    onStopReceive();
    const int idx = sourceCombo_->currentIndex();
    if (idx < 0 || idx >= static_cast<int>(sources_.size())) {
        QMessageBox::warning(this, tr("连接"), tr("请先选择 NDI 源"));
        return;
    }
    if (!receiver_->create(buildConfig())) {
        QMessageBox::critical(this, tr("连接"), tr("创建接收器失败"));
        return;
    }
    receiver_->connectToSource(sources_[static_cast<size_t>(idx)].source);
}

void MainWindow::onDisconnect() {
    onStopReceive();
    receiver_->disconnect();
}

void MainWindow::onStartReceive() {
    if (receiver_->isRunning()) {
        return;
    }
    refreshTimer_->stop();
    const int idx = sourceCombo_->currentIndex();
    if (idx < 0 || idx >= static_cast<int>(sources_.size())) {
        refreshTimer_->start(2000);
        QMessageBox::warning(this, tr("接收"), tr("未发现 NDI 源，请先启动推流端再刷新源列表"));
        return;
    }
    if (!receiver_->create(buildConfig())) {
        QMessageBox::critical(this, tr("接收"), tr("创建接收器失败"));
        refreshTimer_->start(2000);
        return;
    }
    receiver_->connectToSource(sources_[static_cast<size_t>(idx)].source);
    preview_->setAlphaCheckerBackground(alphaCheckerCheck_->isChecked());
    preview_->setPreviewAlphaScale(static_cast<float>(previewAlphaSlider_->value()) / 100.f);
    audioPlayer_->close();

    receiver_->start(
        [this](const NdiVideoFrameData& frame) { onVideoFrame(frame); },
        [this](const NdiAudioFrameData& frame) { onAudioFrame(frame); });

    statsTimer_->start(500);
}

void MainWindow::onStopReceive() {
    statsTimer_->stop();
    receiver_->stop();
    audioPlayer_->close();
    refreshTimer_->start(2000);
}

void MainWindow::onVideoFrame(const NdiVideoFrameData& frame) {
    if (frame.buffer.empty()) {
        return;
    }

    lastFourCC_ = frame.fourCC;
    if (++alphaSampleCounter_ % 15 == 0) {
        int alphaMin = -1;
        int alphaMax = -1;
        sampleAlphaRange(frame, alphaMin, alphaMax);
        lastAlphaMin_ = alphaMin;
        lastAlphaMax_ = alphaMax;
    }

    preview_->submitFrame(frame.buffer.data(), frame.width, frame.height,
                          frame.stride, frame.fourCC);
}

void MainWindow::onAudioFrame(const NdiAudioFrameData& frame) {
    if (frame.samples.empty() || !enableAudioCheck_->isChecked()) {
        return;
    }

    const int sampleRate = frame.sampleRate > 0 ? frame.sampleRate : 48000;
    const int channels = std::min(2, std::max(1, frame.channels));
    if (!audioPlayer_->ensureOpen(sampleRate, channels)) {
        return;
    }
    audioPlayer_->queue(frame.samples.data(), sampleRate, frame.channels, frame.sampleCount);
}

void MainWindow::updateStats() {
    const auto s = receiver_->stats();
    QString alphaText = tr("Alpha: -");
    if (lastAlphaMin_ >= 0 && lastAlphaMax_ >= 0) {
        alphaText = tr("Alpha: %1~%2").arg(lastAlphaMin_).arg(lastAlphaMax_);
    }
    statsLabel_->setText(
        tr("视频帧: %1  音频帧: %2\n丢视频: %3  丢音频: %4\nFourCC: %5  %6")
            .arg(s.videoFrames)
            .arg(s.audioFrames)
            .arg(s.droppedVideo)
            .arg(s.droppedAudio)
            .arg(fourCcToString(lastFourCC_))
            .arg(alphaText));
}
