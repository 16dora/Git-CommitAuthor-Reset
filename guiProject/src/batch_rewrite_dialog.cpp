#include "batch_rewrite_dialog.h"
#include "ui_batchrewritedialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <iostream>

namespace
{

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

} // namespace

// 构造批量身份映射窗口并填充本地分支。
BatchRewriteDialog::BatchRewriteDialog(const QString &repositoryPath, const QStringList &branches,
                                       const QString &currentBranch, const QString &initialName,
                                       const QString &initialEmail, QWidget *parentPtr)
    : QDialog(parentPtr), ui(std::make_unique<Ui::BatchRewriteDialog>()),
      m_repositoryPath(QDir::cleanPath(repositoryPath)), m_currentBranch(currentBranch)
{
    ui->setupUi(this);
    ui->le_oldName->setText(initialName);
    ui->le_oldEmail->setText(initialEmail);

    const QRegularExpression emailPattern(R"(^[^\s@]+@[^\s@]+$)");
    ui->le_oldEmail->setValidator(
        new QRegularExpressionValidator(emailPattern, ui->le_oldEmail));
    ui->le_newEmail->setValidator(
        new QRegularExpressionValidator(emailPattern, ui->le_newEmail));

    ui->table_branches->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->table_branches->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->table_analysis->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < ui->table_analysis->columnCount(); ++column)
    {
        ui->table_analysis->horizontalHeader()->setSectionResizeMode(
            column, QHeaderView::ResizeToContents);
    }

    m_isUpdatingBranchSelection = true;
    ui->table_branches->setRowCount(branches.size());
    for (qsizetype row = 0; row < branches.size(); ++row)
    {
        auto *branchItemPtr = new QTableWidgetItem(branches.at(row));
        branchItemPtr->setFlags(Qt::ItemIsEnabled);
        ui->table_branches->setItem(row, 0, branchItemPtr);

        auto *checkItemPtr = new QTableWidgetItem();
        checkItemPtr->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        checkItemPtr->setCheckState(branches.at(row) == currentBranch ? Qt::Checked
                                                                     : Qt::Unchecked);
        checkItemPtr->setTextAlignment(Qt::AlignCenter);
        ui->table_branches->setItem(row, 1, checkItemPtr);
    }
    m_isUpdatingBranchSelection = false;

    const QFileInfo repositoryInfo(repositoryPath);
    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    m_outputDirectoryName =
        QString("%1-batch-rewrite-%2").arg(repositoryInfo.fileName(), timestamp);
    m_backupBundleName =
        QString("%1-before-batch-rewrite-%2.bundle").arg(repositoryInfo.fileName(), timestamp);
    ui->le_outputPath->setText(QDir::toNativeSeparators(
        QDir(repositoryInfo.absolutePath()).filePath(m_outputDirectoryName)));
    ui->le_backupPath->setText(QDir::toNativeSeparators(
        QDir(repositoryInfo.absolutePath()).filePath(m_backupBundleName)));

    m_rewriteModeButtonGroupPtr = new QButtonGroup(this);
    m_rewriteModeButtonGroupPtr->setExclusive(true);
    m_rewriteModeButtonGroupPtr->addButton(ui->rbtn_safeCopy);
    m_rewriteModeButtonGroupPtr->addButton(ui->rbtn_inPlace);

    QPushButton *executeButtonPtr = ui->buttonBox->button(QDialogButtonBox::Ok);
    executeButtonPtr->setText(tr("执行批量改写"));
    executeButtonPtr->setObjectName("btn_executeBatchRewrite");
    QPushButton *cancelButtonPtr = ui->buttonBox->button(QDialogButtonBox::Cancel);
    cancelButtonPtr->setText(tr("取消"));
    cancelButtonPtr->setObjectName("btn_cancelBatchRewrite");

    connect(ui->btn_browseOutput, &QPushButton::clicked, this,
            &BatchRewriteDialog::chooseOutputParent);
    connect(ui->btn_browseBackup, &QPushButton::clicked, this,
            &BatchRewriteDialog::chooseBackupPath);
    connect(ui->btn_selectAllBranches, &QPushButton::clicked, this,
            &BatchRewriteDialog::selectAllBranches);
    connect(ui->btn_clearBranches, &QPushButton::clicked, this,
            &BatchRewriteDialog::clearAllBranches);
    connect(ui->btn_analyze, &QPushButton::clicked, this,
            &BatchRewriteDialog::requestAnalysis);
    connect(ui->rbtn_safeCopy, &QRadioButton::toggled, this,
            &BatchRewriteDialog::updateModeUi);
    connect(ui->le_oldName, &QLineEdit::textChanged, this,
            &BatchRewriteDialog::invalidateAnalysis);
    connect(ui->le_oldEmail, &QLineEdit::textChanged, this,
            &BatchRewriteDialog::invalidateAnalysis);
    connect(ui->le_newName, &QLineEdit::textChanged, this,
            &BatchRewriteDialog::invalidateAnalysis);
    connect(ui->le_newEmail, &QLineEdit::textChanged, this,
            &BatchRewriteDialog::invalidateAnalysis);
    connect(ui->check_rewriteAuthor, &QCheckBox::toggled, this,
            &BatchRewriteDialog::invalidateAnalysis);
    connect(ui->check_rewriteCommitter, &QCheckBox::toggled, this,
            &BatchRewriteDialog::invalidateAnalysis);
    connect(ui->table_branches, &QTableWidget::itemChanged, this,
            &BatchRewriteDialog::invalidateAnalysis);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &BatchRewriteDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &BatchRewriteDialog::reject);

    updateModeUi();
    invalidateAnalysis();
    ui->le_oldName->setFocus();
}

