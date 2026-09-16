#include "commit_rewriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <iostream>

namespace
{

static constexpr int PROCESS_START_TIMEOUT_MS = 5000;
static constexpr int PROCESS_POLL_INTERVAL_MS = 100;
static constexpr int PROCESS_TERMINATION_TIMEOUT_MS = 3000;
static constexpr int DEFAULT_COMMAND_TIMEOUT_MS = 300000;
static constexpr int FILTER_DETECTION_TIMEOUT_MS = 15000;
static constexpr int VERIFY_COMMAND_TIMEOUT_MS = 30000;
static constexpr int REWRITE_TOTAL_STEPS = 5;

// 外部命令的同步执行结果。
struct CommandResult
{
    bool isOk = false;
    bool isCanceled = false;
    QString output;
    QString error;
};

// 将任意 UTF-8 数据转换为安全的 Python bytes 表达式。
QString pythonBytes(const QByteArray &value)
{
    if (value.isEmpty())
    {
        return "bytes()";
    }

    QStringList byteValues;
    byteValues.reserve(value.size());
    for (const char byte : value)
    {
        byteValues.append(QString::number(static_cast<unsigned char>(byte)));
    }
    return QString("bytes((%1))").arg(byteValues.join(','));
}

// 去除提交说明尾部换行，用于验证比较。
QString normalizeCommitMessage(QString message)
{
    while (message.endsWith('\n') || message.endsWith('\r'))
    {
        message.chop(1);
    }
    return message;
}

// 执行可取消、可超时的外部命令。
CommandResult runCommand(const QString &program, const QStringList &arguments,
                         const QString &workingDirectory, int step, int total, const QString &label,
                         const CommitRewriter::ProgressCallback &progress,
                         int timeoutMs = DEFAULT_COMMAND_TIMEOUT_MS,
                         bool isCancellationAllowed = true)
{
    CommandResult result;
    if (progress && !progress(step, total, label) && isCancellationAllowed)
    {
        result.isCanceled = true;
        return result;
    }

    QProcess process;
    if (!workingDirectory.isEmpty())
    {
        process.setWorkingDirectory(workingDirectory);
    }
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(PROCESS_START_TIMEOUT_MS))
    {
        result.error = QString("无法启动命令：%1").arg(program);
        return result;
    }

    int elapsedMs = 0;
    while (!process.waitForFinished(PROCESS_POLL_INTERVAL_MS))
    {
        elapsedMs += PROCESS_POLL_INTERVAL_MS;
        if (progress && !progress(step, total, label) && isCancellationAllowed)
        {
            process.kill();
            process.waitForFinished(PROCESS_TERMINATION_TIMEOUT_MS);
            result.isCanceled = true;
            return result;
        }
        if (elapsedMs >= timeoutMs)
        {
            process.kill();
            process.waitForFinished(PROCESS_TERMINATION_TIMEOUT_MS);
            result.error = QString("命令执行超时：%1").arg(program);
            return result;
        }
    }

    result.output = QString::fromUtf8(process.readAllStandardOutput());
    result.error = QString::fromUtf8(process.readAllStandardError()).trimmed();
    result.isOk = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (!result.isOk && result.error.isEmpty())
    {
        result.error =
            QString("命令执行失败：%1（退出码 %2）").arg(program).arg(process.exitCode());
    }
    return result;
}

// 判断目标路径是否位于指定目录内部。
bool isPathInsideDirectory(const QString &path, const QString &directory)
{
    const QString candidate =
        QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    const QString base =
        QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(directory).absoluteFilePath()));
    return candidate.compare(base, Qt::CaseInsensitive) == 0 ||
           candidate.startsWith(base + '/', Qt::CaseInsensitive);
}

// 构造失败结果并记录改写出口。
CommitRewriteResult failedResult(const QString &message, const QString &backupBundlePath = {})
{
    CommitRewriteResult result;
    result.error = message;
    result.backupBundlePath = backupBundlePath;
    std::cout << "CommitRewriter::rewrite() <<"
              << " result=false"
              << " message=" << message.toStdString() << std::endl;
    return result;
}

