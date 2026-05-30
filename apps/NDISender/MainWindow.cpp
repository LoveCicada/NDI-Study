#include "MainWindow.h"
#include "NdiContext.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QTimer>
#include <QVBoxLayout>

#include <chrono>

MainWindow::MainWindow(NdiContext& ndiContext, QWidget* parent)
    : QMainWindow(parent)
    , ndiContext_(ndiContext) {
    if (!ndiContext_.isValid()) {
        QMessageBox::critical(this, tr("NDI"), tr("NDIlib_initialize 失败"));
    }
    sender_ = std::make_unique<NdiSender>();
    capture_ = std::make_unique<DxgiScreenCapture>();
    audioCapture_ = std::make_unique<WasapiLoopbackCapture>();
    encoder_ = std::make_unique<MfH264Encoder>();
    setupUi();
    onRefreshOutputs();
}

MainWindow::~MainWindow() {
    onStop();
}

void MainWindow::setupUi() {
    setWindowTitle(tr("NDISender Demo"));
    resize(480, 520);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    auto* sourceGroup = new QGroupBox(tr("NDI 源"), central);
    auto* sourceLayout = new QFormLayout(sourceGroup);
    nameEdit_ = new QLineEdit("NDISender Demo", sourceGroup);
    groupsEdit_ = new QLineEdit(sourceGroup);
    groupsEdit_->setPlaceholderText(tr("可选，逗号分隔"));
    modeCombo_ = new QComboBox(sourceGroup);
    modeCombo_->addItem(tr("High Bandwidth"), static_cast<int>(NdiSendMode::HighBandwidth));
    modeCombo_->addItem(tr("HX H.264 (Media Foundation)"), static_cast<int>(NdiSendMode::HxH264));
    sourceLayout->addRow(tr("源名称"), nameEdit_);
    sourceLayout->addRow(tr("Groups"), groupsEdit_);
    sourceLayout->addRow(tr("发送模式"), modeCombo_);

    auto* captureGroup = new QGroupBox(tr("采集"), central);
    auto* captureLayout = new QFormLayout(captureGroup);
    outputCombo_ = new QComboBox(captureGroup);
    refreshOutputsBtn_ = new QPushButton(tr("刷新显示器"), captureGroup);
    enableVideoCheck_ = new QCheckBox(tr("启用视频"), captureGroup);
    enableVideoCheck_->setChecked(true);
    enableAudioCheck_ = new QCheckBox(tr("启用音频 (WASAPI Loopback)"), captureGroup);
    enableAudioCheck_->setChecked(true);
    clockVideoCheck_ = new QCheckBox(tr("clock_video"), captureGroup);
    clockVideoCheck_->setChecked(true);
    clockAudioCheck_ = new QCheckBox(tr("clock_audio"), captureGroup);
    hxBitrateSpin_ = new QDoubleSpinBox(captureGroup);
    hxBitrateSpin_->setRange(0.1, 2.0);
    hxBitrateSpin_->setSingleStep(0.1);
    hxBitrateSpin_->setValue(1.0);
    captureLayout->addRow(tr("显示器"), outputCombo_);
    captureLayout->addRow(refreshOutputsBtn_);
    captureLayout->addRow(enableVideoCheck_);
    captureLayout->addRow(enableAudioCheck_);
    captureLayout->addRow(clockVideoCheck_);
    captureLayout->addRow(clockAudioCheck_);
    captureLayout->addRow(tr("HX 码率倍率"), hxBitrateSpin_);

    startBtn_ = new QPushButton(tr("开始推流"), central);
    stopBtn_ = new QPushButton(tr("停止推流"), central);
    statusLabel_ = new QLabel(tr("状态: 就绪"), central);
    statusLabel_->setWordWrap(true);

    layout->addWidget(sourceGroup);
    layout->addWidget(captureGroup);
    layout->addWidget(startBtn_);
    layout->addWidget(stopBtn_);
    layout->addWidget(statusLabel_);
    layout->addStretch();
    setCentralWidget(central);

    connect(refreshOutputsBtn_, &QPushButton::clicked, this, &MainWindow::onRefreshOutputs);
    connect(startBtn_, &QPushButton::clicked, this, &MainWindow::onStart);
    connect(stopBtn_, &QPushButton::clicked, this, &MainWindow::onStop);
}

