#include "edit_commit_dialog.h"
#include "ui_editcommitdialog.h"

#include <QButtonGroup>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStyle>

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

// 填充当前提交数据并生成默认副本与备份路径。
EditCommitDialog::EditCommitDialog(const QString &repositoryPath, const CommitEditData &initialData,
                                   QWidget *parentPtr)
    : QDialog(parentPtr), ui(std::make_unique<Ui::EditCommitDialog>()),
      m_repositoryPath(QDir::cleanPath(repositoryPath))
{
    ui->setupUi(this);

    ui->hashEdit->setText(initialData.hash);
    ui->messageEdit->setPlainText(initialData.message);
    ui->authorNameEdit->setText(initialData.authorName);
    ui->authorEmailEdit->setText(initialData.authorEmail);
    ui->committerNameEdit->setText(initialData.committerName);
    ui->committerEmailEdit->setText(initialData.committerEmail);

    // 邮箱输入框即时约束格式，提交前仍保留完整业务校验与错误提示。
    const QRegularExpression emailPattern(R"(^[^\s@]+@[^\s@]+$)");
    ui->authorEmailEdit->setValidator(
        new QRegularExpressionValidator(emailPattern, ui->authorEmailEdit));
    ui->committerEmailEdit->setValidator(
        new QRegularExpressionValidator(emailPattern, ui->committerEmailEdit));

    const QFileInfo repositoryInfo(repositoryPath);
    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    m_outputDirectoryName =
        QString("%1-commit-rewrite-%2").arg(repositoryInfo.fileName(), timestamp);
    m_backupBundleName =
        QString("%1-before-rewrite-%2.bundle").arg(repositoryInfo.fileName(), timestamp);
    const QString defaultOutput =
        initialData.outputDirectory.isEmpty()
            ? QDir(repositoryInfo.absolutePath()).filePath(m_outputDirectoryName)
            : initialData.outputDirectory;
    const QString defaultBackup =
        initialData.backupBundlePath.isEmpty()
            ? QDir(repositoryInfo.absolutePath()).filePath(m_backupBundleName)
            : initialData.backupBundlePath;
    ui->outputPathEdit->setText(QDir::toNativeSeparators(defaultOutput));
    ui->outputPathEdit->setCursorPosition(0);
    ui->backupPathEdit->setText(QDir::toNativeSeparators(defaultBackup));
    ui->backupPathEdit->setCursorPosition(0);
    ui->safeCopyRadio->setChecked(initialData.mode == RewriteMode::SafeCopy);
    ui->inPlaceRadio->setChecked(initialData.mode == RewriteMode::InPlace);

    m_rewriteModeButtonGroupPtr = new QButtonGroup(this);
    m_rewriteModeButtonGroupPtr->setExclusive(true);
    m_rewriteModeButtonGroupPtr->addButton(ui->safeCopyRadio);
    m_rewriteModeButtonGroupPtr->addButton(ui->inPlaceRadio);

    QPushButton *confirmButtonPtr = ui->buttonBox->button(QDialogButtonBox::Ok);
    confirmButtonPtr->setText(tr("生成修改副本"));
    confirmButtonPtr->setObjectName("confirmRewriteButton");
    QPushButton *cancelButtonPtr = ui->buttonBox->button(QDialogButtonBox::Cancel);
    cancelButtonPtr->setText(tr("取消"));
    cancelButtonPtr->setObjectName("cancelRewriteButton");
    for (QPushButton *buttonPtr : {confirmButtonPtr, cancelButtonPtr})
    {
        buttonPtr->style()->unpolish(buttonPtr);
        buttonPtr->style()->polish(buttonPtr);
        buttonPtr->update();
    }
    ui->outputPathEdit->setToolTip(ui->outputPathEdit->text());
    ui->messageEdit->setFocus();

    connect(ui->browseOutputButton, &QPushButton::clicked, this,
            &EditCommitDialog::chooseOutputParent);
    connect(ui->browseBackupButton, &QPushButton::clicked, this,
            &EditCommitDialog::chooseBackupPath);
    connect(ui->safeCopyRadio, &QRadioButton::toggled, this, &EditCommitDialog::updateModeUi);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &EditCommitDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &EditCommitDialog::reject);
    updateModeUi();
}

EditCommitDialog::~EditCommitDialog() = default;

