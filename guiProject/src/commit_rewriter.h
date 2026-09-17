#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
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

// 单个分支的批量身份匹配统计。
struct BatchBranchImpact
{
    // 本地分支名称。
    QString branch;
    // 作者字段匹配数量。
    qint64 authorMatchCount = 0;
    // 提交者字段匹配数量。
    qint64 committerMatchCount = 0;
    // 至少命中一个启用字段的去重提交数量。
    qint64 uniqueCommitCount = 0;
};

// 批量身份改写请求的纯数据载体。
struct BatchRewriteRequest
{
    // 改写模式。
    RewriteMode mode = RewriteMode::SafeCopy;
    // 源 Git 仓库根目录。
    QString sourceRepository;
    // 安全副本默认检出的本地分支。
    QString checkoutBranch;
    // 需要改写的本地分支。
    QStringList branches;
    // 原身份姓名。
    QString oldName;
    // 原身份邮箱。
    QString oldEmail;
    // 新身份姓名。
    QString newName;
    // 新身份邮箱。
    QString newEmail;
    // 是否替换作者字段。
    bool isAuthorRewriteEnabled = true;
    // 是否替换提交者字段。
    bool isCommitterRewriteEnabled = true;
    // 安全副本的输出目录。
    QString outputDirectory;
    // 原地改写使用的持久 bundle 备份。
    QString backupBundlePath;
    // 分析阶段记录的分支顶端，用于执行前防止历史漂移。
    QMap<QString, QString> expectedBranchTips;
    // 分析阶段确认的去重匹配提交。
    QStringList matchingCommitHashes;
};

// 批量身份改写的影响分析结果。
struct BatchRewriteAnalysis
{
    // 分析是否成功。
    bool isOk = false;
    // 分析是否被用户取消。
    bool isCanceled = false;
    // 作者字段匹配数量。
    qint64 authorMatchCount = 0;
    // 提交者字段匹配数量。
    qint64 committerMatchCount = 0;
    // 至少命中一个启用字段的去重提交数量。
    qint64 uniqueCommitCount = 0;
    // 每个选中分支的匹配统计。
    QList<BatchBranchImpact> branchImpacts;
    // 分析时的分支顶端。
    QMap<QString, QString> branchTips;
    // 去重后的匹配提交哈希。
    QStringList matchingCommitHashes;
    // 失败原因。
    QString error;
};

// 批量身份改写结果的纯数据载体。
struct BatchRewriteResult
{
    // 改写与后置验证是否全部成功。
    bool isOk = false;
    // 操作是否在可取消阶段被用户取消。
    bool isCanceled = false;
    // 身份字段发生变化的去重提交数量。
    qint64 matchedCommitCount = 0;
    // 因身份或父提交变化而重建的提交数量。
    qint64 rebuiltCommitCount = 0;
    // 实际更新的本地分支。
    QStringList rewrittenBranches;
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
    // 分析选中分支中的批量身份匹配范围。
    static BatchRewriteAnalysis analyzeBatch(const BatchRewriteRequest &request,
                                             const ProgressCallback &progress = {});
    // 执行多分支批量身份改写，并验证旧身份已被替换。
    static BatchRewriteResult rewriteBatch(const BatchRewriteRequest &request,
                                           const ProgressCallback &progress = {});
};
