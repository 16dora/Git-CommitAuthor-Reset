#pragma once

#include <QMainWindow>
#include <QMap>
#include <QStringList>
#include <memory>

class QProgressDialog;
class QTreeWidgetItem;
class BatchRewriteDialog;

namespace Ui
{
class MainWindow;
}

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    // 创建主窗口并初始化仓库界面。
    explicit MainWindow(QWidget *parentPtr = nullptr);
    // 使用默认逻辑释放 Qt 子对象和 UI 封装。
    ~MainWindow() override;

    // 根据命令行参数安排主窗口或编辑对话框截图。
    void configureScreenshotCapture(const QString &screenshotPath,
                                    const QString &editDialogScreenshotPath);

private slots:
    // 打开目录选择器并加载 Git 仓库。
    void chooseRepository();
    // 重新加载路径输入框中的 Git 仓库。
    void reloadRepository();
    // 处理路径输入框的回车操作。
    void applyRepositoryPath();
    // 处理提交列表选择变化。
    void handleCommitSelectionChanged();
    // 处理本地分支切换。
    void handleBranchChanged(const QString &branch);
    // 追加加载下一批提交。
    void loadMoreCommits();
    // 按关键字过滤已加载的提交。
    void filterCommits(const QString &text);
    // 显示选中提交的详细信息。
    void showCommitDetails(QTreeWidgetItem *itemPtr);
    // 打开单提交修改流程。
    void editSelectedCommit();
    // 打开批量身份改写窗口并执行已确认的请求。
    void openBatchRewriteDialog();
    // 响应批量窗口请求，分析选中分支的身份影响。
    void analyzeBatchRewrite();
    // 保存主窗口截图并退出应用。
    void captureMainWindowScreenshot();
    // 触发编辑对话框，用于自动截图。
    void openEditDialogForScreenshot();
    // 保存当前模态编辑对话框截图。
    void captureEditDialogScreenshot();

private:
    // Git 命令的同步执行结果。
    struct GitResult
    {
        // 命令是否正常退出。
        bool isOk = false;
        // 标准输出文本。
        QString output;
        // 标准错误或启动失败原因。
        QString error;
    };

    // 建立主窗口控件的信号槽连接。
    void connectSignals();
    // 校验并切换当前仓库。
    void setRepository(const QString &path);
    // 清空仓库状态并显示原因。
    void clearRepository(const QString &message);
    // 加载本地分支及当前分支。
    void loadBranches();
    // 重置并加载当前分支的首页提交。
    void loadCommits();
    // 根据已加载提交重建作者统计。
    void rebuildAuthorSummary();
    // 刷新分页进度与加载按钮状态。
    void updateCommitLoadControls();
    // 刷新仓库、提交和作者统计。
    void updateStatistics();
    // 刷新仓库状态标识。
    void updateRepositoryBadge(bool isValid, const QString &text);
    // 从指定路径向上寻找 Git 仓库根目录。
    QString findRepositoryRoot(QString path) const;
    // 在当前仓库中执行只读 Git 命令。
    GitResult runGit(const QStringList &arguments) const;
    // 同步改写进度对话框并返回是否继续。
    bool canContinueRewrite(int step, int total, const QString &message);

    std::unique_ptr<Ui::MainWindow> ui;
    // 当前已加载的仓库根目录。
    QString m_repositoryPath;
    // 当前界面选中的本地分支。
    QString m_currentBranch;
    // 已加载提交的作者统计。
    QMap<QString, int> m_authorCounts;
    // 已加载的提交数。
    qint64 m_loadedCommitCount = 0;
    // 当前分支的提交总数。
    qint64 m_totalCommitCount = 0;
    // 当前改写进度对话框，仅在改写调用期间有效。
    QProgressDialog *m_rewriteProgressDialogPtr = nullptr;
    // 当前打开的批量改写窗口，仅在模态调用期间有效。
    BatchRewriteDialog *m_batchRewriteDialogPtr = nullptr;
    // 当前是否正在执行原地改写。
    bool m_isInPlaceRewriteActive = false;
    // 命令行指定的主窗口截图路径。
    QString m_screenshotPath;
    // 命令行指定的编辑对话框截图路径。
    QString m_editDialogScreenshotPath;

    static constexpr int COMMIT_PAGE_SIZE = 1000;
    static constexpr int GIT_START_TIMEOUT_MS = 3000;
    static constexpr int GIT_COMMAND_TIMEOUT_MS = 15000;
    static constexpr int MAIN_SCREENSHOT_DELAY_MS = 800;
    static constexpr int EDIT_DIALOG_SCREENSHOT_DELAY_MS = 1400;
};
