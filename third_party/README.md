# Third-Party SDK 放置说明

本目录 SDK 已纳入 Git 仓库，克隆后可直接构建；若需自行更新版本，请替换对应子目录并保持 CMake 期望的目录结构。

## NDI 6 Advanced SDK

```
third_party/NDI 6 Advanced SDK/
├── Include/Processing.NDI.Lib.h
├── Lib/x64/Processing.NDI.Lib.Advanced.x64.lib
└── Bin/x64/Processing.NDI.Lib.Advanced.x64.dll
```

申请地址：https://ndi.video/for-developers/ndi-advanced/software/

也可使用免费 NDI SDK，目录命名为 `NDI_SDK/`，CMake 会自动搜索。

## SDL2（可选，音频播放）

```
third_party/SDL2/
├── include/SDL.h
├── lib/x64/SDL2.lib
└── bin/x64/SDL2.dll
```

下载：https://github.com/libsdl-org/SDL/releases

## 配置 CMake

```powershell
cmake -B build -S . -DNDI_SDK_DIR="E:/code/private/NDI/NDI-Study/third_party/NDI_Advanced_SDK"
```

## 发布打包：运行时依赖收集

构建完成后，`build/bin/Release/`（或 `Debug/`）目录中已包含 **NDI**、**SDL2** 等 DLL（CMake `ndi_study_deploy_runtime` 自动复制）。  
**Qt** 与 **Visual C++ 运行时** 需额外收集，可使用项目根目录脚本（用法类似 `windeployqt`）。

### Qt 运行时（windeployqt）

在 Qt 安装目录的 `bin` 下执行，或将 `windeployqt.exe` 加入 PATH：

```bat
cd build\bin\Release
windeployqt --dir . NDIReceiver.exe
windeployqt --dir . NDISender.exe
```

### VC++ / UCRT 运行时（ReleaseVcDeploy / DebugVcDeploy）

脚本位于**项目根目录**，通过 `dumpbin` 分析 exe 导入表，从 VS Redist / Windows SDK 复制对应 DLL 到 `--dir` 指定目录。

| 脚本 | 适用构建 | 典型 DLL |
|------|----------|----------|
| `ReleaseVcDeploy.bat` | Release（`/MD`） | `msvcp140.dll`、`vcruntime140.dll`、`vcruntime140_1.dll`、`ucrtbase.dll` |
| `DebugVcDeploy.bat` | Debug（`/MDd`） | `msvcp140d.dll`、`vcruntime140d.dll`、`ucrtbased.dll` 等 |

**Release 发布包示例**（与 windeployqt 配合）：

```bat
cd build\bin\Release

windeployqt --dir . NDIReceiver.exe
windeployqt --dir . NDISender.exe

..\..\..\ReleaseVcDeploy.bat --dir . NDIReceiver.exe NDISender.exe
```

**Debug 本地运行示例**：

```bat
cd build\bin\Debug

windeployqt --dir . NDIReceiver.exe
windeployqt --dir . NDISender.exe

..\..\..\DebugVcDeploy.bat --dir . NDIReceiver.exe NDISender.exe
```

也可指定独立输出目录（与 `windeployqt --dir NDI` 相同语义）：

```bat
ReleaseVcDeploy.bat --dir NDI .\build\bin\Release\NDIReceiver.exe .\build\bin\Release\NDISender.exe
DebugVcDeploy.bat --dir NDI .\build\bin\Debug\NDIReceiver.exe
```

**常用选项**：

| 选项 | 说明 |
|------|------|
| `--dir <path>` | 复制目标目录（不存在则创建） |
| `--no-ucrt` | 不复制 UCRT（`ucrtbase.dll` / `ucrtbased.dll`） |
| `--no-system` | 禁止从 `System32` 回退复制 |
| `--help` | 显示帮助 |

**说明**：

- Release 与 Debug 脚本不可混用；构建配置与脚本不一致时会提示无匹配依赖。
- Debug 运行时为**不可再分发**组件，仅用于本机/开发环境；对外发布请使用 **Release** 构建 + `ReleaseVcDeploy.bat`。
- 需要已安装 Visual Studio（提供 `dumpbin`）和/或 Windows SDK，以便从官方 redist 目录取 DLL。
