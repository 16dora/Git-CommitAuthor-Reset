#pragma once

#include <QMainWindow>
#include <QMap>
#include <QStringList>
#include <memory>

class QTreeWidgetItem;

namespace Ui {
class MainWindow;
}

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void chooseRepository();
    void reloadRepository();
    void filterCommits(const QString &text);
    void showCommitDetails(QTreeWidgetItem *item);
    void showPrototypeNotice();

private:
    struct GitResult {
        bool ok = false;
        QString output;
        QString error;
    };

    void connectSignals();
    void setRepository(const QString &path);
    void clearRepository(const QString &message);
    void loadBranches();
    void loadCommits();
    void updateStatistics();
    void updateRepositoryBadge(bool valid, const QString &text);
    QString findRepositoryRoot(QString path) const;
    GitResult runGit(const QStringList &arguments) const;

    std::unique_ptr<Ui::MainWindow> ui;
    QString m_repositoryPath;
    QString m_currentBranch;
    QMap<QString, int> m_authorCounts;
};

