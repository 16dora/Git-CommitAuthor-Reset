#include "main_window.h"
#include "commit_rewriter.h"
#include "edit_commit_dialog.h"
#include "ui_mainwindow.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <QWidget>

#include <functional>
#include <iostream>

// 初始化主窗口控件、列表样式和启动仓库。
MainWindow::MainWindow(QWidget *parentPtr)
    : QMainWindow(parentPtr), ui(std::make_unique<Ui::MainWindow>())
{
    ui->setupUi(this);
    connectSignals();

    ui->commitTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->commitTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->commitTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->commitTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    ui->commitTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    ui->authorTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->authorTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    const QString startupRepository = findRepositoryRoot(QDir::currentPath());
    if (!startupRepository.isEmpty())
    {
        setRepository(startupRepository);
    }
    else
    {
        clearRepository(tr("选择一个包含 .git 的本地仓库开始分析"));
    }
}

MainWindow::~MainWindow() = default;

// 根据命令行参数安排自动截图。
void MainWindow::configureScreenshotCapture(const QString &screenshotPath,
                                            const QString &editDialogScreenshotPath)
{
    m_screenshotPath = screenshotPath;
    m_editDialogScreenshotPath = editDialogScreenshotPath;

    if (!m_screenshotPath.isEmpty())
    {
        QTimer::singleShot(MAIN_SCREENSHOT_DELAY_MS, this,
                           &MainWindow::captureMainWindowScreenshot);
    }
    if (!m_editDialogScreenshotPath.isEmpty())
    {
        QTimer::singleShot(MAIN_SCREENSHOT_DELAY_MS, this,
                           &MainWindow::openEditDialogForScreenshot);
        QTimer::singleShot(EDIT_DIALOG_SCREENSHOT_DELAY_MS, this,
                           &MainWindow::captureEditDialogScreenshot);
    }
}

// 建立控件信号与具名槽函数的连接。
void MainWindow::connectSignals()
{
    connect(ui->browseButton, &QPushButton::clicked, this, &MainWindow::chooseRepository);
    connect(ui->reloadButton, &QPushButton::clicked, this, &MainWindow::reloadRepository);
    connect(ui->loadMoreButton, &QPushButton::clicked, this, &MainWindow::loadMoreCommits);
    connect(ui->pathEdit, &QLineEdit::returnPressed, this, &MainWindow::applyRepositoryPath);
    connect(ui->searchEdit, &QLineEdit::textChanged, this, &MainWindow::filterCommits);
    connect(ui->commitTree, &QTreeWidget::itemSelectionChanged, this,
            &MainWindow::handleCommitSelectionChanged);
    connect(ui->branchCombo, &QComboBox::currentTextChanged, this,
            &MainWindow::handleBranchChanged);
    connect(ui->editCommitButton, &QPushButton::clicked, this, &MainWindow::editSelectedCommit);
    connect(ui->batchRewriteButton, &QPushButton::clicked, this, &MainWindow::showPrototypeNotice);
}

// 打开目录选择器并尝试加载选中仓库。
void MainWindow::chooseRepository()
{
    std::cout << "MainWindow::chooseRepository() >>" << std::endl;
    const QString start = m_repositoryPath.isEmpty() ? QDir::homePath() : m_repositoryPath;
    const QString directory = QFileDialog::getExistingDirectory(this, tr("选择 Git 仓库"), start,
                                                                QFileDialog::ShowDirsOnly);
    if (!directory.isEmpty())
    {
        setRepository(directory);
    }
    std::cout << "MainWindow::chooseRepository() <<"
              << " result=" << !directory.isEmpty() << std::endl;
}

// 重新加载路径输入框对应的仓库。
void MainWindow::reloadRepository()
{
    std::cout << "MainWindow::reloadRepository() >>" << std::endl;
    setRepository(ui->pathEdit->text().trimmed());
    std::cout << "[MainWindow] reloadRepository() <<" << std::endl;
}

// 处理路径输入框的回车操作。
void MainWindow::applyRepositoryPath()
{
    setRepository(ui->pathEdit->text().trimmed());
}

// 同步当前选中提交的详情。
void MainWindow::handleCommitSelectionChanged()
{
    showCommitDetails(ui->commitTree->currentItem());
}

