# 发布到 GitHub

本文件说明如何把当前目录推到 GitHub、生成 1.0 的两个架构压缩包，并创建 Release。

## 1. 发布前检查

- 确认没有把 `build/`、`dist/`、`settings.ini`、`exports/` 或本地路径提交进仓库。
- 确认 `assets/fonts/LICENSE-OFL.txt`、`assets/fonts/NOTICE.md` 和
  `THIRD_PARTY_NOTICES.md` 存在。
- 在 x64 与 x86 上分别运行 `--selftest`，两者都应为 `193 passed, 0 failed`。
- 发布包内不要包含 `settings.ini`（其中可能有本机路径），也不要包含旧版
  `PolyNoteStudio.exe`。

## 2. 初始化并推送仓库

仓库已经由维护者执行过 `git init` 与首次提交；如从零开始：

```powershell
git init -b main
git add .
git commit -m "release: PhiPhysiceEditorNeo 1.0.0"
git remote add origin https://github.com/<你的账号>/PhiPhysiceEditorNeo.git
git push -u origin main
```

也可以在 GitHub 网页新建空仓库（不要勾选自动生成 README），再执行上面的
`git remote add` 和 `git push`。

## 3. 生成两个压缩包

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package_windows.ps1
```

脚本会：

1. 用 MSVC 分别构建 x64 和 x86；
2. 运行 `--selftest`；
3. 把 exe、`components/`、`assets/`、`projects/`、`export_rules.txt`、
   `README.md`、`LICENSE`、`THIRD_PARTY_NOTICES.md` 和必要运行库整理到
   `dist/PhiPhysiceEditorNeo-v1.0.0-win-x64/` 与 `...-win-x86/`；
4. 生成 `dist/PhiPhysiceEditorNeo_v1.0.0_windows-x64.zip`、
   `dist/PhiPhysiceEditorNeo_v1.0.0_windows-x86.zip` 和 SHA256 校验文件。

## 4. 创建 GitHub Release

推荐使用 GitHub CLI：

```powershell
gh release create v1.0.0 `
  dist/PhiPhysiceEditorNeo_v1.0.0_windows-x64.zip `
  dist/PhiPhysiceEditorNeo_v1.0.0_windows-x86.zip `
  dist/SHA256SUMS.txt `
  --title "PhiPhysiceEditorNeo 1.0.0" `
  --notes-file docs/RELEASE_NOTES_v1.0.0.md
```

也可以打开仓库的 **Releases → Draft a new release**，Tag 填 `v1.0.0`，
标题填 `PhiPhysiceEditorNeo 1.0.0`，正文从
`docs/RELEASE_NOTES_v1.0.0.md` 复制，然后上传两个 ZIP 与校验文件。

## 5. 仓库设置建议

- Description：`2D 多边形物理音符编辑器 / 2D polygon physics note editor`
- Topics：`cpp` `raylib` `physics` `editor` `note` `windows` `chinese`
- License：GitHub 自动识别为 MIT。
- 在 **Releases** 中保留 x64 / x86 两个资产；源码压缩包由 GitHub 自动生成。
- 如果以后启用 Actions，可把 `scripts/package_windows.ps1` 放到
  `windows-latest` 上运行；raylib 通过 CMake `FetchContent` 获取，无需提交
  第三方二进制。