// 将对话框当前值转换为规范化数据。
CommitEditData EditCommitDialog::data() const
{
    CommitEditData values;
    values.mode = ui->inPlaceRadio->isChecked() ? RewriteMode::InPlace : RewriteMode::SafeCopy;
    values.hash = ui->hashEdit->text().trimmed();
    values.message = ui->messageEdit->toPlainText();
    values.authorName = ui->authorNameEdit->text().trimmed();
    values.authorEmail = ui->authorEmailEdit->text().trimmed();
    values.committerName = ui->committerNameEdit->text().trimmed();
    values.committerEmail = ui->committerEmailEdit->text().trimmed();
    values.outputDirectory = QDir::cleanPath(ui->outputPathEdit->text().trimmed());
    values.backupBundlePath = QDir::cleanPath(ui->backupPathEdit->text().trimmed());
    return values;
}

// 选择安全副本的上级目录。
void EditCommitDialog::chooseOutputParent()
{
    const QFileInfo currentOutput(ui->outputPathEdit->text().trimmed());
    const QString startDirectory = currentOutput.absolutePath();
    const QString parentDirectory = QFileDialog::getExistingDirectory(
        this, tr("选择新仓库副本的保存位置"), startDirectory, QFileDialog::ShowDirsOnly);
    if (!parentDirectory.isEmpty())
    {
        ui->outputPathEdit->setText(
            QDir::toNativeSeparators(QDir(parentDirectory).filePath(m_outputDirectoryName)));
        ui->outputPathEdit->setCursorPosition(0);
        ui->outputPathEdit->setToolTip(ui->outputPathEdit->text());
    }
}

// 选择原地改写使用的 bundle 备份文件。
void EditCommitDialog::chooseBackupPath()
{
    const QFileInfo currentBackup(ui->backupPathEdit->text().trimmed());
    const QString selectedPath =
        QFileDialog::getSaveFileName(this, tr("选择恢复备份文件"), currentBackup.absoluteFilePath(),
                                     tr("Git Bundle (*.bundle);;所有文件 (*.*)"));
    if (!selectedPath.isEmpty())
    {
        ui->backupPathEdit->setText(QDir::toNativeSeparators(selectedPath));
        ui->backupPathEdit->setCursorPosition(0);
        ui->backupPathEdit->setToolTip(ui->backupPathEdit->text());
    }
}

// 根据当前改写模式集中刷新输出组件和风险说明。
void EditCommitDialog::updateModeUi()
{
    const bool isSafeCopy = ui->safeCopyRadio->isChecked();
    ui->outputGroup->setVisible(isSafeCopy);
    ui->backupGroup->setVisible(!isSafeCopy);

    QPushButton *confirmButtonPtr = ui->buttonBox->button(QDialogButtonBox::Ok);
    confirmButtonPtr->setText(isSafeCopy ? tr("生成修改副本") : tr("备份并修改当前仓库"));
    if (isSafeCopy)
    {
        ui->dialogSubtitle->setText(tr("编辑提交说明及身份信息，并在独立副本中安全重写历史。"));
        ui->rewriteWarning->setText(
            tr("历史重写会改变该提交及其后续提交的哈希。原仓库不会被修改，也不会自动推送远端。"));
    }
    else
    {
        ui->dialogSubtitle->setText(
            tr("直接重写当前仓库的已检出分支，执行前会生成可恢复的 Git bundle。"));
        ui->rewriteWarning->setText(tr("高风险操作：当前分支将立即指向新历史，目标提交及后续提交的"
                                       "哈希都可能改变。暂存区存在已 add 的变更时禁止执行；"
                                       "未暂存修改和未跟踪文件不会被备份。"));
    }
}

