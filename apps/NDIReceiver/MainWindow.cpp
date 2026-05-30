#include "MainWindow.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSplitter>
#include <QVBoxLayout>

#include <memory>

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
    onRefreshSources();
}

MainWindow::~MainWindow() {
    onStopReceive();
    onDisconnect();
}

void MainWindow::setupUi() {
    setWindowTitle(tr("NDIReceiver Demo"));
    resize(1100, 700);

    auto* central = new QWidget(this);
    auto* rootLayout = new QHBoxLayout(central);

    auto* controlPanel = new QWidget(central);
    auto* controlLayout = new QVBoxLayout(controlPanel);
    controlPanel->setMaximumWidth(360);

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
    connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::onRefreshSources);
    refreshTimer_->start(2000);
    connect(refreshBtn_, &QPushButton::clicked, this, &MainWindow::onRefreshSources);
    connect(connectBtn_, &QPushButton::clicked, this, &MainWindow::onConnect);
    connect(disconnectBtn_, &QPushButton::clicked, this, &MainWindow::onDisconnect);
    connect(startBtn_, &QPushButton::clicked, this, &MainWindow::onStartReceive);
    connect(stopBtn_, &QPushButton::clicked, this, &MainWindow::onStopReceive);
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

void MainWindow::onRefreshSources() {
    if (receiver_->isRunning()) {
        return;
    }
    sources_ = finder_->refresh();
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
    const int idx = sourceCombo_->currentIndex();
    if (idx < 0 || idx >= static_cast<int>(sources_.size())) {
        QMessageBox::warning(this, tr("连接"), tr("请先选择 NDI 源"));
        return;
    }
    receiver_->destroy();
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
    onRefreshSources();
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

    if (enableAudioCheck_->isChecked()) {
        audioPlayer_->open(48000, 2);
    }

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
    auto copy = std::make_shared<NdiVideoFrameData>(frame);
    QMetaObject::invokeMethod(this, [this, copy]() {
        preview_->submitFrame(copy->buffer.data(), copy->width, copy->height,
                              copy->stride, copy->fourCC);
    }, Qt::QueuedConnection);
}

void MainWindow::onAudioFrame(const NdiAudioFrameData& frame) {
    if (frame.samples.empty()) {
        return;
    }
    audioPlayer_->queue(frame.samples.data(), frame.sampleRate, frame.channels, frame.sampleCount);
}

void MainWindow::updateStats() {
    const auto s = receiver_->stats();
    statsLabel_->setText(
        tr("视频帧: %1  音频帧: %2\n丢视频: %3  丢音频: %4")
            .arg(s.videoFrames)
            .arg(s.audioFrames)
            .arg(s.droppedVideo)
            .arg(s.droppedAudio));
}
