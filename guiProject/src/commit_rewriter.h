#pragma once

#include <QString>
#include <functional>

// 提交历史改写的目标模式。
enum class RewriteMode
{
    // 在独立副本中改写历史。
    SafeCopy,
    // 备份后直接改写当前仓库。
    InPlace
};

// 单条提交改写请求的纯数据载体。
struct CommitRewriteRequest
{
    // 改写模式。
    RewriteMode mode = RewriteMode::SafeCopy;
    // 源 Git 仓库根目录。
    QString sourceRepository;
    // 需要改写的本地分支。
    QString branch;
    // 目标提交的完整哈希。
    QString commitHash;
    // 新的完整提交说明。
    QString message;
    // 新作者姓名。
    QString authorName;
    // 新作者邮箱。
    QString authorEmail;
    // 新提交者姓名。
    QString committerName;
    // 新提交者邮箱。
    QString committerEmail;
    // 安全副本的输出目录。
    QString outputDirectory;
    // 原地改写使用的持久 bundle 备份。
    QString backupBundlePath;
};

// 提交改写操作结果的纯数据载体。
struct CommitRewriteResult
{
    // 改写与后置验证是否全部成功。
    bool isOk = false;
    // 操作是否在可取消阶段被用户取消。
    bool isCanceled = false;
    // 目标提交改写后的新哈希。
    QString newCommitHash;
    // 实际被改写的仓库目录。
    QString targetRepository;
    // 原地改写使用的恢复备份路径。
    QString backupBundlePath;
    // 失败原因。
    QString error;
};

// 封装单条提交历史改写的无状态服务。
class CommitRewriter final
{
public:
    using ProgressCallback = std::function<bool(int step, int total, const QString &message)>;

    // 执行安全副本或原地历史改写，并验证新提交。
    static CommitRewriteResult rewrite(const CommitRewriteRequest &request,
                                       const ProgressCallback &progress = {});
};
