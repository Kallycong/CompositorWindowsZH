# CompositorWindowsZH · Compositor Windows 简体中文汉化版

**Compositor**（原版由 [Robbie Tilton](https://robbietilton.com/compositor) 开发）在 Windows 11 上的独立移植版（[IAmTheBlurr/CompositorWindows](https://github.com/IAmTheBlurr/CompositorWindows)）的**界面简体中文化**。

层、蒙版、选区、绘画、调整、滤镜与离线背景移除等能力与上游一致，界面文字（菜单 / 工具栏 / 面板 / 对话框 / 提示）全部汉化。

## 快速开始

- **直接用**：下载 [Releases](https://github.com/Kallycong/CompositorWindowsZH/releases) 里的可运行包（`CompositorWindowsZH-*-zh-x64.zip`），解压后运行 `Compositor.exe`。
- **从源码构建**：见下方「构建」。

## 汉化范围

- 全部菜单：文件 / 编辑 / 图层 / 选择 / 图像 / 视图 / 变换 / 蒙版 / 工具 / 帮助
- 全部工具栏：选框 / 套索 / 魔棒、画笔、修复、渐变、变换、文字、缩放、裁剪等选项
- 图层面板（右键菜单、蒙版子菜单、混合模式下拉）
- 所有对话框：新建画布、导入/导出、打开项目、颜色选择器、更新、调整（色阶/色相/曲线/Camera Raw 等）
- 撤销/重做提示、状态栏消息、无障碍文本

详见 [汉化说明.md](汉化说明.md) 与 [界面中英对照速查表.md](界面中英对照速查表.md)。

## 保留英文的部分（有意为之）

- 工程文件序列化格式（混合模式枚举、调整层 kind、滤镜 Kind）——改动会破坏 `.comp` 工程读取
- 文件类型过滤器（`*.png`、`*.comp` 等）——避免系统对话框过滤失效
- 产品名 `Compositor`、快捷键键名、底层 Direct2D/剪贴板错误消息

## 构建

本仓库包含**完整汉化源码**与**已适配 VS2026 的构建脚本**；其余依赖与上游一致。

前置：Windows 11 x64、Visual Studio 2022/2026（C++ 桌面开发，MSVC 14.44+）、Windows SDK 10.0.26100、CMake 3.31+、Python 3.12（模型转换，可选）。

```powershell
# 1. 取上游完整源码包（含 dependencies/ 目录结构）：
#    https://github.com/IAmTheBlurr/CompositorWindows/releases
# 2. 用本仓库的 src/、scripts/、CMakeLists.txt、CMakePresets.json、dependencies.lock.json 覆盖到上游解压目录
# 3. 构建（本仓库已将 CMakePresets 的 generator 适配为 VS2026 "Visual Studio 18 2026"）：
.\scripts\bootstrap.ps1 -Python C:\Python312\python.exe -RuntimeDirectory <上游可运行目录>
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release --target Compositor --parallel 8
```

产物：`build/release/Release/Compositor.exe`，再用 `dependencies/qt/bin/windeployqt.exe` 部署 Qt 运行时，并把 `models/`、`shaders/` 放到 exe 旁。

> 提示：若本机是 VS2026 且希望跳过模型转换（复用上游已发布的 `birefnet-lite.onnx`），可手动执行 `bootstrap-imaging.ps1 -SkipModel` 后放置模型文件。

## 许可证与合规

- 应用源码：**MIT**，保留上游版权 `Copyright (c) 2026 Wonder Assembly LLC`（见 [LICENSE](LICENSE)）。
- 上游 Windows 移植：[IAmTheBlurr/CompositorWindows](https://github.com/IAmTheBlurr/CompositorWindows)（MIT），本仓库为其独立汉化改造，未获上游作者认可。
- Qt（LGPL-3.0 等）、libheif/libde265、ONNX Runtime 及模型权重各有其许可证与声明，见上游 `dependencies/` 与 `assets/`。
- 分发二进制时 Qt 以动态链接方式使用，符合 LGPL 要求（可替换 DLL、可重链接）。

## 致谢

- [Robbie Tilton / Compositor](https://robbietilton.com/compositor) —— 原作者
- [IAmTheBlurr / CompositorWindows](https://github.com/IAmTheBlurr/CompositorWindows) —— Windows 移植版
