# NDI SDK 学习文档索引

本目录包含 NDI SDK 学习与小工具开发的全部中文文档。

## 文档列表

| 文档 | 说明 |
|------|------|
| [01-NDI5与NDI6差异与兼容性.md](01-NDI5与NDI6差异与兼容性.md) | NDI 5/6 版本差异、向后兼容性与选型建议 |
| [02-开发环境搭建与下载清单.md](02-开发环境搭建与下载清单.md) | 开发环境依赖、下载链接、环境变量配置 |
| [03-NDI官方工具使用指南.md](03-NDI官方工具使用指南.md) | NDI Tools 各组件安装与使用说明 |
| [04-开发计划与架构设计.md](04-开发计划与架构设计.md) | Demo 架构、API 参考、分阶段实施与联调流程 |
| [05-联调验证指南.md](05-联调验证指南.md) | 与 NDI Tools 联调步骤与验收清单 |
| [06-功能清单与变更记录.md](06-功能清单与变更记录.md) | Demo 功能清单、BUG 修复与版本变更记录 |
| [07-NDIReceiver预览渲染与Alpha混合.md](07-NDIReceiver预览渲染与Alpha混合.md) | 棋盘格背景、DX11 Alpha 混合、RenderDoc 对照 |
| [08-软件整体架构与UML设计.md](08-软件整体架构与UML设计.md) | 整体架构、运行流程图、时序图、各模块 UML 类图 |
| [09-NDI发送端GPU与硬件加速调研.md](09-NDI发送端GPU与硬件加速调研.md) | NDI 发送端 GPU 纹理与硬件编码支持、HX GPU 管线 |
| [10-COM套间与HX编码线程问题.md](10-COM套间与HX编码线程问题.md) | HX H.264 模式下 COM 套间冲突根因与修复方案 |
| [11-Media-Foundation框架介绍与使用.md](11-Media-Foundation框架介绍与使用.md) | MF 框架定位、硬件加速、驱动检测、与 FFmpeg/DXVA2 对比 |

## 工程说明

- **NDISender**：DXGI 屏幕采集 / Alpha 测试图 + NDI 推流（High Bandwidth / HX）
- **NDIReceiver**：NDI 拉流 + DX11 画面渲染（含 Alpha 棋盘格预览）+ SDL2 音频播放

构建与运行请参阅仓库根目录 [README.md](../README.md)。
