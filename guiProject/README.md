# Git Identity Studio（Qt 6 GUI）

这是基于 `../script/commitchange.ps1` 安全工作流构建的 Qt Widgets 应用。

## 当前能力

- 选择任意目录，并向上识别最近的 `.git` 仓库；
- 读取本地分支的提交总数，首次加载最近 1000 条提交；
- 通过“加载更多”每次继续读取最多 1000 条，直到显示全部历史；
- 展示提交哈希、说明、作者、邮箱、时间和提交者信息；
- 按哈希、说明、作者或邮箱过滤；
- 汇总已加载提交中的作者身份及数量，并在继续加载后自动更新；
- 修改选定提交的完整说明、作者姓名/邮箱和提交者姓名/邮箱；
- 默认使用“生成安全副本”模式，原仓库不会被修改；
- 可选“直接修改当前仓库”，程序会在改写前生成持久化 `.bundle` 恢复备份；
- 使用 `git-filter-repo` 重写并验证新提交字段，两种模式都不会自动推送远端。

## 本机 Qt 构建

工程预设基于当前环境：Qt 6.11.1、MinGW 13.1、Ninja。

在 `guiProject` 目录执行：

```powershell
cmake --preset qt6-mingw-debug
cmake --build --preset build-debug
```

程序路径：

```text
build/qt6-mingw-debug/GitIdentityStudio.exe
```

也可以在 Qt Creator 中直接打开 `guiProject/CMakeLists.txt`。

## 测试

```powershell
ctest --test-dir build/qt6-mingw-debug --output-on-failure
```

集成测试会验证安全副本、原地改写、bundle 恢复以及脏工作区拒绝逻辑。

## 安全说明

修改历史会改变目标提交及其后续提交的哈希。直接修改模式只允许改写当前已检出的分支，并且会拒绝以下状态：

- 存在未提交或未跟踪文件；
- 正在进行 merge、rebase、cherry-pick、revert 或 bisect；
- 界面选择的分支不是当前已检出分支；
- 备份路径位于当前仓库内部或者已经存在。

如需从备份检查改写前的历史，最安全的方式是克隆到新目录：

```powershell
git clone "<backup-file>.bundle" "<restored-directory>"
```

程序不会自动推送远端。检查确认后，远端更新应作为独立步骤进行。
