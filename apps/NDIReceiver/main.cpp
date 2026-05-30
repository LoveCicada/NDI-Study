#include "MainWindow.h"
#include "NdiContext.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    NdiContext ndi;
    MainWindow window(ndi);
    window.show();
    return app.exec();
}
