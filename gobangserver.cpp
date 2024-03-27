#include "gobangserver.h"
#include "ui_gobangserver.h"
#include <QTableWidgetItem>

GobangServer::GobangServer(QWidget *parent) :
    QWidget(parent), _userCount(0),
    _ui(new Ui::GobangServer)
{
    _ui->setupUi(this);
    this->setFixedSize(this->size());
    setWindowTitle(tr("五子棋服务器端"));
    _ui->ranklist->setRowCount(MAXCONNCOUNT);
    for(int i=0; i<MAXCONNCOUNT; i++)
    {
         _userInfo[i]._myName = QString::number(i+1);
         _userInfo[i]._read = _userInfo[i]._myScore = 0;
         _userInfo[i]._mySocketPtr = nullptr;
         _rankList[i] = i;
    }
}

GobangServer::~GobangServer()
{
    delete _ui;
}

void GobangServer::on_startListenBtn_clicked()
{
    if(_server.listen(QHostAddress::Any,_ui->portLE->text().toInt()))
    {
        qDebug()<<"服务器成功的启动了监听";
        _ui->startListenBtn->setEnabled(false);
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

    for(int i=0; i<MAXCONNCOUNT; i++) //遍历用户池，找到一个空的套接字给新用户
    {
        if(nullptr == _userInfo[i]._mySocketPtr) //若找到了空的套接字，那么给这个新连接的用户套上
        {
            _userInfo[i]._mySocketPtr = socket;
            _userInfo[i]._read = false;
            _userCount++;

            QByteArray buf;
            QDataStream out(&buf,QIODevice::WriteOnly);
            out<<USERCONN<<_userInfo[i]._myName.toInt();
            _userInfo[i]._mySocketPtr->write(buf);


            /********************************************************************************************************
             * 此游戏的通信原理如下：每次客户端给服务器端发包时，服务器首先取出包的头四个字节当成int整型解析，也就是下面的“type”，然后根 *
             * 根这个type的值来确定客户端想要干啥。比如客户端想广播，那么解包后发现它的type是BOARDCAST，然后服务器知道它要广播，接着就 *
             * 把包中type后面的内容解析出字符串，最后给所有用户发包即可；再比如客户端走了一步棋，然后打包发给服务器，服务器首先解析type  *
             * 发现不在DataType枚举定以里，然后服务器就知道了客户端发来的是走棋位置信息，于是服务器解包type后面的值，此值其实就是该客   *
             * 户端的对手的套接字值，最后服务器根据这个套接字向他的对手发包该客户端的走棋位置，在此之前，该客户端自己也被服务器发包了走棋  *
             * 信息；需要注意的是，向客户端以及他对手发包后，他们两个会自行判断有无分出胜负，若是，由生者发包给服务器端。。。            *
             * ******************************************************************************************************/
            connect(_userInfo[i]._mySocketPtr,&QTcpSocket::readyRead,[this,i](){
                while(_userInfo[i]._mySocketPtr && _userInfo[i]._mySocketPtr->bytesAvailable())
                {
                    QDataStream in;
                    in.setDevice(_userInfo[i]._mySocketPtr);
                    int type;
                    in>>type;
                    switch(type)
                    {
                    case GAMESTART0: //这两个是客户端发来他已经准备战斗的信息
                    case GAMESTART1:
                    {
                        _userInfo[i]._read = true;
                        for(int index=0; index<MAXCONNCOUNT; index++)
                        {
                            if(i!=index && _userInfo[index]._mySocketPtr && _userInfo[index]._read)//如果匹配到了两个对手
                            {
                                _ui->textBrowser->append(tr("%1号和%2号开始了对局").arg(i+1).arg(index+1));
                                _userInfo[i]._read = _userInfo[index]._read = false;
                                updateInfo(i+1,index+1,false);
                                int mynum,rivalnum;
                                mynum = int(_userInfo[i]._mySocketPtr);
                                rivalnum = int(_userInfo[index]._mySocketPtr);

                                QByteArray buf1;
                                QDataStream out1(&buf1,QIODevice::WriteOnly);
                                out1<<GAMESTART0<<mynum<<_userInfo[index]._myName.toInt()<<_userInfo[i]._myName.toInt();
                                _userInfo[index]._mySocketPtr->write(buf1);

                                QByteArray buf2;
                                QDataStream out2(&buf2,QIODevice::WriteOnly);
                                out2<<GAMESTART1<<rivalnum<<_userInfo[i]._myName.toInt()<<_userInfo[index]._myName.toInt();
                                _userInfo[i]._mySocketPtr->write(buf2);
                            }
                        }
                    }break;
                    case USERCONN: break;
                    case QUITGAME: break;
                    case SERVERCLOSE: break;
                    case SERVERFULL: break;
                    case RIVALCHAT: //私聊信息，向他的对手发送他要说的话
                    {
                        QTcpSocket* rivalSocket;
                        int numSocket;
                        in>>numSocket;
                        rivalSocket = (QTcpSocket *)numSocket;
                        QString str;
                        in>>str;

                        QByteArray buf;
                        QDataStream out(&buf,QIODevice::WriteOnly);
                        out<<RIVALCHAT<<str;
                        rivalSocket->write(buf);
                    }break;
                    case BROADCAST: //广播消息，向所有人（包括他自己）发送他要说的话
                    {
                        QString str;
                        in>>str;
                        QByteArray buf;
                        QDataStream out(&buf,QIODevice::WriteOnly);
                        out<<BROADCAST<<str;
                        for(int index=0; index<MAXCONNCOUNT; index++)
                        {
                            if(nullptr != _userInfo[index]._mySocketPtr)
                                _userInfo[index]._mySocketPtr->write(buf);
                        }
                    }break;
                    case GIVEUP: {}break; //这个选手超市了
                    case RANKLIST: //该用户发来了查看排行榜的请求信息
                    {
                        int ranking;
                        for(int index=0; index<MAXCONNCOUNT; index++)
                            if(_rankList[index] == i)
                            {
                                ranking = index+1;
                                break;
                            }

                        QString str = tr("您当前是第%1名，排行榜名单如下：\n").arg(ranking);
                        str.append("********************\n");
                        for(int index=0; index<MAXCONNCOUNT; index++)
                        {
                            str.append(tr("第%1名：%2号选手，净胜%3盘\n").arg(index+1)
                                       .arg(_userInfo[_rankList[index]]._myName.toInt()).arg(_userInfo[_rankList[index]]._myScore));
                        }

                        QByteArray buf;
                        QDataStream out(&buf,QIODevice::WriteOnly);
                        out<<RANKLIST<<str;
                        _userInfo[i]._mySocketPtr->write(buf);
                    }break;
                    case GAMEOVER: //有客户端发来自己获胜的信息（默认前面是胜者序号，后面的是败者序号）
                    {
                        int winNum,loseNum;
                        in>>winNum>>loseNum;
                        _ui->textBrowser->append(tr("%1号选手战胜了%2号选手").arg(winNum).arg(loseNum));
                       if(!_userInfo[winNum-1]._read && !_userInfo[loseNum-1]._read)//如果两人都是对战中状态发来的
                       {
                           _userInfo[winNum-1]._myScore++;
                           updateRankList(winNum-1, 1);
                           _userInfo[loseNum-1]._myScore--;
                           updateRankList(loseNum-1, 0);
                           updateInfo(winNum,0,1);
                       }

                       //如果发送gameover消息的人不是winner，说明是败者发的包，可能是败者超时或者主动认输，所以这里需要向胜者发包
                       if(_userInfo[winNum-1]._mySocketPtr != _userInfo[i]._mySocketPtr)
                       {
                           QByteArray buf;
                           QDataStream out(&buf,QIODevice::WriteOnly);
                           out<<GIVEUP;
                           _userInfo[winNum-1]._mySocketPtr->write(buf);
                       }

                        QByteArray buf;
                        QDataStream out(&buf,QIODevice::WriteOnly);
                        out<<GAMEOVER<<tr("恭喜%1号选手在对局中胜出！").arg(winNum);
                        for(int index=0; index<MAXCONNCOUNT; index++)
                            if(_userInfo[index]._mySocketPtr)
                                _userInfo[index]._mySocketPtr->write(buf);
                    }break;

                    default: //客户端发来的走棋信息，type值是他走棋的pos值，后面的是他对手的套接字int值
                    {
                        QTcpSocket* rivalSocket;
                        int numSocket;
                        in>>numSocket;
                        rivalSocket = (QTcpSocket *)numSocket;

                        QByteArray buf;
                        QDataStream out(&buf,QIODevice::WriteOnly);
                        out<<type;
                        _userInfo[i]._mySocketPtr->write(buf); //先向走棋的选手发送走棋信息
                        rivalSocket->write(buf);      //再向他的对手发送该走棋信息
                    }break;

                    }
                }
            });
            connect(_userInfo[i]._mySocketPtr,&QTcpSocket::disconnected,[this,i](){
                int winnum = updateInfo(i+1, 0, true); //找出胜者，在函数里面，还把胜者与逃跑者一起踢出了房间
                if(winnum && _userInfo[winnum-1]._mySocketPtr)
                {
                    _userInfo[winnum-1]._myScore++;
                    updateRankList(winnum-1, 1);

                    QByteArray buf;
                    QDataStream out(&buf,QIODevice::WriteOnly);
                    out<<QUITGAME;
                    _userInfo[winnum-1]._mySocketPtr->write(buf);
                }

                for(int index=0; index<MAXCONNCOUNT; index++) //向所有用户广播有人逃跑了
                {
                    if(_userInfo[index]._mySocketPtr)
                    {
                        QString str = tr("%1号选手逃跑了！").arg(i+1);
                        QByteArray buf;
                        QDataStream out(&buf,QIODevice::WriteOnly);
                        out<<BROADCAST<<str;
                        _userInfo[index]._mySocketPtr->write(buf);
                    }
                }

                _ui->textBrowser->append(tr("%1号选手离开了服务器").arg(i+1));
                _userInfo[i]._mySocketPtr->close();
                _userInfo[i]._mySocketPtr = nullptr;
                _userInfo[i]._read = false;
                /***因为该选手召唤师逃跑，所以要重新分配这个选手号，就要更新选手初始分为0***/
                if(_userInfo[i]._myScore > 0) //逃跑时选手是高分逃跑（比如掉线），所以是降序更新排行榜
                {
                    _userInfo[i]._myScore = 0;
                    updateRankList(i,0);
                }else if(_userInfo[i]._myScore < 0)//这表明逃跑时该选手是负分逃跑了，所以是升序更新排行榜
                {
                    _userInfo[i]._myScore = 0;
                    updateRankList(i,1);
                }
            });
            break;
        }
    }
}


void GobangServer::closeEvent(QCloseEvent *event)
{
    this->close();
}

int GobangServer::updateInfo(int a, int b, bool remove)
{
    if(remove) //如果是删除桌子成对的选手（比如分出胜负或者一方逃跑）
    {
        for(int i=0; i<_ui->tableWidget->rowCount(); i++)
        {
            if(a == _ui->tableWidget->item(i,0)->text().toInt()) //根据选手a的编号找出他与对手b的桌子，然后把他俩踢出对决房间
            {
                int ret =  _ui->tableWidget->item(i,1)->text().toInt();
                int other = _ui->tableWidget->item(i,0)->text().toInt();
                _userInfo[ret-1]._read = false;
                _userInfo[other-1]._read = false;
                _ui->tableWidget->removeRow(i);
                return ret;
            }
            if(a == _ui->tableWidget->item(i,1)->text().toInt())
            {
                int ret =  _ui->tableWidget->item(i,0)->text().toInt();
                int other = _ui->tableWidget->item(i,1)->text().toInt();
                _userInfo[ret-1]._read = false;
                _userInfo[other-1]._read = false;
                _ui->tableWidget->removeRow(i);
                return ret;
            }
        }
    }else{ //不然就是增加一个桌子供新配对玩家对决
        QTableWidgetItem* item1 = new QTableWidgetItem(QString::number(a));
        QTableWidgetItem* item2 = new QTableWidgetItem(QString::number(b));
        int row = _ui->tableWidget->rowCount();
        _ui->tableWidget->setRowCount(row+1);
        _ui->tableWidget->setItem(row,1,item1);
        _ui->tableWidget->setItem(row,0,item2);
        return 0;
    }
    return 0;
}



void GobangServer::updateRankList(int num, bool ascending)//这里的num是下标，不用-1；第二个参数决定是升序更新还是降序更新
{
    int i=0;
    for(i=0; i<MAXCONNCOUNT; i++) //首先查出被更新的选手当前排行榜的位置
        if(_rankList[i] == num)break;   //然后去更新该选手的新位置

    if(ascending) //如果是胜者要求刷新排行榜（升序更新）
    {
        while(i > 0)
        {
            if(_userInfo[_rankList[i]]._myScore > _userInfo[_rankList[i-1]]._myScore)
            {
                int temp = _rankList[i];
                _rankList[i] = _rankList[i-1];
                _rankList[i-1] = temp;
            }else break;
            i--;
        }
    }else{ //不然就是败者更新，降序
        while(i<MAXCONNCOUNT-1)
        {
            if(_userInfo[_rankList[i]]._myScore <= _userInfo[_rankList[i+1]]._myScore)
            {
                int temp = _rankList[i];
                _rankList[i] = _rankList[i+1];
                _rankList[i+1] = temp;
            }else break;
            i++;
        }
    }

    for(int i=0; i<MAXCONNCOUNT; i++)
    {
        QTableWidgetItem* index = new QTableWidgetItem(tr("第%1名").arg(QString::number(i+1)));
        QTableWidgetItem* num  = new QTableWidgetItem(tr("%1号选手").arg(_userInfo[_rankList[i]]._myName));
        QTableWidgetItem* flag = new QTableWidgetItem(_userInfo[_rankList[i]]._read ? "等待对手中..." : "对局中或未进入");
        QTableWidgetItem* score = new QTableWidgetItem(tr("%1分").arg(QString::number(_userInfo[_rankList[i]]._myScore)));
        _ui->ranklist->setItem(i,0,index);
        _ui->ranklist->setItem(i,1,num);
        _ui->ranklist->setItem(i,2,flag);
        _ui->ranklist->setItem(i,3,score);
    }
}


void GobangServer::on_pushButton_clicked() //如果更新排行榜按钮被按下
{
    for(int i=0; i<MAXCONNCOUNT; i++)
    {
        QTableWidgetItem* index = new QTableWidgetItem(tr("第%1名").arg(QString::number(i+1)));
        QTableWidgetItem* num  = new QTableWidgetItem(tr("%1号选手").arg(_userInfo[_rankList[i]]._myName));
        QTableWidgetItem* flag = new QTableWidgetItem(_userInfo[_rankList[i]]._read ? "等待对手中..." : "对局中或未进入");
        QTableWidgetItem* score = new QTableWidgetItem(tr("%1分").arg(QString::number(_userInfo[_rankList[i]]._myScore)));
        _ui->ranklist->setItem(i,0,index);
        _ui->ranklist->setItem(i,1,num);
        _ui->ranklist->setItem(i,2,flag);
        _ui->ranklist->setItem(i,3,score);
    }
}
