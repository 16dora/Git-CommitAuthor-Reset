#pragma once

#include <QString>
#include <functional>

enum class RewriteMode {
    SafeCopy,
    InPlace
};

struct CommitRewriteRequest {
    RewriteMode mode = RewriteMode::SafeCopy;
    QString sourceRepository;
    QString branch;
    QString commitHash;
    QString message;
    QString authorName;
    QString authorEmail;
    QString committerName;
    QString committerEmail;
    QString outputDirectory;
    QString backupBundlePath;
};

struct CommitRewriteResult {
    bool ok = false;
    bool canceled = false;
    QString newCommitHash;
    QString targetRepository;
    QString backupBundlePath;
    QString error;
};

class CommitRewriter final
{
public:
    using ProgressCallback = std::function<bool(int step, int total, const QString &message)>;

    static CommitRewriteResult rewrite(const CommitRewriteRequest &request,
                                       const ProgressCallback &progress = {});
};