// 在本地分支真正变化时重新加载提交。
void MainWindow::handleBranchChanged(const QString &branch)
{
    if (branch.isEmpty() || branch == m_currentBranch)
    {
        return;
    }
    m_currentBranch = branch;
    loadCommits();
}

// 从指定目录向上逐层查找 `.git`。
QString MainWindow::findRepositoryRoot(QString path) const
{
    QDir directory(path);
    if (!directory.exists())
    {
        return {};
    }
    while (true)
    {
        if (QFileInfo::exists(directory.filePath(".git")))
        {
            return QDir::cleanPath(directory.absolutePath());
        }
        if (!directory.cdUp())
        {
            break;
        }
    }
    return {};
}

// 校验路径并切换当前仓库。
void MainWindow::setRepository(const QString &path)
{
    std::cout << "MainWindow::setRepository() >>" << std::endl;
    const QString root = findRepositoryRoot(path);
    if (root.isEmpty())
    {
        ui->pathEdit->setText(QDir::toNativeSeparators(path));
        clearRepository(tr("所选目录不是 Git 仓库"));
        std::cout << "MainWindow::setRepository() <<"
                  << " result=false" << std::endl;
        return;
    }

    m_repositoryPath = root;
    ui->pathEdit->setText(QDir::toNativeSeparators(root));
    ui->repoNameLabel->setText(QFileInfo(root).fileName());
    ui->repoMetaLabel->setText(tr("本地仓库  ·  只读分析模式"));
    updateRepositoryBadge(true, tr("●  仓库已就绪"));
    ui->reloadButton->setEnabled(true);
    ui->branchCombo->setEnabled(true);
    ui->batchRewriteButton->setEnabled(true);
    loadBranches();
    loadCommits();
    const bool isLoaded = !m_repositoryPath.isEmpty();
    std::cout << "MainWindow::setRepository() <<"
              << " result=" << isLoaded << std::endl;
}

// 清空仓库数据并将界面恢复为未加载状态。
void MainWindow::clearRepository(const QString &message)
{
    std::cout << "MainWindow::clearRepository() >>" << std::endl;
    m_repositoryPath.clear();
    m_currentBranch.clear();
    m_authorCounts.clear();
    m_loadedCommitCount = 0;
    m_totalCommitCount = 0;
    ui->commitTree->clear();
    ui->authorTree->clear();
    ui->branchCombo->clear();
    ui->repoNameLabel->setText(tr("尚未选择仓库"));
    ui->repoMetaLabel->setText(message);
    ui->commitCountLabel->setText(tr("—"));
    ui->authorCountLabel->setText(tr("—"));
    ui->branchCountLabel->setText(tr("—"));
    ui->reloadButton->setEnabled(false);
    ui->branchCombo->setEnabled(false);
    ui->batchRewriteButton->setEnabled(false);
    ui->editCommitButton->setEnabled(false);
    updateCommitLoadControls();
    updateRepositoryBadge(false, tr("●  等待选择仓库"));
    showCommitDetails(nullptr);
    std::cout << "[MainWindow] clearRepository() <<" << std::endl;
}

// 在当前仓库中同步执行只读 Git 命令。
MainWindow::GitResult MainWindow::runGit(const QStringList &arguments) const
{
    GitResult result;
    if (m_repositoryPath.isEmpty())
    {
        result.error = tr("未选择仓库");
        return result;
    }

    QProcess process;
    QStringList gitArguments;
    gitArguments << "-c"
                 << QString("safe.directory=%1").arg(QDir::fromNativeSeparators(m_repositoryPath));
    gitArguments << "-C" << m_repositoryPath;
    gitArguments << arguments;
    process.start("git", gitArguments);
    if (!process.waitForStarted(GIT_START_TIMEOUT_MS))
    {
        result.error = tr("无法启动 git，请检查 PATH 环境变量");
        return result;
    }
    if (!process.waitForFinished(GIT_COMMAND_TIMEOUT_MS))
    {
        process.kill();
        result.error = tr("Git 命令执行超时");
        return result;
    }

    result.output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    result.error = QString::fromUtf8(process.readAllStandardError()).trimmed();
    result.isOk = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    return result;
}

