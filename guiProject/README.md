# Git Identity Studio（Qt 6 GUI 原型）

这是基于 `../script/commitchange.ps1` 工作流搭建的第一版 Qt Widgets 界面。当前版本只读取仓库，不执行历史改写。

## 当前能力

- 选择任意目录，并向上识别最近的 `.git` 仓库；
- 读取本地分支和最近 1000 条提交；
- 展示提交哈希、说明、作者、邮箱、时间、提交者信息；
- 按哈希、说明、作者或邮箱过滤；
- 汇总不同作者身份及其提交数量；
- 为“单提交身份修改”和“作者 A 批量映射到作者 B”保留明确入口。

## 本机 Qt 构建

工程预设基于当前环境：Qt 6.11.1、MinGW 13.1、Ninja。

```powershell
cmake --preset qt6-mingw-debug
cmake --build --preset build-debug
```

在 `guiProject` 目录执行，程序路径：

```text
build/qt6-mingw-debug/GitIdentityStudio.exe
```

也可以在 Qt Creator 中直接打开 `guiProject/CMakeLists.txt`。

## 后续建议

下一步可实现“改写方案编辑器”：先收集单提交规则和作者批量映射规则，再生成新仓库副本、预览影响范围、执行 `git-filter-repo`，最后验证旧身份是否残留。远端推送应保持为单独的最终步骤，并要求用户明确确认。
