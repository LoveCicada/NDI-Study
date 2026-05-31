#include "MainWindow.h"
#include "NdiContext.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <chrono>
#include <algorithm>

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
    resize(480, 560);

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
    videoSourceCombo_ = new QComboBox(captureGroup);
    videoSourceCombo_->addItem(tr("DXGI 屏幕采集"), static_cast<int>(NdiVideoSourceChoice::ScreenCapture));
    videoSourceCombo_->addItem(tr("Alpha 测试图"), static_cast<int>(NdiVideoSourceChoice::AlphaTestPattern));

    colorFormatCombo_ = new QComboBox(captureGroup);
    colorFormatCombo_->addItem(tr("BGRA（含 Alpha）"), static_cast<int>(NdiSendColorFormatChoice::BGRA));
    colorFormatCombo_->addItem(tr("BGRX（无 Alpha）"), static_cast<int>(NdiSendColorFormatChoice::BGRX));
    colorFormatCombo_->addItem(tr("UYVY（YUV 4:2:2）"), static_cast<int>(NdiSendColorFormatChoice::UYVY));
    colorFormatCombo_->addItem(tr("UYVA（YUV + Alpha 平面）"), static_cast<int>(NdiSendColorFormatChoice::UYVA));
    colorFormatCombo_->setToolTip(
        tr("High Bandwidth 下提交给 NDI SDK 的 FourCC。\n"
           "Alpha 测试推荐 UYVA + Receiver Fastest；BGRA 可能被 SDK 转为 UYVY/UYVA。"));

    outputCombo_ = new QComboBox(captureGroup);
    refreshOutputsBtn_ = new QPushButton(tr("刷新显示器"), captureGroup);
    screenCaptureRow_ = new QWidget(captureGroup);
    auto* screenCaptureLayout = new QVBoxLayout(screenCaptureRow_);
    screenCaptureLayout->setContentsMargins(0, 0, 0, 0);
    screenCaptureLayout->addWidget(outputCombo_);
    screenCaptureLayout->addWidget(refreshOutputsBtn_);

    patternSizeRow_ = new QWidget(captureGroup);
    auto* patternSizeLayout = new QHBoxLayout(patternSizeRow_);
    patternSizeLayout->setContentsMargins(0, 0, 0, 0);
    patternWidthSpin_ = new QSpinBox(patternSizeRow_);
    patternWidthSpin_->setRange(64, 3840);
    patternWidthSpin_->setValue(1280);
    patternHeightSpin_ = new QSpinBox(patternSizeRow_);
    patternHeightSpin_->setRange(64, 2160);
    patternHeightSpin_->setValue(720);
    patternSizeLayout->addWidget(new QLabel(tr("宽"), patternSizeRow_));
    patternSizeLayout->addWidget(patternWidthSpin_);
    patternSizeLayout->addWidget(new QLabel(tr("高"), patternSizeRow_));
    patternSizeLayout->addWidget(patternHeightSpin_);
    patternSizeRow_->setVisible(false);

    patternAlphaRow_ = new QWidget(captureGroup);
    auto* patternAlphaLayout = new QHBoxLayout(patternAlphaRow_);
    patternAlphaLayout->setContentsMargins(0, 0, 0, 0);
    patternAlphaSlider_ = new QSlider(Qt::Horizontal, patternAlphaRow_);
    patternAlphaSlider_->setRange(0, 100);
    patternAlphaSlider_->setValue(100);
    patternAlphaValueLabel_ = new QLabel(tr("100%"), patternAlphaRow_);
    patternAlphaValueLabel_->setMinimumWidth(40);
    patternAlphaLayout->addWidget(patternAlphaSlider_, 1);
    patternAlphaLayout->addWidget(patternAlphaValueLabel_);
    patternAlphaRow_->setVisible(false);

    patternAlphaHintLabel_ = new QLabel(
        tr("调节推流 Alpha 倍率；Sender 无本地预览。下方状态栏显示「发送 Alpha」范围，"
           "Receiver 端在预览 Alpha=100% 时可观察 NDI 是否保留透明。"),
        captureGroup);
    patternAlphaHintLabel_->setWordWrap(true);
    patternAlphaHintLabel_->setVisible(false);

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
    captureLayout->addRow(tr("视频源"), videoSourceCombo_);
    captureLayout->addRow(tr("发送色彩格式"), colorFormatCombo_);
    captureLayout->addRow(tr("屏幕"), screenCaptureRow_);
    captureLayout->addRow(tr("测试图尺寸"), patternSizeRow_);
    captureLayout->addRow(tr("测试图 Alpha"), patternAlphaRow_);
    captureLayout->addRow(patternAlphaHintLabel_);
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
    connect(videoSourceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onVideoSourceChanged);
    connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::updateCaptureControls);
    connect(patternAlphaSlider_, &QSlider::valueChanged, this, [this](int value) {
        patternAlphaValueLabel_->setText(tr("%1%").arg(value));
        patternAlphaScale_.store(static_cast<float>(value) / 100.f);
        alphaPattern_.setAlphaScale(patternAlphaScale_.load());
    });
    statusTimer_ = new QTimer(this);
    connect(statusTimer_, &QTimer::timeout, this, &MainWindow::updateStatus);
    connect(startBtn_, &QPushButton::clicked, this, &MainWindow::onStart);
    connect(stopBtn_, &QPushButton::clicked, this, &MainWindow::onStop);
    updateCaptureControls();
}

