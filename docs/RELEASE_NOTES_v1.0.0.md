# PhiPhysiceEditorNeo 1.0.0

首个以 **PhiPhysiceEditorNeo** 名称发布的版本。

## 下载

- `PhiPhysiceEditorNeo_v1.0.0_windows-x64.zip`：64 位 Windows 10/11，推荐。
- `PhiPhysiceEditorNeo_v1.0.0_windows-x86.zip`：32 位 Windows 10/11。

解压即用，无需安装。

## 主要变化

- 项目由 PolyNote Studio 全面更名为 **PhiPhysiceEditorNeo**，包括可执行文件、
  窗口标题、Windows 版本资源、设置/工程文件头、文档和构建脚本。
- 旧版 `POLYNOTE-PROJECT`、`POLYNOTE-SETTINGS` 和旧版 `.note` 注释头保持
  向后兼容；旧工程和旧原件可以直接打开，保存时写入新文件头。
- 字体合规整理：界面字体改为 Noto Sans SC 的 **Regular 静态实例**，重命名字体族为
  PhiPhysice Neo Sans，保留完整 OFL 1.1 版权与许可文本，并补充第三方许可说明。
- 移除无法确认授权的第三方缓动头文件，改用项目内按标准缓动定义实现的 28 条曲线。
- 清理构建缓存、运行日志、本机设置、导出结果和旧版字形生成文件。
- 增加 `projects/Demo.pnp` 示例工程、`THIRD_PARTY_NOTICES.md`、字体说明和
  GitHub 发布说明。
- 构建脚本支持 `-Arch x64` / `-Arch x86`，新增两个架构的一键打包脚本。

## 自检

Windows x64 / x86 均通过：

```text
193 passed, 0 failed
```

## 已知说明

- 可执行文件未使用商业代码签名证书，首次从网络下载后可能触发 SmartScreen；
  可选择“仍要运行”，或校验 SHA256 后使用。
- 发布包内含 Microsoft VC++ 运行时 DLL，以兼容未安装 VC++ 运行库的系统。
- 界面字体与第三方组件许可见 `THIRD_PARTY_NOTICES.md`。
