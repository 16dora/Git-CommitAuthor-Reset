#include "editcommitdialog.h"
#include "ui_editcommitdialog.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyle>

namespace {

bool isPathInsideDirectory(const QString &path, const QString &directory)
{
    const QString candidate = QDir::fromNativeSeparators(
        QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    const QString base = QDir::fromNativeSeparators(
        QDir::cleanPath(QFileInfo(directory).absoluteFilePath()));
    return candidate.compare(base, Qt::CaseInsensitive) == 0
           || candidate.startsWith(base + '/', Qt::CaseInsensitive);
}

} // namespace

EditCommitDialog::EditCommitDialog(const QString &repositoryPath,
                                   const CommitEditData &initialData,
                                   QWidget *parent)
    : QDialog(parent)
    , ui(std::make_unique<Ui::EditCommitDialog>())
    , m_repositoryPath(QDir::cleanPath(repositoryPath))
{
    ui->setupUi(this);

    ui->hashEdit->setText(initialData.hash);
    ui->messageEdit->setPlainText(initialData.message);
    ui->authorNameEdit->setText(initialData.authorName);
    ui->authorEmailEdit->setText(initialData.authorEmail);
    ui->committerNameEdit->setText(initialData.committerName);
    ui->committerEmailEdit->setText(initialData.committerEmail);

    const QFileInfo repositoryInfo(repositoryPath);
    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    m_outputDirectoryName = QString("%1-commit-rewrite-%2")
                                .arg(repositoryInfo.fileName(), timestamp);
    m_backupBundleName = QString("%1-before-rewrite-%2.bundle")
                             .arg(repositoryInfo.fileName(), timestamp);
    const QString defaultOutput = initialData.outputDirectory.isEmpty()
                                      ? QDir(repositoryInfo.absolutePath()).filePath(m_outputDirectoryName)
                                      : initialData.outputDirectory;
    const QString defaultBackup = initialData.backupBundlePath.isEmpty()
                                      ? QDir(repositoryInfo.absolutePath()).filePath(m_backupBundleName)
                                      : initialData.backupBundlePath;
    ui->outputPathEdit->setText(QDir::toNativeSeparators(defaultOutput));
    ui->outputPathEdit->setCursorPosition(0);
    ui->backupPathEdit->setText(QDir::toNativeSeparators(defaultBackup));
    ui->backupPathEdit->setCursorPosition(0);
    ui->safeCopyRadio->setChecked(initialData.mode == RewriteMode::SafeCopy);
    ui->inPlaceRadio->setChecked(initialData.mode == RewriteMode::InPlace);

    QPushButton *confirmButton = ui->buttonBox->button(QDialogButtonBox::Ok);
    confirmButton->setText(tr("生成修改副本"));
    confirmButton->setObjectName("confirmRewriteButton");
    QPushButton *cancelButton = ui->buttonBox->button(QDialogButtonBox::Cancel);
    cancelButton->setText(tr("取消"));
    cancelButton->setObjectName("cancelRewriteButton");
    for (QPushButton *button : {confirmButton, cancelButton}) {
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    }
    ui->outputPathEdit->setToolTip(ui->outputPathEdit->text());
    ui->messageEdit->setFocus();

    connect(ui->browseOutputButton, &QPushButton::clicked,
            this, &EditCommitDialog::chooseOutputParent);
    connect(ui->browseBackupButton, &QPushButton::clicked,
            this, &EditCommitDialog::chooseBackupPath);
    connect(ui->safeCopyRadio, &QRadioButton::toggled,
            this, &EditCommitDialog::updateModeUi);
    connect(ui->buttonBox, &QDialogButtonBox::accepted,
            this, &EditCommitDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected,
            this, &EditCommitDialog::reject);
    updateModeUi();
}

EditCommitDialog::~EditCommitDialog() = default;

CommitEditData EditCommitDialog::data() const
{
    CommitEditData values;
    values.mode = ui->inPlaceRadio->isChecked() ? RewriteMode::InPlace
                                                 : RewriteMode::SafeCopy;
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

void EditCommitDialog::chooseOutputParent()
{
    const QFileInfo currentOutput(ui->outputPathEdit->text().trimmed());
    const QString startDirectory = currentOutput.absolutePath();
    const QString parentDirectory = QFileDialog::getExistingDirectory(
        this, tr("选择新仓库副本的保存位置"), startDirectory, QFileDialog::ShowDirsOnly);
    if (!parentDirectory.isEmpty()) {
        ui->outputPathEdit->setText(
            QDir::toNativeSeparators(QDir(parentDirectory).filePath(m_outputDirectoryName)));
        ui->outputPathEdit->setCursorPosition(0);
        ui->outputPathEdit->setToolTip(ui->outputPathEdit->text());
    }
}

void EditCommitDialog::chooseBackupPath()
{
    const QFileInfo currentBackup(ui->backupPathEdit->text().trimmed());
    const QString selectedPath = QFileDialog::getSaveFileName(
        this, tr("选择恢复备份文件"), currentBackup.absoluteFilePath(),
        tr("Git Bundle (*.bundle);;所有文件 (*.*)"));
    if (!selectedPath.isEmpty()) {
        ui->backupPathEdit->setText(QDir::toNativeSeparators(selectedPath));
        ui->backupPathEdit->setCursorPosition(0);
        ui->backupPathEdit->setToolTip(ui->backupPathEdit->text());
    }
}

void EditCommitDialog::updateModeUi()
{
    const bool safeCopy = ui->safeCopyRadio->isChecked();
    ui->outputGroup->setVisible(safeCopy);
    ui->backupGroup->setVisible(!safeCopy);

    QPushButton *confirmButton = ui->buttonBox->button(QDialogButtonBox::Ok);
    confirmButton->setText(safeCopy ? tr("生成修改副本") : tr("备份并修改当前仓库"));
    if (safeCopy) {
        ui->dialogSubtitle->setText(tr("编辑提交说明及身份信息，并在独立副本中安全重写历史。"));
        ui->rewriteWarning->setText(
            tr("历史重写会改变该提交及其后续提交的哈希。原仓库不会被修改，也不会自动推送远端。"));
    } else {
        ui->dialogSubtitle->setText(tr("直接重写当前仓库的已检出分支，执行前会生成可恢复的 Git bundle。"));
        ui->rewriteWarning->setText(
            tr("高风险操作：当前分支将立即指向新历史，目标提交及后续提交的哈希都可能改变。仅允许在工作区完全干净时执行。"));
    }
}

void EditCommitDialog::accept()
{
    const CommitEditData values = data();
    const QRegularExpression emailPattern(R"(^[^\s@]+@[^\s@]+$)");

    if (values.message.trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("信息不完整"), tr("提交说明不能为空。"));
        ui->messageEdit->setFocus();
        return;
    }
    if (values.authorName.isEmpty() || !emailPattern.match(values.authorEmail).hasMatch()) {
        QMessageBox::warning(this, tr("作者信息无效"), tr("请填写作者姓名和有效的作者邮箱。"));
        ui->authorNameEdit->setFocus();
        return;
    }
    if (values.committerName.isEmpty() || !emailPattern.match(values.committerEmail).hasMatch()) {
        QMessageBox::warning(this, tr("提交者信息无效"), tr("请填写提交者姓名和有效的提交者邮箱。"));
        ui->committerNameEdit->setFocus();
        return;
    }
    if (values.mode == RewriteMode::SafeCopy) {
        if (values.outputDirectory.isEmpty()) {
            QMessageBox::warning(this, tr("输出目录无效"), tr("请指定新仓库副本的保存目录。"));
            ui->outputPathEdit->setFocus();
            return;
        }
        if (QFileInfo::exists(values.outputDirectory)) {
            QMessageBox::warning(this, tr("输出目录已存在"),
                                 tr("为避免覆盖文件，请选择一个尚不存在的输出目录。"));
            ui->outputPathEdit->setFocus();
            return;
        }
        if (!QFileInfo(values.outputDirectory).absoluteDir().exists()) {
            QMessageBox::warning(this, tr("输出目录无效"), tr("输出目录的上级目录不存在。"));
            ui->outputPathEdit->setFocus();
            return;
        }
        if (isPathInsideDirectory(values.outputDirectory, m_repositoryPath)) {
            QMessageBox::warning(this, tr("输出位置无效"),
                                 tr("新仓库副本不能保存在当前仓库内部。"));
            ui->outputPathEdit->setFocus();
            return;
        }
    } else {
        if (values.backupBundlePath.isEmpty()
            || !QFileInfo(values.backupBundlePath).absoluteDir().exists()) {
            QMessageBox::warning(this, tr("备份路径无效"),
                                 tr("请指定上级目录已存在的 .bundle 备份文件。"));
            ui->backupPathEdit->setFocus();
            return;
        }
        if (QFileInfo::exists(values.backupBundlePath)) {
            QMessageBox::warning(this, tr("备份文件已存在"),
                                 tr("为避免覆盖备份，请指定一个尚不存在的文件。"));
            ui->backupPathEdit->setFocus();
            return;
        }
        if (isPathInsideDirectory(values.backupBundlePath, m_repositoryPath)) {
            QMessageBox::warning(this, tr("备份位置无效"),
                                 tr("备份文件必须保存在当前仓库外部。"));
            ui->backupPathEdit->setFocus();
            return;
        }

        const QString confirmation = tr(
            "即将直接重写当前仓库的已检出分支。\n\n"
            "目标提交及其后续提交的哈希将改变，已推送分支可能需要强制推送。\n"
            "恢复备份：%1\n\n"
            "确定继续吗？")
                                         .arg(QDir::toNativeSeparators(values.backupBundlePath));
        if (QMessageBox::warning(this, tr("确认直接修改"), confirmation,
                                 QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No)
            != QMessageBox::Yes) {
            return;
        }
    }

    QDialog::accept();
}
