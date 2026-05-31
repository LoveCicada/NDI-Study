# 软件整体架构与 UML 设计

本文档描述 NDI-Study Demo 的**整体架构**、**运行流程**、**时序交互**与**各模块 UML 类图**。参数映射与像素格式细节见 [04-开发计划与架构设计](04-开发计划与架构设计.md)；Alpha 渲染细节见 [07-NDIReceiver预览渲染与Alpha混合](07-NDIReceiver预览渲染与Alpha混合.md)。

---

## 1. 系统上下文

```mermaid
flowchart TB
    subgraph Host["Windows 主机"]
        SenderApp["NDISender.exe\n(Qt UI)"]
        ReceiverApp["NDIReceiver.exe\n(Qt UI)"]
        WASAPI["WASAPI Loopback\n系统音频"]
        DXGI["DXGI Desktop\nDuplication"]
    end

    subgraph NDI["NDI SDK / LAN"]
        SpeedHQ["SpeedHQ / HX\n压缩与传输"]
    end

    subgraph Tools["联调工具（可选）"]
        StudioMon["NDI Studio Monitor"]
        TestPat["NDI Test Patterns"]
    end

    DXGI --> SenderApp
    WASAPI --> SenderApp
    SenderApp --> SpeedHQ
    SpeedHQ --> ReceiverApp
    SpeedHQ --> StudioMon
    TestPat --> ReceiverApp
```

| 组件 | 技术栈 | 职责 |
|------|--------|------|
| **NDISender** | Qt 5.15、DXGI、WASAPI、MF H.264 | 采集桌面/测试图与系统音频，经 NDI SDK 推流 |
| **NDIReceiver** | Qt 5.15、DX11、SDL2 | 发现/连接 NDI 源，DX11 预览与 SDL2 播放 |
| **ndi_study_common** | C++17 静态库 | NDI 封装、采集、编码、渲染、音频、格式转换 |

---

## 2. 分层架构

```mermaid
flowchart TB
    subgraph Apps["应用层 apps/"]
        SW["NDISender::MainWindow"]
        RW["NDIReceiver::MainWindow"]
    end

    subgraph Common["公共层 common/"]
        subgraph NDI["ndi/"]
            Ctx["NdiContext"]
            Find["NdiFinder"]
            Send["NdiSender"]
            Recv["NdiReceiver"]
        end
        subgraph Cap["capture/"]
            DXGIc["DxgiScreenCapture"]
            Alpha["AlphaTestPattern"]
            VFC["VideoFormatConvert"]
        end
        subgraph Enc["encode/"]
            H264["MfH264Encoder"]
        end
        subgraph Rend["render/"]
            DX11["Dx11VideoRenderer"]
            VPW["VideoPreviewWidget"]
        end
        subgraph Aud["audio/"]
            WAS["WasapiLoopbackCapture"]
            SDL["SdlAudioPlayer"]
        end
    end

    subgraph External["外部依赖"]
        NDIlib["NDI Advanced SDK"]
        Qt["Qt5 Widgets"]
        D3D["Direct3D 11 / DXGI"]
        MF["Media Foundation"]
        SDL2["SDL2"]
    end

    SW --> Send
    SW --> DXGIc
    SW --> Alpha
    SW --> WAS
    SW --> H264
    SW --> Ctx

    RW --> Recv
    RW --> Find
    RW --> VPW
    RW --> SDL
    RW --> Ctx

    Send --> VFC
    Send --> NDIlib
    Recv --> VFC
    Recv --> NDIlib
    Find --> NDIlib
    Ctx --> NDIlib

    VPW --> DX11
    DXGIc --> D3D
    DX11 --> D3D
    H264 --> MF
    SDL --> SDL2
    SW --> Qt
    RW --> Qt
```

---

## 3. 线程模型

