# NDI 5 与 NDI 6 差异与兼容性

## 兼容性结论

**NDI 协议向后兼容**：NDI 6 与 NDI 5 设备可在同一局域网内互通。NDI 5 发送端可被 NDI 6 接收端发现与连接，反之亦然。官方 Best Practices 文档明确说明 *"NDI is backwards compatible"*。

### 边界场景

| 场景 | 兼容性说明 |
|------|-----------|
| 基础 High Bandwidth 推拉流 | 完全互通 |
| NDI\|HX2 / HX3 | HX3 需 NDI 5.5+ 运行时；HX2/HX3 互通正常 |
| Discovery Server 冗余 | 需 NDI 5+ 客户端 |
| NDI 6 新 API（Sender/Receiver 监控与控制） | 仅 NDI 6 SDK 可用，不影响与 NDI 5 基础互通 |
| HDR 10+ bit | NDI 6 新增；**免费 SDK 仅可接收 HDR**，**发送 HDR 需 Advanced SDK** |
| Advanced SDK License ID | NDI 6 起 Vendor ID 改为 License ID，需向 NDI 申请更新 |

## NDI 5 典型能力

- **High Bandwidth（SpeedHQ）**：I 帧为主的高带宽低延时编解码
- **NDI\|HX2**：H.264/H.265 压缩，低带宽
- **Discovery Server**：大规模网络源发现
- **Genlock / AV Sync**（Advanced SDK）：多源时钟同步
- 单 TCP / 组播传输优化

## NDI 6 新增与增强

来源：[Release Notes](https://docs.ndi.video/all/developing-with-ndi/sdk/release-notes)、[NDI 6 技术页](https://ndi.video/tech/ndi6/)

- **原生 HDR 支持**：PQ/HLG，P216/PA16 等 16-bit 格式
- **HX3**：低延时 Long-GOP 变体（mostly I-frame / IP，无 B-frame）
- **Sender/Receiver 监控 API**：配合 Discovery Server 的发现、监控与控制
- **16-bit 色彩格式改进**、SpeedHQ pass-through
- **NDI Bridge Utility for Hardware**：Linux 设备 WAN 接入
- **单 TCP scatter-gather** 发送性能优化

## 版本能力对比

| 能力 | NDI 5 | NDI 6 |
|------|-------|-------|
| High Bandwidth 8-bit | 支持 | 支持 |
| High Bandwidth HDR 10+ bit | 有限 | 完整支持（接收：免费 SDK；发送：Advanced） |
| HX2 | 支持 | 支持 |
| HX3 | 5.5+ 支持 | 增强低延时 |
| Discovery Server | 支持 | 增强监控 API |
| Bridge Utility (Hardware) | 需 Bridge 工具 | 嵌入式 API（Linux 设备） |

## 本项目选型

- **SDK**：NDI Advanced SDK **6.3.x**
- **运行时参照**：NDI Tools **6.3.x**
- **互通测试**：可与网内 NDI 5 设备互测验证

## 参考链接

- [NDI SDK Release Notes](https://docs.ndi.video/all/developing-with-ndi/sdk/release-notes)
- [NDI 6 技术介绍](https://ndi.video/tech/ndi6/)
- [NDI SDK 下载](https://ndi.video/for-developers/ndi-sdk/)
- [NDI Advanced SDK 申请](https://ndi.video/for-developers/ndi-advanced/software/)
