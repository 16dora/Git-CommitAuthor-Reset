#include "commit_rewriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <iostream>

namespace
{

static constexpr int PROCESS_START_TIMEOUT_MS = 5000;
static constexpr int PROCESS_POLL_INTERVAL_MS = 100;
static constexpr int PROCESS_TERMINATION_TIMEOUT_MS = 3000;
static constexpr int DEFAULT_COMMAND_TIMEOUT_MS = 300000;
static constexpr int VERIFY_COMMAND_TIMEOUT_MS = 30000;
static constexpr int REWRITE_TOTAL_STEPS = 5;

// 外部命令的同步执行结果。
struct CommandResult
{
    bool isOk = false;
    bool isCanceled = false;
    QByteArray rawOutput;
    QString output;
    QString error;
};

// 判断提交头字段是否会在历史改写后失效。
bool isInvalidatedCommitHeader(const QByteArray &line)
{
    return line.startsWith("gpgsig ") || line.startsWith("gpgsig-sha256 ") ||
           line.startsWith("mergetag ");
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
                         bool isCancellationAllowed = true,
                         const QByteArray &standardInput = {})
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
    if (!standardInput.isEmpty())
    {
        if (process.write(standardInput) != standardInput.size())
        {
            process.kill();
            process.waitForFinished(PROCESS_TERMINATION_TIMEOUT_MS);
            result.error = QString("无法向命令写入数据：%1").arg(program);
            return result;
        }
        process.closeWriteChannel();
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

    result.rawOutput = process.readAllStandardOutput();
    result.output = QString::fromUtf8(result.rawOutput);
    result.error = QString::fromUtf8(process.readAllStandardError()).trimmed();
    result.isOk = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (!result.isOk && result.error.isEmpty())
    {
        result.error =
            QString("命令执行失败：%1（退出码 %2）").arg(program).arg(process.exitCode());
    }
    return result;
}

// 在保留时间戳和时区的前提下替换提交身份字段。
bool rewriteIdentityHeader(const QByteArray &line, const QByteArray &fieldPrefix,
                           const QString &name, const QString &email, QByteArray &rewrittenLine)
{
    const qsizetype emailEndIndex = line.lastIndexOf('>');
    const qsizetype emailStartIndex = line.lastIndexOf('<', emailEndIndex);
    if (!line.startsWith(fieldPrefix) || emailStartIndex < fieldPrefix.size() ||
        emailEndIndex <= emailStartIndex)
    {
        return false;
    }

    rewrittenLine = fieldPrefix + name.toUtf8() + " <" + email.toUtf8() + ">" +
                    line.mid(emailEndIndex + 1);
    return true;
}

// 重建单个提交对象，并按旧新哈希映射更新父提交引用。
bool rewriteCommitObject(const QByteArray &originalObject, bool isTargetCommit,
                         const CommitRewriteRequest &request,
                         const QHash<QString, QString> &rewrittenHashes,
                         QByteArray &rewrittenObject, QString &error)
{
    const qsizetype separatorIndex = originalObject.indexOf("\n\n");
    if (separatorIndex < 0)
    {
        error = "提交对象缺少头部与说明分隔符。";
        return false;
    }

    const QList<QByteArray> headerLines = originalObject.left(separatorIndex).split('\n');
    bool hasChangedParent = false;
    for (const QByteArray &line : headerLines)
    {
        if (!line.startsWith("parent "))
        {
            continue;
        }
        const QString parentHash = QString::fromLatin1(line.mid(7));
        hasChangedParent = rewrittenHashes.contains(parentHash) &&
                           rewrittenHashes.value(parentHash) != parentHash;
        if (hasChangedParent)
        {
            break;
        }
    }

    if (!isTargetCommit && !hasChangedParent)
    {
        rewrittenObject = originalObject;
        return true;
    }

    QList<QByteArray> rewrittenHeaderLines;
    rewrittenHeaderLines.reserve(headerLines.size());
    bool hasAuthor = false;
    bool hasCommitter = false;
    for (qsizetype lineIndex = 0; lineIndex < headerLines.size(); ++lineIndex)
    {
        const QByteArray &line = headerLines.at(lineIndex);
        if (isInvalidatedCommitHeader(line))
        {
            while (lineIndex + 1 < headerLines.size() &&
                   headerLines.at(lineIndex + 1).startsWith(' '))
            {
                ++lineIndex;
            }
            continue;
        }

        if (line.startsWith("parent "))
        {
            const QString parentHash = QString::fromLatin1(line.mid(7));
            rewrittenHeaderLines.append(
                "parent " + rewrittenHashes.value(parentHash, parentHash).toLatin1());
            continue;
        }

        if (isTargetCommit && line.startsWith("author "))
        {
            QByteArray rewrittenLine;
            if (!rewriteIdentityHeader(line, "author ", request.authorName,
                                       request.authorEmail, rewrittenLine))
            {
                error = "目标提交的作者字段格式无效。";
                return false;
            }
            rewrittenHeaderLines.append(rewrittenLine);
            hasAuthor = true;
            continue;
        }

        if (isTargetCommit && line.startsWith("committer "))
        {
            QByteArray rewrittenLine;
            if (!rewriteIdentityHeader(line, "committer ", request.committerName,
                                       request.committerEmail, rewrittenLine))
            {
                error = "目标提交的提交者字段格式无效。";
                return false;
            }
            rewrittenHeaderLines.append(rewrittenLine);
            hasCommitter = true;
            continue;
        }

        // 新说明统一按 UTF-8 写入，不能继续保留旧提交的编码声明。
        if (isTargetCommit && line.startsWith("encoding "))
        {
            continue;
        }
        rewrittenHeaderLines.append(line);
    }

    if (isTargetCommit && (!hasAuthor || !hasCommitter))
    {
        error = "目标提交缺少作者或提交者字段。";
        return false;
    }

    QByteArray message = originalObject.mid(separatorIndex + 2);
    if (isTargetCommit)
    {
        message = request.message.toUtf8();
        if (!message.endsWith('\n'))
        {
            message.append('\n');
        }
    }
    rewrittenObject = rewrittenHeaderLines.join('\n') + "\n\n" + message;
    return true;
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

    const QString branchReference = QString("refs/heads/%1").arg(request.branch);
    CommandResult command;
    command = runCommand(gitProgram,
                         {"-c", QString("safe.directory=%1").arg(safeSource), "-C",
                          request.sourceRepository, "merge-base", "--is-ancestor",
                          request.commitHash, branchReference},
                         request.sourceRepository, 1, REWRITE_TOTAL_STEPS,
                         "正在检查目标提交与分支…", progress, VERIFY_COMMAND_TIMEOUT_MS);
    if (command.isCanceled)
    {
        return canceledResult();
    }
    if (!command.isOk)
    {
        return failedResult("所选提交不在目标分支中，未执行任何改写。");
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

    if (isInPlace)
    {
        if (progress && !progress(3, REWRITE_TOTAL_STEPS, "备份已完成，即将开始修改当前仓库…"))
        {
            return canceledResult({}, request.backupBundlePath);
        }
    }

    command = runCommand(gitProgram,
                         {"-c", QString("safe.directory=%1").arg(safeTarget), "-C",
                          targetRepository, "rev-parse", "--verify",
                          QString("%1^{commit}").arg(branchReference)},
                         targetRepository, 4, REWRITE_TOTAL_STEPS,
                         "正在读取目标分支历史…", progress, VERIFY_COMMAND_TIMEOUT_MS,
                         !isInPlace);
    if (command.isCanceled)
    {
        return canceledResult(request.outputDirectory);
    }
    if (!command.isOk)
    {
        return failedResult(QString("无法读取目标分支顶端：\n%1").arg(command.error),
                            isInPlace ? request.backupBundlePath : QString());
    }
    const QString originalBranchTip = command.output.trimmed();

    const QString revisionRange = QString("%1..%2").arg(request.commitHash, branchReference);
    command = runCommand(gitProgram,
                         {"-c", QString("safe.directory=%1").arg(safeTarget), "-C",
                          targetRepository, "rev-list", "--reverse", "--topo-order",
                          "--ancestry-path", revisionRange},
                         targetRepository, 4, REWRITE_TOTAL_STEPS,
                         "正在计算需要重建的提交…", progress, VERIFY_COMMAND_TIMEOUT_MS,
                         !isInPlace);
    if (command.isCanceled)
    {
        return canceledResult(request.outputDirectory);
    }
    if (!command.isOk)
    {
        return failedResult(QString("无法计算提交改写范围：\n%1").arg(command.error),
                            isInPlace ? request.backupBundlePath : QString());
    }

    QStringList affectedCommitHashes = {request.commitHash};
    const QStringList descendantHashes = command.output.split('\n', Qt::SkipEmptyParts);
    for (const QString &descendantHash : descendantHashes)
    {
        const QString normalizedHash = descendantHash.trimmed();
        if (!normalizedHash.isEmpty())
        {
            affectedCommitHashes.append(normalizedHash);
        }
    }

    QHash<QString, QString> rewrittenHashes;
    for (qsizetype commitIndex = 0; commitIndex < affectedCommitHashes.size(); ++commitIndex)
    {
        const QString oldCommitHash = affectedCommitHashes.at(commitIndex);
        const QString rewriteLabel =
            QString("正在重建提交历史（%1/%2）…")
                .arg(commitIndex + 1)
                .arg(affectedCommitHashes.size());
        command = runCommand(gitProgram,
                             {"-c", QString("safe.directory=%1").arg(safeTarget), "-C",
                              targetRepository, "cat-file", "commit", oldCommitHash},
                             targetRepository, 4, REWRITE_TOTAL_STEPS, rewriteLabel, progress,
                             DEFAULT_COMMAND_TIMEOUT_MS, !isInPlace);
        if (command.isCanceled)
        {
            return canceledResult(request.outputDirectory);
        }
        if (!command.isOk)
        {
            return failedResult(
                QString("读取提交对象失败（%1）：\n%2").arg(oldCommitHash, command.error),
                isInPlace ? request.backupBundlePath : QString());
        }

        QByteArray rewrittenObject;
        QString rewriteError;
        if (!rewriteCommitObject(command.rawOutput, oldCommitHash == request.commitHash, request,
                                 rewrittenHashes, rewrittenObject, rewriteError))
        {
            return failedResult(QString("重建提交对象失败（%1）：%2")
                                    .arg(oldCommitHash, rewriteError),
                                isInPlace ? request.backupBundlePath : QString());
        }

        if (rewrittenObject == command.rawOutput)
        {
            rewrittenHashes.insert(oldCommitHash, oldCommitHash);
            continue;
        }

        command = runCommand(gitProgram,
                             {"-c", QString("safe.directory=%1").arg(safeTarget), "-C",
                              targetRepository, "hash-object", "-t", "commit", "-w", "--stdin"},
                             targetRepository, 4, REWRITE_TOTAL_STEPS, rewriteLabel, progress,
                             DEFAULT_COMMAND_TIMEOUT_MS, !isInPlace, rewrittenObject);
        if (command.isCanceled)
        {
            return canceledResult(request.outputDirectory);
        }
        if (!command.isOk || command.output.trimmed().isEmpty())
        {
            return failedResult(
                QString("写入提交对象失败（%1）：\n%2").arg(oldCommitHash, command.error),
                isInPlace ? request.backupBundlePath : QString());
        }
        rewrittenHashes.insert(oldCommitHash, command.output.trimmed());
    }

    const QString newCommitHash = rewrittenHashes.value(request.commitHash);
    const QString rewrittenBranchTip = rewrittenHashes.value(originalBranchTip);
    if (newCommitHash.isEmpty() || rewrittenBranchTip.isEmpty())
    {
        return failedResult("改写完成，但无法确定目标提交或分支顶端的新哈希。",
                            isInPlace ? request.backupBundlePath : QString());
    }

    command = runCommand(gitProgram,
                         {"-c", QString("safe.directory=%1").arg(safeTarget), "-C",
                          targetRepository, "update-ref", "-m",
                          "Git Identity Studio rewrite", branchReference, rewrittenBranchTip,
                          originalBranchTip},
                         targetRepository, 4, REWRITE_TOTAL_STEPS,
                         "正在更新目标分支引用…", progress, VERIFY_COMMAND_TIMEOUT_MS,
                         !isInPlace);
    if (command.isCanceled)
    {
        return canceledResult(request.outputDirectory);
    }
    if (!command.isOk)
    {
        return failedResult(QString("更新目标分支失败：\n%1").arg(command.error),
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