| 线程 | 所属 | 职责 |
|------|------|------|
| **Qt UI 主线程** | Sender / Receiver | 控件、定时器、状态栏/统计刷新 |
| **captureThread_** | Sender | `runCaptureLoop`：DXGI 或测试图 → 编码/发送 |
| **WASAPI 采集线程** | Sender | Loopback 采集 → `NdiSender::sendAudio` |
| **recvThread_** | Receiver | `NdiReceiver::recvLoop`：`recv_capture_v3` / Frame Sync |
| **sourceRefreshThread_** | Receiver | 后台 `NdiFinder::refresh`，避免 UI 阻塞 |
| **SDL 音频回调** | Receiver | `SdlAudioPlayer` 从队列取 float 样本播放 |

**跨线程原则**

- Sender：视频双缓冲 ping-pong（`NdiSender::videoSendBuffers_`）；音频在独立采集线程发送
- Receiver：接收线程写 `VideoPreviewWidget::displayFrame_`（mutex）；UI 定时器 ~33ms 触发 DX11 渲染，**不**每帧 `invokeMethod` 投递整帧

---

## 4. 运行流程图

### 4.1 NDISender 推流总览

```mermaid
flowchart TD
    Start([用户点击 开始推流]) --> BuildCfg[buildConfig / updateCaptureControls]
    BuildCfg --> OpenSrc{视频源?}
    OpenSrc -->|屏幕| OpenDXGI[DxgiScreenCapture::open]
    OpenSrc -->|Alpha 测试图| LockHB[锁定 High Bandwidth]
    OpenDXGI --> CreateSend[NdiSender::create]
    LockHB --> CreateSend
    CreateSend --> HX{HX H.264?}
    HX -->|是| OpenEnc[MfH264Encoder::open]
    HX -->|否| AudioQ{启用音频?}
    OpenEnc --> AudioQ
    AudioQ -->|是 且 屏幕| StartWAS[WasapiLoopbackCapture::start\n排除 NDIReceiver 进程]
    AudioQ -->|否| SpawnCap[启动 captureThread_]
    StartWAS --> SpawnCap
    SpawnCap --> Loop{running_?}

    Loop -->|否| Stop([停止])
    Loop -->|是| VidOn{enableVideo?}
    VidOn -->|否| Sleep1[sleep 16ms] --> Loop
    VidOn -->|是| Src{activeVideoSource_?}

    Src -->|Alpha 测试图| FillPat[AlphaTestPattern::fillFrame]
    FillPat --> SendVid[NdiSender::sendVideo\n+ VideoFormatConvert]
    SendVid --> Sleep2[sleep 16ms] --> Loop

    Src -->|屏幕| CapDXGI[capture_->captureFrame]
    CapDXGI -->|失败| Loop
    CapDXGI -->|成功| Mode{HX + encoder?}
    Mode -->|是| EncH264[encoder_->encodeBGRA]
    EncH264 --> CB[回调 sendVideoH264]
    Mode -->|否| SendVid2[sendVideo + colorFormat]
    CB --> Loop
    SendVid2 --> Loop

    Stop --> Join[join captureThread_]
    Join --> StopAud[audioCapture_->stop]
    StopAud --> Flush[NdiSender::flushVideoAsync / destroy]
```

### 4.2 NDIReceiver 接收总览