// 读取当前分支与全部本地分支。
void MainWindow::loadBranches()
{
    std::cout << "MainWindow::loadBranches() >>" << std::endl;
    const GitResult current = runGit({"branch", "--show-current"});
    const GitResult branches = runGit({"for-each-ref", "--format=%(refname:short)", "refs/heads"});

    ui->branchCombo->blockSignals(true);
    ui->branchCombo->clear();
    if (branches.isOk)
    {
        const QStringList names = branches.output.split('\n', Qt::SkipEmptyParts);
        for (const QString &name : names)
        {
            ui->branchCombo->addItem(name.trimmed());
        }
    }
    m_currentBranch = current.output.trimmed();
    int branchIndex = ui->branchCombo->findText(m_currentBranch);
    if (branchIndex < 0 && ui->branchCombo->count() > 0)
    {
        branchIndex = 0;
        m_currentBranch = ui->branchCombo->itemText(0);
    }
    ui->branchCombo->setCurrentIndex(branchIndex);
    ui->branchCombo->blockSignals(false);
    ui->branchCountLabel->setText(QString::number(ui->branchCombo->count()));
    std::cout << "MainWindow::loadBranches() <<"
              << " result=" << branches.isOk << " branchCount=" << ui->branchCombo->count()
              << std::endl;
}

// 重置分页状态并加载当前分支首页。
void MainWindow::loadCommits()
{
    std::cout << "MainWindow::loadCommits() >>" << std::endl;
    ui->commitTree->setSortingEnabled(false);
    ui->commitTree->clear();
    ui->authorTree->clear();
    m_authorCounts.clear();
    m_loadedCommitCount = 0;
    m_totalCommitCount = 0;

    QStringList countArguments = {"rev-list", "--count"};
    countArguments << (m_currentBranch.isEmpty() ? "--all" : m_currentBranch);
    const GitResult countResult = runGit(countArguments);
    bool isCountValid = false;
    const qint64 totalCount = countResult.output.toLongLong(&isCountValid);
    if (!countResult.isOk || !isCountValid || totalCount < 0)
    {
        clearRepository(countResult.error.isEmpty() ? tr("无法统计 Git 提交历史")
                                                    : countResult.error);
        std::cout << "MainWindow::loadCommits() <<"
                  << " result=false" << std::endl;
        return;
    }

    m_totalCommitCount = totalCount;
    updateCommitLoadControls();

    if (m_totalCommitCount == 0)
    {
        ui->commitTree->setSortingEnabled(true);
        updateStatistics();
        showCommitDetails(nullptr);
        std::cout << "MainWindow::loadCommits() <<"
                  << " result=true totalCommitCount=0" << std::endl;
        return;
    }

    loadMoreCommits();
    const bool isLoaded = !m_repositoryPath.isEmpty() && m_loadedCommitCount > 0;
    std::cout << "MainWindow::loadCommits() <<"
              << " result=" << isLoaded << " totalCommitCount=" << m_totalCommitCount << std::endl;
}

