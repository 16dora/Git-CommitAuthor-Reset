# Git Commit Author Reset

用于查看 Git 提交历史并安全修改提交说明、作者和提交者信息。

项目分为两个部分：

- `script/`：PowerShell 历史改写脚本及使用说明；
- `guiProject/`：Qt 6 图形界面工程，使用 CMake 管理。

GUI 默认在独立仓库副本中修改历史；也可选择直接修改当前仓库，此时程序会先生成 `.bundle` 恢复备份。两种模式都不会自动推送远端。

## 运行环境依赖

### 基础查看功能

选择仓库、查看提交历史、搜索和作者统计需要：

- Windows 10/11 64 位；
- Git，建议使用 2.36 或更高版本；
- `git.exe` 可以通过系统 `PATH` 找到；
- Qt 6 运行库，或者已经包含 Qt 运行库的完整发布包。

检查 Git：

```powershell
git --version
where.exe git
```

### 修改提交历史

使用“修改此提交身份”功能还需要：

- Python 3；
- `git-filter-repo` Python 模块；
- `py`、`python` 或 `python3` 中至少一个命令可以通过 `PATH` 找到。

安装 `git-filter-repo`：

```powershell
py -m pip install --user git-filter-repo
```

验证安装：

```powershell
py -m git_filter_repo --version
```

程序会依次检测以下调用方式：

```text
py -m git_filter_repo
python -m git_filter_repo
python3 -m git_filter_repo
git filter-repo
```

GUI 的单提交修改功能已实现安全副本和带 bundle 备份的原地改写流程，运行时不依赖 `script/commitchange.ps1`。

## Qt 运行库

当前开发版本使用 Qt 6.11.1 和 MinGW 13.1 动态链接，直接运行开发构建产物需要：

```text
Qt6Core.dll
Qt6Gui.dll
Qt6Widgets.dll
plugins/platforms/qwindows.dll
libgcc_s_seh-1.dll
libstdc++-6.dll
libwinpthread-1.dll
```

当前开发机上的运行库目录为：

```text
E:\Qt\Qt6\6.11.1\mingw_64\bin
E:\Qt\Qt6\Tools\mingw1310_64\bin
```

单独复制 `GitIdentityStudio.exe` 到其他电脑通常无法直接运行。正式发布时应使用 `windeployqt` 将 Qt DLL、平台插件和 MinGW 运行库一起打包。完整打包后，目标电脑不需要单独安装 Qt。

## 源码构建依赖

- Qt 6.11.1；
- MinGW 13.1 64 位；
- CMake 3.21 或更高版本；
- Ninja；
- 支持 C++17 的编译器。

当前 `CMakePresets.json` 使用以下路径：

```text
Qt:     E:\Qt\Qt6\6.11.1\mingw_64
MinGW:  E:\Qt\Qt6\Tools\mingw1310_64
Ninja:  E:\Qt\Qt6\Tools\Ninja\ninja.exe
```

构建命令：

```powershell
cd guiProject
cmake --preset qt6-mingw-debug
cmake --build --preset build-debug
```

运行测试：

```powershell
ctest --test-dir build/qt6-mingw-debug --output-on-failure
```

## 依赖总结

```text
基础查看：Git + 已包含 Qt 运行库的程序
修改历史：Git + Python + git-filter-repo + 已包含 Qt 运行库的程序
源码编译：Qt 6.11.1 + MinGW 13.1 + CMake + Ninja
```

GUI 的详细构建与安全说明见 `guiProject/README.md`。