```mermaid
flowchart TD
    Start([用户点击 开始接收]) --> StopRefresh[暂停 UI 源刷新定时器]
    StopRefresh --> CreateRecv[NdiReceiver::create + connectToSource]
    CreateRecv --> StartRecv[receiver_->start\n注册 video/audio 回调]
    StartRecv --> StatsTimer[statsTimer_ 500ms]
    StatsTimer --> RecvLoop[NdiReceiver::recvLoop 后台线程]

    RecvLoop --> FS{frameSync_?}
    FS -->|是| FSCap[framesync_capture_video/audio]
    FS -->|否| DirectCap[NDIlib_recv_capture_v3]
    FSCap --> ProcV[processVideoFrame]
    DirectCap --> Type{frame type?}
    Type -->|video| ProcV
    Type -->|audio| ProcA[processAudioFrame]
    Type -->|other| RecvLoop

    ProcV --> UYVA{FourCC UYVA?}
    UYVA -->|是| Conv[VideoFormatConvert::convertUyvaFrameToBgra]
    UYVA -->|否| Copy[copyVideoBuffer]
    Conv --> CBV[VideoCallback → onVideoFrame]
    Copy --> CBV
    CBV --> Submit[VideoPreviewWidget::submitFrame]
    Submit --> RecvLoop

    ProcA --> CBA[AudioCallback → onAudioFrame]
    CBA --> SDLQ[SdlAudioPlayer::queue]
    SDLQ --> RecvLoop

    Submit --> UITick[UI renderTimer ~33ms]
    UITick --> Render[Dx11VideoRenderer::renderFrame + present]

    Stop([停止接收]) --> StopRecv[receiver_->stop]
    StopRecv --> CloseSDL[audioPlayer_->close]
```

### 4.3 视频帧数据路径（High Bandwidth）

```mermaid
flowchart LR
    subgraph SenderSide["发送端"]
        BGRA1["BGRA 源\nDXGI / AlphaTestPattern"]
        VFC1["VideoFormatConvert\npackBgraForSend"]
        NS["NdiSender::sendVideo\n双缓冲 async"]
    end

    subgraph Network["NDI"]
        SQ["SpeedHQ"]
    end

    subgraph ReceiverSide["接收端"]
        NR["NdiReceiver::recvLoop"]
        VFC2["UYVA → BGRA\n(可选)"]
        VPW["VideoPreviewWidget"]
        DX11["Dx11VideoRenderer\nUYVY / BGRA shader"]
    end

    BGRA1 --> VFC1 --> NS --> SQ --> NR
    NR --> VFC2 --> VPW --> DX11
```

---

## 5. 时序图

### 5.1 NDISender：High Bandwidth 屏幕采集推流

```mermaid
sequenceDiagram
    autonumber
    actor User
    participant UI as MainWindow (UI)
    participant Cap as captureThread_
    participant DXGI as DxgiScreenCapture
    participant VFC as VideoFormatConvert
    participant Send as NdiSender
    participant WAS as WasapiLoopbackCapture
    participant NDI as NDI SDK

    User->>UI: 开始推流
    UI->>DXGI: open(outputIndex)
    UI->>Send: create(config)
    UI->>WAS: start(callback) [可选]
    UI->>Cap: std::thread(runCaptureLoop)

    loop 每帧 (~16ms)
        Cap->>DXGI: captureFrame(frame)
        DXGI-->>Cap: CapturedFrame BGRA
        Cap->>Send: sendVideo(bgra, colorFormat)
        Send->>VFC: packBgraForSend
        VFC-->>Send: planar / UYVY / UYVA
        Send->>NDI: send_video_async_v2
    end

    par 音频 [可选]
        loop WASAPI 包
            WAS->>Send: sendAudio(planar FLTP)
            Send->>NDI: send_audio_v3
        end
    end

    User->>UI: 停止推流
    UI->>Cap: running_=false, join
    UI->>WAS: stop
    UI->>Send: flushVideoAsync, destroy
```

### 5.2 NDISender：HX H.264 推流

```mermaid
sequenceDiagram
    autonumber
    participant Cap as captureThread_
    participant DXGI as DxgiScreenCapture
    participant Enc as MfH264Encoder
    participant Send as NdiSender
    participant NDI as NDI SDK

    Cap->>DXGI: captureFrame(BGRA)
    Cap->>Enc: encodeBGRA(data, stride)
    Enc->>Enc: MF H.264 编码
    Enc-->>Cap: FrameCallback(EncodedH264Frame)
    Cap->>Send: sendVideoH264(annexB, timecode)
    Send->>NDI: send_video_async_v2\nFourCC=H264
```

### 5.3 NDIReceiver：拉流与预览

