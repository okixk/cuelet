#include "ui/MainWindow.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("Cuelet");
    QApplication::setOrganizationDomain("cuelet.local");
    QApplication::setApplicationName("Cuelet");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setWindowIcon(QIcon(":/resources/app_icon.svg"));

    MainWindow window;
    window.show();

    return QApplication::exec();
}
