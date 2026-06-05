# NDI 发送端 GPU 与硬件加速调研

本文档记录 NDI 6 Advanced SDK 发送端对 GPU 纹理与硬件编码的支持情况，以及 NDISender 的优化方向。架构细节见 [08-软件整体架构与UML设计](08-软件整体架构与UML设计.md)。

---

## 1. 结论摘要

| 问题 | 结论 |
|------|------|
| NDI SDK 是否支持直接把 GPU 纹理交给 SDK 编码并推流？ | **不支持** |
| 发送 API 的数据形态 | 仅 `NDIlib_video_frame_v2_t.p_data`（CPU 指针）或已压缩码流 |
| SpeedHQ（High Bandwidth）编码由谁完成 | **NDI SDK 内部**（应用须先提供 CPU 未压缩帧） |
| HX（H.264/H.265）编码由谁完成 | **应用侧**（NVENC / MF 硬件 MFT / QSV 等），SDK 只传输码流 |
| 发送端是否有 GPU 纹理 / D3D 接口 | **无**（接收端有 GPU 解码示例与自定义 allocator） |

官方 [Decoding with NDI](https://docs.ndi.video/all/developing-with-ndi/developer-guides/decoding-with-ndi)：**HX 编码必须由应用完成**；SpeedHQ 的 FPGA 硬件编解码属于 Advanced SDK 专用方案，非 Windows 桌面 D3D 直通 API。

---

## 2. SDK 发送 API 证据

[`NDIlib_video_frame_v2_t`](../../third_party/NDI%206%20Advanced%20SDK/Include/Processing.NDI.structs.h) 仅含 `uint8_t* p_data`，无 D3D/Vulkan/CUDA 句柄字段。

[`Processing.NDI.Send.h`](../../third_party/NDI%206%20Advanced%20SDK/Include/Processing.NDI.Send.h) 提供的接口：

- `NDIlib_send_send_video_v2` / `NDIlib_send_send_video_async_v2` — 未压缩或压缩帧
- 注释说明 async 可在 SDK 内部线程做色彩转换与压缩，**不能替代**应用侧的 GPU→CPU 回读

[`Processing.NDI.Advanced.h`](../../third_party/NDI%206%20Advanced%20SDK/Include/Processing.NDI.Advanced.h)：

- `NDIlib_send_send_video_scatter_async` — 仅用于**已压缩** H.264 等 scatter 发送
- `NDIlib_recv_set_video_allocator` — **仅接收端**自定义分配器，发送端无等价 API

SDK 示例：

- 有 [`NDIlib_Recv_GPUDecode`](../third_party/NDI%206%20Advanced%20SDK/Examples/C++/NDIlib_Recv_GPUDecode/)（MFT + D3D11 硬件**解码**）
- **无** `NDIlib_Send_GPUEncode` 或 D3D 纹理发送示例

---

## 3. 两种发送模式与硬件加速

| 模式 | 编码责任 | SDK 是否接受 GPU 纹理 | 硬件加速落点 |
|------|----------|----------------------|-------------|
| High Bandwidth | NDI SDK（SpeedHQ） | 否 | SDK 内部（不透明）；应用须 GPU→CPU |
| HX H.264/HEVC | 应用 | 否 | 应用侧 MF HW MFT / NVENC 等 |
| 预压缩 SHQ FourCC | 通常仍由 SDK 从未压缩帧生成 | 否 | FPGA 等专用方案 |

---

## 4. 本项目实现（HX GPU 管线）

High Bandwidth 仍使用 [`DxgiScreenCapture::captureFrame`](../../common/capture/DxgiScreenCapture.cpp)（staging + Map，不可避免 GPU→CPU）。

HX 屏幕采集使用 GPU 路径（无 BGRA 全帧回读）：

```
DXGI DuplicateOutput → GPU BGRA 纹理
  → GpuBgraToNv12（D3D11 Video Processor）
  → MfH264Encoder::encodeNv12Texture（MF + IMFDXGIBuffer）
  → H.264 码流（CPU）
  → NdiSender::sendVideoH264
```

相关代码：

- [`DxgiScreenCapture::captureGpuFrame`](../../common/capture/DxgiScreenCapture.cpp)
- [`GpuBgraToNv12`](../../common/encode/GpuBgraToNv12.cpp)
- [`MfH264Encoder`](../../common/encode/MfH264Encoder.cpp) — `open(..., ID3D11Device*)` 与 `encodeGpuBgraTexture`

若 GPU 编码器初始化失败，自动回退 CPU `encodeBGRA` 路径。

---

## 5. 参考

- [NDI-SEND](https://docs.ndi.video/all/developing-with-ndi/sdk/ndi-send)
- [Sending Video Frames (H.264)](https://docs.ndi.video/all/developing-with-ndi/advanced-sdk/using-h.264-h.265-and-aac-codecs/sending-video-frames)
- [Decoding with NDI](https://docs.ndi.video/all/developing-with-ndi/developer-guides/decoding-with-ndi)
- NVIDIA [nvEncDXGIOutputDuplicationSample](https://github.com/NVIDIA/video-sdk-samples/blob/master/nvEncDXGIOutputDuplicationSample/main.cpp)

## 6. 延伸阅读

- [11-Media-Foundation框架介绍与使用](11-Media-Foundation框架介绍与使用.md) — MF 框架原理、硬件加速机制、驱动检测、与 FFmpeg/DXVA2 对比及选型分析
- [10-COM套间与HX编码线程问题](10-COM套间与HX编码线程问题.md) — `MfH264Encoder` COM 套间冲突与修复