```mermaid
sequenceDiagram
    autonumber
    actor User
    participant UI as MainWindow (UI)
    participant Recv as NdiReceiver
    participant NDI as NDI SDK
    participant VFC as VideoFormatConvert
    participant VPW as VideoPreviewWidget
    participant DX11 as Dx11VideoRenderer
    participant SDL as SdlAudioPlayer

    User->>UI: 开始接收
    UI->>Recv: create(config), connectToSource
    UI->>Recv: start(onVideo, onAudio)

    loop recvThread_
        Recv->>NDI: recv_capture_v3 / framesync
        NDI-->>Recv: video / audio frame
        alt video
            Recv->>VFC: convertUyvaFrameToBgra [若 UYVA]
            Recv->>UI: onVideoFrame(NdiVideoFrameData)
            UI->>VPW: submitFrame (mutex 写入)
        else audio
            Recv->>UI: onAudioFrame
            UI->>SDL: ensureOpen + queue
        end
    end

    loop UI renderTimer ~33ms
        VPW->>DX11: renderFrame + present
    end

    loop statsTimer 500ms
        UI->>Recv: stats()
        UI->>UI: updateStats (FourCC, Alpha)
    end
```

### 5.4 Alpha 透明度联调（推荐路径）

```mermaid
sequenceDiagram
    autonumber
    participant Pat as AlphaTestPattern
    participant Send as NdiSender
    participant NDI as NDI LAN
    participant Recv as NdiReceiver
    participant DX11 as Dx11VideoRenderer

    Note over Pat,Send: Sender: UYVA 发送格式
    Pat->>Send: BGRA 测试图
    Send->>Send: packBgraForSend(UYVA)
    Send->>NDI: FourCC=UYVA

    Note over Recv,DX11: Receiver: Fastest + 关 HW 解码 + 棋盘格
    NDI->>Recv: UYVA 或 SDK 转换后 UYVA
    Recv->>Recv: convertUyvaFrameToBgra
    Recv->>DX11: BGRA + alphaChecker + blend
```

---

## 6. 模块 UML 类图

### 6.1 ndi 模块

```mermaid
classDiagram
    class NdiContext {
        -bool valid_
        +NdiContext()
        +~NdiContext()
        +isValid() bool
        +version() const char*
    }

    class NdiFinder {
        -NDIlib_find_instance_t finder_
        -uint32_t defaultWaitMs_
        -string groups_
        -vector~NdiSourceInfo~ sources_
        +refresh(waitMs) vector~NdiSourceInfo~
        +sources() vector
        +setGroups(groups)
    }

    class NdiSourceInfo {
        +string name
        +string urlAddress
        +NDIlib_source_t source
    }

    class NdiSender {
        -NDIlib_send_instance_t sender_
        -NdiSenderConfig config_
        -vector~uint8_t~ audioBuffer_
        -vector~uint8_t~ videoSendBuffers_[2]
        +create(config) bool
        +destroy()
        +sendVideo(bgra, format) bool
        +sendVideoH264(...) bool
        +sendAudio(interleaved, ...) bool
        +flushVideoAsync()
    }

    class NdiSenderConfig {
        +string ndiName
        +NdiSendMode mode
        +bool enableVideo
        +bool enableAudio
        +NdiSendColorFormatChoice colorFormat
    }

    class NdiReceiver {
        -NDIlib_recv_instance_t receiver_
        -NDIlib_framesync_instance_t frameSync_
        -NdiReceiverConfig config_
        -thread recvThread_
        -atomic~bool~ running_
        +create(config) bool
        +connectToSource(source) bool
        +start(videoCb, audioCb) bool
        +stop()
        +stats() NdiReceiverStats
    }

    class NdiReceiverConfig {
        +NdiRecvColorFormatChoice colorFormat
        +bool useFrameSync
        +bool enableHardwareDecode
        +bool enableVideo
        +bool enableAudio
    }

    class NdiVideoFrameData {
        +vector~uint8_t~ buffer
        +int width
        +int fourCC
    }

    class NdiAudioFrameData {
        +vector~float~ samples
        +int sampleRate
        +int channels
    }

    NdiFinder --> NdiSourceInfo
    NdiSender --> NdiSenderConfig
    NdiReceiver --> NdiReceiverConfig
    NdiReceiver ..> NdiVideoFrameData : callback
    NdiReceiver ..> NdiAudioFrameData : callback
```

