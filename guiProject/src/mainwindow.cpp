#include "mainwindow.h"
#include "commitrewriter.h"
#include "editcommitdialog.h"
#include "ui_mainwindow.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QStyle>
#include <QTreeWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(std::make_unique<Ui::MainWindow>())
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
    if (!startupRepository.isEmpty()) {
        setRepository(startupRepository);
    } else {
        clearRepository(tr("选择一个包含 .git 的本地仓库开始分析"));
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::connectSignals()
{
    connect(ui->browseButton, &QPushButton::clicked, this, &MainWindow::chooseRepository);
    connect(ui->reloadButton, &QPushButton::clicked, this, &MainWindow::reloadRepository);
    connect(ui->loadMoreButton, &QPushButton::clicked, this, &MainWindow::loadMoreCommits);
    connect(ui->pathEdit, &QLineEdit::returnPressed, this, [this] {
        setRepository(ui->pathEdit->text().trimmed());
    });
    connect(ui->searchEdit, &QLineEdit::textChanged, this, &MainWindow::filterCommits);
    connect(ui->commitTree, &QTreeWidget::itemSelectionChanged, this, [this] {
        showCommitDetails(ui->commitTree->currentItem());
    });
    connect(ui->branchCombo, &QComboBox::currentTextChanged, this, [this](const QString &branch) {
        if (branch.isEmpty() || branch == m_currentBranch) {
            return;
        }
        m_currentBranch = branch;
        loadCommits();
    });
    connect(ui->editCommitButton, &QPushButton::clicked, this, &MainWindow::editSelectedCommit);
    connect(ui->batchRewriteButton, &QPushButton::clicked, this, &MainWindow::showPrototypeNotice);
}

void MainWindow::chooseRepository()
{
    const QString start = m_repositoryPath.isEmpty() ? QDir::homePath() : m_repositoryPath;
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("选择 Git 仓库"), start, QFileDialog::ShowDirsOnly);
    if (!directory.isEmpty()) {
        setRepository(directory);
    }
}

void MainWindow::reloadRepository()
{
    setRepository(ui->pathEdit->text().trimmed());
}

QString MainWindow::findRepositoryRoot(QString path) const
{
    QDir directory(path);
    if (!directory.exists()) {
        return {};
    }
    while (true) {
        if (QFileInfo::exists(directory.filePath(".git"))) {
            return QDir::cleanPath(directory.absolutePath());
        }
        if (!directory.cdUp()) {
            break;
        }
    }
    return {};
}

