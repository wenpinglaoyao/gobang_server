#ifndef GOBANGSERVER_H
#define GOBANGSERVER_H

#include <QWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDebug>

#define MAXCONNCOUNT 50 //服务器能被连接的最大用户数量
enum DataType
{
    GAMESTART0 = 256,
    GAMESTART1,
    USERCONN,
    QUITGAME,
    SERVERCLOSE,
    SERVERFULL,
    TALK,
    BROADCAST,
    TIMEOUT,
    RANKLIST,
    WIN
};
struct UserInfo
{
    QString name;
    short score;
    QTcpSocket* ptrSocket;
    bool read;
};

namespace Ui {
class GobangServer;
}

class GobangServer : public QWidget
{
    Q_OBJECT
public:
    explicit GobangServer(QWidget *parent = 0);
    ~GobangServer();

private:
    int updateInfo(int,int,bool);
    void updateRanklist(int num, bool ascending);
private slots:
    void on_startListenBtn_clicked();

    void slotCreateConn();

    void on_pushButton_clicked();

private:
    Ui::GobangServer *ui;
    QTcpServer _server;
    UserInfo _userInfo[MAXCONNCOUNT];
    qint8 _ranklist[MAXCONNCOUNT];
    int _userCount;
    QString _arrName[MAXCONNCOUNT];
};

#endif // GOBANGSERVER_H
