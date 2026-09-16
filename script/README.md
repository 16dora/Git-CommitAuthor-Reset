# Git-CommitAuthor-Reset
批量修改Git历史提交记录中指定的人员信息，便于在意外使用其他username提交的时候，没有点亮对应的git小绿点。

# Git 提交身份批量修改指南

使用 `commitchange.ps1` 将历史提交中的：

```text
OLD_NAME <OLD_EMAIL>
```

批量改为：

```text
NEW_NAME <NEW_EMAIL>
```

> 此操作会重写提交历史并改变提交哈希。操作期间应暂停其他协作者推送，并保留原仓库作为备份。

## 1. 环境要求

- Windows PowerShell 5.1 或 PowerShell 7
- Git 2.36+
- Python 3.6+
- `git-filter-repo`
- 远程仓库的强制推送权限

检查环境：

```powershell
git --version
py --version
$PSVersionTable.PSVersion
```

安装并验证 `git-filter-repo`：

```powershell
py -m pip install --user git-filter-repo
py -m git_filter_repo --version
```

如果系统使用 `python` 命令，请用 `python` 替换 `py`。

官方安装文档：<https://github.com/newren/git-filter-repo/blob/main/INSTALL.md>

## 2. 准备脚本

将 `commitchange.ps1` 放在目标仓库根目录，与 `.git` 同级：

```text
repository/
├── .git/
└── commitchange.ps1
```

进入仓库并检查状态：

```powershell
cd C:\path\to\repository
git status
git remote -v
git branch --show-current
```

先提交、暂存或备份重要的本地修改。

## 3. 先在本地重写并检查

默认处理 `main` 分支，不会更新远程：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\commitchange.ps1
```

脚本不会修改原仓库，而是在同级目录生成一个新仓库：

```text
repository-author-rewrite-yyyyMMdd-HHmmss
```

处理其他分支：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\commitchange.ps1 -Branch TARGET_BRANCH
```

指定输出目录：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\commitchange.ps1 `
    -OutputDirectory C:\path\to\repository-rewritten
```

## 4. 验证结果

进入新生成的仓库：

```powershell
cd C:\path\to\repository-author-rewrite-yyyyMMdd-HHmmss
```

查看提交身份：

```powershell
git log -10 --format="%h  %an <%ae> | %cn <%ce>"
```

检查旧身份是否仍然存在。先把代称替换为实际旧身份：

```powershell
git log main --format="%an <%ae> | %cn <%ce>" |
    Select-String "OLD_NAME|OLD_EMAIL"
```

正确情况下没有输出。

## 5. 更新远程仓库

确认结果正确后，回到原仓库执行：

```powershell
cd C:\path\to\repository

powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\commitchange.ps1 -Push
```

脚本使用 `--force-with-lease` 更新远程分支。如果推送被拒绝，请检查仓库权限、身份验证、分支保护规则以及远程分支是否刚被他人更新。

## 6. 同步原来的本地仓库

推送后，原仓库仍指向旧历史。也可以直接使用脚本生成的新仓库。

如需继续使用原仓库，先保存本地文件，再切换到远程新历史：

```powershell
cd C:\path\to\repository

git stash push --include-untracked -m "before history rewrite"
git fetch origin --prune
git reset --hard origin/main
git stash pop
git status
```

不要使用普通 `git pull` 合并新旧历史。如果 `stash pop` 出现冲突，应先解决冲突。

## 7. 最终检查

```powershell
git rev-parse main
git rev-parse origin/main

git log main --format="%an <%ae> | %cn <%ce>" |
    Select-String "OLD_NAME|OLD_EMAIL"
```

前两个哈希应相同，旧身份检查应没有输出。

## 8. GitHub 贡献图说明

贡献图未立即更新时，检查：

- `NEW_EMAIL` 的实际邮箱是否已添加到 GitHub 并完成验证；
- 提交是否位于默认分支；
- 私有贡献显示是否已开启；
- 是否已经等待最多约 24 小时。

GitHub 按作者邮箱关联账号，并按原 Author Date 统计贡献。历史提交不会全部计入推送当天，超出当前展示年份的提交也不会显示在当前贡献图中。

## 9. 注意事项

- 脚本一次只处理 `-Branch` 指定的分支，默认是 `main`。
- 脚本不会自动处理其他分支和标签。
- 历史重写后，其他协作者应重新克隆仓库。
- 不要将旧克隆中的历史合并回新历史。