BatchRewriteDialog::~BatchRewriteDialog() = default;

// 汇总窗口当前输入和最近一次有效分析状态。
BatchRewriteRequest BatchRewriteDialog::request() const
{
    BatchRewriteRequest values;
    values.mode = ui->rbtn_inPlace->isChecked() ? RewriteMode::InPlace : RewriteMode::SafeCopy;
    values.sourceRepository = m_repositoryPath;
    values.checkoutBranch = m_currentBranch;
    values.branches = selectedBranches();
    values.oldName = ui->le_oldName->text().trimmed();
    values.oldEmail = ui->le_oldEmail->text().trimmed();
    values.newName = ui->le_newName->text().trimmed();
    values.newEmail = ui->le_newEmail->text().trimmed();
    values.isAuthorRewriteEnabled = ui->check_rewriteAuthor->isChecked();
    values.isCommitterRewriteEnabled = ui->check_rewriteCommitter->isChecked();
    values.outputDirectory = QDir::cleanPath(ui->le_outputPath->text().trimmed());
    values.backupBundlePath = QDir::cleanPath(ui->le_backupPath->text().trimmed());
    if (m_hasValidAnalysis)
    {
        values.expectedBranchTips = m_analysis.branchTips;
        values.matchingCommitHashes = m_analysis.matchingCommitHashes;
    }
    return values;
}

// 展示总量和逐分支影响，并锁定本轮分析结果。
void BatchRewriteDialog::setAnalysisResult(const BatchRewriteAnalysis &analysis)
{
    m_analysis = analysis;
    m_hasValidAnalysis = analysis.isOk && analysis.uniqueCommitCount > 0;
    ui->table_analysis->setRowCount(0);

    if (analysis.isCanceled)
    {
        ui->label_analysisSummary->setText(tr("影响分析已取消。"));
        updateExecuteButton();
        return;
    }
    if (!analysis.isOk)
    {
        ui->label_analysisSummary->setText(tr("分析失败：%1").arg(analysis.error));
        QMessageBox::critical(this, tr("影响分析失败"), analysis.error);
        updateExecuteButton();
        return;
    }

    ui->label_analysisSummary->setText(
        tr("作者命中 %1 次，提交者命中 %2 次，共影响 %3 条去重提交。")
            .arg(analysis.authorMatchCount)
            .arg(analysis.committerMatchCount)
            .arg(analysis.uniqueCommitCount));
    ui->table_analysis->setRowCount(analysis.branchImpacts.size());
    for (qsizetype row = 0; row < analysis.branchImpacts.size(); ++row)
    {
        const BatchBranchImpact &impact = analysis.branchImpacts.at(row);
        ui->table_analysis->setItem(row, 0, new QTableWidgetItem(impact.branch));
        ui->table_analysis->setItem(
            row, 1, new QTableWidgetItem(QString::number(impact.authorMatchCount)));
        ui->table_analysis->setItem(
            row, 2, new QTableWidgetItem(QString::number(impact.committerMatchCount)));
        ui->table_analysis->setItem(
            row, 3, new QTableWidgetItem(QString::number(impact.uniqueCommitCount)));
        for (int column = 1; column < ui->table_analysis->columnCount(); ++column)
        {
            ui->table_analysis->item(row, column)->setTextAlignment(Qt::AlignCenter);
        }
    }
    if (analysis.uniqueCommitCount == 0)
    {
        ui->label_analysisSummary->setText(
            tr("选中分支中没有找到完全匹配的作者或提交者身份。"));
    }
    updateExecuteButton();
}

