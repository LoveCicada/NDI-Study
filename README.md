# NDI-Study

NDI SDK 学习项目：基于 **NDI Advanced SDK 6.x**、**Qt 5.15.2**、**CMake** 实现的推拉流 Demo。

## 工具

| 程序 | 功能 |
|------|------|
| **NDISender** | DXGI 屏幕采集 → NDI 推流（High Bandwidth / HX H.264） |
| **NDIReceiver** | NDI 拉流 → DX11 预览 + SDL2 音频 |

## 前置依赖

请参阅 [docs/02-开发环境搭建与下载清单.md](docs/02-开发环境搭建与下载清单.md)。

必需：

- NDI SDK（Advanced 或免费 SDK）→ 设置 `NDI_SDK_DIR`
- Qt 5.15.2 MSVC 2019 64-bit → `CMAKE_PREFIX_PATH`
- Visual Studio 2019/2022 + Windows SDK
- SDL2（可选，默认尝试 `third_party/SDL2`）

## 构建

### 方式一：双击 bat 脚本（推荐）

1. 复制 `build_env.bat.example` 为 `build_env.bat`，设置 `QT_DIR`
2. 双击 **`Debug.bat`** 或 **`Release.bat`**

输出：`build/bin/Debug/` 或 `build/bin/Release/`

### 方式二：命令行

```powershell
cmake -B build -S . `
  -DNDI_SDK_DIR="E:/code/private/NDI/NDI-Study/third_party/NDI 6 Advanced SDK" `
  -DSDL2_DIR="E:/code/private/NDI/NDI-Study/third_party/SDL2" `
  -DCMAKE_PREFIX_PATH="C:/Qt/5.15.2/msvc2019_64"

cmake --build build --config Release
```

## 运行

1. 确保 NDI / SDL2 运行时 DLL 与 exe 同目录（CMake 会自动复制）
2. 安装 [NDI Tools](https://ndi.video/tools/) 用于联调
3. **Receiver**：选择 NDI 源 → 开始接收
4. **Sender**：配置源名与模式 → 开始推流 → 在 Studio Monitor 中查看

## 联调流程

详见 [docs/03-NDI官方工具使用指南.md](docs/03-NDI官方工具使用指南.md) 第 13 节。

```
Test Patterns → NDIReceiver     （验证拉流）
NDISender → Studio Monitor        （验证推流）
Screen Capture HX ↔ NDISender HX  （对比 HX）
```

完整步骤见 [docs/05-联调验证指南.md](docs/05-联调验证指南.md)。

## 文档

- [文档索引](docs/README.md)
- [NDI 5/6 差异](docs/01-NDI5与NDI6差异与兼容性.md)
- [环境搭建](docs/02-开发环境搭建与下载清单.md)
- [官方工具指南](docs/03-NDI官方工具使用指南.md)
- [架构设计](docs/04-开发计划与架构设计.md)
