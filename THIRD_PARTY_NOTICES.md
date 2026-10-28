# 第三方组件与许可

本项目自身代码采用 MIT 许可，见根目录 [LICENSE](LICENSE)。发布包中还包含以下
第三方内容；它们各自保留原有许可，不与本项目的 MIT 许可冲突。

## 1. 界面字体：PhiPhysice Neo Sans

- 来源：Noto Sans SC 2.004（Google Fonts / Adobe），SIL Open Font License 1.1。
- 本项目将其实例化为 Regular 并重命名字体族，属于 OFL 允许的修改版本。
- 版权：`Copyright 2014-2021 Adobe (http://www.adobe.com/), with Reserved Font Name 'Source'.`
- 完整许可见 [assets/fonts/LICENSE-OFL.txt](assets/fonts/LICENSE-OFL.txt)，
  修改说明见 [assets/fonts/NOTICE.md](assets/fonts/NOTICE.md)。

## 2. raylib 5.5

本项目静态链接 raylib 5.5 的 Windows 预编译库。

```text
Copyright (c) 2013-2024 Ramon Santamaria (@raysan5)

This software is provided "as-is", without any express or implied warranty. In no event
will the authors be held liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose, including commercial
applications, and to alter it and redistribute it freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not claim that you
  wrote the original software. If you use this software in a product, an acknowledgment
  in the product documentation would be appreciated but is not required.

  2. Altered source versions must be plainly marked as such, and must not be misrepresented
  as being the original software.

  3. This notice may not be removed or altered from any source distribution.
```

## 3. raylib 静态库内含的组件

raylib 的 Windows 静态库会一起编入下列单文件库。它们均使用宽松许可，完整许可
文本随 raylib 源码分发；本项目未修改这些组件。

| 组件 | 用途 | 许可 |
| --- | --- | --- |
| GLFW / rglfw | 窗口与输入 | zlib/libpng |
| glad | OpenGL 加载 | MIT |
| stb_image / stb_truetype / stb_vorbis 等 | 图像、字体、音频解码 | MIT 或 public domain |
| miniaudio | 音频设备 | MIT-0 或 public domain |
| dr_flac / dr_mp3 / dr_wav | 音频解码 | MIT-0 或 public domain |
| cgltf | glTF 解析 | MIT |
| dirent | 目录遍历 | zlib 风格许可 |
| par_shapes、qoa、sgl 等 | 辅助 | MIT / public domain |

逐字许可文本已随本项目放在 `docs/licenses/`：

- [raylib.txt](docs/licenses/raylib.txt)
- [glfw.txt](docs/licenses/glfw.txt)
- [glad.txt](docs/licenses/glad.txt)
- [stb.txt](docs/licenses/stb.txt)
- [miniaudio.txt](docs/licenses/miniaudio.txt)
- [dr_libs.txt](docs/licenses/dr_libs.txt)
- [cgltf.txt](docs/licenses/cgltf.txt)
- [dirent.txt](docs/licenses/dirent.txt)

原始源码与更多细节：
<https://github.com/raysan5/raylib/tree/5.5/src/external>。

## 4. Microsoft Visual C++ 运行库（仅 Windows 发布包）

Windows 发布包内可能附带 `MSVCP140.dll`、`VCRUNTIME140.dll`、
`VCRUNTIME140_1.dll`，用于在没有安装 Visual C++ 运行库的机器上直接运行。
这些文件来自 Microsoft Visual Studio 可再发行组件，按 Microsoft 的可再发行
条款随本程序一起分发，不属于本项目代码：

<https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist>

## 5. 未随发布包分发的开发工具

构建脚本使用的 CMake、Ninja、Visual Studio/MSVC、Python/Pillow、Node.js 等
只在开发/打包阶段使用，不包含在发布压缩包中。它们各自遵循自己的许可。

## 6. 项目内代码说明

- 2D 刚体物理、界面控件、文件格式、缓动曲线均为项目内实现。
- 缓动曲线依据公开的标准缓动定义（线性、正弦、二次至五次、指数、圆形、
  回弹、弹性、弹跳）编写，不包含无法确认授权的第三方缓动头文件。
- 未使用 Box2D、Dear ImGui 或原生文件对话框库。
