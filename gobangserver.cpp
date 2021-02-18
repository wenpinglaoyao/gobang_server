#include "gobangserver.h"
#include "ui_gobangserver.h"
#include <QTableWidgetItem>

GobangServer::GobangServer(QWidget *parent) :
    QWidget(parent), _userCount(0),
    ui(new Ui::GobangServer)
{
    ui->setupUi(this);
    setWindowTitle(tr("五子棋服务器端"));
    QRegExp regIP = QRegExp("([1-9]|[1-9]\\d|1\\d{2}|2[0-4]\\d|25[0-5])(\\.(\\d|[1-9]\\d|1\\d{2}|2[0-4]\\d|25[0-5])){3}");
    ui->IPLE->setValidator(new QRegExpValidator(regIP,this));
    QRegExp regPort = QRegExp("([1-9]\\d{3})|(65[1-4]\\d{2})");
    ui->portLE->setValidator(new QRegExpValidator(regPort,this));
    ui->ranklist->setRowCount(MAXCONNCOUNT);
    for(int i=0; i<MAXCONNCOUNT; i++)
    {
         _arrName[i] = "";
         _userInfo[i].name = QString::number(i+1);
         _userInfo[i].read = _userInfo[i].score = 0;
         _userInfo[i].ptrSocket = nullptr;
         _ranklist[i] = i;
    }
}

GobangServer::~GobangServer()
{
    delete ui;
}

void GobangServer::on_startListenBtn_clicked()
{
    if(_server.listen(QHostAddress::Any,ui->portLE->text().toInt()))
    {
        qDebug()<<"服务器成功的启动了监听";
        ui->startListenBtn->setEnabled(false);
    }
    connect(&_server,&QTcpServer::newConnection,  this,&GobangServer::slotCreateConn);
}

