#include <QApplication>
#include <iostream>

#include "chat_window.h"

int main(int argc, char* argv[]) {
    QApplication a(argc, argv);
    ChatWindow w;
    w.show();
    return a.exec();
}