// 校验所有输入，原地模式确认后才接受对话框。
void EditCommitDialog::accept()
{
    std::cout << "EditCommitDialog::accept() >>" << std::endl;
    const CommitEditData values = data();
    const QRegularExpression emailPattern(R"(^[^\s@]+@[^\s@]+$)");

    if (values.message.trimmed().isEmpty())
    {
        QMessageBox::warning(this, tr("信息不完整"), tr("提交说明不能为空。"));
        ui->messageEdit->setFocus();
        std::cout << "EditCommitDialog::accept() << result=false reason=empty-message" << std::endl;
        return;
    }
    if (values.authorName.isEmpty() || !emailPattern.match(values.authorEmail).hasMatch())
    {
        QMessageBox::warning(this, tr("作者信息无效"), tr("请填写作者姓名和有效的作者邮箱。"));
        ui->authorNameEdit->setFocus();
        std::cout << "EditCommitDialog::accept() << result=false reason=invalid-author"
                  << std::endl;
        return;
    }
    if (values.committerName.isEmpty() || !emailPattern.match(values.committerEmail).hasMatch())
    {
        QMessageBox::warning(this, tr("提交者信息无效"),
                             tr("请填写提交者姓名和有效的提交者邮箱。"));
        ui->committerNameEdit->setFocus();
        std::cout << "EditCommitDialog::accept() << result=false reason=invalid-committer"
                  << std::endl;
        return;
    }
    if (values.mode == RewriteMode::SafeCopy)
    {
        if (values.outputDirectory.isEmpty())
        {
            QMessageBox::warning(this, tr("输出目录无效"), tr("请指定新仓库副本的保存目录。"));
            ui->outputPathEdit->setFocus();
            std::cout << "EditCommitDialog::accept() << result=false reason=empty-output"
                      << std::endl;
            return;
        }
        if (QFileInfo::exists(values.outputDirectory))
        {
            QMessageBox::warning(this, tr("输出目录已存在"),
                                 tr("为避免覆盖文件，请选择一个尚不存在的输出目录。"));
            ui->outputPathEdit->setFocus();
            std::cout << "EditCommitDialog::accept() << result=false reason=output-exists"
                      << std::endl;
            return;
        }
        if (!QFileInfo(values.outputDirectory).absoluteDir().exists())
        {
            QMessageBox::warning(this, tr("输出目录无效"), tr("输出目录的上级目录不存在。"));
            ui->outputPathEdit->setFocus();
            std::cout << "EditCommitDialog::accept() << result=false reason=output-parent-missing"
                      << std::endl;
            return;
        }
        if (isPathInsideDirectory(values.outputDirectory, m_repositoryPath))
        {
            QMessageBox::warning(this, tr("输出位置无效"),
                                 tr("新仓库副本不能保存在当前仓库内部。"));
            ui->outputPathEdit->setFocus();
            std::cout
                << "EditCommitDialog::accept() << result=false reason=output-inside-repository"
                << std::endl;
            return;
        }
    }
    else
    {
        if (values.backupBundlePath.isEmpty() ||
            !QFileInfo(values.backupBundlePath).absoluteDir().exists())
        {
            QMessageBox::warning(this, tr("备份路径无效"),
                                 tr("请指定上级目录已存在的 .bundle 备份文件。"));
            ui->backupPathEdit->setFocus();
            std::cout << "EditCommitDialog::accept() << result=false reason=invalid-backup-parent"
                      << std::endl;
            return;
        }
        if (QFileInfo::exists(values.backupBundlePath))
        {
            QMessageBox::warning(this, tr("备份文件已存在"),
                                 tr("为避免覆盖备份，请指定一个尚不存在的文件。"));
            ui->backupPathEdit->setFocus();
            std::cout << "EditCommitDialog::accept() << result=false reason=backup-exists"
                      << std::endl;
            return;
        }
        if (isPathInsideDirectory(values.backupBundlePath, m_repositoryPath))
        {
            QMessageBox::warning(this, tr("备份位置无效"), tr("备份文件必须保存在当前仓库外部。"));
            ui->backupPathEdit->setFocus();
            std::cout
                << "EditCommitDialog::accept() << result=false reason=backup-inside-repository"
                << std::endl;
            return;
        }

        const QString confirmation =
            tr("即将直接重写当前仓库的已检出分支。\n\n"
               "目标提交及其后续提交的哈希将改变，已推送分支可能需要强制推送。\n"
               "恢复备份：%1\n\n"
               "确定继续吗？")
                .arg(QDir::toNativeSeparators(values.backupBundlePath));
        if (QMessageBox::warning(this, tr("确认直接修改"), confirmation,
                                 QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No) != QMessageBox::Yes)
        {
            std::cout
                << "EditCommitDialog::accept() << result=false reason=user-canceled-confirmation"
                << std::endl;
            return;
        }
    }

    QDialog::accept();
    std::cout << "EditCommitDialog::accept() << result=true" << std::endl;
}