void GobangServer::slotCreateConn()
{
    QTcpSocket* socket = _server.nextPendingConnection();
    if(_userCount >= MAXCONNCOUNT)
    {
        QByteArray buf;
        QDataStream out(&buf,QIODevice::WriteOnly);
        out<<SERVERFULL;
        socket->write(buf);
        return;
    }

    for(int i=0; i<MAXCONNCOUNT; i++)
    {
        if(nullptr == _userInfo[i].ptrSocket)//如果哪个套接字是闲置的，那么把这个socket分配给该套接字
        {
            _userInfo[i].ptrSocket = socket;
            _userInfo[i].read = false;
            _userCount++;

            QByteArray buf;
            QDataStream out(&buf,QIODevice::WriteOnly);
            out<<USERCONN<<_userInfo[i].name.toInt();
            _userInfo[i].ptrSocket->write(buf);

            connect(_userInfo[i].ptrSocket,&QTcpSocket::readyRead,[this,i](){
                while(_userInfo[i].ptrSocket && _userInfo[i].ptrSocket->bytesAvailable())
                {
                    QDataStream in;
                    in.setDevice(_userInfo[i].ptrSocket);
                    int type;
                    in>>type;
                    switch(type)
                    {
                    case GAMESTART0:
                    case GAMESTART1:
                    {
                        _userInfo[i].read = true;
                        for(int index=0; index<MAXCONNCOUNT; index++)
                        {
                            if(i!=index && _userInfo[index].ptrSocket && _userInfo[index].read)//如果匹配到了两个对手
                            {
                                ui->textBrowser->append(tr("%1号和%2号开始了对局").arg(i+1).arg(index+1));
                                _userInfo[i].read = _userInfo[index].read = false;
                                updateInfo(i+1,index+1,false);
                                int mynum,rivalnum;
                                mynum = int(_userInfo[i].ptrSocket);
                                rivalnum = int(_userInfo[index].ptrSocket);

                                QByteArray buf1;
                                QDataStream out1(&buf1,QIODevice::WriteOnly);
                                out1<<GAMESTART0<<mynum<<_userInfo[index].name.toInt()<<_userInfo[i].name.toInt();
                                _userInfo[index].ptrSocket->write(buf1);

                                QByteArray buf2;
                                QDataStream out2(&buf2,QIODevice::WriteOnly);
                                out2<<GAMESTART1<<rivalnum<<_userInfo[i].name.toInt()<<_userInfo[index].name.toInt();
                                _userInfo[i].ptrSocket->write(buf2);
                            }
                        }
                    }break;
                    case USERCONN: break;
                    case QUITGAME: break;
                    case SERVERCLOSE: break;
                    case SERVERFULL: break;
                    case TALK:
                    {
                        QTcpSocket* rivalSocket;
                        int numSocket;
                        in>>numSocket;
                        rivalSocket = (QTcpSocket *)numSocket;
                        QString str;
                        in>>str;

                        QByteArray buf;
                        QDataStream out(&buf,QIODevice::WriteOnly);
                        out<<TALK<<str;
                        rivalSocket->write(buf);
                    }break;
                    case BROADCAST:
                    {
                        QString str;
                        in>>str;
                        QByteArray buf;
                        QDataStream out(&buf,QIODevice::WriteOnly);
                        out<<BROADCAST<<str;
                        for(int index=0; index<MAXCONNCOUNT; index++)
                        {
                            if(nullptr != _userInfo[index].ptrSocket)
                                _userInfo[index].ptrSocket->write(buf);
                        }
                    }break;
                    case TIMEOUT: //这个选手超市了
                    {
                    }break;
                    case RANKLIST: //该用户发来了查看排行榜的请求信息
                    {
                        int ranking;
                        for(int index=0; index<MAXCONNCOUNT; index++)
                            if(_ranklist[index] == i)
                            {
                                ranking = index+1;
                                break;
                            }

                        QString str = tr("您当前是第%1名，排行榜名单如下：\n").arg(ranking);
                        str.append("********************\n");
                        for(int index=0; index<MAXCONNCOUNT; index++)
                        {
                            str.append(tr("第%1名：%2号选手，净胜%3盘\n").arg(index+1)
                                       .arg(_userInfo[_ranklist[index]].name.toInt()).arg(_userInfo[_ranklist[index]].score));
                        }

                        QByteArray buf;
                        QDataStream out(&buf,QIODevice::WriteOnly);
                        out<<RANKLIST<<str;
                        _userInfo[i].ptrSocket->write(buf);
                    }break;
                    case WIN:
                    {
                        int winnum,losenum;
                        in>>winnum>>losenum;
                        ui->textBrowser->append(tr("%1号选手战胜了%2号选手").arg(winnum).arg(losenum));
                       if(!_userInfo[winnum].read && !_userInfo[losenum].read)//如果两人都是对战中状态发来的
                       {
                           _userInfo[winnum-1].score++;
                           updateRanklist(winnum-1, 1);
                           _userInfo[losenum-1].score--;
                           updateRanklist(losenum-1, 0);
                           updateInfo(winnum,0,1);
                       }
                       if(_userInfo[winnum-1].ptrSocket != _userInfo[i].ptrSocket)//如果winner不是发送WIN消息的人
                       {
                           QByteArray buf;
                           QDataStream out(&buf,QIODevice::WriteOnly);
                           out<<TIMEOUT;
                           _userInfo[winnum-1].ptrSocket->write(buf);
                       }

                        QByteArray buf;
                        QDataStream out(&buf,QIODevice::WriteOnly);
                        out<<WIN<<tr("恭喜%1号选手在对局中胜出！").arg(winnum);
                        for(int index=0; index<MAXCONNCOUNT; index++)
                            if(_userInfo[index].ptrSocket)
                                _userInfo[index].ptrSocket->write(buf);
                    }break;

                    default:
                    {
                        QTcpSocket* rivalSocket;
                        int numSocket;
                        in>>numSocket;
                        rivalSocket = (QTcpSocket *)numSocket;

                        QByteArray buf;
                        QDataStream out(&buf,QIODevice::WriteOnly);
                        out<<type;
                        _userInfo[i].ptrSocket->write(buf); //先向走棋的选手发送走棋信息
                        rivalSocket->write(buf);      //再向他的对手发送该走棋信息
                    }break;

                    }
                }
            });
            connect(_userInfo[i].ptrSocket,&QTcpSocket::disconnected,[this,i](){
                int winnum = updateInfo(i+1, 0, true); //找出胜者，在函数里面，还把胜者与逃跑者一起踢出了房间
                if(winnum && _userInfo[winnum-1].ptrSocket)
                {
                    _userInfo[winnum-1].score++;
                    updateRanklist(winnum-1, 1);

                    QByteArray buf;
                    QDataStream out(&buf,QIODevice::WriteOnly);
                    out<<QUITGAME;
                    _userInfo[winnum-1].ptrSocket->write(buf);
                }

                for(int index=0; index<MAXCONNCOUNT; index++)
                {
                    if(_userInfo[index].ptrSocket)
                    {
                        QString str = tr("%1号选手逃跑了！").arg(i+1);
                        QByteArray buf;
                        QDataStream out(&buf,QIODevice::WriteOnly);
                        out<<BROADCAST<<str;
                        _userInfo[index].ptrSocket->write(buf);
                    }
                }

                ui->textBrowser->append(tr("%1号选手离开了服务器").arg(i+1));
                _userInfo[i].ptrSocket->close();
                _userInfo[i].ptrSocket = nullptr;
                _userInfo[i].read = false;
                /***因为该选手召唤师逃跑，所以要重新分配这个选手号，就要更新选手初始分为0***/
                if(_userInfo[i].score > 0) //逃跑时选手是高分逃跑（比如掉线），所以是降序更新排行榜
                {
                    _userInfo[i].score = 0;
                    updateRanklist(i,0);
                }else if(_userInfo[i].score < 0)//这表明逃跑时该选手是负分逃跑了，所以是升序更新排行榜
                {
                    _userInfo[i].score = 0;
                    updateRanklist(i,1);
                }
            });
            break;
        }
    }
}

