/*
 * Private server for agar.io
 * (c) Renze Nicolai
 * License: GPLv2
 *
 * Note: This program is not in any way
 * related to the developers behind agar.io
 *
 * In case you encounter problems while using this software
 * please do NOT contact the agar.io developers but instead
 * contact me through Github.
 */



#include "gameserver.h"
#include "QtWebSockets/qwebsocketserver.h"
#include "QtWebSockets/qwebsocket.h"
#include <QtCore/QDebug>

QT_USE_NAMESPACE

struct item
{
    uint32_t id;
    int player;
    float x;
    float y;
    float size;
    float velocityx;
    float velocityy;
    uint8_t colorR;
    uint8_t colorG;
    uint8_t colorB;
    uint8_t isVirus;
    uint8_t isFood;
    QString name;
};
struct client
{
    QString name;
    double mousex;
    double mousey;
    double almostplayerx;
    double almostplayery;
    bool isbot;
    bool isready;
};
struct highscore
{
    QString name;
    float size;
};

struct kill
{
    int attacker;
    int victim;
};

QList<highscore> highscores;

uint32_t newitemid = 1;
QList<item> items;
QList<QWebSocket*> clientConnections;
QList<client> clients;

QList<kill> kills;

float maxmovex = 1;
float maxmovey = 1;


//World border
double world_min = 0, world_max = 10000;


GameServer::GameServer(quint16 port, bool debug, QObject *parent) :
    QObject(parent),
    m_pWebSocketServer(new QWebSocketServer(QStringLiteral("Game Server"),
                                            QWebSocketServer::NonSecureMode, this)),
    m_clients(),
    m_debug(debug)
{
    if (m_pWebSocketServer->listen(QHostAddress::Any, port)) {
        if (m_debug)
            qDebug() << "Gameserver listening on port" << port;
        connect(m_pWebSocketServer, &QWebSocketServer::newConnection,
                this, &GameServer::onNewConnection);
        connect(m_pWebSocketServer, &QWebSocketServer::closed, this, &GameServer::closed);
    }

    newitemid = 1;

    kills.clear();

    //Setup world
    for (uint32_t i = 0; i<1000; i++) {
        createFood();
    }
    for (uint32_t i = 0; i<100; i++) {
        createVirus();
    }
    //Start game
    QTimer::singleShot(100, this, SLOT(game()));
    QTimer::singleShot(1000, this, SLOT(updateHighscore()));
}

GameServer::~GameServer()
{
    m_pWebSocketServer->close();
    qDeleteAll(m_clients.begin(), m_clients.end());
}