// 选择安全副本的上级目录。
void BatchRewriteDialog::chooseOutputParent()
{
    const QFileInfo currentOutput(ui->le_outputPath->text().trimmed());
    const QString parentDirectory = QFileDialog::getExistingDirectory(
        this, tr("选择完整仓库副本的保存位置"), currentOutput.absolutePath(),
        QFileDialog::ShowDirsOnly);
    if (!parentDirectory.isEmpty())
    {
        ui->le_outputPath->setText(
            QDir::toNativeSeparators(QDir(parentDirectory).filePath(m_outputDirectoryName)));
    }
}

// 选择原地改写使用的 bundle 备份文件。
void BatchRewriteDialog::chooseBackupPath()
{
    const QFileInfo currentBackup(ui->le_backupPath->text().trimmed());
    const QString selectedPath =
        QFileDialog::getSaveFileName(this, tr("选择完整恢复备份文件"),
                                     currentBackup.absoluteFilePath(),
                                     tr("Git Bundle (*.bundle);;所有文件 (*.*)"));
    if (!selectedPath.isEmpty())
    {
        ui->le_backupPath->setText(QDir::toNativeSeparators(selectedPath));
    }
}

// 勾选分支列表中的全部项目。
void BatchRewriteDialog::selectAllBranches()
{
    setAllBranchesChecked(true);
}

// 清空分支列表中的全部勾选。
void BatchRewriteDialog::clearAllBranches()
{
    setAllBranchesChecked(false);
}

// 清除旧分析，防止使用已过期的匹配范围执行改写。
void BatchRewriteDialog::invalidateAnalysis()
{
    if (m_isUpdatingBranchSelection)
    {
        return;
    }
    m_analysis = {};
    m_hasValidAnalysis = false;
    ui->table_analysis->setRowCount(0);
    ui->label_analysisSummary->setText(
        tr("尚未分析。修改身份或分支选择后需要重新分析。"));
    updateExecuteButton();
}

// 切换安全副本和原地修改相关组件。
void BatchRewriteDialog::updateModeUi()
{
    const bool isSafeCopy = ui->rbtn_safeCopy->isChecked();
    ui->group_output->setVisible(isSafeCopy);
    ui->group_backup->setVisible(!isSafeCopy);
    ui->label_warning->setText(
        isSafeCopy
            ? tr("必须先分析影响范围。副本会保留全部本地分支，只改写勾选的分支。")
            : tr("高风险操作：将直接更新选中的本地分支。暂存区存在已 add 的变更时禁止执行；"
                 "未暂存修改和未跟踪文件不会被 bundle 备份。"));
}

// 通知主窗口执行只读的影响分析。
void BatchRewriteDialog::requestAnalysis()
{
    const BatchRewriteRequest values = request();
    const QRegularExpression emailPattern(R"(^[^\s@]+@[^\s@]+$)");
    if (values.branches.isEmpty())
    {
        QMessageBox::warning(this, tr("未选择分支"), tr("请至少选择一个本地分支。"));
        return;
    }
    if (!values.isAuthorRewriteEnabled && !values.isCommitterRewriteEnabled)
    {
        QMessageBox::warning(this, tr("未选择字段"),
                             tr("请至少勾选“修改作者”或“修改提交者”。"));
        return;
    }
    if (values.oldName.isEmpty() || values.newName.isEmpty() ||
        !emailPattern.match(values.oldEmail).hasMatch() ||
        !emailPattern.match(values.newEmail).hasMatch())
    {
        QMessageBox::warning(this, tr("身份信息无效"),
                             tr("请填写原身份和新身份的姓名及有效邮箱。"));
        return;
    }
    if (values.oldName == values.newName && values.oldEmail == values.newEmail)
    {
        QMessageBox::warning(this, tr("身份没有变化"),
                             tr("原身份与新身份不能完全相同。"));
        return;
    }
    emit analysisRequested();
}

