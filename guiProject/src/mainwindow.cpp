#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
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
    connect(ui->editCommitButton, &QPushButton::clicked, this, &MainWindow::showPrototypeNotice);
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

    QStringList arguments = {
        "log",
        "--max-count=1000",
        "--date=format:%Y-%m-%d %H:%M",
        "--pretty=format:%H%x1f%h%x1f%s%x1f%an%x1f%ae%x1f%ad%x1f%cn%x1f%ce%x1e"
    };
    arguments << (m_currentBranch.isEmpty() ? "--all" : m_currentBranch);

    const GitResult history = runGit(arguments);
    if (!history.ok) {
        clearRepository(history.error.isEmpty() ? tr("无法读取 Git 提交历史") : history.error);
        return;
    }

    const QStringList records = history.output.split(QChar(0x1e), Qt::SkipEmptyParts);
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
    }

    for (auto it = m_authorCounts.cbegin(); it != m_authorCounts.cend(); ++it) {
        auto *author = new QTreeWidgetItem(ui->authorTree);
        author->setText(0, it.key());
        author->setText(1, QString::number(it.value()));
        author->setTextAlignment(1, Qt::AlignCenter);
    }
    ui->authorTree->sortItems(1, Qt::DescendingOrder);
    ui->commitTree->setSortingEnabled(true);
    ui->commitTree->sortItems(4, Qt::DescendingOrder);
    updateStatistics();
    filterCommits(ui->searchEdit->text());

    if (ui->commitTree->topLevelItemCount() > 0) {
        ui->commitTree->setCurrentItem(ui->commitTree->topLevelItem(0));
    } else {
        showCommitDetails(nullptr);
    }
}

void MainWindow::updateStatistics()
{
    ui->commitCountLabel->setText(QString::number(ui->commitTree->topLevelItemCount()));
    ui->authorCountLabel->setText(QString::number(m_authorCounts.count()));
    ui->repoMetaLabel->setText(tr("%1  ·  %2 位作者  ·  只读分析模式")
                                   .arg(m_currentBranch.isEmpty() ? tr("游离 HEAD") : m_currentBranch)
                                   .arg(m_authorCounts.count()));
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

void MainWindow::showPrototypeNotice()
{
    QMessageBox::information(
        this,
        tr("界面原型"),
        tr("当前版本已完成仓库识别、历史读取、作者汇总和筛选。\n\n"
           "下一步将设计改写方案编辑器，并复用现有脚本的安全副本流程；本原型不会修改任何 Git 历史。"));
}