void GameServer::game()
{
    //Move player items according to mouse position
    for (int i = 0; i<items.count(); i++) {
        item it = items.at(i);
        if (it.player>=0) {
            client player = clients.at(it.player);
            //qDebug() << "change: "<<QString::number(change);
            /*if ((player.mousex < it.x) && (it.x>min)) it.x-=change; //Basic movement
            if ((player.mousex > it.x) && (it.x<max)) it.x+=change; //Needs to be replaced by algorithm with item velocity
            if ((player.mousey < it.y) && (it.y>min)) it.y-=change;
            if ((player.mousey > it.y) && (it.y<max)) it.y+=change;*/


            //My first try at a better moving algorithm... (meh)
            double maxvelocity = 20;

            double changex, changey;

            if (maxmovex<abs(player.mousex-it.x)) maxmovex = abs(player.mousex-it.x);
            if (maxmovey<abs(player.mousex-it.y)) maxmovex = abs(player.mousey-it.y);

            changex = ((player.mousex-it.x)*(maxvelocity/10.0))/maxmovex;
            changey = ((player.mousey-it.y)*(maxvelocity/10.0))/maxmovey;

            if (abs(player.mousex-it.x)<maxvelocity+10) changex = 0;
            if (abs(player.mousey-it.y)<maxvelocity+10) changey = 0;

            //qDebug()<<"C "<<QString::number(changex);


            /*if (player.mousex < it.x) it.velocityx-=changex;
            if (player.mousex > it.x) it.velocityx+=changex;
            if (player.mousey < it.y) it.velocityy-=changey;
            if (player.mousey > it.y) it.velocityy+=changey;*/

            it.velocityx += changex;
            it.velocityy += changey;

            if (it.velocityx>0) it.velocityx-=0.01;
            if (it.velocityx<0) it.velocityx+=0.01;
            if (it.velocityy>0) it.velocityy-=0.01;
            if (it.velocityy<0) it.velocityy+=0.01;

            if (it.velocityx>0 && it.velocityx<0.1) it.velocityx=0;
            if (it.velocityx>-0.1 && it.velocityx<0) it.velocityx=0;
            if (it.velocityy>0 && it.velocityy<0.1) it.velocityy=0;
            if (it.velocityy>-0.1 && it.velocityy<0) it.velocityx=0;

            if (it.velocityx>maxvelocity) {
                it.velocityx = maxvelocity;
            }
            if (it.velocityx<-maxvelocity) {
                it.velocityx = -maxvelocity;
            }
            if (it.velocityy>maxvelocity) {
                it.velocityy = maxvelocity;
            }
            if (it.velocityy<-maxvelocity) {
                it.velocityy = -maxvelocity;
            }

            it.x = it.x + it.velocityx;
            it.y = it.y + it.velocityy;

            if (it.x<world_min) {
                it.x = world_min;
            }
            if (it.x>world_max) {
                it.x = world_max;
            }
            if (it.y<world_min) {
                it.y = world_min;
            }
            if (it.y>world_max) {
                it.y = world_max;
            }
            items.replace(i, it);

            player.almostplayerx = it.x;
            player.almostplayery = it.y;
            clients.replace(it.player, player);
        }
    }

    //Collisions
    for (int i = 0; i<items.count(); i++) {
        item i1 = items.at(i);
        for (int j = 0; j<items.count(); j++) {
            item i2 = items.at(j);
            if (i1.id!=i2.id) {
                if(sqrt((i1.x-i2.x)*(i1.x-i2.x)+(i1.y-i2.y)*(i1.y-i2.y))<((i1.size+i2.size)/2.0)) {
                    //qDebug() << "Collision between "<<QString::number(i1.id)<<" and "<<QString::number(i2.id);
                    kill k;
                    k.attacker = 0;

                    bool isfoodhit = false;

                    if (i1.size < 23) {
                        k.attacker = j;
                        k.victim = i;
                        //qDebug() << "hit food A";
                        isfoodhit = true;
                    }

                    if (i2.size < 23) {
                        k.attacker = i;
                        k.victim = j;
                        //qDebug() << "hit food B";
                        isfoodhit = true;
                    }

                    if ((i1.size>(i2.size+20))&&(!isfoodhit)) {
                        k.attacker = i;
                        k.victim = j;
                        //qDebug() << "a hit b";
                    }
                    if ((i2.size>(i1.size+20))&&(!isfoodhit)) {
                        k.attacker = j;
                        k.victim = i;
                        //qDebug() << "b hit a";
                    }
                    if (k.attacker>0) {
                        bool m = 0;
                        for (int l=0; l<kills.count(); l++) {
                            kill n = kills.at(l);
                            if (k.victim==n.victim) {
                                m = 1;
                                break;
                            }
                        }
                        if (!m) {
                            if (items.at(k.attacker).isVirus) {
                                //qDebug("Attacker is virus!");
                                if (items.at(k.victim).player<0) {
                                    //This is random mass that hit a virus (most likely shot out by a player)
                                    //qDebug("This is random mass that hit a virus (most likely shot out by a player)");
                                    kills.append(k);
                                } else {
                                    //This is a player hiding behind a virus cell, do nothing
                                    //qDebug("This is a player hiding behind a virus cell, do nothing");
                                }
                            } else {
                                if (items.at(k.victim).isVirus) {
                                    //qDebug("<not implemented> now the attacker should have been split...");
                                } else {
                                    kills.append(k);
                                    //qDebug("Kill created!");
                                    if (isfoodhit &&(!items.at(i).isFood || !items.at(j).isFood)) {
                                        //qDebug() << "Added food";
                                        createFood();
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    //Send game state to every player
    for (int i = 0; i<m_clients.count(); i++) {
        if (clients.at(i).isready) {
          sendUpdate(i);
        }
    }
    QTimer::singleShot(20, this, SLOT(game()));
}

void GameServer::updateHighscore()
{

    //Clear highscores
    highscores.clear();
    highscore temp;
    temp.name = "EMPTY";
    temp.size = 0;
    for (int i = 0; i<10; i++) {
        highscores.append(temp);
    }


    for (int i = 0; i<clients.count(); i++) {
        float score = 0;
        client player = clients.at(i);
        for (int j = 0; j<items.count(); j++) {
            item it = items.at(j);
            if (it.player==i) {
                score = score + it.size;
            }
        }

        for (int i = 0; i<10; i++) {
            if (score>highscores.at(i).size) {
                temp.name = player.name;
                temp.size = score;
                highscores.insert(i, temp);
                break;
            }
        }

        if (highscores.count()>10) {
            for (int i = 10; i < highscores.count(); i++) highscores.removeLast();
        }

        //qDebug() << "Totalsize player "<<QString::number(i)<<" is "<<QString::number(score);
        /*for (int k = 0; k<10; k++) {
            qDebug() << "Pos "<<QString::number(k)<<" is "<<QString::number(highscores.at(k).size);
            if (score>highscores.at(k).size) {
                for (int l = k; l<9; l++) {
                    highscores.replace(l+1, highscores.at(l));
                }
                temp.name = player.name;
                temp.size = score;
                highscores.replace(k, temp);
                qDebug() << "Pos "<<QString::number(k)<<" is replaced ("<<QString::number(temp.size)<<").";
                break;
            }
        }*/
    }
    for (int i = 0; i<m_clients.count(); i++) {
        if (clients.at(i).isready) {
            sendHighscore(m_clients.at(i));
        }
    }
    QTimer::singleShot(1000, this, SLOT(updateHighscore()));
}

void GameServer::onNewConnection()
{
    QWebSocket *pSocket = m_pWebSocketServer->nextPendingConnection();

    connect(pSocket, &QWebSocket::textMessageReceived, this, &GameServer::processTextMessage);
    connect(pSocket, &QWebSocket::binaryMessageReceived, this, &GameServer::processBinaryMessage);
    connect(pSocket, &QWebSocket::disconnected, this, &GameServer::socketDisconnected);

    m_clients << pSocket;
    client newuser;
    newuser.name = "";
    newuser.mousex = 0;
    newuser.mousey = 0;
    newuser.almostplayerx = 0;
    newuser.almostplayery = 0;
    newuser.isbot = false;
    newuser.isready = false;
    clients.append(newuser);

    QByteArray message;
    message.clear();
    for (int i = 0; i<3; i++) {
      message.append('H');
      message.append('e');
      message.append('l');
      message.append('l');
      message.append('o');
    }
    pSocket->sendBinaryMessage(message);
}

void GameServer::processTextMessage(QString message)
{
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());
    if (m_debug)
        qDebug() << "Message received:" << message;
    if (pClient) {
        //pClient->sendTextMessage(message);
    }
}

void GameServer::processBinaryMessage(QByteArray message)
{
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());
    int clientid = m_clients.indexOf(pClient);
    //qDebug() << "ClientID is "<<QString::number(clientid)<<" ["<<clients.at(clientid).name<<"]";
    if (pClient) {
        //if (m_debug) {
        switch ((uint8_t) message.at(0)) {
        case 0: {
            QString nickname = "";
            QByteArray chardata;
            for (int i = 0; i < ((message.count()-1)/2); i++) {
                chardata.clear();
                chardata.append(message.at((i*2)+1));
                chardata.append(message.at((i*2)+2));
                nickname.append(chardata);
            }
            client temp = clients.at(clientid);
            temp.name = nickname;

            //When nickname is set the user wants to start a new game
            deletePlayersItems(clientid);
            item newitem;
            newitem.player = clientid;
            newitem.x = randomByte()*10;
            newitem.y = randomByte()*10;
            newitem.velocityx = 0;
            newitem.velocityy = 0;
            newitem.size = weighttosize(40);
            newitem.isVirus = 0;
            newitem.isFood = 0;
            newitem.name = nickname;
            newitem.colorR = randomByte();
            newitem.colorG = randomByte();
            newitem.colorB = randomByte();
            newitem.id = newitemid;
            newitemid++;
            items.append(newitem);
            sendClearMyID(pClient);
            sendNewMyID(pClient, newitem.id);

            clients.replace(clientid, temp);

            //Reset camera en MyID
            sendCamera(pClient, 0,0,11180,11180);

            qDebug() << "Nickname for user "<<QString::number(clientid)<<" set to " << nickname;
            break; }
        case 1: {
            qDebug()<<"["<<QString::number(clientid)<<"] "<<clients.at(clientid).name<<" is now spectating.";
            break;
        }
        case 16: {
            if (message.count()==21) {
            double mouse_x;
            double mouse_y;
            uint8_t temp[8];
            for (int i = 1; i < 9; i++) {
                temp[i-1] = message.at(i);
            }
            memcpy(&mouse_x, temp, sizeof(double));
            for (int i = 9; i < 17; i++) {
                temp[i-9] = message.at(i);
            }
            memcpy(&mouse_y, temp, sizeof(double));

            client cl = clients.at(clientid);

            if ((cl.mousex == mouse_x) && (cl.mousey == mouse_y)) cl.isbot = true;

            cl.mousex = mouse_x;
            cl.mousey = mouse_y;
            clients.replace(clientid, cl);
            //qDebug() << "Mouse position message [X = "<< QString::number(mouse_x) << ", Y = "<< QString::number(mouse_y) << "].";
            } else {
                qDebug() << "Ignored invalid user input message.";
            }
            break;
        }
        case 17: {
            qDebug() << "Client "<<QString::number(clientid)<<"  split his cell";
            int j = items.count();
            for (int i = 0; i<j; i++) {
                item it = items.at(i);
                if (it.player==clientid && it.size>100) {
                    item newitem;
                    newitem.x = it.x+it.size+2*randomByte()-2*randomByte();
                    newitem.y = it.y+it.size+2*randomByte()-2*randomByte();
                    newitem.size = weighttosize(sizetoweight(it.size)/2);
                    newitem.velocityx = 0;
                    newitem.velocityy = 0;
                    newitem.isVirus = 0;
                    newitem.isFood = 0;
                    newitem.name = it.name;
                    newitem.colorR = it.colorR;
                    newitem.colorG = it.colorG;
                    newitem.colorB = it.colorB;
                    newitem.player = it.player;
                    newitem.id = newitemid;
                    newitemid++;
                    items.append(newitem);
                    sendNewMyID(pClient, newitem.id);
                    it.size = newitem.size;
                    //qDebug() << "Newsize = "<<QString::number(it.size);
                    items.replace(i, it);
                    //break;
                }
            }

            break;
        }
        case 19: {
            qDebug() << "Client "<<QString::number(clientid)<<" Lost focus of browser window";
            break;
        }
        case 21: {
            qDebug() << "Client "<<QString::number(clientid)<<" ejected mass";
            //sendTeamHighscore(pClient);
            break;
        }
        case 254: {
            break;
        }
        case 255: {
            client player = clients.at(clientid);
            player.isready = true;
            clients.replace(clientid, player);
            qDebug()<<"Client "<<QString::number(clientid)<<" is now ready to receive.";
            break;
        }
        default:
            qDebug() << "Unknown message received. Type is '" << QString::number((uint8_t) message.at(0)) << "'";
            for (int i = 0; i < message.count(); i++) {
              qDebug() << "  <"<< QString::number(i) <<"> " << QString::number((uint8_t) message.at(i));
            }
        }
    }
}

void GameServer::sendUpdate(int clientid)
{
    QWebSocket *pClient = m_clients.at(clientid);
    client player = clients.at(clientid);

    QByteArray message;
    message.clear();
    char data[100];

    uint16_t numkills = kills.count();
    data[0] = 16;
    data[1] = *((char*) &numkills);
    data[2] = *((char*) &numkills+1);
    message.append(data, 3);

    /* Voor iedere kill in deze update */
    for (uint16_t i = 0; i<kills.count(); i++) {
        uint32_t aid = kills.at(i).attacker;
        uint32_t vid = kills.at(i).victim;
        uint32_t attacker = items.at(aid).id;
        uint32_t victim = items.at(vid).id;
        //qDebug() << "Found kill between items: "<<QString::number(attacker)<<"("<<QString::number(sizetoweight(items.at(aid).size))<<") killed "<<QString::number(victim)<<" ("<<QString::number(sizetoweight(items.at(vid).size))<<")";
        data[0] = *((char*) &(attacker));
        data[1] = *((char*) &(attacker)+1);
        data[2] = *((char*) &(attacker)+2);
        data[3] = *((char*) &(attacker)+3);
        message.append(data, 4);
        data[0] = *((char*) &(victim));
        data[1] = *((char*) &(victim)+1);
        data[2] = *((char*) &(victim)+2);
        data[3] = *((char*) &(victim)+3);
        message.append(data, 4);
        //uint32 attacker
        //uint32 victim
        item a = items.at(aid);
        item v = items.at(vid);
        a.size = weighttosize(sizetoweight(a.size) + sizetoweight(v.size));
        items.replace(aid, a);
    }
    for (int i = 0; i<kills.count(); i++) items.removeAt(kills.at(i).victim); //Remove all cells that were killed
    kills.clear();


    QList<uint32_t> cleans;
    cleans.clear();

    for (int i = 0; i<items.count(); i++){
        item currentitem = items.at(i);
        if ( (!currentitem.isFood)||((abs(currentitem.x-player.almostplayerx)<1000) && (abs(currentitem.y-player.almostplayery)<1000)) ) {
            uint32_t itemnr = currentitem.id;
            data[0] = *((char*) &(itemnr));
            data[1] = *((char*) &(itemnr)+1);
            data[2] = *((char*) &(itemnr)+2);
            data[3] = *((char*) &(itemnr)+3);
            message.append(data, 4);
            addFloat(&message, currentitem.x);
            addFloat(&message, currentitem.y);
            addFloat(&message, currentitem.size);
            data[0] = currentitem.colorR;//R
            message.append(data, 1);
            data[0] = currentitem.colorG;//G
            message.append(data, 1);
            data[0] = currentitem.colorB;//B
            message.append(data, 1);
            data[0] = currentitem.isVirus;//virus
            message.append(data, 1);
            QString name = currentitem.name;  //"["+QString::number(currentitem.size)+"] "+

            /* Bot check test */
            if (currentitem.player==clientid && player.isbot) {
                name = "[BOT] "+currentitem.name;
            }

            addString(&message, name);
        } else {
            //qDebug("Item ignored.");
            //cleans.append(currentitem.id);
        }
        //Float64 x
        //Float64 y
        //Float64 size
        //Uint8 colorCode
        //Uint8 flags
        //    --> Doet iets met isvirus
        //String name
    }

    uint32_t itemid = 0;
    data[0] = *((char*) &itemid);
    data[1] = *((char*) &itemid+1);
    data[2] = *((char*) &itemid+2);
    data[3] = *((char*) &itemid+3);
    message.append(data, 4);

    uint16_t notused = 0;
    data[0] = *((char*) &notused);
    data[1] = *((char*) &notused+1);
    message.append(data, 2);

    uint32_t numClean = cleans.count();
    data[0] = *((char*) &numClean);
    data[1] = *((char*) &numClean+1);
    data[2] = *((char*) &numClean+2);
    data[3] = *((char*) &numClean+3);
    message.append(data, 4);

    for (uint32_t i = 0; i < numClean; i++) {
        uint32_t cid = cleans.at(i);
        data[0] = *((char*) &cid);
        data[1] = *((char*) &cid+1);
        data[2] = *((char*) &cid+2);
        data[3] = *((char*) &cid+3);
        message.append(data, 4);
    }
    cleans.clear();

    pClient->sendBinaryMessage(message);
}

void GameServer::addFloat(QByteArray* input, float value)
{
    char temp[sizeof(float)];
    memcpy(temp, &value, sizeof(float));
    input->append(temp, sizeof(float));
}

void GameServer::addDouble(QByteArray* input, double value)
{
    char temp[sizeof(double)];
    memcpy(temp, &value, sizeof(double));
    input->append(temp, sizeof(double));
}

void GameServer::addString(QByteArray* input, QString value)
{
    char data[3];
    for (int n = 0; n < value.length(); n++) {
      data[0] = value.data()[n].toLatin1();
      data[1] = 0;
      input->append(data, 2);
    }
    data[0] = 0;
    data[1] = 0;
    input->append(data, 2);
}

void GameServer::sendHighscore(QWebSocket *pClient)
{
    QByteArray message;
    message.clear();

    uint32_t amount = 0;
    for (int i = 0; i<10; i++) {
        if (highscores.at(i).size > 0) amount = i+1;
    }
    char data[100];
    data[0] = 49;
    data[1] = *((char*) &amount);
    data[2] = *((char*) &amount+1);
    data[3] = *((char*) &amount+2);
    data[4] = *((char*) &amount+3);
    message.append(data, 5);
    for (uint32_t i = 0; i < amount; i++) {
        uint32_t uid = 1;
        data[0] = *((char*) &uid);
        data[1] = *((char*) &uid+1);
        data[2] = *((char*) &uid+2);
        data[3] = *((char*) &uid+3);
        message.append(data, 4);
        QString name = highscores.at(i).name;
        if (name.length() > 20) {
            name = name.mid(0,16);
            name = name + "...";
        }
        addString(&message, name);
    }
    pClient->sendBinaryMessage(message);
}

void GameServer::socketDisconnected()
{
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());
    if (m_debug)
        qDebug() << "socketDisconnected:" << pClient;
    if (pClient) {
        int clientid = m_clients.indexOf(pClient);
        deletePlayersItems(clientid);
        clients.removeAt(clientid);
        m_clients.removeAll(pClient);
        pClient->deleteLater();
        for (int i = 0; i<items.count(); i++) {
            item it = items.at(i);
            if (it.player>=clientid) {
                it.player = it.player - 1;
                items.replace(i, it);
                qDebug() << "Moved player for item "<<QString::number(i)<<"("<<items.at(i).name<<") to "<<clients.at(it.player).name;
            }
        }
    }
}

void GameServer::deletePlayersItems(int clientid)
{
    int ic = items.count();
    for (int i = 0; i<ic; i++) {
        if (items.at(i).player==clientid) {
            qDebug() << "Deleting item "<<QString::number(i)<<" with name "<<items.at(i).name;
            items.removeAt(i);
            ic = items.count();
            i--;
        }
    }
}

void GameServer::sendCamera(QWebSocket *pClient, double minx, double maxx, double miny, double maxy)
{
    QByteArray message;
    message.clear();
    message.append(64); //message id
    addDouble(&message, minx);
    addDouble(&message, miny);
    addDouble(&message, maxx);
    addDouble(&message, maxy);
    pClient->sendBinaryMessage(message);
}

void GameServer::sendNewMyID(QWebSocket *pClient, uint32_t id)
{
    QByteArray message;
    message.clear();
    message.append(32); //message id
    char data[4];
    data[0] = *((char*) &id);
    data[1] = *((char*) &id+1);
    data[2] = *((char*) &id+2);
    data[3] = *((char*) &id+3);
    message.append(data, 4);
    pClient->sendBinaryMessage(message);
}

void GameServer::sendClearMyID(QWebSocket *pClient)
{
    QByteArray message;
    message.clear();
    message.append(20); //message id
    pClient->sendBinaryMessage(message);
}

uint8_t GameServer::randomByte()
{
    return (uint8_t) qrand() % (256);
}

double GameServer::sizetoweight(double size)
{
    double result = (size*size)/100.0;
    //qDebug()<<"Sizetoweight: "<<QString::number(size)<<" to "<<QString::number(result);
    return result;
}

double GameServer::weighttosize(double weight)
{
    double result = sqrt(weight*100.0);
    //qDebug()<<"Weighttosize: "<<QString::number(weight)<<" to "<<QString::number(result);
    return result;
}

void GameServer::createFood()
{
    item newitem;
    newitem.x = world_max+1;
    newitem.y = world_max+1;
    while (newitem.x>world_max) newitem.x = randomByte()*40;
    while (newitem.y>world_max) newitem.y = randomByte()*40;
    newitem.size = weighttosize((uint8_t) qrand() % (5));
    newitem.velocityx = 0;
    newitem.velocityy = 0;
    newitem.isVirus = 0;
    newitem.isFood = 1;
    newitem.name = "";//QString::number(i+1);
    newitem.colorR = randomByte();
    newitem.colorG = randomByte();
    newitem.colorB = randomByte();
    newitem.player = -1;
    newitem.id = newitemid;
    newitemid++;
    items.append(newitem);
}

void GameServer::createVirus()
{
    item newitem;
    newitem.x = world_max+1;
    newitem.y = world_max+1;
    while (newitem.x>world_max) newitem.x = randomByte()*40;
    while (newitem.y>world_max) newitem.y = randomByte()*40;
    newitem.size = weighttosize(100);
    newitem.velocityx = 0;
    newitem.velocityy = 0;
    newitem.isVirus = 1;
    newitem.isFood = 0;
    newitem.name = "";
    newitem.colorR = 51;
    newitem.colorG = 255;
    newitem.colorB = 51;
    newitem.player = -1;
    newitem.id = newitemid;
    newitemid++;
    items.append(newitem);
}

void GameServer::sendTeamHighscore(QWebSocket *pClient)
{
    uint32_t amount = (uint32_t) highscores.count();

    QByteArray message;
    message.clear();
    message.append(50); //message id
    char data[4];
    data[0] = *((char*) &(amount));
    data[1] = *((char*) &(amount)+1);
    data[2] = *((char*) &(amount)+2);
    data[3] = *((char*) &(amount)+3);
    message.append(data, 4);
    for (int i = 0; i<highscores.count(); i++) {
        addFloat(&message, highscores.at(i).size);
    }
    pClient->sendBinaryMessage(message);
}

/*
 * Message with id 17: (unknown function);
            float A = 0;
            float B = 0;
            float C = 0;

            QByteArray message;
            message.clear();
            message.append(17); //message id
            addFloat(&message, A);
            addFloat(&message, B);
            addFloat(&message, C);
            pClient->sendBinaryMessage(message);
*/