void MainWindow::setRepository(const QString &path)
{
    const QString root = findRepositoryRoot(path);
    if (root.isEmpty()) {
        ui->pathEdit->setText(QDir::toNativeSeparators(path));
        clearRepository(tr("所选目录不是 Git 仓库"));
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
}

void MainWindow::clearRepository(const QString &message)
{
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
    ui->commitCountLabel->setText("—");
    ui->authorCountLabel->setText("—");
    ui->branchCountLabel->setText("—");
    ui->reloadButton->setEnabled(false);
    ui->branchCombo->setEnabled(false);
    ui->batchRewriteButton->setEnabled(false);
    ui->editCommitButton->setEnabled(false);
    updateCommitLoadControls();
    updateRepositoryBadge(false, tr("●  等待选择仓库"));
    showCommitDetails(nullptr);
}

MainWindow::GitResult MainWindow::runGit(const QStringList &arguments) const
{
    GitResult result;
    if (m_repositoryPath.isEmpty()) {
        result.error = tr("未选择仓库");
        return result;
    }

    QProcess process;
    QStringList gitArguments;
    gitArguments << "-c" << QString("safe.directory=%1").arg(QDir::fromNativeSeparators(m_repositoryPath));
    gitArguments << "-C" << m_repositoryPath;
    gitArguments << arguments;
    process.start("git", gitArguments);
    if (!process.waitForStarted(3000)) {
        result.error = tr("无法启动 git，请检查 PATH 环境变量");
        return result;
    }
    if (!process.waitForFinished(15000)) {
        process.kill();
        result.error = tr("Git 命令执行超时");
        return result;
    }

    result.output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    result.error = QString::fromUtf8(process.readAllStandardError()).trimmed();
    result.ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    return result;
}

void MainWindow::loadBranches()
{
    const GitResult current = runGit({"branch", "--show-current"});
    const GitResult branches = runGit({"for-each-ref", "--format=%(refname:short)", "refs/heads"});

    ui->branchCombo->blockSignals(true);
    ui->branchCombo->clear();
    if (branches.ok) {
        const QStringList names = branches.output.split('\n', Qt::SkipEmptyParts);
        for (const QString &name : names) {
            ui->branchCombo->addItem(name.trimmed());
        }
    }
    m_currentBranch = current.output.trimmed();
    int branchIndex = ui->branchCombo->findText(m_currentBranch);
    if (branchIndex < 0 && ui->branchCombo->count() > 0) {
        branchIndex = 0;
        m_currentBranch = ui->branchCombo->itemText(0);
    }
    ui->branchCombo->setCurrentIndex(branchIndex);
    ui->branchCombo->blockSignals(false);
    ui->branchCountLabel->setText(QString::number(ui->branchCombo->count()));
}

void MainWindow::loadCommits()
{
    ui->commitTree->setSortingEnabled(false);
    ui->commitTree->clear();
    ui->authorTree->clear();
    m_authorCounts.clear();
    m_loadedCommitCount = 0;
    m_totalCommitCount = 0;

    QStringList countArguments = {"rev-list", "--count"};
    countArguments << (m_currentBranch.isEmpty() ? "--all" : m_currentBranch);
    const GitResult countResult = runGit(countArguments);
    bool countOk = false;
    const qint64 totalCount = countResult.output.toLongLong(&countOk);
    if (!countResult.ok || !countOk || totalCount < 0) {
        clearRepository(countResult.error.isEmpty() ? tr("无法统计 Git 提交历史")
                                                     : countResult.error);
        return;
    }

    m_totalCommitCount = totalCount;
    updateCommitLoadControls();

    if (m_totalCommitCount == 0) {
        ui->commitTree->setSortingEnabled(true);
        updateStatistics();
        showCommitDetails(nullptr);
        return;
    }

    loadMoreCommits();
}

void MainWindow::loadMoreCommits()
{
    if (m_repositoryPath.isEmpty() || m_loadedCommitCount >= m_totalCommitCount) {
        updateCommitLoadControls();
        return;
    }

    ui->loadMoreButton->setEnabled(false);
    ui->loadMoreButton->setText(tr("正在加载…"));
    QApplication::processEvents();

    QStringList arguments = {
        "log",
        QString("--max-count=%1").arg(CommitPageSize),
        QString("--skip=%1").arg(m_loadedCommitCount),
        "--date=format:%Y-%m-%d %H:%M",
        "--pretty=format:%H%x1f%h%x1f%s%x1f%an%x1f%ae%x1f%ad%x1f%cn%x1f%ce%x1e"
    };
    arguments << (m_currentBranch.isEmpty() ? "--all" : m_currentBranch);

    const GitResult history = runGit(arguments);
    if (!history.ok) {
        updateCommitLoadControls();
        const QString message = history.error.isEmpty() ? tr("无法读取更多 Git 提交历史")
                                                        : history.error;
        if (m_loadedCommitCount == 0) {
            clearRepository(message);
        } else {
            QMessageBox::critical(this, tr("加载失败"), message);
        }
        return;
    }

    const bool selectFirstCommit = m_loadedCommitCount == 0;
    ui->commitTree->setSortingEnabled(false);
    const QStringList records = history.output.split(QChar(0x1e), Qt::SkipEmptyParts);
    qint64 appendedCount = 0;
    for (const QString &rawRecord : records) {
        const QString record = rawRecord.trimmed();
        if (record.isEmpty()) {
            continue;
        }
        const QStringList fields = record.split(QChar(0x1f));
        if (fields.size() < 8) {
            continue;
        }

        auto *item = new QTreeWidgetItem(ui->commitTree);
        item->setText(0, fields.at(1));
        item->setText(1, fields.at(2));
        item->setText(2, fields.at(3));
        item->setText(3, fields.at(4));
        item->setText(4, fields.at(5));
        item->setToolTip(1, fields.at(2));
        item->setData(0, Qt::UserRole, fields.at(0));
        item->setData(0, Qt::UserRole + 1, fields.at(6));
        item->setData(0, Qt::UserRole + 2, fields.at(7));
        const QString authorKey = QString("%1 <%2>").arg(fields.at(3), fields.at(4));
        m_authorCounts[authorKey] += 1;
        ++appendedCount;
    }

    m_loadedCommitCount += appendedCount;
    if (appendedCount == 0 && m_loadedCommitCount < m_totalCommitCount) {
        m_totalCommitCount = m_loadedCommitCount;
    }

    rebuildAuthorSummary();
    ui->commitTree->setSortingEnabled(true);
    ui->commitTree->sortItems(4, Qt::DescendingOrder);
    updateStatistics();
    updateCommitLoadControls();
    filterCommits(ui->searchEdit->text());

    if (selectFirstCommit && ui->commitTree->topLevelItemCount() > 0) {
        ui->commitTree->setCurrentItem(ui->commitTree->topLevelItem(0));
    } else if (ui->commitTree->topLevelItemCount() == 0) {
        showCommitDetails(nullptr);
    }
}

void MainWindow::rebuildAuthorSummary()
{
    ui->authorTree->clear();
    for (auto it = m_authorCounts.cbegin(); it != m_authorCounts.cend(); ++it) {
        auto *author = new QTreeWidgetItem(ui->authorTree);
        author->setText(0, it.key());
        author->setText(1, QString::number(it.value()));
        author->setTextAlignment(1, Qt::AlignCenter);
    }
    ui->authorTree->sortItems(1, Qt::DescendingOrder);
}

void MainWindow::updateCommitLoadControls()
{
    if (m_repositoryPath.isEmpty()) {
        ui->loadProgressLabel->setText(tr("尚未加载提交"));
        ui->loadMoreButton->setText(tr("加载更多"));
        ui->loadMoreButton->setEnabled(false);
        return;
    }

    ui->loadProgressLabel->setText(
        tr("已加载 %1 / 共 %2 条提交").arg(m_loadedCommitCount).arg(m_totalCommitCount));
    const bool hasMore = m_loadedCommitCount < m_totalCommitCount;
    ui->loadMoreButton->setEnabled(hasMore);
    if (hasMore) {
        const qint64 remaining = m_totalCommitCount - m_loadedCommitCount;
        ui->loadMoreButton->setText(
            tr("加载更多（%1 条）").arg(qMin<qint64>(CommitPageSize, remaining)));
    } else {
        ui->loadMoreButton->setText(tr("已全部加载"));
    }
}

void MainWindow::updateStatistics()
{
    ui->commitCountLabel->setText(
        tr("%1 / %2").arg(m_loadedCommitCount).arg(m_totalCommitCount));
    ui->authorCountLabel->setText(QString::number(m_authorCounts.count()));
    ui->repoMetaLabel->setText(tr("%1  ·  已加载 %2/%3 条  ·  %4 位作者")
                                   .arg(m_currentBranch.isEmpty() ? tr("游离 HEAD") : m_currentBranch)
                                   .arg(m_loadedCommitCount)
                                   .arg(m_totalCommitCount)
                                   .arg(m_authorCounts.count()));

    if (m_loadedCommitCount < m_totalCommitCount) {
        ui->authorHint->setText(
            tr("当前作者统计基于已加载的 %1/%2 条提交；加载更多后会自动更新。")
                .arg(m_loadedCommitCount)
                .arg(m_totalCommitCount));
    } else {
        ui->authorHint->setText(tr("已统计当前分支的全部提交；可在下一步建立作者 A → B 的批量映射。"));
    }
}

void MainWindow::updateRepositoryBadge(bool valid, const QString &text)
{
    ui->repositoryBadge->setProperty("valid", valid);
    ui->repositoryBadge->setText(text);
    ui->repositoryBadge->style()->unpolish(ui->repositoryBadge);
    ui->repositoryBadge->style()->polish(ui->repositoryBadge);
}

void MainWindow::filterCommits(const QString &text)
{
    const QString needle = text.trimmed();
    for (int row = 0; row < ui->commitTree->topLevelItemCount(); ++row) {
        QTreeWidgetItem *item = ui->commitTree->topLevelItem(row);
        bool matches = needle.isEmpty();
        for (int column = 0; !matches && column < item->columnCount(); ++column) {
            matches = item->text(column).contains(needle, Qt::CaseInsensitive);
        }
        item->setHidden(!matches);
    }
}

void MainWindow::showCommitDetails(QTreeWidgetItem *item)
{
    if (!item) {
        ui->detailSubject->setText(tr("选择一条提交记录"));
        ui->detailHash->setText("—");
        ui->detailAuthor->setText("—");
        ui->detailCommitter->setText("—");
        ui->detailDate->setText("—");
        ui->editCommitButton->setEnabled(false);
        return;
    }

    ui->detailSubject->setText(item->text(1));
    ui->detailHash->setText(item->data(0, Qt::UserRole).toString());
    ui->detailAuthor->setText(QString("%1 <%2>").arg(item->text(2), item->text(3)));
    ui->detailCommitter->setText(QString("%1 <%2>")
                                     .arg(item->data(0, Qt::UserRole + 1).toString(),
                                          item->data(0, Qt::UserRole + 2).toString()));
    ui->detailDate->setText(item->text(4));
    ui->editCommitButton->setEnabled(true);
}

void MainWindow::editSelectedCommit()
{
    QTreeWidgetItem *item = ui->commitTree->currentItem();
    if (!item || m_repositoryPath.isEmpty()) {
        QMessageBox::warning(this, tr("未选择提交"), tr("请先在提交历史中选择一条记录。"));
        return;
    }

    if (m_currentBranch.isEmpty()) {
        QMessageBox::warning(
            this, tr("无法改写"),
            tr("当前仓库处于游离 HEAD 状态。请先切换到一个本地分支，再重新加载仓库。"));
        return;
    }

    const QString commitHash = item->data(0, Qt::UserRole).toString();
    const GitResult messageResult = runGit({"show", "-s", "--format=%B", commitHash});
    if (!messageResult.ok) {
        QMessageBox::critical(
            this, tr("读取提交失败"),
            messageResult.error.isEmpty() ? tr("无法读取选定提交的完整说明。")
                                          : messageResult.error);
        return;
    }

    CommitEditData initialData;
    initialData.hash = commitHash;
    initialData.message = messageResult.output;
    initialData.authorName = item->text(2);
    initialData.authorEmail = item->text(3);
    initialData.committerName = item->data(0, Qt::UserRole + 1).toString();
    initialData.committerEmail = item->data(0, Qt::UserRole + 2).toString();

    EditCommitDialog dialog(m_repositoryPath, initialData, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const CommitEditData editedData = dialog.data();
    const bool unchanged = editedData.message.trimmed() == initialData.message.trimmed()
                           && editedData.authorName == initialData.authorName
                           && editedData.authorEmail == initialData.authorEmail
                           && editedData.committerName == initialData.committerName
                           && editedData.committerEmail == initialData.committerEmail;
    if (unchanged) {
        QMessageBox::information(this, tr("没有需要改写的内容"),
                                 tr("提交说明和身份信息均未发生变化。"));
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
    progress.setWindowTitle(request.mode == RewriteMode::InPlace
                                ? tr("正在修改当前仓库")
                                : tr("正在生成修改副本"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.show();

    const CommitRewriteResult result = CommitRewriter::rewrite(
        request,
        [&progress, &request](int step, int total, const QString &message) {
            progress.setMaximum(total);
            progress.setValue(step);
            progress.setLabelText(message);
            const bool destructiveInPlaceStep = request.mode == RewriteMode::InPlace && step >= 4;
            if (destructiveInPlaceStep) {
                progress.setCancelButton(nullptr);
            }
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            return destructiveInPlaceStep || !progress.wasCanceled();
        });
    progress.close();

    if (result.canceled) {
        QMessageBox::information(this, tr("操作已取消"),
                                 tr("改写操作已取消，未修改原仓库。"));
        return;
    }
    if (!result.ok) {
        QString errorMessage = result.error;
        if (request.mode == RewriteMode::InPlace && !result.backupBundlePath.isEmpty()) {
            const QString nativeBackupPath = QDir::toNativeSeparators(result.backupBundlePath);
            if (!errorMessage.contains(nativeBackupPath, Qt::CaseInsensitive)) {
                errorMessage += tr("\n\n恢复备份：%1").arg(nativeBackupPath);
            }
        }
        QMessageBox::critical(this, tr("改写失败"), errorMessage);
        if (request.mode == RewriteMode::InPlace) {
            setRepository(request.sourceRepository);
        }
        return;
    }

    if (request.mode == RewriteMode::InPlace) {
        const QString successMessage = tr(
            "选定提交已在当前仓库中完成改写并通过验证。\n\n"
            "原提交：%1\n"
            "新提交：%2\n"
            "恢复备份：%3\n\n"
            "远端仓库未修改。如果该分支曾经推送，请先检查新历史，再决定是否强制推送。")
                                           .arg(request.commitHash,
                                                result.newCommitHash,
                                                QDir::toNativeSeparators(result.backupBundlePath));
        setRepository(request.sourceRepository);
        QMessageBox::information(this, tr("当前仓库改写成功"), successMessage);
        return;
    }

    const QString successMessage = tr(
        "选定提交已在新仓库副本中完成改写并通过验证。\n\n"
        "原提交：%1\n"
        "新提交：%2\n"
        "副本目录：%3\n\n"
        "原仓库和远端仓库均未修改。\n\n"
        "是否立即加载新的仓库副本？")
                                       .arg(request.commitHash,
                                            result.newCommitHash,
                                            QDir::toNativeSeparators(request.outputDirectory));
    if (QMessageBox::question(this, tr("改写成功"), successMessage,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::Yes)
        == QMessageBox::Yes) {
        setRepository(request.outputDirectory);
    }
}

void MainWindow::showPrototypeNotice()
{
    QMessageBox::information(
        this,
        tr("界面原型"),
        tr("单条提交修改已经可用。\n\n"
           "作者批量映射功能将在下一步实现；当前按钮不会修改任何 Git 历史。"));
}