### 6.2 capture 模块

```mermaid
classDiagram
    class DxgiScreenCapture {
        -ComPtr~ID3D11Device~ device_
        -ComPtr~IDXGIOutputDuplication~ duplication_
        -ComPtr~ID3D11Texture2D~ staging_
        +listOutputs() vector~DxgiOutputInfo~
        +open(outputIndex) bool
        +captureFrame(out, timeoutMs) bool
        +close()
    }

    class CapturedFrame {
        +vector~uint8_t~ bgra
        +int width
        +int stride
    }

    class DxgiOutputInfo {
        +int index
        +string name
        +int width
        +int height
    }

    class AlphaTestPattern {
        -int width_
        -int height_
        -float alphaScale_
        +setSize(w, h)
        +setAlphaScale(scale)
        +fillFrame(bgra, frameIndex)
    }

    class NdiSendColorFormatChoice {
        <<enumeration>>
        BGRA
        BGRX
        UYVY
        UYVA
    }

    class VideoFormatConvert {
        <<namespace>>
        +packBgraForSend() bool
        +convertUyvaFrameToBgra() bool
        +sendColorFormatToFourCC()
        +uyvyToRgb()
        +rgbToUyvy()
    }

    DxgiScreenCapture ..> CapturedFrame : produces
    DxgiScreenCapture ..> DxgiOutputInfo : listOutputs
    VideoFormatConvert ..> NdiSendColorFormatChoice : uses
```

### 6.3 encode 模块

```mermaid
classDiagram
    class MfH264Encoder {
        -unique_ptr~Impl~ impl_
        -bool open_
        -FrameCallback callback_
        +open(w, h, fpsN, fpsD, bitrate) bool
        +encodeBGRA(data, stride, ts) bool
        +setCallback(cb)
        +close()
    }

    class EncodedH264Frame {
        +vector~uint8_t~ data
        +int64_t timestamp100ns
        +bool keyFrame
    }

    MfH264Encoder ..> EncodedH264Frame : callback
```

### 6.4 render 模块

```mermaid
classDiagram
    class VideoPreviewWidget {
        -unique_ptr~Dx11VideoRenderer~ renderer_
        -QTimer* renderTimer_
        -mutex frameMutex_
        -vector~uint8_t~ displayFrame_
        -atomic~bool~ frameUpdated_
        +submitFrame(data, w, h, fourCC)
        +setAlphaCheckerBackground(enabled)
        +setPreviewAlphaScale(scale)
        +saveCurrentFramePng(path) bool
    }

    class Dx11VideoRenderer {
        -ComPtr~ID3D11Device~ device_
        -ComPtr~IDXGISwapChain~ swapChain_
        -ComPtr~ID3D11PixelShader~ psBgra_
        -ComPtr~ID3D11PixelShader~ psUyvy_
        -ComPtr~ID3D11BlendState~ alphaBlendState_
        +initialize(hwnd, w, h) bool
        +renderFrame(data, fourCC)
        +drawCheckerBackground()
        +present()
    }

    VideoPreviewWidget *-- Dx11VideoRenderer : owns
    VideoPreviewWidget --|> QWidget
```

### 6.5 audio 模块