int GobangServer::updateInfo(int a, int b, bool remove)
{
    if(remove) //如果是删除桌子成对的选手（比如分出胜负或者一方逃跑）
    {
        for(int i=0; i<ui->tableWidget->rowCount(); i++)
        {
            if(a == ui->tableWidget->item(i,0)->text().toInt()) //根据选手a的编号找出他与对手b的桌子，然后把他俩踢出对决房间
            {
                int ret =  ui->tableWidget->item(i,1)->text().toInt();
                int other = ui->tableWidget->item(i,0)->text().toInt();
                _userInfo[ret-1].read = false;
                _userInfo[other-1].read = false;
                ui->tableWidget->removeRow(i);
                return ret;
            }
            if(a == ui->tableWidget->item(i,1)->text().toInt())
            {
                int ret =  ui->tableWidget->item(i,0)->text().toInt();
                int other = ui->tableWidget->item(i,1)->text().toInt();
                _userInfo[ret-1].read = false;
                _userInfo[other-1].read = false;
                ui->tableWidget->removeRow(i);
                return ret;
            }
        }
    }else{ //不然就是增加一个桌子供新配对玩家对决
        QTableWidgetItem* item1 = new QTableWidgetItem(QString::number(a));
        QTableWidgetItem* item2 = new QTableWidgetItem(QString::number(b));
        int row = ui->tableWidget->rowCount();
        ui->tableWidget->setRowCount(row+1);
        ui->tableWidget->setItem(row,1,item1);
        ui->tableWidget->setItem(row,0,item2);
        return 0;
    }
    return 0;
}

void GobangServer::updateRanklist(int num, bool ascending)//这里的num是下标，不用-1；第二个参数决定是升序更新还是降序更新
{
    int i=0;
    for(i=0; i<MAXCONNCOUNT; i++) //首先查出被更新的选手当前排行榜的位置
        if(_ranklist[i] == num)break;   //然后去更新该选手的新位置

    if(ascending) //如果是胜者要求刷新排行榜（升序更新）
    {
        while(i > 0)
        {
            if(_userInfo[_ranklist[i]].score > _userInfo[_ranklist[i-1]].score)
            {
                int temp = _ranklist[i];
                _ranklist[i] = _ranklist[i-1];
                _ranklist[i-1] = temp;
            }else break;
            i--;
        }
    }else{ //不然就是败者更新，降序
        while(i<MAXCONNCOUNT-1)
        {
            if(_userInfo[_ranklist[i]].score <= _userInfo[_ranklist[i+1]].score)
            {
                int temp = _ranklist[i];
                _ranklist[i] = _ranklist[i+1];
                _ranklist[i+1] = temp;
            }else break;
            i++;
        }
    }

    for(int i=0; i<MAXCONNCOUNT; i++)
    {
        QTableWidgetItem* index = new QTableWidgetItem(tr("第%1名").arg(QString::number(i+1)));
        QTableWidgetItem* num  = new QTableWidgetItem(tr("%1号选手").arg(_userInfo[_ranklist[i]].name));
        QTableWidgetItem* flag = new QTableWidgetItem(_userInfo[_ranklist[i]].read ? "等待对手中..." : "对局中或未进入");
        QTableWidgetItem* score = new QTableWidgetItem(tr("%1分").arg(QString::number(_userInfo[_ranklist[i]].score)));
        ui->ranklist->setItem(i,0,index);
        ui->ranklist->setItem(i,1,num);
        ui->ranklist->setItem(i,2,flag);
        ui->ranklist->setItem(i,3,score);
    }
}


void GobangServer::on_pushButton_clicked() //如果更新排行榜按钮被按下
{
    for(int i=0; i<MAXCONNCOUNT; i++)
    {
        QTableWidgetItem* index = new QTableWidgetItem(tr("第%1名").arg(QString::number(i+1)));
        QTableWidgetItem* num  = new QTableWidgetItem(tr("%1号选手").arg(_userInfo[_ranklist[i]].name));
        QTableWidgetItem* flag = new QTableWidgetItem(_userInfo[_ranklist[i]].read ? "等待对手中..." : "对局中或未进入");
        QTableWidgetItem* score = new QTableWidgetItem(tr("%1分").arg(QString::number(_userInfo[_ranklist[i]].score)));
        ui->ranklist->setItem(i,0,index);
        ui->ranklist->setItem(i,1,num);
        ui->ranklist->setItem(i,2,flag);
        ui->ranklist->setItem(i,3,score);
    }
}
