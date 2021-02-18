#include "gobangserver.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    GobangServer s;
    s.show();

    return a.exec();
}
