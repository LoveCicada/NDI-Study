# Third-Party SDK 放置说明

将下载的 SDK 解压到本目录，**不要提交到 Git**。

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
