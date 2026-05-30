#pragma once

#include "NdiContext.h"
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

#include <atomic>
#include <memory>
#include <thread>

class NdiContext;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(NdiContext& ndiContext, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onRefreshOutputs();
    void onStart();
    void onStop();
    void updateStatus();

private:
    NdiSenderConfig buildConfig() const;
    void setupUi();
    void runCaptureLoop();

    NdiContext& ndiContext_;
    std::unique_ptr<NdiSender> sender_;
    std::unique_ptr<DxgiScreenCapture> capture_;
    std::unique_ptr<WasapiLoopbackCapture> audioCapture_;
    std::unique_ptr<MfH264Encoder> encoder_;

    QLineEdit* nameEdit_ = nullptr;
    QLineEdit* groupsEdit_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QComboBox* outputCombo_ = nullptr;
    QCheckBox* enableVideoCheck_ = nullptr;
    QCheckBox* enableAudioCheck_ = nullptr;
    QCheckBox* clockVideoCheck_ = nullptr;
    QCheckBox* clockAudioCheck_ = nullptr;
    QDoubleSpinBox* hxBitrateSpin_ = nullptr;
    QPushButton* refreshOutputsBtn_ = nullptr;
    QPushButton* startBtn_ = nullptr;
    QPushButton* stopBtn_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    std::vector<DxgiOutputInfo> outputs_;
    std::thread captureThread_;
    std::atomic<bool> running_{false};
    NdiSenderConfig activeConfig_;
    int frameRateN_ = 60000;
    int frameRateD_ = 1001;
};