// 校验已分析请求和输出位置，并确认原地改写。
void BatchRewriteDialog::accept()
{
    std::cout << "BatchRewriteDialog::accept() >>" << std::endl;
    const BatchRewriteRequest values = request();
    const QRegularExpression emailPattern(R"(^[^\s@]+@[^\s@]+$)");
    if (values.branches.isEmpty())
    {
        QMessageBox::warning(this, tr("未选择分支"), tr("请至少选择一个本地分支。"));
        std::cout << "BatchRewriteDialog::accept() << result=false reason=no-branch"
                  << std::endl;
        return;
    }
    if (!values.isAuthorRewriteEnabled && !values.isCommitterRewriteEnabled)
    {
        QMessageBox::warning(this, tr("未选择字段"),
                             tr("请至少勾选“修改作者”或“修改提交者”。"));
        std::cout << "BatchRewriteDialog::accept() << result=false reason=no-field"
                  << std::endl;
        return;
    }
    if (values.oldName.isEmpty() || values.newName.isEmpty() ||
        !emailPattern.match(values.oldEmail).hasMatch() ||
        !emailPattern.match(values.newEmail).hasMatch())
    {
        QMessageBox::warning(this, tr("身份信息无效"),
                             tr("请填写原身份和新身份的姓名及有效邮箱。"));
        std::cout << "BatchRewriteDialog::accept() << result=false reason=invalid-identity"
                  << std::endl;
        return;
    }
    if (!m_hasValidAnalysis)
    {
        QMessageBox::warning(this, tr("需要重新分析"),
                             tr("请先完成影响分析，并确保至少命中一条提交。"));
        std::cout << "BatchRewriteDialog::accept() << result=false reason=no-analysis"
                  << std::endl;
        return;
    }

    if (values.mode == RewriteMode::SafeCopy)
    {
        if (values.outputDirectory.isEmpty() || QFileInfo::exists(values.outputDirectory) ||
            !QFileInfo(values.outputDirectory).absoluteDir().exists() ||
            isPathInsideDirectory(values.outputDirectory, m_repositoryPath))
        {
            QMessageBox::warning(this, tr("输出目录无效"),
                                 tr("请选择仓库外部、上级目录已存在且当前尚不存在的输出目录。"));
            std::cout << "BatchRewriteDialog::accept() << result=false reason=invalid-output"
                      << std::endl;
            return;
        }
    }
    else
    {
        if (values.backupBundlePath.isEmpty() || QFileInfo::exists(values.backupBundlePath) ||
            !QFileInfo(values.backupBundlePath).absoluteDir().exists() ||
            isPathInsideDirectory(values.backupBundlePath, m_repositoryPath))
        {
            QMessageBox::warning(this, tr("备份路径无效"),
                                 tr("请选择仓库外部、上级目录已存在且当前尚不存在的 bundle 文件。"));
            std::cout << "BatchRewriteDialog::accept() << result=false reason=invalid-backup"
                      << std::endl;
            return;
        }

        const QString confirmation =
            tr("即将直接修改当前仓库中的 %1 个本地分支。\n\n"
               "匹配提交：%2\n"
               "恢复备份：%3\n\n"
               "所有选中分支将在同一引用事务中更新。确定继续吗？")
                .arg(values.branches.size())
                .arg(m_analysis.uniqueCommitCount)
                .arg(QDir::toNativeSeparators(values.backupBundlePath));
        if (QMessageBox::warning(this, tr("确认批量修改"), confirmation,
                                 QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No) != QMessageBox::Yes)
        {
            std::cout << "BatchRewriteDialog::accept() <<"
                      << " result=false reason=user-canceled-confirmation" << std::endl;
            return;
        }
    }

    QDialog::accept();
    std::cout << "BatchRewriteDialog::accept() << result=true" << std::endl;
}

// 读取分支表格中的所有勾选项。
QStringList BatchRewriteDialog::selectedBranches() const
{
    QStringList branches;
    for (int row = 0; row < ui->table_branches->rowCount(); ++row)
    {
        const QTableWidgetItem *branchItemPtr = ui->table_branches->item(row, 0);
        const QTableWidgetItem *checkItemPtr = ui->table_branches->item(row, 1);
        if (branchItemPtr && checkItemPtr && checkItemPtr->checkState() == Qt::Checked)
        {
            branches.append(branchItemPtr->text());
        }
    }
    return branches;
}

// 统一设置分支列表复选框，最后只触发一次分析失效。
void BatchRewriteDialog::setAllBranchesChecked(bool isChecked)
{
    m_isUpdatingBranchSelection = true;
    for (int row = 0; row < ui->table_branches->rowCount(); ++row)
    {
        QTableWidgetItem *checkItemPtr = ui->table_branches->item(row, 1);
        if (checkItemPtr)
        {
            checkItemPtr->setCheckState(isChecked ? Qt::Checked : Qt::Unchecked);
        }
    }
    m_isUpdatingBranchSelection = false;
    invalidateAnalysis();
}

// 仅在具有非空有效分析时允许执行批量改写。
void BatchRewriteDialog::updateExecuteButton()
{
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(m_hasValidAnalysis);
}
