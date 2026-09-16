#include "mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QPushButton>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Git Identity Studio");
    QApplication::setOrganizationName("Git Identity Studio");

    QFont font("Microsoft YaHei UI");
    font.setPixelSize(14);
    app.setFont(font);

    QFile styleFile(":/styles/styles.qss");
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }

    MainWindow window;
    window.show();

    QString screenshotPath;
    QString editDialogScreenshotPath;
    for (const QString &argument : app.arguments()) {
        if (argument.startsWith("--screenshot=")) {
            screenshotPath = argument.mid(QString("--screenshot=").size());
        } else if (argument.startsWith("--screenshot-edit-dialog=")) {
            editDialogScreenshotPath =
                argument.mid(QString("--screenshot-edit-dialog=").size());
        }
    }
    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(800, &window, [&app, &window, screenshotPath] {
            window.grab().save(QDir::toNativeSeparators(screenshotPath));
            app.quit();
        });
    }
    if (!editDialogScreenshotPath.isEmpty()) {
        QTimer::singleShot(800, &window, [&window] {
            if (QPushButton *button = window.findChild<QPushButton *>("editCommitButton")) {
                button->click();
            }
        });
        QTimer::singleShot(1400, &app, [&app, editDialogScreenshotPath] {
            if (QWidget *dialog = QApplication::activeModalWidget()) {
                dialog->grab().save(QDir::toNativeSeparators(editDialogScreenshotPath));
                dialog->close();
            }
            app.quit();
        });
    }

    return app.exec();
}
