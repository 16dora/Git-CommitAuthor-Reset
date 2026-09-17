#include "commit_rewriter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <iostream>

namespace
{

static constexpr int GIT_START_TIMEOUT_MS = 5000;
static constexpr int GIT_COMMAND_TIMEOUT_MS = 30000;

struct CommandResult
{
    bool isOk = false;
    QString output;
    QString error;
};

// 在指定测试仓库中执行 Git 命令。
CommandResult git(const QString &repository, const QStringList &arguments)
{
    QProcess process;
    QStringList gitArguments = {"-C", repository};
    gitArguments << arguments;
    process.start("git", gitArguments);
    if (!process.waitForStarted(GIT_START_TIMEOUT_MS) ||
        !process.waitForFinished(GIT_COMMAND_TIMEOUT_MS))
    {
        return {false, {}, "git command could not be executed"};
    }
    return {process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
            QString::fromUtf8(process.readAllStandardOutput()).trimmed(),
            QString::fromUtf8(process.readAllStandardError()).trimmed()};
}

// 使用固定测试身份创建空提交。
bool hasCreatedCommit(const QString &repository, const QString &message)
{
    return git(repository,
               {"-c", "user.name=Original Author", "-c", "user.email=original@example.com",
                "commit", "--allow-empty", "-m", message})
        .isOk;
}

// 输出测试失败原因并返回非零退出码。
int fail(const QString &message)
{
    qCritical().noquote() << message;
    std::cout << "main() <<"
              << " result=1"
              << " message=" << message.toStdString() << std::endl;
    return 1;
}

} // namespace