```mermaid
classDiagram
    class WasapiLoopbackCapture {
        -unique_ptr~Impl~ impl_
        -AudioCallback callback_
        -thread thread_
        -atomic~bool~ running_
        +findProcessIdsByExeName(name) vector
        +setExcludeProcessNames(names)
        +start(callback) bool
        +stop()
        +usedProcessExclude() bool
    }

    class SdlAudioPlayer {
        -bool open_
        -int sampleRate_
        -mutex mutex_
        -vector~float~ pending_
        +open(sampleRate, channels) bool
        +ensureOpen(sampleRate, channels) bool
        +queue(interleaved, frames)
        +clearPending()
        +close()
    }
```

### 6.6 应用层 MainWindow

```mermaid
classDiagram
    class SenderMainWindow {
        -NdiContext& ndiContext_
        -unique_ptr~NdiSender~ sender_
        -unique_ptr~DxgiScreenCapture~ capture_
        -unique_ptr~WasapiLoopbackCapture~ audioCapture_
        -unique_ptr~MfH264Encoder~ encoder_
        -AlphaTestPattern alphaPattern_
        -thread captureThread_
        -atomic~bool~ running_
        +onStart()
        +onStop()
        -runCaptureLoop()
        -buildConfig() NdiSenderConfig
    }

    class ReceiverMainWindow {
        -NdiContext& ndiContext_
        -unique_ptr~NdiFinder~ finder_
        -unique_ptr~NdiReceiver~ receiver_
        -unique_ptr~SdlAudioPlayer~ audioPlayer_
        -VideoPreviewWidget* preview_
        -thread sourceRefreshThread_
        +onStartReceive()
        +onStopReceive()
        -onVideoFrame(frame)
        -onAudioFrame(frame)
    }

    SenderMainWindow --|> QMainWindow
    ReceiverMainWindow --|> QMainWindow
    SenderMainWindow --> NdiSender
    SenderMainWindow --> DxgiScreenCapture
    SenderMainWindow --> WasapiLoopbackCapture
    SenderMainWindow --> MfH264Encoder
    SenderMainWindow --> AlphaTestPattern
    ReceiverMainWindow --> NdiReceiver
    ReceiverMainWindow --> NdiFinder
    ReceiverMainWindow --> SdlAudioPlayer
    ReceiverMainWindow --> VideoPreviewWidget
    SenderMainWindow --> NdiContext
    ReceiverMainWindow --> NdiContext
```

---

## 7. CMake 与依赖关系

```mermaid
flowchart LR
    subgraph Apps
        NDISender
        NDIReceiver
    end

    subgraph Lib["ndi_study_common (STATIC)"]
        ndi
        capture
        encode
        render
        audio
    end

    NDISender --> Lib
    NDIReceiver --> Lib
    Lib --> NDIlib
    Lib --> Qt5Core
    Lib --> Qt5Widgets
    NDIReceiver --> Qt5Widgets
    NDISender --> Qt5Widgets
```

| 目标 | 链接库 | POST_BUILD 部署 |
|------|--------|-----------------|
| `NDISender` | `ndi_study_common`, Qt | NDI DLL、SDL2 DLL |
| `NDIReceiver` | `ndi_study_common`, Qt | NDI DLL、SDL2 DLL |
| `ndi_study_common` | NDI, Qt, d3d11, dxgi, mfplat, mmdevapi, SDL2(可选) | — |

---

## 8. 文档与代码对照

| 主题 | 文档 |
|------|------|
| 参数映射 / FourCC | [04-开发计划与架构设计](04-开发计划与架构设计.md) |
| Alpha 渲染 / DX11 混合 | [07-NDIReceiver预览渲染与Alpha混合](07-NDIReceiver预览渲染与Alpha混合.md) |
| 联调步骤 | [05-联调验证指南](05-联调验证指南.md) |
| 功能清单 | [06-功能清单与变更记录](06-功能清单与变更记录.md) |
| SDK 放置 / 打包脚本 | [third_party/README.md](../third_party/README.md) |

---

## 9. 修订记录

| 日期 | 说明 |
|------|------|
| 2026-05-30 | 初版：整体架构、运行流程、时序图、分模块 UML 类图 |