// 清理未完成产物，构造已取消结果并记录出口。
CommitRewriteResult canceledResult(const QString &cleanupDirectory = {},
                                   const QString &cleanupFile = {})
{
    if (!cleanupDirectory.isEmpty() && QFileInfo::exists(cleanupDirectory))
    {
        QDir(cleanupDirectory).removeRecursively();
    }
    if (!cleanupFile.isEmpty() && QFileInfo::exists(cleanupFile))
    {
        QFile::remove(cleanupFile);
    }
    CommitRewriteResult result;
    result.isCanceled = true;
    std::cout << "CommitRewriter::rewrite() <<"
              << " result=false canceled=true" << std::endl;
    return result;
}

} // namespace

// 统一执行环境检测、安全准备、历史改写和结果验证。
CommitRewriteResult CommitRewriter::rewrite(const CommitRewriteRequest &request,
                                            const ProgressCallback &progress)
{
    std::cout << "CommitRewriter::rewrite() >>" << std::endl;
    const bool isInPlace = request.mode == RewriteMode::InPlace;
    const QString safeSource = QDir::fromNativeSeparators(request.sourceRepository);
    const QString targetRepository = isInPlace ? request.sourceRepository : request.outputDirectory;
    const QString safeTarget = QDir::fromNativeSeparators(targetRepository);
    const QString gitProgram = QStandardPaths::findExecutable("git");

    if (!QFileInfo::exists(QDir(request.sourceRepository).filePath(".git")))
    {
        return failedResult("源目录不是有效的 Git 仓库。");
    }
    if (request.branch.isEmpty())
    {
        return failedResult("当前处于游离 HEAD 状态，暂不支持改写。请先切换到本地分支。");
    }
    if (request.commitHash.isEmpty())
    {
        return failedResult("没有指定需要改写的提交。");
    }
    if (gitProgram.isEmpty())
    {
        return failedResult("未在 PATH 中找到 git 可执行文件。");
    }
    if (!isInPlace)
    {
        if (request.outputDirectory.isEmpty())
        {
            return failedResult("未指定新仓库副本的输出目录。");
        }
        if (QFileInfo::exists(request.outputDirectory))
        {
            return failedResult("输出目录已经存在，未执行任何改写。");
        }
        if (!QFileInfo(request.outputDirectory).absoluteDir().exists())
        {
            return failedResult("输出目录的上级目录不存在。");
        }
        if (isPathInsideDirectory(request.outputDirectory, request.sourceRepository))
        {
            return failedResult("新仓库副本不能保存在源仓库内部。");
        }
    }
    else
    {
        if (request.backupBundlePath.isEmpty())
        {
            return failedResult("直接修改前必须指定 .bundle 备份文件。");
        }
        if (QFileInfo::exists(request.backupBundlePath))
        {
            return failedResult("备份文件已经存在，未执行任何改写。");
        }
        if (!QFileInfo(request.backupBundlePath).absoluteDir().exists())
        {
            return failedResult("备份文件的上级目录不存在。");
        }
        if (isPathInsideDirectory(request.backupBundlePath, request.sourceRepository))
        {
            return failedResult("备份文件必须保存在当前仓库外部。");
        }
    }

    QString filterProgram;
    QStringList filterPrefix;
    QStringList detectionErrors;
    CommandResult command;
    const QString neutralWorkingDirectory = QDir::tempPath();
    for (const QString &candidate : {QString("py"), QString("python"), QString("python3")})
    {
        const QString executable = QStandardPaths::findExecutable(candidate);
        if (executable.isEmpty())
        {
            detectionErrors.append(QString("%1：未在 PATH 中找到").arg(candidate));
            continue;
        }

        command =
            runCommand(executable, {"-m", "git_filter_repo", "--version"}, neutralWorkingDirectory,
                       1, REWRITE_TOTAL_STEPS, "正在检查 git-filter-repo 环境…", progress,
                       FILTER_DETECTION_TIMEOUT_MS);
        if (command.isCanceled)
        {
            return canceledResult();
        }
        if (command.isOk)
        {
            filterProgram = executable;
            filterPrefix = {"-m", "git_filter_repo"};
            break;
        }
        detectionErrors.append(QString("%1：%2").arg(executable, command.error));
    }

    if (filterProgram.isEmpty())
    {
        command = runCommand(gitProgram, {"filter-repo", "--version"}, neutralWorkingDirectory, 1,
                             REWRITE_TOTAL_STEPS, "正在检查 git-filter-repo 环境…", progress,
                             FILTER_DETECTION_TIMEOUT_MS);
        if (command.isCanceled)
        {
            return canceledResult();
        }
        if (command.isOk)
        {
            filterProgram = gitProgram;
            filterPrefix = {"filter-repo"};
        }
        else
        {
            detectionErrors.append(QString("git filter-repo：%1").arg(command.error));
            return failedResult(QString("未检测到可用的 git-filter-repo。\n\n"
                                        "请先执行：\npy -m pip install --user git-filter-repo\n\n"
                                        "环境检查详情：\n%1")
                                    .arg(detectionErrors.join('\n')));
        }
    }

    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid())
    {
        return failedResult("无法创建临时工作目录。");
    }

    if (isInPlace)
    {
        command = runCommand(gitProgram,
                             {"-c", QString("safe.directory=%1").arg(safeSource), "-C",
                              request.sourceRepository, "branch", "--show-current"},
                             request.sourceRepository, 2, REWRITE_TOTAL_STEPS,
                             "正在检查当前分支和工作区…", progress, VERIFY_COMMAND_TIMEOUT_MS);
        if (command.isCanceled)
        {
            return canceledResult();
        }
        if (!command.isOk || command.output.trimmed() != request.branch)
        {
            return failedResult(QString("直接修改仅支持当前已检出的分支。\n\n"
                                        "界面选择：%1\n当前检出：%2")
                                    .arg(request.branch, command.output.trimmed().isEmpty()
                                                             ? "(游离 HEAD)"
                                                             : command.output.trimmed()));
        }

        command = runCommand(gitProgram,
                             {"-c", QString("safe.directory=%1").arg(safeSource), "-C",
                              request.sourceRepository, "status", "--porcelain=v1",
                              "--untracked-files=all"},
                             request.sourceRepository, 2, REWRITE_TOTAL_STEPS,
                             "正在检查当前分支和工作区…", progress, VERIFY_COMMAND_TIMEOUT_MS);
        if (command.isCanceled)
        {
            return canceledResult();
        }
        if (!command.isOk)
        {
            return failedResult(QString("无法检查工作区状态：\n%1").arg(command.error));
        }
        if (!command.output.trimmed().isEmpty())
        {
            return failedResult(QString("当前仓库存在未提交或未跟踪的文件，已拒绝直接修改。\n\n%1")
                                    .arg(command.output.trimmed()));
        }

        command = runCommand(gitProgram,
                             {"-c", QString("safe.directory=%1").arg(safeSource), "-C",
                              request.sourceRepository, "rev-parse", "--absolute-git-dir"},
                             request.sourceRepository, 2, REWRITE_TOTAL_STEPS,
                             "正在检查当前分支和工作区…", progress, VERIFY_COMMAND_TIMEOUT_MS);
        if (command.isCanceled)
        {
            return canceledResult();
        }
        if (!command.isOk)
        {
            return failedResult(QString("无法读取 Git 元数据目录：\n%1").arg(command.error));
        }
        const QDir gitDirectory(command.output.trimmed());
        for (const QString &marker :
             {QString("MERGE_HEAD"), QString("CHERRY_PICK_HEAD"), QString("REVERT_HEAD"),
              QString("BISECT_LOG"), QString("rebase-merge"), QString("rebase-apply"),
              QString("sequencer")})
        {
            if (QFileInfo::exists(gitDirectory.filePath(marker)))
            {
                return failedResult(
                    QString("检测到未完成的 Git 操作（%1），请先完成或取消后再改写。").arg(marker));
            }
        }

        command = runCommand(gitProgram,
                             {"-c", QString("safe.directory=%1").arg(safeSource), "-C",
                              request.sourceRepository, "merge-base", "--is-ancestor",
                              request.commitHash, request.branch},
                             request.sourceRepository, 2, REWRITE_TOTAL_STEPS,
                             "正在检查当前分支和工作区…", progress, VERIFY_COMMAND_TIMEOUT_MS);
        if (command.isCanceled)
        {
            return canceledResult();
        }
        if (!command.isOk)
        {
            return failedResult("所选提交不在当前已检出分支中，未执行任何改写。");
        }

        command = runCommand(gitProgram,
                             {"-c", QString("safe.directory=%1").arg(safeSource), "-C",
                              request.sourceRepository, "bundle", "create",
                              request.backupBundlePath, "--all"},
                             request.sourceRepository, 3, REWRITE_TOTAL_STEPS,
                             "正在生成可恢复的 .bundle 备份…", progress);
        if (command.isCanceled)
        {
            return canceledResult({}, request.backupBundlePath);
        }
        if (!command.isOk)
        {
            QFile::remove(request.backupBundlePath);
            return failedResult(
                QString("创建恢复备份失败，未执行任何改写：\n%1").arg(command.error));
        }
    }
    else
    {
        const QString bundlePath = QDir(temporaryDirectory.path()).filePath("source.bundle");
        command = runCommand(
            gitProgram,
            {"-c", QString("safe.directory=%1").arg(safeSource), "-C", request.sourceRepository,
             "bundle", "create", bundlePath, request.branch},
            request.sourceRepository, 2, REWRITE_TOTAL_STEPS, "正在创建源仓库快照…", progress);
        if (command.isCanceled)
        {
            return canceledResult(request.outputDirectory);
        }
        if (!command.isOk)
        {
            return failedResult(QString("创建仓库快照失败：\n%1").arg(command.error));
        }

        command = runCommand(gitProgram,
                             {"clone", "--branch", request.branch, "--single-branch", "--",
                              bundlePath, request.outputDirectory},
                             QFileInfo(request.outputDirectory).absolutePath(), 3,
                             REWRITE_TOTAL_STEPS, "正在生成独立仓库副本…", progress);
        if (command.isCanceled)
        {
            return canceledResult(request.outputDirectory);
        }
        if (!command.isOk)
        {
            return failedResult(QString("创建仓库副本失败：\n%1").arg(command.error));
        }
    }

    QByteArray messageBytes = request.message.toUtf8();
    if (!messageBytes.endsWith('\n'))
    {
        messageBytes.append('\n');
    }
    const QString callback = QString("target = b'%1'\n"
                                     "if commit.original_id == target:\n"
                                     "    commit.message = %2\n"
                                     "    commit.author_name = %3\n"
                                     "    commit.author_email = %4\n"
                                     "    commit.committer_name = %5\n"
                                     "    commit.committer_email = %6\n")
                                 .arg(request.commitHash, pythonBytes(messageBytes),
                                      pythonBytes(request.authorName.toUtf8()),
                                      pythonBytes(request.authorEmail.toUtf8()),
                                      pythonBytes(request.committerName.toUtf8()),
                                      pythonBytes(request.committerEmail.toUtf8()));

    QStringList filterArguments = filterPrefix;
    filterArguments << "--force";
    if (isInPlace)
    {
        if (progress && !progress(3, REWRITE_TOTAL_STEPS, "备份已完成，即将开始修改当前仓库…"))
        {
            return canceledResult({}, request.backupBundlePath);
        }
        filterArguments << "--refs" << QString("refs/heads/%1").arg(request.branch);
    }
    filterArguments << "--commit-callback" << callback;
    command =
        runCommand(filterProgram, filterArguments, targetRepository, 4, REWRITE_TOTAL_STEPS,
                   "正在改写选定提交及后续历史…", progress, DEFAULT_COMMAND_TIMEOUT_MS, !isInPlace);
    if (command.isCanceled)
    {
        return canceledResult(request.outputDirectory);
    }
    if (!command.isOk)
    {
        if (isInPlace)
        {
            return failedResult(
                QString("当前仓库改写失败。请停止后续 Git 操作，并使用备份检查或恢复：\n%1\n\n%2")
                    .arg(QDir::toNativeSeparators(request.backupBundlePath), command.error),
                request.backupBundlePath);
        }
        return failedResult(QString("提交历史改写失败。副本已保留以便检查：\n%1\n\n%2")
                                .arg(request.outputDirectory, command.error));
    }

    const QString commitMapPath = QDir(targetRepository).filePath(".git/filter-repo/commit-map");
    QFile commitMap(commitMapPath);
    if (!commitMap.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return failedResult(QString("无法读取改写结果映射：\n%1").arg(commitMapPath),
                            isInPlace ? request.backupBundlePath : QString());
    }

    QString newCommitHash;
    const QStringList mapLines = QString::fromUtf8(commitMap.readAll()).split('\n');
    for (const QString &line : mapLines)
    {
        const QStringList fields = line.simplified().split(' ');
        if (fields.size() >= 2 && fields.at(0) == request.commitHash)
        {
            newCommitHash = fields.at(1);
            break;
        }
    }
    if (newCommitHash.isEmpty() || newCommitHash == QString(40, '0'))
    {
        return failedResult("改写完成，但无法在 commit-map 中找到目标提交的新哈希。",
                            isInPlace ? request.backupBundlePath : QString());
    }

    command =
        runCommand(gitProgram,
                   {"-c", QString("safe.directory=%1").arg(safeTarget), "-C", targetRepository,
                    "show", "-s", "--format=%an%x1f%ae%x1f%cn%x1f%ce", newCommitHash},
                   targetRepository, 5, REWRITE_TOTAL_STEPS, "正在验证改写结果…", progress,
                   VERIFY_COMMAND_TIMEOUT_MS, !isInPlace);
    if (command.isCanceled)
    {
        return canceledResult(request.outputDirectory);
    }
    if (!command.isOk)
    {
        return failedResult(QString("验证新提交失败：\n%1").arg(command.error),
                            isInPlace ? request.backupBundlePath : QString());
    }

    const QStringList identities = command.output.trimmed().split(QChar(0x1f));
    if (identities.size() < 4 || identities.at(0) != request.authorName ||
        identities.at(1) != request.authorEmail || identities.at(2) != request.committerName ||
        identities.at(3) != request.committerEmail)
    {
        return failedResult("验证失败：新提交中的作者或提交者信息与输入值不一致。",
                            isInPlace ? request.backupBundlePath : QString());
    }

    command = runCommand(gitProgram,
                         {"-c", QString("safe.directory=%1").arg(safeTarget), "-C",
                          targetRepository, "show", "-s", "--format=%B", newCommitHash},
                         targetRepository, 5, REWRITE_TOTAL_STEPS, "正在验证改写结果…", progress,
                         VERIFY_COMMAND_TIMEOUT_MS, !isInPlace);
    if (command.isCanceled)
    {
        return canceledResult(request.outputDirectory);
    }
    if (!command.isOk ||
        normalizeCommitMessage(command.output) != normalizeCommitMessage(request.message))
    {
        return failedResult("验证失败：新提交中的提交说明与输入值不一致。",
                            isInPlace ? request.backupBundlePath : QString());
    }

    if (progress)
    {
        progress(REWRITE_TOTAL_STEPS, REWRITE_TOTAL_STEPS, "改写与验证完成。");
    }

    CommitRewriteResult result;
    result.isOk = true;
    result.newCommitHash = newCommitHash;
    result.targetRepository = targetRepository;
    result.backupBundlePath = isInPlace ? request.backupBundlePath : QString();
    std::cout << "CommitRewriter::rewrite() <<"
              << " result=true"
              << " newCommitHash=" << newCommitHash.toStdString() << std::endl;
    return result;
}