NdiSenderConfig MainWindow::buildConfig() const {
    NdiSenderConfig cfg;
    cfg.ndiName = nameEdit_->text().toStdString();
    cfg.groups = groupsEdit_->text().toStdString();
    cfg.mode = static_cast<NdiSendMode>(modeCombo_->currentData().toInt());
    cfg.enableVideo = enableVideoCheck_->isChecked();
    cfg.enableAudio = enableAudioCheck_->isChecked();
    cfg.clockVideo = clockVideoCheck_->isChecked();
    cfg.clockAudio = clockAudioCheck_->isChecked();
    cfg.hxBitrateMultiplier = static_cast<float>(hxBitrateSpin_->value());
    return cfg;
}

void MainWindow::onRefreshOutputs() {
    outputs_ = DxgiScreenCapture::listOutputs();
    outputCombo_->clear();
    for (const auto& o : outputs_) {
        outputCombo_->addItem(
            QString("%1 (%2x%3)").arg(QString::fromStdString(o.name)).arg(o.width).arg(o.height),
            o.index);
    }
}

void MainWindow::onStart() {
    if (running_.load()) {
        return;
    }

    const int outputIndex = outputCombo_->currentData().toInt();
    if (!capture_->open(outputIndex)) {
        QMessageBox::critical(this, tr("采集"), tr("打开 DXGI 屏幕采集失败"));
        return;
    }

    const auto cfg = buildConfig();
    activeConfig_ = cfg;
    if (!sender_->create(cfg)) {
        capture_->close();
        QMessageBox::critical(this, tr("推流"), tr("创建 NDI Sender 失败"));
        return;
    }

    if (cfg.mode == NdiSendMode::HxH264 && cfg.enableVideo) {
        const int baseBitrate = sender_->getTargetBitrate(
            capture_->width(), capture_->height(), frameRateN_, frameRateD_);
        const uint32_t bitrate = static_cast<uint32_t>(baseBitrate * cfg.hxBitrateMultiplier);
        encoder_->setCallback([this](const EncodedH264Frame& frame) {
            sender_->sendVideoH264(frame.data.data(), frame.data.size(),
                                   capture_->width(), capture_->height(),
                                   frameRateN_, frameRateD_, frame.timestamp100ns);
        });
        if (!encoder_->open(capture_->width(), capture_->height(), frameRateN_, frameRateD_, bitrate)) {
            QMessageBox::warning(this, tr("HX 编码"),
                                 tr("Media Foundation H.264 编码器初始化失败，请检查系统编码器"));
        }
    }

    if (cfg.enableAudio) {
        audioCapture_->start([this](const float* audioData, int sampleRate, int channels, int frames) {
            sender_->sendAudio(audioData, sampleRate, channels, frames);
        });
    }

    running_.store(true);
    captureThread_ = std::thread(&MainWindow::runCaptureLoop, this);
    statusLabel_->setText(tr("状态: 推流中 (%1x%2)")
                              .arg(capture_->width())
                              .arg(capture_->height()));
}

void MainWindow::onStop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (captureThread_.joinable()) {
        captureThread_.join();
    }
    audioCapture_->stop();
    encoder_->close();
    sender_->flushVideoAsync();
    sender_->destroy();
    capture_->close();
    statusLabel_->setText(tr("状态: 已停止"));
}

void MainWindow::runCaptureLoop() {
    CapturedFrame frame;
    while (running_.load()) {
        if (!capture_->captureFrame(frame, 33)) {
            continue;
        }

        const auto cfg = activeConfig_;
        if (!cfg.enableVideo) {
            continue;
        }

        if (cfg.mode == NdiSendMode::HxH264 && encoder_->isOpen()) {
            encoder_->encodeBGRA(frame.bgra.data(), frame.stride, 0);
        } else {
            sender_->sendVideoBGRA(frame.bgra.data(), frame.width, frame.height, frame.stride,
                                   frameRateN_, frameRateD_);
        }
    }
}

void MainWindow::updateStatus() {
    // reserved for future stats
}
