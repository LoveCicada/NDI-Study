#pragma once

#include "NdiContext.h"
#include "NdiReceiver.h"
#include "NdiFinder.h"
#include "SdlAudioPlayer.h"
#include "VideoPreviewWidget.h"

#include <QComboBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>

#include <memory>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(NdiContext& ndiContext, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onRefreshSources();
    void onConnect();
    void onDisconnect();
    void onStartReceive();
    void onStopReceive();
    void updateStats();
    void onAlphaTestPreset();
    void onSaveFramePng();
    void onAlphaCheckerToggled(bool enabled);
    void onPreviewAlphaChanged(int value);

private:
    NdiReceiverConfig buildConfig() const;
    void setupUi();
    void onVideoFrame(const NdiVideoFrameData& frame);
    void onAudioFrame(const NdiAudioFrameData& frame);
    static QString fourCcToString(NDIlib_FourCC_video_type_e fourCC);

    NdiContext& ndiContext_;
    std::unique_ptr<NdiFinder> finder_;
    std::unique_ptr<NdiReceiver> receiver_;
    std::unique_ptr<SdlAudioPlayer> audioPlayer_;

    QComboBox* sourceCombo_ = nullptr;
    QLineEdit* receiverNameEdit_ = nullptr;
    QComboBox* colorFormatCombo_ = nullptr;
    QComboBox* bandwidthCombo_ = nullptr;
    QCheckBox* allowFieldsCheck_ = nullptr;
    QCheckBox* frameSyncCheck_ = nullptr;
    QCheckBox* hwDecodeCheck_ = nullptr;
    QCheckBox* enableVideoCheck_ = nullptr;
    QCheckBox* enableAudioCheck_ = nullptr;
    QCheckBox* alphaCheckerCheck_ = nullptr;
    QSlider* previewAlphaSlider_ = nullptr;
    QLabel* previewAlphaValueLabel_ = nullptr;
    QPushButton* refreshBtn_ = nullptr;
    QPushButton* connectBtn_ = nullptr;
    QPushButton* disconnectBtn_ = nullptr;
    QPushButton* startBtn_ = nullptr;
    QPushButton* stopBtn_ = nullptr;
    QPushButton* alphaPresetBtn_ = nullptr;
    QPushButton* savePngBtn_ = nullptr;
    QLabel* statsLabel_ = nullptr;
    QLabel* sourceStatusLabel_ = nullptr;
    VideoPreviewWidget* preview_ = nullptr;
    QTimer* statsTimer_ = nullptr;
    QTimer* refreshTimer_ = nullptr;

    std::vector<NdiSourceInfo> sources_;
    NDIlib_FourCC_video_type_e lastFourCC_ = NDIlib_FourCC_type_UYVY;
    int lastAlphaMin_ = -1;
    int lastAlphaMax_ = -1;
    int alphaSampleCounter_ = 0;
};
