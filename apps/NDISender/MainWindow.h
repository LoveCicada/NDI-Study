#pragma once

#include "NdiContext.h"
#include "AlphaTestPattern.h"
#include "DxgiScreenCapture.h"
#include "MfH264Encoder.h"
#include "NdiSender.h"
#include "WasapiLoopbackCapture.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <atomic>
#include <memory>
#include <thread>

enum class NdiVideoSourceChoice {
    ScreenCapture,
    AlphaTestPattern,
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(NdiContext& ndiContext, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onRefreshOutputs();
    void onVideoSourceChanged();
    void onStart();
    void onStop();
    void updateStatus();

private:
    static void sampleBufferAlphaRange(const uint8_t* data, int width, int height, int stride,
                                       int& alphaMin, int& alphaMax);
    NdiSenderConfig buildConfig() const;
    void setupUi();
    void runCaptureLoop();
    void updateCaptureControls();

    NdiContext& ndiContext_;
    std::unique_ptr<NdiSender> sender_;
    std::unique_ptr<DxgiScreenCapture> capture_;
    std::unique_ptr<WasapiLoopbackCapture> audioCapture_;
    std::unique_ptr<MfH264Encoder> encoder_;
    AlphaTestPattern alphaPattern_;

    QLineEdit* nameEdit_ = nullptr;
    QLineEdit* groupsEdit_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QComboBox* videoSourceCombo_ = nullptr;
    QComboBox* outputCombo_ = nullptr;
    QSpinBox* patternWidthSpin_ = nullptr;
    QSpinBox* patternHeightSpin_ = nullptr;
    QSlider* patternAlphaSlider_ = nullptr;
    QLabel* patternAlphaValueLabel_ = nullptr;
    QLabel* patternAlphaHintLabel_ = nullptr;
    QCheckBox* enableVideoCheck_ = nullptr;
    QCheckBox* enableAudioCheck_ = nullptr;
    QCheckBox* clockVideoCheck_ = nullptr;
    QCheckBox* clockAudioCheck_ = nullptr;
    QDoubleSpinBox* hxBitrateSpin_ = nullptr;
    QPushButton* refreshOutputsBtn_ = nullptr;
    QPushButton* startBtn_ = nullptr;
    QPushButton* stopBtn_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QWidget* screenCaptureRow_ = nullptr;
    QWidget* patternSizeRow_ = nullptr;
    QWidget* patternAlphaRow_ = nullptr;

    std::vector<DxgiOutputInfo> outputs_;
    std::thread captureThread_;
    std::atomic<bool> running_{false};
    std::atomic<float> patternAlphaScale_{1.f};
    std::atomic<int> sentAlphaMin_{-1};
    std::atomic<int> sentAlphaMax_{-1};
    QTimer* statusTimer_ = nullptr;
    NdiSenderConfig activeConfig_;
    NdiVideoSourceChoice activeVideoSource_ = NdiVideoSourceChoice::ScreenCapture;
    int patternWidth_ = 1280;
    int patternHeight_ = 720;
    int frameRateN_ = 60000;
    int frameRateD_ = 1001;
};