void MainWindow::onVideoSourceChanged() {
    updateCaptureControls();
}

void MainWindow::updateCaptureControls() {
    const auto source = static_cast<NdiVideoSourceChoice>(videoSourceCombo_->currentData().toInt());
    const bool pattern = source == NdiVideoSourceChoice::AlphaTestPattern;
    const bool hxMode = static_cast<NdiSendMode>(modeCombo_->currentData().toInt()) == NdiSendMode::HxH264;
    const bool streaming = running_.load();

    screenCaptureRow_->setVisible(!pattern);
    patternSizeRow_->setVisible(pattern);
    patternAlphaRow_->setVisible(pattern);
    patternAlphaHintLabel_->setVisible(pattern);
    if (pattern) {
        modeCombo_->setCurrentIndex(modeCombo_->findData(static_cast<int>(NdiSendMode::HighBandwidth)));
        modeCombo_->setEnabled(false);
        enableAudioCheck_->setChecked(false);
        enableAudioCheck_->setEnabled(false);
        clockVideoCheck_->setChecked(false);
        if (!streaming) {
            const int uyvaIdx = colorFormatCombo_->findData(
                static_cast<int>(NdiSendColorFormatChoice::UYVA));
            if (uyvaIdx >= 0) {
                colorFormatCombo_->setCurrentIndex(uyvaIdx);
            }
        }
    } else {
        modeCombo_->setEnabled(!streaming);
        enableAudioCheck_->setEnabled(!streaming);
    }

    const bool colorFormatEnabled = !streaming && !hxMode;
    colorFormatCombo_->setEnabled(colorFormatEnabled);
    if (hxMode) {
        colorFormatCombo_->setToolTip(tr("HX H.264 为压缩 Annex-B 流，不提供色彩格式选项。"));
    } else {
        colorFormatCombo_->setToolTip(
            tr("High Bandwidth 下提交给 NDI SDK 的 FourCC。\n"
               "Alpha 测试推荐 UYVA + Receiver Fastest；BGRA 可能被 SDK 转为 UYVY/UYVA。"));
    }
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
    cfg.colorFormat = static_cast<NdiSendColorFormatChoice>(colorFormatCombo_->currentData().toInt());
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

    activeVideoSource_ = static_cast<NdiVideoSourceChoice>(videoSourceCombo_->currentData().toInt());
    patternWidth_ = patternWidthSpin_->value();
    patternHeight_ = patternHeightSpin_->value();
    patternAlphaScale_.store(static_cast<float>(patternAlphaSlider_->value()) / 100.f);
    alphaPattern_.setSize(patternWidth_, patternHeight_);
    alphaPattern_.setAlphaScale(patternAlphaScale_.load());

    if (activeVideoSource_ == NdiVideoSourceChoice::ScreenCapture) {
        const int outputIndex = outputCombo_->currentData().toInt();
        if (!capture_->open(outputIndex)) {
            QMessageBox::critical(this, tr("采集"), tr("打开 DXGI 屏幕采集失败"));
            return;
        }
    }

    const auto cfg = buildConfig();
    if (activeVideoSource_ == NdiVideoSourceChoice::AlphaTestPattern &&
        cfg.mode != NdiSendMode::HighBandwidth) {
        capture_->close();
        QMessageBox::warning(this, tr("Alpha 测试"), tr("Alpha 测试图仅支持 High Bandwidth 模式"));
        return;
    }

    activeConfig_ = cfg;
    if (!sender_->create(cfg)) {
        capture_->close();
        QMessageBox::critical(this, tr("推流"), tr("创建 NDI Sender 失败"));
        return;
    }

    if (activeVideoSource_ == NdiVideoSourceChoice::ScreenCapture &&
        cfg.mode == NdiSendMode::HxH264 && cfg.enableVideo) {
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

    if (cfg.enableAudio && activeVideoSource_ == NdiVideoSourceChoice::ScreenCapture) {
        audioCapture_->setExcludeProcessNames({L"NDIReceiver.exe"});
        if (!audioCapture_->start([this](const float* audioData, int sampleRate, int channels, int frames) {
                sender_->sendAudio(audioData, sampleRate, channels, frames);
            })) {
            QMessageBox::warning(this, tr("音频"),
                                 tr("WASAPI Loopback 音频采集启动失败，将仅推流视频。"));
        } else if (!audioCapture_->usedProcessExclude()) {
            QMessageBox::information(this, tr("音频"),
                                     tr("未能排除 NDIReceiver 进程，同机推拉流时可能产生音频回授。"
                                        "建议关闭 Receiver 本地播放，或使用 Windows 10 2004+ 同机测试。"));
        }
    }

    running_.store(true);
    captureThread_ = std::thread(&MainWindow::runCaptureLoop, this);
    statusTimer_->start(500);
    updateCaptureControls();

    const QString fourCc = QString::fromStdString(sendColorFormatName(activeConfig_.colorFormat));
    if (activeVideoSource_ == NdiVideoSourceChoice::AlphaTestPattern) {
        statusLabel_->setText(tr("状态: Alpha 测试图推流中 (%1x%2)\n发送 FourCC: %3")
                                  .arg(patternWidth_)
                                  .arg(patternHeight_)
                                  .arg(fourCc));
    } else if (activeConfig_.mode == NdiSendMode::HxH264) {
        statusLabel_->setText(tr("状态: HX 推流中 (%1x%2)")
                                  .arg(capture_->width())
                                  .arg(capture_->height()));
    } else {
        statusLabel_->setText(tr("状态: 推流中 (%1x%2)\n发送 FourCC: %3")
                                  .arg(capture_->width())
                                  .arg(capture_->height())
                                  .arg(fourCc));
    }
}

void MainWindow::onStop() {
    if (!running_.exchange(false)) {
        return;
    }
    statusTimer_->stop();
    if (captureThread_.joinable()) {
        captureThread_.join();
    }
    audioCapture_->stop();
    encoder_->close();
    sender_->flushVideoAsync();
    sender_->destroy();
    capture_->close();
    updateCaptureControls();
    statusLabel_->setText(tr("状态: 已停止"));
}

void MainWindow::runCaptureLoop() {
    CapturedFrame frame;
    std::vector<uint8_t> patternBuffer;
    uint64_t patternFrameIndex = 0;

    while (running_.load()) {
        const auto cfg = activeConfig_;
        if (!cfg.enableVideo) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        if (activeVideoSource_ == NdiVideoSourceChoice::AlphaTestPattern) {
            int width = 0;
            int height = 0;
            int stride = 0;
            alphaPattern_.setAlphaScale(patternAlphaScale_.load());
            alphaPattern_.fillFrame(patternBuffer, width, height, stride, patternFrameIndex++);
            int alphaMin = -1;
            int alphaMax = -1;
            sampleBufferAlphaRange(patternBuffer.data(), width, height, stride, alphaMin, alphaMax);
            sentAlphaMin_.store(alphaMin);
            sentAlphaMax_.store(alphaMax);
            sender_->sendVideo(patternBuffer.data(), width, height, stride,
                               frameRateN_, frameRateD_, cfg.colorFormat);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        if (!capture_->captureFrame(frame, 33)) {
            continue;
        }

        if (cfg.mode == NdiSendMode::HxH264 && encoder_->isOpen()) {
            encoder_->encodeBGRA(frame.bgra.data(), frame.stride, 0);
        } else {
            sender_->sendVideo(frame.bgra.data(), frame.width, frame.height, frame.stride,
                               frameRateN_, frameRateD_, cfg.colorFormat);
        }
    }
}

void MainWindow::sampleBufferAlphaRange(const uint8_t* data, int width, int height, int stride,
                                        int& alphaMin, int& alphaMax) {
    alphaMin = 255;
    alphaMax = 0;
    if (!data || width <= 0 || height <= 0) {
        alphaMin = -1;
        alphaMax = -1;
        return;
    }
    const int rowStride = stride > 0 ? stride : width * 4;
    const int stepX = std::max(1, width / 64);
    const int stepY = std::max(1, height / 64);
    for (int y = 0; y < height; y += stepY) {
        const uint8_t* row = data + static_cast<size_t>(y) * rowStride;
        for (int x = 0; x < width; x += stepX) {
            const uint8_t a = row[x * 4 + 3];
            alphaMin = std::min(alphaMin, static_cast<int>(a));
            alphaMax = std::max(alphaMax, static_cast<int>(a));
        }
    }
}

void MainWindow::updateStatus() {
    if (!running_.load()) {
        return;
    }

    if (activeVideoSource_ == NdiVideoSourceChoice::AlphaTestPattern) {
        const int alphaMin = sentAlphaMin_.load();
        const int alphaMax = sentAlphaMax_.load();
        QString alphaText = tr("发送 Alpha: -");
        if (alphaMin >= 0 && alphaMax >= 0) {
            alphaText = tr("发送 Alpha: %1~%2").arg(alphaMin).arg(alphaMax);
        }
        const QString fourCc = QString::fromStdString(
            sendColorFormatName(activeConfig_.colorFormat));
        statusLabel_->setText(
            tr("状态: Alpha 测试图推流中 (%1x%2)\n发送 FourCC: %3\n%4")
                .arg(patternWidth_)
                .arg(patternHeight_)
                .arg(fourCc)
                .arg(alphaText));
        return;
    }

    if (activeConfig_.mode != NdiSendMode::HxH264) {
        const QString fourCc = QString::fromStdString(
            sendColorFormatName(activeConfig_.colorFormat));
        statusLabel_->setText(tr("状态: 推流中 (%1x%2)\n发送 FourCC: %3")
                                  .arg(capture_->width())
                                  .arg(capture_->height())
                                  .arg(fourCc));
        return;
    }

    statusLabel_->setText(tr("状态: HX 推流中 (%1x%2)")
                              .arg(capture_->width())
                              .arg(capture_->height()));
}
