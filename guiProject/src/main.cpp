#include "main_window.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QIcon>

#include <iostream>

namespace
{

static constexpr int APPLICATION_FONT_PIXEL_SIZE = 14;

} // namespace

// 初始化 Qt 应用、加载样式并进入事件循环。
int main(int argc, char *argv[])
{
    std::cout << "main() >>" << std::endl;

    QApplication app(argc, argv);
    QApplication::setApplicationName("Git-Commit-RewriterGUI");
    QApplication::setOrganizationName("Git-Commit-RewriterGUI");
    QApplication::setWindowIcon(QIcon(":/icons/mainicon.png"));

    QFont font("Microsoft YaHei UI");
    font.setPixelSize(APPLICATION_FONT_PIXEL_SIZE);
    app.setFont(font);

    QFile styleFile(":/styles/styles.qss");
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }

    MainWindow window;
    window.show();

    QString screenshotPath;
    QString editDialogScreenshotPath;
    for (const QString &argument : app.arguments())
    {
        if (argument.startsWith("--screenshot="))
        {
            screenshotPath = argument.mid(QString("--screenshot=").size());
        }
        else if (argument.startsWith("--screenshot-edit-dialog="))
        {
            editDialogScreenshotPath = argument.mid(QString("--screenshot-edit-dialog=").size());
        }
    }
    window.configureScreenshotCapture(screenshotPath, editDialogScreenshotPath);

    const int result = app.exec();
    std::cout << "main() <<"
              << " result=" << result << std::endl;
    return result;
}