// 使用 skip/max-count 追加加载下一页提交。
void MainWindow::loadMoreCommits()
{
    std::cout << "MainWindow::loadMoreCommits() >>" << std::endl;
    if (m_repositoryPath.isEmpty() || m_loadedCommitCount >= m_totalCommitCount)
    {
        updateCommitLoadControls();
        std::cout << "MainWindow::loadMoreCommits() <<"
                  << " result=false reason=no-more-commits" << std::endl;
        return;
    }

    ui->loadMoreButton->setEnabled(false);
    ui->loadMoreButton->setText(tr("正在加载…"));
    QApplication::processEvents();

    QStringList arguments = {
        "log", QString("--max-count=%1").arg(COMMIT_PAGE_SIZE),
        QString("--skip=%1").arg(m_loadedCommitCount), "--date=format:%Y-%m-%d %H:%M",
        "--pretty=format:%H%x1f%h%x1f%s%x1f%an%x1f%ae%x1f%ad%x1f%cn%x1f%ce%x1e"};
    arguments << (m_currentBranch.isEmpty() ? "--all" : m_currentBranch);

    const GitResult history = runGit(arguments);
    if (!history.isOk)
    {
        updateCommitLoadControls();
        const QString message =
            history.error.isEmpty() ? tr("无法读取更多 Git 提交历史") : history.error;
        if (m_loadedCommitCount == 0)
        {
            clearRepository(message);
        }
        else
        {
            QMessageBox::critical(this, tr("加载失败"), message);
        }
        std::cout << "MainWindow::loadMoreCommits() <<"
                  << " result=false" << std::endl;
        return;
    }

    const bool shouldSelectFirstCommit = m_loadedCommitCount == 0;
    ui->commitTree->setSortingEnabled(false);
    const QStringList records = history.output.split(QChar(0x1e), Qt::SkipEmptyParts);
    qint64 appendedCount = 0;
    for (const QString &rawRecord : records)
    {
        const QString record = rawRecord.trimmed();
        if (record.isEmpty())
        {
            continue;
        }
        const QStringList fields = record.split(QChar(0x1f));
        if (fields.size() < 8)
        {
            continue;
        }

        auto *commitItemPtr = new QTreeWidgetItem(ui->commitTree);
        commitItemPtr->setText(0, fields.at(1));
        commitItemPtr->setText(1, fields.at(2));
        commitItemPtr->setText(2, fields.at(3));
        commitItemPtr->setText(3, fields.at(4));
        commitItemPtr->setText(4, fields.at(5));
        commitItemPtr->setToolTip(1, fields.at(2));
        commitItemPtr->setData(0, Qt::UserRole, fields.at(0));
        commitItemPtr->setData(0, Qt::UserRole + 1, fields.at(6));
        commitItemPtr->setData(0, Qt::UserRole + 2, fields.at(7));
        const QString authorKey = QString("%1 <%2>").arg(fields.at(3), fields.at(4));
        m_authorCounts[authorKey] += 1;
        ++appendedCount;
    }

    m_loadedCommitCount += appendedCount;
    if (appendedCount == 0 && m_loadedCommitCount < m_totalCommitCount)
    {
        m_totalCommitCount = m_loadedCommitCount;
    }

    rebuildAuthorSummary();
    ui->commitTree->setSortingEnabled(true);
    ui->commitTree->sortItems(4, Qt::DescendingOrder);
    updateStatistics();
    updateCommitLoadControls();
    filterCommits(ui->searchEdit->text());

    if (shouldSelectFirstCommit && ui->commitTree->topLevelItemCount() > 0)
    {
        ui->commitTree->setCurrentItem(ui->commitTree->topLevelItem(0));
    }
    else if (ui->commitTree->topLevelItemCount() == 0)
    {
        showCommitDetails(nullptr);
    }
    std::cout << "MainWindow::loadMoreCommits() <<"
              << " result=true"
              << " loadedCommitCount=" << m_loadedCommitCount
              << " totalCommitCount=" << m_totalCommitCount << std::endl;
}

// 根据已加载提交统计作者及提交数。
void MainWindow::rebuildAuthorSummary()
{
    ui->authorTree->clear();
    for (auto it = m_authorCounts.cbegin(); it != m_authorCounts.cend(); ++it)
    {
        auto *authorItemPtr = new QTreeWidgetItem(ui->authorTree);
        authorItemPtr->setText(0, it.key());
        authorItemPtr->setText(1, QString::number(it.value()));
        authorItemPtr->setTextAlignment(1, Qt::AlignCenter);
    }
    ui->authorTree->sortItems(1, Qt::DescendingOrder);
}

// 集中刷新分页进度与加载按钮状态。
void MainWindow::updateCommitLoadControls()
{
    if (m_repositoryPath.isEmpty())
    {
        ui->loadProgressLabel->setText(tr("尚未加载提交"));
        ui->loadMoreButton->setText(tr("加载更多"));
        ui->loadMoreButton->setEnabled(false);
        return;
    }

    ui->loadProgressLabel->setText(
        tr("已加载 %1 / 共 %2 条提交").arg(m_loadedCommitCount).arg(m_totalCommitCount));
    const bool hasMoreCommits = m_loadedCommitCount < m_totalCommitCount;
    ui->loadMoreButton->setEnabled(hasMoreCommits);
    if (hasMoreCommits)
    {
        const qint64 remaining = m_totalCommitCount - m_loadedCommitCount;
        ui->loadMoreButton->setText(
            tr("加载更多（%1 条）").arg(qMin<qint64>(COMMIT_PAGE_SIZE, remaining)));
    }
    else
    {
        ui->loadMoreButton->setText(tr("已全部加载"));
    }
}

