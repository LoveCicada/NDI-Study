# NDI 官方工具使用指南

本文档基于 [NDI Tools 官方文档](https://docs.ndi.video/all/using-ndi/ndi-tools) 整理，适用于 Windows 平台 NDI 6.3.x。

## 1. 安装与启动

### 1.1 下载安装

1. 打开 [https://ndi.video/tools/](https://ndi.video/tools/)
2. 点击 **Download NDI Tools**，选择 Windows 版本（当前 6.3.2）
3. 运行安装程序，按向导完成安装
4. 默认安装路径包含 **NDI Tools Launcher**

### 1.2 系统要求

- Windows 10 64-bit 或更高
- Intel/AMD CPU，SSE4 或更高
- NVIDIA / AMD / Intel GPU
- 6 GB 内存或更高
- 1 Gbps 局域网

### 1.3 启动方式

- 从开始菜单运行 **NDI Tools Launcher**
- 或在安装目录中直接启动各子工具

---

## 2. NDI Tools Launcher

Launcher 提供所有 NDI 工具的入口，包括：

- Access Manager
- Studio Monitor
- Screen Capture / Screen Capture HX
- Test Patterns
- Webcam Input
- Bridge、Router、Remote
- Discovery（NDI 6.3+）

---

## 3. Access Manager（访问管理器）

### 用途

控制 NDI 源在网络中的**可见性**与**发现方式**，管理收发组、网卡、Discovery Server。

### 启动

安装 NDI Tools 后，从 Launcher 或开始菜单启动 **NDI Access Manager**。

### Groups（组）标签页

1. 点击 **+** 创建 NDI 组
2. 配置 **Send Groups**（发送组）与 **Receive Groups**（接收组）
3. 只有 Receive Groups 中包含对应组名的设备才能看到该组的 NDI 源

**示例**：创建组 `Public`，本机 Send/Receive 均包含 `Public`，则本机可与同组设备互通。

### External Sources（外部源）标签页

- 添加不在当前子网的 NDI 设备 IP
- 为外部源设置易识别的名称

### Advanced（高级）标签页

| 设置项 | 说明 |
|--------|------|
| Discovery Server | 指定 NDI 发现服务器 IP（大规模网络） |
| Preferred NIC | 选择首选网卡 |
| Transport Mode | TCP / UDP / 组播范围 |
| Device Aliases | 设备别名 |

### 配置文件位置

```
%ProgramData%\NDI\ndi-config.v1.json
```

所有 NDI 应用启动时会读取此文件。**修改配置后需重启应用**才能生效。

> 配置网络前请咨询 IT 部门，确保符合网络策略。

---

## 4. Studio Monitor（演播室监视器）

### 用途

实时查看、录制 NDI 音视频流；支持 PTZ 控制、多画面布局、Web 远程控制、KVM。

### 基本操作

1. 启动 Studio Monitor
2. **右键**窗口空白处，或点击左上角菜单
3. 从列表中选择 NDI 源

### 常用设置（右键 → Settings）

| 设置 | 说明 |
|------|------|
| Run a new NDI Studio Monitor | 打开第二个实例 |
| Run at Windows Start | 开机自启（数字标牌） |
| Allow Web Control | 启用 Web 远程控制 |
| Show Web Control URL | 显示 Web 控制地址 |
| Set Record Path | 录制文件保存路径 |
| Allow Receiver Advertising | Discovery Server 环境下广播接收器 |
| Allow Receiver Control | 允许远程控制连接源 |
| Full Screen | 全屏显示 |
| Scale Video | 缩放视频 |
| Always on Top | 窗口置顶 |
| PTZ Settings | 启用 PTZ 控制 |
| Streaming Latency | 流延时设置 |

### Web 控制

启动后窗口显示 QR 码与 IP 地址，手机/电脑浏览器可远程切换源、配置多画面。

### 与 Demo 联调

- 运行 **NDISender** 后，在 Studio Monitor 中选择对应源名验证推流
- 对比 Demo 与官方 Screen Capture 的延时与画质

---

## 5. Screen Capture（屏幕采集）

### 用途

将桌面视频和音频以 NDI 流形式分享到局域网。

### 基本操作

1. 启动后图标出现在**系统托盘**（NDI  logo）
2. 启动即开始推流，局域网内 NDI 设备可见
3. 右键托盘图标访问设置

### 设置项

| 设置 | 说明 |
|------|------|
| Capture Settings | 全屏 / 指定区域（ROI）/ 鼠标指针显示 |
| Webcam | 使用摄像头创建 NDI 流 |
| KVM Control | 远程键鼠控制 |
| Frame Rate | 帧率限制 |

### 查看输出

在另一台设备或本机 Studio Monitor 中，右键选择本机名 + Screen Capture 流。

---

## 6. Screen Capture HX

### 用途

低带宽版本的屏幕采集，使用 H.264/H.265 压缩（类似 NDI\|HX）。

### 使用场景

- 带宽受限网络
- 与 Demo **HX 模式**对比编码质量与延时

---

## 7. Test Patterns（测试图）

### 用途

生成标准测试图 NDI 源，无需摄像头或屏幕采集。

### 使用场景

- 联调 **NDIReceiver** 时作为稳定测试源
- 验证色彩、分辨率、帧率

### 操作

1. 启动 Test Patterns
2. 选择图案类型与参数
3. 在 NDIReceiver 或 Studio Monitor 中选择对应源

---

## 8. Webcam Input（摄像头输入）

### 用途

将 USB 摄像头转为 NDI 源。

### 使用场景

- 测试音视频分离（仅视频 / 仅音频）
- 与屏幕采集源对比

---

## 9. Bridge（桥接）

### 用途

跨网段、WAN 远程 NDI 连接。NDI 6 增强了 Bridge Utility，支持加密与 headless Windows 服务。

### 使用场景

- 远程制作：异地设备加入本地 NDI 网络
- 云制作工作流

---

## 10. Router（路由）

### 用途

视频/音频信号路由与切换，简化多路 NDI 调度。

---

## 11. Remote（远程贡献）

### 用途

连接多个远程贡献者，分布式采集场景。

---

## 12. Discovery（发现工具，NDI 6.3+）

### 用途

**Sender Monitoring**：实时可视化、监控 NDI 发送端流信息（编解码器、分辨率、alpha 等）。

### 与 Demo 联调

对比 Discovery 显示的流参数与 NDISender 配置是否一致。

---

## 13. 与 Demo 联调流程

```
1. Access Manager → 确认网卡与 Demo 一致
2. Test Patterns → NDIReceiver → 验证拉流与 DX11 渲染
3. NDISender → Studio Monitor → 验证推流
4. Screen Capture HX ↔ NDISender HX 模式 → 对比延时
5. Discovery → 核对 codec / 分辨率 / 延时信息
```

## 参考链接

- [NDI Tools 文档首页](https://docs.ndi.video/all/using-ndi/ndi-tools)
- [安装指南](https://docs.ndi.video/all/using-ndi/ndi-tools/installing-ndi-tools)
- [Windows 工具列表](https://docs.ndi.video/all/using-ndi/ndi-tools/ndi-tools-for-windows)
- [Access Manager](https://docs.ndi.video/all/using-ndi/ndi-tools/ndi-tools-for-windows/access-manager)
- [Studio Monitor](https://docs.ndi.video/all/using-ndi/ndi-tools/ndi-tools-for-windows/studio-monitor)
- [Screen Capture](https://docs.ndi.video/all/using-ndi/ndi-tools/ndi-tools-for-windows/screen-capture)
