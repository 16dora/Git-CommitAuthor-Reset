#pragma once

#include "commit_rewriter.h"

#include <QDialog>
#include <memory>

class QButtonGroup;

namespace Ui
{
class EditCommitDialog;
}

// 编辑对话框与主窗口之间的纯数据载体。
struct CommitEditData
{
    // 用户选择的改写模式。
    RewriteMode mode = RewriteMode::SafeCopy;
    // 目标提交的完整哈希。
    QString hash;
    // 目标提交的完整说明。
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
    // 原地改写前的 bundle 备份路径。
    QString backupBundlePath;
};

class EditCommitDialog final : public QDialog
{
    Q_OBJECT

public:
    // 使用当前仓库和提交数据初始化编辑对话框。
    EditCommitDialog(const QString &repositoryPath, const CommitEditData &initialData,
                     QWidget *parentPtr = nullptr);
    // 使用默认逻辑释放 Qt 子对象和 UI 封装。
    ~EditCommitDialog() override;

    // 返回经过界面整理的改写输入。
    CommitEditData data() const;

protected:
    // 校验表单并在高风险模式下执行二次确认。
    void accept() override;

private slots:
    // 选择安全副本的上级目录。
    void chooseOutputParent();
    // 选择原地改写前的 bundle 备份路径。
    void chooseBackupPath();
    // 根据改写模式集中刷新界面状态。
    void updateModeUi();

private:
    std::unique_ptr<Ui::EditCommitDialog> ui;
    // 当前源仓库路径。
    QString m_repositoryPath;
    // 默认安全副本目录名。
    QString m_outputDirectoryName;
    // 默认原地改写备份文件名。
    QString m_backupBundleName;
    // 显式管理两个互斥改写模式按钮。
    QButtonGroup *m_rewriteModeButtonGroupPtr = nullptr;
};