// 集中刷新仓库、提交和作者统计。
void MainWindow::updateStatistics()
{
    ui->commitCountLabel->setText(tr("%1 / %2").arg(m_loadedCommitCount).arg(m_totalCommitCount));
    ui->authorCountLabel->setText(QString::number(m_authorCounts.count()));
    ui->repoMetaLabel->setText(
        tr("%1  ·  已加载 %2/%3 条  ·  %4 位作者")
            .arg(m_currentBranch.isEmpty() ? tr("游离 HEAD") : m_currentBranch)
            .arg(m_loadedCommitCount)
            .arg(m_totalCommitCount)
            .arg(m_authorCounts.count()));

    if (m_loadedCommitCount < m_totalCommitCount)
    {
        ui->authorHint->setText(tr("当前作者统计基于已加载的 %1/%2 条提交；加载更多后会自动更新。")
                                    .arg(m_loadedCommitCount)
                                    .arg(m_totalCommitCount));
    }
    else
    {
        ui->authorHint->setText(
            tr("已统计当前分支的全部提交；可在下一步建立作者 A → B 的批量映射。"));
    }
}

// 刷新仓库状态标识及其动态样式。
void MainWindow::updateRepositoryBadge(bool isValid, const QString &text)
{
    ui->repositoryBadge->setProperty("valid", isValid);
    ui->repositoryBadge->setText(text);
    ui->repositoryBadge->style()->unpolish(ui->repositoryBadge);
    ui->repositoryBadge->style()->polish(ui->repositoryBadge);
}

// 在已加载行中执行大小写不敏感过滤。
void MainWindow::filterCommits(const QString &text)
{
    const QString needle = text.trimmed();
    for (int row = 0; row < ui->commitTree->topLevelItemCount(); ++row)
    {
        QTreeWidgetItem *itemPtr = ui->commitTree->topLevelItem(row);
        bool isMatched = needle.isEmpty();
        for (int column = 0; !isMatched && column < itemPtr->columnCount(); ++column)
        {
            isMatched = itemPtr->text(column).contains(needle, Qt::CaseInsensitive);
        }
        itemPtr->setHidden(!isMatched);
    }
}

// 将选中提交的完整身份和时间写入详情区。
void MainWindow::showCommitDetails(QTreeWidgetItem *itemPtr)
{
    if (!itemPtr)
    {
        ui->detailSubject->setText(tr("选择一条提交记录"));
        ui->detailHash->setText(tr("—"));
        ui->detailAuthor->setText(tr("—"));
        ui->detailCommitter->setText(tr("—"));
        ui->detailDate->setText(tr("—"));
        ui->editCommitButton->setEnabled(false);
        return;
    }

    ui->detailSubject->setText(itemPtr->text(1));
    ui->detailHash->setText(itemPtr->data(0, Qt::UserRole).toString());
    ui->detailAuthor->setText(QString("%1 <%2>").arg(itemPtr->text(2), itemPtr->text(3)));
    ui->detailCommitter->setText(
        QString("%1 <%2>").arg(itemPtr->data(0, Qt::UserRole + 1).toString(),
                               itemPtr->data(0, Qt::UserRole + 2).toString()));
    ui->detailDate->setText(itemPtr->text(4));
    ui->editCommitButton->setEnabled(true);
}

