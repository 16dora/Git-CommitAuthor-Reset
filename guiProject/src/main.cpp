#include "mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFont>
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
    for (const QString &argument : app.arguments()) {
        if (argument.startsWith("--screenshot=")) {
            screenshotPath = argument.mid(QString("--screenshot=").size());
            break;
        }
    }
    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(800, &window, [&app, &window, screenshotPath] {
            window.grab().save(QDir::toNativeSeparators(screenshotPath));
            app.quit();
        });
    }

    return app.exec();
}