// 构造临时 Git 仓库并验证安全副本与原地改写流程。
int main(int argc, char *argv[])
{
    std::cout << "main() >>" << std::endl;
    QCoreApplication app(argc, argv);
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid())
    {
        return fail("Could not create temporary directory");
    }

    const QString sourceRepository = QDir(temporaryDirectory.path()).filePath("source");
    const QString outputRepository = QDir(temporaryDirectory.path()).filePath("rewritten");
    QDir().mkpath(sourceRepository);

    if (!git(sourceRepository, {"init", "-b", "main"}).isOk ||
        !hasCreatedCommit(sourceRepository, "First commit") ||
        !hasCreatedCommit(sourceRepository, "Target commit"))
    {
        return fail("Could not create source history");
    }

    const QString targetHash = git(sourceRepository, {"rev-parse", "HEAD"}).output;
    if (targetHash.size() != 40 || !hasCreatedCommit(sourceRepository, "Following commit"))
    {
        return fail("Could not resolve target commit");
    }
    const QString originalTip = git(sourceRepository, {"rev-parse", "HEAD"}).output;

    CommitRewriteRequest request;
    request.sourceRepository = sourceRepository;
    request.branch = "main";
    request.commitHash = targetHash;
    request.message = "Updated target message\nwith a second line";
    request.authorName = "New Author";
    request.authorEmail = "new-author@example.com";
    request.committerName = "New Committer";
    request.committerEmail = "new-committer@example.com";
    request.outputDirectory = outputRepository;

    const CommitRewriteResult rewriteResult = CommitRewriter::rewrite(request);
    if (!rewriteResult.isOk)
    {
        return fail(QString("Rewrite failed: %1").arg(rewriteResult.error));
    }

    if (git(sourceRepository, {"rev-parse", "HEAD"}).output != originalTip ||
        git(sourceRepository, {"show", "-s", "--format=%s", targetHash}).output != "Target commit")
    {
        return fail("Source repository was modified");
    }

    const CommandResult identity =
        git(outputRepository,
            {"show", "-s", "--format=%an%x1f%ae%x1f%cn%x1f%ce", rewriteResult.newCommitHash});
    const QStringList fields = identity.output.split(QChar(0x1f));
    if (!identity.isOk || fields.size() != 4 || fields.at(0) != request.authorName ||
        fields.at(1) != request.authorEmail || fields.at(2) != request.committerName ||
        fields.at(3) != request.committerEmail)
    {
        return fail("Rewritten identity did not match requested values");
    }

    const QString rewrittenMessage =
        git(outputRepository, {"show", "-s", "--format=%B", rewriteResult.newCommitHash}).output;
    if (rewrittenMessage != request.message)
    {
        return fail("Rewritten message did not match requested value");
    }

    const QString rewrittenTip = git(outputRepository, {"rev-parse", "main"}).output;
    if (rewrittenTip == originalTip)
    {
        return fail("Descendant commit hash was not rewritten");
    }

    const QStringList rewrittenHistory =
        git(outputRepository, {"rev-list", "--reverse", "main"}).output.split('\n');
    if (rewrittenHistory.size() != 3)
    {
        return fail("Unexpected history after first rewrite");
    }

    CommitRewriteRequest secondRequest = request;
    secondRequest.sourceRepository = outputRepository;
    secondRequest.commitHash = rewrittenHistory.first();
    secondRequest.message = "Second rewrite from rewritten repository";
    secondRequest.authorName = "Second Author";
    secondRequest.authorEmail = "second-author@example.com";
    secondRequest.committerName = "Second Committer";
    secondRequest.committerEmail = "second-committer@example.com";
    secondRequest.outputDirectory = QDir(temporaryDirectory.path()).filePath("rewritten-again");

    const CommitRewriteResult secondRewriteResult = CommitRewriter::rewrite(secondRequest);
    if (!secondRewriteResult.isOk)
    {
        return fail(QString("Second rewrite failed: %1").arg(secondRewriteResult.error));
    }
    if (git(outputRepository, {"rev-parse", "main"}).output != rewrittenTip)
    {
        return fail("First rewritten repository was modified by second rewrite");
    }
    if (git(secondRequest.outputDirectory,
            {"show", "-s", "--format=%s", secondRewriteResult.newCommitHash})
            .output != secondRequest.message)
    {
        return fail("Second rewrite message did not match requested value");
    }

    const QString inPlaceRepository = QDir(temporaryDirectory.path()).filePath("in-place-source");
    const QString backupBundle = QDir(temporaryDirectory.path()).filePath("in-place-backup.bundle");
    const QString restoredRepository =
        QDir(temporaryDirectory.path()).filePath("restored-from-backup");
    QDir().mkpath(inPlaceRepository);
    if (!git(inPlaceRepository, {"init", "-b", "main"}).isOk ||
        !hasCreatedCommit(inPlaceRepository, "In-place first commit") ||
        !hasCreatedCommit(inPlaceRepository, "In-place target commit"))
    {
        return fail("Could not create in-place source history");
    }
    const QString inPlaceTarget = git(inPlaceRepository, {"rev-parse", "HEAD"}).output;
    if (inPlaceTarget.size() != 40 ||
        !git(inPlaceRepository, {"branch", "side", inPlaceTarget}).isOk ||
        !hasCreatedCommit(inPlaceRepository, "In-place following commit"))
    {
        return fail("Could not resolve in-place target commit");
    }
    const QString inPlaceOriginalTip = git(inPlaceRepository, {"rev-parse", "HEAD"}).output;

    CommitRewriteRequest inPlaceRequest = request;
    inPlaceRequest.mode = RewriteMode::InPlace;
    inPlaceRequest.sourceRepository = inPlaceRepository;
    inPlaceRequest.commitHash = inPlaceTarget;
    inPlaceRequest.message = "Updated directly in current repository";
    inPlaceRequest.authorName = "In-place Author";
    inPlaceRequest.authorEmail = "in-place-author@example.com";
    inPlaceRequest.committerName = "In-place Committer";
    inPlaceRequest.committerEmail = "in-place-committer@example.com";
    inPlaceRequest.outputDirectory.clear();
    inPlaceRequest.backupBundlePath = backupBundle;

    const CommitRewriteResult inPlaceResult = CommitRewriter::rewrite(inPlaceRequest);
    if (!inPlaceResult.isOk)
    {
        return fail(QString("In-place rewrite failed: %1").arg(inPlaceResult.error));
    }
    if (!QFileInfo::exists(backupBundle) || inPlaceResult.backupBundlePath != backupBundle ||
        inPlaceResult.targetRepository != inPlaceRepository)
    {
        return fail("In-place rewrite did not preserve the recovery bundle");
    }
    if (git(inPlaceRepository, {"rev-parse", "HEAD"}).output == inPlaceOriginalTip)
    {
        return fail("In-place repository tip was not rewritten");
    }
    if (git(inPlaceRepository, {"rev-parse", "side"}).output != inPlaceTarget)
    {
        return fail("In-place rewrite unexpectedly changed another local branch");
    }
    if (git(inPlaceRepository, {"show", "-s", "--format=%s", inPlaceResult.newCommitHash}).output !=
        inPlaceRequest.message)
    {
        return fail("In-place rewritten message did not match requested value");
    }
    if (!git(inPlaceRepository, {"bundle", "verify", backupBundle}).isOk)
    {
        return fail("Recovery bundle verification failed");
    }
    if (!git(temporaryDirectory.path(), {"clone", backupBundle, restoredRepository}).isOk ||
        git(restoredRepository, {"rev-parse", "HEAD"}).output != inPlaceOriginalTip ||
        git(restoredRepository, {"show", "-s", "--format=%s", inPlaceTarget}).output !=
            "In-place target commit")
    {
        return fail("Recovery bundle did not restore the original history");
    }

    QFile trackedFile(QDir(inPlaceRepository).filePath("tracked.txt"));
    if (!trackedFile.open(QIODevice::WriteOnly | QIODevice::Text) ||
        trackedFile.write("tracked baseline\n") < 0)
    {
        return fail("Could not create tracked-file fixture");
    }
    trackedFile.close();
    if (!git(inPlaceRepository, {"add", "--", "tracked.txt"}).isOk ||
        !hasCreatedCommit(inPlaceRepository, "Track worktree fixture"))
    {
        return fail("Could not commit tracked-file fixture");
    }
    if (!trackedFile.open(QIODevice::WriteOnly | QIODevice::Text) ||
        trackedFile.write("unstaged content must remain untouched\n") < 0)
    {
        return fail("Could not create unstaged-change fixture");
    }
    trackedFile.close();

    const QString worktreeAllowedBackup =
        QDir(temporaryDirectory.path()).filePath("worktree-allowed.bundle");
    QFile untrackedFile(QDir(inPlaceRepository).filePath("untracked.txt"));
    if (!untrackedFile.open(QIODevice::WriteOnly | QIODevice::Text) ||
        untrackedFile.write("must remain untouched\n") < 0)
    {
        return fail("Could not create dirty-worktree fixture");
    }
    untrackedFile.close();
    CommitRewriteRequest untrackedRequest = inPlaceRequest;
    untrackedRequest.commitHash = inPlaceResult.newCommitHash;
    untrackedRequest.message = "Rewrite while unstaged worktree changes exist";
    untrackedRequest.backupBundlePath = worktreeAllowedBackup;
    const CommitRewriteResult untrackedResult = CommitRewriter::rewrite(untrackedRequest);
    if (!untrackedResult.isOk || !QFileInfo::exists(worktreeAllowedBackup) ||
        !QFileInfo::exists(untrackedFile.fileName()))
    {
        return fail("Unstaged worktree changes unexpectedly blocked in-place rewrite");
    }
    if (!trackedFile.open(QIODevice::ReadOnly | QIODevice::Text) ||
        trackedFile.readAll() != "unstaged content must remain untouched\n")
    {
        return fail("Unstaged tracked-file content was not preserved");
    }
    trackedFile.close();

    if (!git(inPlaceRepository, {"add", "--", "untracked.txt"}).isOk)
    {
        return fail("Could not create staged-change fixture");
    }
    const QString stagedRejectedBackup =
        QDir(temporaryDirectory.path()).filePath("staged-rejected.bundle");
    const QString tipBeforeRejectedRewrite = git(inPlaceRepository, {"rev-parse", "HEAD"}).output;
    CommitRewriteRequest stagedRequest = inPlaceRequest;
    stagedRequest.commitHash = untrackedResult.newCommitHash;
    stagedRequest.message = "This rewrite must be rejected";
    stagedRequest.backupBundlePath = stagedRejectedBackup;
    const CommitRewriteResult stagedResult = CommitRewriter::rewrite(stagedRequest);
    if (stagedResult.isOk || !stagedResult.error.contains("暂存区") ||
        QFileInfo::exists(stagedRejectedBackup) ||
        git(inPlaceRepository, {"rev-parse", "HEAD"}).output != tipBeforeRejectedRewrite)
    {
        return fail("Staged change was not rejected safely");
    }

    qInfo().noquote()
        << "Safe-copy, in-place, worktree and staged-change integration tests passed";
    std::cout << "main() <<"
              << " result=0" << std::endl;
    return 0;
}
