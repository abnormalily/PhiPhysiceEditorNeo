# 如何上传到 GitHub（图文步骤）

目标仓库名：`PhiPhysiceEditorNeo`，版本标签：`v1.0.0`。
本项目已经完成 `git init`、首次提交和 `v1.0.0` 标签，只差"推送到 GitHub"。

## 路线 A：网页建仓 + 命令行推送（最通用）

1. 打开 <https://github.com/new>，填写：
   - Repository name：`PhiPhysiceEditorNeo`
   - Public（或 Private）
   - **不要**勾选 "Add a README file"、"Add .gitignore"、"Choose a license"（本地已经有了）
2. 在项目根目录打开 PowerShell（或 Git Bash），执行：

   ```powershell
   git remote add origin https://github.com/<你的账号>/PhiPhysiceEditorNeo.git
   git push -u origin main
   git push origin v1.0.0
   ```

3. 第一次推送会弹窗要求登录。推荐用 GitHub 的 **Personal Access Token** 当密码：
   <https://github.com/settings/tokens> → Generate new token (classic) → 勾选 `repo` → 复制 token，
   在弹窗的 Password 里粘贴 token（不是账号密码）。
4. 刷新仓库页面，能看到代码、README、字体与文档即成功。

## 路线 B：GitHub CLI 一条命令（装好后最快）

1. 安装 <https://cli.github.com/>，然后：

   ```powershell
   gh auth login
   gh repo create PhiPhysiceEditorNeo --public --source . --remote origin --push
   git push origin v1.0.0
   ```

2. `gh repo create` 会自动创建远程仓库并推送 `main`，无需手动建仓库。

## 已有远程时

```powershell
git remote -v
git remote set-url origin https://github.com/<你的账号>/PhiPhysiceEditorNeo.git
git push -u origin main
git push origin v1.0.0
```

## 发布 1.0 压缩包（Release）

两个 ZIP 在 `dist/` 下，**不进仓库**（已在 .gitignore 中排除），只作为 Release 附件：

- `dist/PhiPhysiceEditorNeo_v1.0.0_windows-x64.zip`
- `dist/PhiPhysiceEditorNeo_v1.0.0_windows-x86.zip`
- `dist/SHA256SUMS.txt`

网页上传：

1. 仓库 → **Releases → Draft a new release**
2. Choose a tag 选择 `v1.0.0`，标题填 `PhiPhysiceEditorNeo 1.0.0`
3. 描述从 `docs/RELEASE_NOTES_v1.0.0.md` 复制
4. 把上面两个 ZIP 和 `SHA256SUMS.txt` 拖进 Assets，点击 **Publish release**

命令行上传（推荐）：

```powershell
gh release create v1.0.0 `
  dist/PhiPhysiceEditorNeo_v1.0.0_windows-x64.zip `
  dist/PhiPhysiceEditorNeo_v1.0.0_windows-x86.zip `
  dist/SHA256SUMS.txt `
  --title "PhiPhysiceEditorNeo 1.0.0" `
  --notes-file docs/RELEASE_NOTES_v1.0.0.md
```

## 校验哈希为什么和打包时不同？

ZIP 文件头会记录每个文件的修改时间，所以**同一内容在不同时间/不同电脑上重新打包，
ZIP 的 SHA256 会不同**，这属于正常现象。请以发布时 `SHA256SUMS.txt` 里的值为准；
校验方式：

```powershell
Get-FileHash .\PhiPhysiceEditorNeo_v1.0.0_windows-x64.zip -Algorithm SHA256
```

如果想在任何机器上得到完全相同的压缩包，可以使用
`7z a -tzip -mtc=off`（不写入时间戳）或 `zip -X` 重新打包，再更新校验文件。

## 建议的仓库设置

- Description：`2D 多边形物理音符编辑器 / 2D polygon physics note editor`
- Topics：`cpp` `raylib` `physics` `editor` `note` `windows` `chinese`
- License：GitHub 会自动识别根目录 `LICENSE` 为 MIT
