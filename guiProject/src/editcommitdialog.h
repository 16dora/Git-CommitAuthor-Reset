#pragma once

#include "commitrewriter.h"

#include <QDialog>
#include <memory>

namespace Ui {
class EditCommitDialog;
}

struct CommitEditData {
    RewriteMode mode = RewriteMode::SafeCopy;
    QString hash;
    QString message;
    QString authorName;
    QString authorEmail;
    QString committerName;
    QString committerEmail;
    QString outputDirectory;
    QString backupBundlePath;
};

class EditCommitDialog final : public QDialog
{
    Q_OBJECT

public:
    EditCommitDialog(const QString &repositoryPath,
                     const CommitEditData &initialData,
                     QWidget *parent = nullptr);
    ~EditCommitDialog() override;

    CommitEditData data() const;

protected:
    void accept() override;

private slots:
    void chooseOutputParent();
    void chooseBackupPath();
    void updateModeUi();

private:
    std::unique_ptr<Ui::EditCommitDialog> ui;
    QString m_repositoryPath;
    QString m_outputDirectoryName;
    QString m_backupBundleName;
};