// 收集选中提交的修改内容并执行安全改写。
void MainWindow::editSelectedCommit()
{
    std::cout << "MainWindow::editSelectedCommit() >>" << std::endl;
    QTreeWidgetItem *itemPtr = ui->commitTree->currentItem();
    if (!itemPtr || m_repositoryPath.isEmpty())
    {
        QMessageBox::warning(this, tr("未选择提交"), tr("请先在提交历史中选择一条记录。"));
        std::cout << "MainWindow::editSelectedCommit() <<"
                  << " result=false reason=no-selection" << std::endl;
        return;
    }

    if (m_currentBranch.isEmpty())
    {
        QMessageBox::warning(
            this, tr("无法改写"),
            tr("当前仓库处于游离 HEAD 状态。请先切换到一个本地分支，再重新加载仓库。"));
        std::cout << "MainWindow::editSelectedCommit() <<"
                  << " result=false reason=detached-head" << std::endl;
        return;
    }

    const QString commitHash = itemPtr->data(0, Qt::UserRole).toString();
    const GitResult messageResult = runGit({"show", "-s", "--format=%B", commitHash});
    if (!messageResult.isOk)
    {
        QMessageBox::critical(this, tr("读取提交失败"),
                              messageResult.error.isEmpty() ? tr("无法读取选定提交的完整说明。")
                                                            : messageResult.error);
        std::cout << "MainWindow::editSelectedCommit() <<"
                  << " result=false reason=read-message-failed" << std::endl;
        return;
    }

    CommitEditData initialData;
    initialData.hash = commitHash;
    initialData.message = messageResult.output;
    initialData.authorName = itemPtr->text(2);
    initialData.authorEmail = itemPtr->text(3);
    initialData.committerName = itemPtr->data(0, Qt::UserRole + 1).toString();
    initialData.committerEmail = itemPtr->data(0, Qt::UserRole + 2).toString();

    EditCommitDialog dialog(m_repositoryPath, initialData, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        std::cout << "MainWindow::editSelectedCommit() <<"
                  << " result=false reason=user-canceled-dialog" << std::endl;
        return;
    }

    const CommitEditData editedData = dialog.data();
    const bool isUnchanged = editedData.message.trimmed() == initialData.message.trimmed() &&
                             editedData.authorName == initialData.authorName &&
                             editedData.authorEmail == initialData.authorEmail &&
                             editedData.committerName == initialData.committerName &&
                             editedData.committerEmail == initialData.committerEmail;
    if (isUnchanged)
    {
        QMessageBox::information(this, tr("没有需要改写的内容"),
                                 tr("提交说明和身份信息均未发生变化。"));
        std::cout << "MainWindow::editSelectedCommit() <<"
                  << " result=false reason=unchanged" << std::endl;
        return;
    }

    CommitRewriteRequest request;
    request.mode = editedData.mode;
    request.sourceRepository = m_repositoryPath;
    request.branch = m_currentBranch;
    request.commitHash = editedData.hash;
    request.message = editedData.message;
    request.authorName = editedData.authorName;
    request.authorEmail = editedData.authorEmail;
    request.committerName = editedData.committerName;
    request.committerEmail = editedData.committerEmail;
    request.outputDirectory = editedData.outputDirectory;
    request.backupBundlePath = editedData.backupBundlePath;

    QProgressDialog progress(tr("准备改写提交历史…"), tr("取消"), 0, 5, this);
    progress.setWindowTitle(request.mode == RewriteMode::InPlace ? tr("正在修改当前仓库")
                                                                 : tr("正在生成修改副本"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.show();

    m_rewriteProgressDialogPtr = &progress;
    m_isInPlaceRewriteActive = request.mode == RewriteMode::InPlace;
    const CommitRewriter::ProgressCallback progressCallback =
        std::bind(&MainWindow::canContinueRewrite, this, std::placeholders::_1,
                  std::placeholders::_2, std::placeholders::_3);
    const CommitRewriteResult result = CommitRewriter::rewrite(request, progressCallback);
    m_rewriteProgressDialogPtr = nullptr;
    m_isInPlaceRewriteActive = false;
    progress.close();

    if (result.isCanceled)
    {
        QMessageBox::information(this, tr("操作已取消"), tr("改写操作已取消，未修改原仓库。"));
        std::cout << "MainWindow::editSelectedCommit() <<"
                  << " result=false reason=rewrite-canceled" << std::endl;
        return;
    }
    if (!result.isOk)
    {
        QString errorMessage = result.error;
        if (request.mode == RewriteMode::InPlace && !result.backupBundlePath.isEmpty())
        {
            const QString nativeBackupPath = QDir::toNativeSeparators(result.backupBundlePath);
            if (!errorMessage.contains(nativeBackupPath, Qt::CaseInsensitive))
            {
                errorMessage += tr("\n\n恢复备份：%1").arg(nativeBackupPath);
            }
        }
        QMessageBox::critical(this, tr("改写失败"), errorMessage);
        if (request.mode == RewriteMode::InPlace)
        {
            setRepository(request.sourceRepository);
        }
        std::cout << "MainWindow::editSelectedCommit() <<"
                  << " result=false reason=rewrite-failed" << std::endl;
        return;
    }

    if (request.mode == RewriteMode::InPlace)
    {
        const QString successMessage =
            tr("选定提交已在当前仓库中完成改写并通过验证。\n\n"
               "原提交：%1\n"
               "新提交：%2\n"
               "恢复备份：%3\n\n"
               "如果改写链路包含提交签名，失效签名已被移除。\n\n"
               "远端仓库未修改。如果该分支曾经推送，请先检查新历史，再决定是否强制推送。")
                .arg(request.commitHash, result.newCommitHash,
                     QDir::toNativeSeparators(result.backupBundlePath));
        setRepository(request.sourceRepository);
        QMessageBox::information(this, tr("当前仓库改写成功"), successMessage);
        std::cout << "MainWindow::editSelectedCommit() <<"
                  << " result=true mode=in-place" << std::endl;
        return;
    }

    const QString successMessage = tr("选定提交已在新仓库副本中完成改写并通过验证。\n\n"
                                      "原提交：%1\n"
                                      "新提交：%2\n"
                                      "副本目录：%3\n\n"
                                      "如果改写链路包含提交签名，失效签名已被移除。\n\n"
                                      "原仓库和远端仓库均未修改。\n\n"
                                      "是否立即加载新的仓库副本？")
                                       .arg(request.commitHash, result.newCommitHash,
                                            QDir::toNativeSeparators(request.outputDirectory));
    if (QMessageBox::question(this, tr("改写成功"), successMessage,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::Yes) == QMessageBox::Yes)
    {
        setRepository(request.outputDirectory);
    }
    std::cout << "MainWindow::editSelectedCommit() <<"
              << " result=true mode=safe-copy" << std::endl;
}

// 将改写器进度回调同步到模态进度对话框。
bool MainWindow::canContinueRewrite(int step, int total, const QString &message)
{
    if (!m_rewriteProgressDialogPtr)
    {
        return false;
    }

    m_rewriteProgressDialogPtr->setMaximum(total);
    m_rewriteProgressDialogPtr->setValue(step);
    m_rewriteProgressDialogPtr->setLabelText(message);
    const bool isDestructiveInPlaceStep = m_isInPlaceRewriteActive && step >= 4;
    if (isDestructiveInPlaceStep)
    {
        m_rewriteProgressDialogPtr->setCancelButton(nullptr);
    }
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    return isDestructiveInPlaceStep || !m_rewriteProgressDialogPtr->wasCanceled();
}

// 显示批量改写入口的当前实现状态。
void MainWindow::showPrototypeNotice()
{
    QMessageBox::information(this, tr("界面原型"),
                             tr("单条提交修改已经可用。\n\n"
                                "作者批量映射功能将在下一步实现；当前按钮不会修改任何 Git 历史。"));
}

// 保存主窗口截图并结束自动截图流程。
void MainWindow::captureMainWindowScreenshot()
{
    if (!m_screenshotPath.isEmpty())
    {
        grab().save(QDir::toNativeSeparators(m_screenshotPath));
    }
    QApplication::quit();
}

// 为自动截图打开单提交编辑对话框。
void MainWindow::openEditDialogForScreenshot()
{
    ui->editCommitButton->click();
}

// 保存当前模态编辑对话框截图并退出。
void MainWindow::captureEditDialogScreenshot()
{
    QWidget *dialogPtr = QApplication::activeModalWidget();
    if (dialogPtr && !m_editDialogScreenshotPath.isEmpty())
    {
        dialogPtr->grab().save(QDir::toNativeSeparators(m_editDialogScreenshotPath));
        dialogPtr->close();
    }
    QApplication::quit();
}
