#pragma once

#include "commit_rewriter.h"

#include <QDialog>
#include <memory>

class QButtonGroup;

namespace Ui
{
class BatchRewriteDialog;
}

// 收集批量身份映射、分支范围和改写模式的专用窗口。
class BatchRewriteDialog final : public QDialog
{
    Q_OBJECT

public:
    // 使用仓库分支和可选的当前作者初始化批量改写窗口。
    BatchRewriteDialog(const QString &repositoryPath, const QStringList &branches,
                       const QString &currentBranch, const QString &initialName,
                       const QString &initialEmail, QWidget *parentPtr = nullptr);
    // 使用默认逻辑释放 Qt 子对象和 UI 封装。
    ~BatchRewriteDialog() override;

    // 返回当前经过整理的批量改写请求。
    BatchRewriteRequest request() const;
    // 显示最新影响分析，并决定是否允许执行。
    void setAnalysisResult(const BatchRewriteAnalysis &analysis);

signals:
    // 请求主窗口执行批量影响分析。
    void analysisRequested();

protected:
    // 校验分析状态和输出路径，高风险模式确认后接受窗口。
    void accept() override;

private slots:
    // 选择安全副本的上级目录。
    void chooseOutputParent();
    // 选择原地改写前的 bundle 备份路径。
    void chooseBackupPath();
    // 勾选全部本地分支。
    void selectAllBranches();
    // 清空全部本地分支选择。
    void clearAllBranches();
    // 使已有分析结果失效。
    void invalidateAnalysis();
    // 根据改写模式刷新路径区域与风险说明。
    void updateModeUi();
    // 将分析操作转交给主窗口。
    void requestAnalysis();

private:
    // 返回当前勾选的本地分支。
    QStringList selectedBranches() const;
    // 批量设置分支复选框状态。
    void setAllBranchesChecked(bool isChecked);
    // 刷新执行按钮是否可用。
    void updateExecuteButton();

    std::unique_ptr<Ui::BatchRewriteDialog> ui;
    // 当前源仓库路径。
    QString m_repositoryPath;
    // 安全副本默认检出的分支。
    QString m_currentBranch;
    // 默认安全副本目录名。
    QString m_outputDirectoryName;
    // 默认原地改写备份文件名。
    QString m_backupBundleName;
    // 显式管理两个互斥改写模式按钮。
    QButtonGroup *m_rewriteModeButtonGroupPtr = nullptr;
    // 最近一次有效影响分析。
    BatchRewriteAnalysis m_analysis;
    // 当前输入是否具有有效分析结果。
    bool m_hasValidAnalysis = false;
    // 批量更新分支勾选状态时抑制重复失效通知。
    bool m_isUpdatingBranchSelection = false;
};
