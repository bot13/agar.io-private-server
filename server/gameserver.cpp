/*
 * Private server for agar.io
 * (c) Renze Nicolai 2015
 * License: GPLv2
 *
 * Note: This program is not in any way related to agar.io or it's developers
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
    float targetx;
    float targety;
    float angle;
    int dirx;
    int diry;
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
    bool sentdebugcommand;
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

//Nieuw: QMap<uint32_t, item> items;

QList<QWebSocket*> clientConnections;
QList<client> clients;

QList<kill> kills;
QList<uint32_t> cleans;

float maxmovex = 1;
float maxmovey = 1;


//World border
double world_min = 0, world_max = 11180;//10000;

qint64 exec_time = 0;


GameServer::GameServer(quint16 port, bool debug, QObject *parent) :
    QObject(parent),
    m_pWebSocketServer(new QWebSocketServer(QStringLiteral("Game Server"),
                                            QWebSocketServer::NonSecureMode, this)),
    m_clients(),
    m_debug(debug)
{

    if (m_pWebSocketServer->listen(QHostAddress::Any, port)) {
        //if (m_debug)
        qDebug() << "Server is listening on port" << port;
        connect(m_pWebSocketServer, &QWebSocketServer::newConnection,
                this, &GameServer::onNewConnection);
        connect(m_pWebSocketServer, &QWebSocketServer::closed, this, &GameServer::closed);
    }

    newitemid = 1;

    kills.clear();

    //Setup world
    for (uint32_t i = 0; i<2500; i++) {
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
    try {
        qint64 start_time = QDateTime::currentMSecsSinceEpoch();
        for (int i = 0; i<items.count(); i++) {
            item i1 = items.at(i);

            //Move player items according to mouse position
            if (i1.player>=0) {
                client player = clients.at(i1.player);

                float dx = i1.x-player.mousex;
                float dy = i1.y-player.mousey;

                int dirx = -1, diry = -1;

                if (dx<0) { dirx = 1; dx = abs(dx);}
                if (dy<0) { diry = 1; dy = abs(dy);}

                i1.dirx = dirx;
                i1.diry = diry;

                //float distance = sqrt(pow(dx,2)+pow(dy,2));
                float angle = atan2(dy,dx);
                i1.angle = angle;

                float speed = 10.0 * ((500.0-sizetoweight(i1.size))/500.0) * (exec_time/20.0);

                i1.velocityx = speed * cos(angle)*dirx;
                i1.velocityy = speed * sin(angle)*diry;
                i1.x = i1.x + i1.velocityx;
                i1.y = i1.y + i1.velocityy;
                if (i1.x<world_min) i1.x = world_min;
                if (i1.x>world_max) i1.x = world_max;
                if (i1.y<world_min) i1.y = world_min;
                if (i1.y>world_max) i1.y = world_max;

                //If too big make smaller
                //i1.size -= weighttosize(0.00001);
                if (i1.size > weighttosize(200)) i1.size -= weighttosize(0.00001);
                if (i1.size > weighttosize(600)) i1.size -= weighttosize(0.0001);
                if (i1.size < 10 ) i1.size = 10;

                items.replace(i, i1);

                player.almostplayerx = i1.x;
                player.almostplayery = i1.y;
                clients.replace(i1.player, player);
            } else if (i1.targetx>=0 || i1.targety>=0) {
                //Used for ejecting mass
                float dx = i1.x-i1.targetx;
                float dy = i1.y-i1.targety;
                int dirx = -1, diry = -1;
                if (dx<0) { dirx = 1; dx = abs(dx);}
                if (dy<0) { diry = 1; dy = abs(dy);}
                i1.dirx = dirx;
                i1.diry = diry;
                float angle = atan2(dy,dx);
                i1.angle = angle;
                i1.velocityx = 15 * cos(angle)*dirx;
                i1.velocityy = 15 * sin(angle)*diry;
                i1.x = i1.x + i1.velocityx;
                i1.y = i1.y + i1.velocityy;
                if (i1.x<world_min) i1.x = world_min;
                if (i1.x>world_max) i1.x = world_max;
                if (i1.y<world_min) i1.y = world_min;
                if (i1.y>world_max) i1.y = world_max;
                items.replace(i, i1);
                if (i1.x==i1.targetx && i1.y==i1.targety) {
                    i1.targetx = -1;
                    i1.targety = -1;
                }
            }


            //Collision checking
            if (!i1.isFood) {
                //qDebug() << "C";
                for (int j = 0; j<items.count(); j++) {
                    item i2 = items.at(j);
                    if (i1.id!=i2.id) {
                        float distance = sqrt(pow((i1.x-i2.x),2)+pow((i1.y-i2.y),2));
                        float combinedradius = i1.size+i2.size;

                        if ((i1.player==i2.player)&&(distance<combinedradius)&&(i1.player>=0)) {
                            //qDebug() << "Splitted cell is touching!";
                            // Stop the cells from colliding
                            bool touch = true;
                            int timeout = 9999;
                            while(touch) {
                                if (i2.size<i1.size) {
                                    i2.dirx = -i2.dirx;
                                    i2.diry = -i2.diry;
                                    if (i2.dirx == 0) {
                                        i2.dirx = 1;
                                        i2.diry = 1;
                                    }
                                } else {
                                    i1.dirx = -i1.dirx;
                                    i1.diry = -i1.diry;
                                    if (i1.dirx == 0) {
                                        i1.dirx = 1;
                                        i1.diry = 1;
                                    }
                                }
                                    i2.x = i2.x + (i2.dirx);
                                    i2.y = i2.y + (i2.diry);
                                    i1.x = i1.x + (i1.dirx);
                                    i1.y = i1.y + (i1.diry);
                                /*} else {
                                    i1.x = i1.x + (i1.velocityx+1);
                                    i1.y = i1.y + (i1.velocityy+1);
                                    i2.x = i2.x - (i2.velocityx);
                                    i2.y = i2.y - (i2.velocityy);
                                }*/
                                if (i2.x<world_min) { i1.x += i2.x; i2.x = world_min; }
                                if (i2.x>world_max) i2.x = world_max;
                                if (i2.y<world_min) { i1.y += i2.y; i2.y = world_min; }
                                if (i2.y>world_max) i2.y = world_max;

                                if (i1.x<world_min) { i2.x += i1.x; i1.x = world_min; }
                                if (i1.x>world_max) i1.x = world_max;
                                if (i1.y<world_min) { i2.y += i1.y; i1.y = world_min; }
                                if (i1.y>world_max) i1.y = world_max;
                                float distance = sqrt(pow((i1.x-i2.x),2)+pow((i1.y-i2.y),2));
                                float combinedradius = i1.size+i2.size;
                                if (distance>=combinedradius) touch = false;
                                timeout--;
                                if (timeout<=0) { touch = false; qDebug()<<"ERROR, ITEMS KEEP TOUCHING"; }
                            }
                            items.replace(i, i1);
                            items.replace(j, i2);
                        }


                        if(distance<(combinedradius-(i1.size/3))) {
                            //qDebug() << "Collision between "<<QString::number(i1.id)<<" and "<<QString::number(i2.id);
                            kill k;
                            k.attacker = 0;

                            bool isfoodhit = false;

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
                                            if ((!isfoodhit) && (i1.player == i2.player)) {
                                                //These are cells that split that have come back together (FIX ME!)
                                                if (i1.velocityx<3 && i1.velocityy<3) { //Fix me!
                                                qDebug()<<"Combine!";
                                                kills.append(k);
                                                }
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
            }

            exec_time = QDateTime::currentMSecsSinceEpoch()-start_time;
        }

        //Send game state to every player
        for (int i = 0; i<m_clients.count(); i++) {
            if (clients.at(i).isready) {
              sendUpdate(i);
            }
        }
    } catch (int e) {
        qDebug() << "An exception occurred in the game() function. Exception Nr. " << QString::number(e);
    }

    QTimer::singleShot(1, this, SLOT(game()));
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
                QString name = player.name;
                if (player.isbot) name = "[BOT] "+name;
                if (player.sentdebugcommand) name = name+"***";
                temp.name = name;
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

    highscore exti;
    exti.size = 99999;
    exti.name = QString::number(items.count())+" items";
    highscores.push_front(exti);
    exti.size = 99999;
    exti.name = "Tick every "+QString::number(exec_time)+"ms";
    highscores.push_front(exti);

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
    newuser.sentdebugcommand = false;
    clients.append(newuser);

    qDebug() << "New connection from: "<<pSocket->peerAddress().toString();

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
            newitem.size = weighttosize(10.0);
            newitem.isFood = 0;
            newitem.name = nickname;
            if (temp.sentdebugcommand) {
                newitem.colorR = 255;
                newitem.colorG = 255;
                newitem.colorB = 255;
                newitem.isVirus = 1;
            } else {
                newitem.colorR = randomByte();
                newitem.colorG = randomByte();
                newitem.colorB = randomByte();
                newitem.isVirus = 0;
            }
            newitem.id = newitemid;
            newitem.targetx = -1;
            newitem.targety = -1;
            newitem.angle = 0;
            newitem.dirx = 0;
            newitem.diry = 0;
            newitemid++;
            items.append(newitem);
            sendClearMyID(pClient);
            sendNewMyID(pClient, newitem.id);

            clients.replace(clientid, temp);

            qDebug() << "Nickname for user "<<QString::number(clientid)<<" set to " << nickname;
            break; }
        case 1: {
            qDebug()<<"["<<QString::number(clientid)<<"] "<<clients.at(clientid).name<<" is now spectating.";
            break;
        }
        case 2: {
            qDebug()<<"["<<QString::number(clientid)<<"] "<<clients.at(clientid).name<<" is sending debug packets!";
            QByteArray message;
            message.clear();
            message.append(2);
            pClient->sendBinaryMessage(message);

            client player = clients.at(clientid);
            player.sentdebugcommand = true;
            clients.replace(clientid, player);
            debugMakeVirus(clientid);
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
            splitCellsForPlayer(clientid);
            break;
        }
        case 19: {
            qDebug() << "Client "<<QString::number(clientid)<<" Lost focus of browser window";
            break;
        }
        case 21: {
            qDebug() << "Client "<<QString::number(clientid)<<" ejected mass";
            for (int i = 0; i<items.count(); i++) {
                item i1 = items.at(i);
                if (i1.player==clientid) {
                    if (sizetoweight(i1.size)>100) {
                        float x = i1.x+(i1.dirx*i1.size);
                        float y = i1.y+(i1.diry*i1.size);
                        float changex = 500 * cos(i1.angle)*i1.dirx;
                        float changey = 500 * sin(i1.angle)*i1.diry;
                        createMass(i1.colorR, i1.colorG, i1.colorB, x, y, x+changex, y+changey, weighttosize(50));
                        i1.size = weighttosize(sizetoweight(i1.size) - 50);
                        items.replace(i, i1);
                    }
                    qDebug()<<"Created mass for item id "<<QString::number(i1.id);
                }
            }
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
            sendCamera(pClient, world_min,world_max,world_min,world_max);
            break;
        }
        default:
            qDebug() << "Unknown message received from "<<QString::number(clientid)<<". Type is '" << QString::number((uint8_t) message.at(0)) << "'";
            for (int i = 0; i < message.count(); i++) {
              qDebug() << "  <"<< QString::number(i) <<"> " << QString::number((uint8_t) message.at(i));
            }
        }
    }
}

void GameServer::sendUpdate(int clientid)
{
    try {
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
        for (int i = 0; i<kills.count(); i++) {
            cleans.append(items.at(kills.at(i).victim).id);
        }

        QList<int> deletethese; //To avoid crash when items are added to the kills list out of order.
        deletethese.clear();

        for (int i = 0; i<kills.count(); i++) {
            deletethese.append(kills.at(i).victim); //Remove all cells that were killed (1/2)
        }

        qSort(deletethese);

        for (int i = 0; i<deletethese.count(); i++) {
            items.removeAt(deletethese.at(i)); //Remove all cells that were killed (2/2)
        }


        kills.clear();

        for (int i = 0; i<items.count(); i++){
            item currentitem = items.at(i);
            if ( (!currentitem.isFood)||((abs(currentitem.x-player.almostplayerx)<1920) && (abs(currentitem.y-player.almostplayery)<1080)) ) {
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

                /* Show that a user is doing funny stuff */
                if (currentitem.player==clientid && player.isbot) name = "[BOT] "+currentitem.name;
                if (currentitem.player==clientid && player.sentdebugcommand) name = currentitem.name+"***";

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
        //qDebug() << "Sent world data to "<<QString::number(clientid);
    } catch (int e) {
        qDebug() << "An exception occurred in the sendUpdate() function. Exception Nr. " << QString::number(e);
    }
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
        qDebug() << "socketDisconnected:" << QString::number(m_clients.indexOf(pClient));
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
            cleans.append(items.at(i).id);
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
    while (newitem.x>world_max) newitem.x = randomByte()*world_max/255;
    while (newitem.y>world_max) newitem.y = randomByte()*world_max/255;
    newitem.size = weighttosize((uint8_t) qrand() % (3));
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
    newitem.targetx = -1;
    newitem.targety = -1;
    newitem.angle = 0;
    newitem.dirx = 0;
    newitem.diry = 0;
    newitemid++;
    items.append(newitem);
}

void GameServer::createVirus()
{
    item newitem;
    newitem.x = world_max+1;
    newitem.y = world_max+1;
    while (newitem.x>world_max) newitem.x = randomByte()*world_max/255;
    while (newitem.y>world_max) newitem.y = randomByte()*world_max/255;
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
    newitem.targetx = -1;
    newitem.targety = -1;
    newitem.angle = 0;
    newitem.dirx = 0;
    newitem.diry = 0;
    newitem.id = newitemid;
    newitemid++;
    items.append(newitem);
}

void GameServer::createMass(uint8_t cR, uint8_t cG, uint8_t cB, float x, float y, float targetx, float targety, float size)
{
    item newitem;
    newitem.x = x;
    newitem.y = y;
    newitem.size = size;
    newitem.velocityx = 0;
    newitem.velocityy = 0;
    newitem.isVirus = 0;
    newitem.isFood = 0;
    newitem.name = "";
    newitem.colorR = cR;
    newitem.colorG = cG;
    newitem.colorB = cB;
    newitem.player = -1;
    newitem.targetx = targetx;
    newitem.targety = targety;
    newitem.id = newitemid;
    newitem.angle = 0;
    newitem.dirx = 0;
    newitem.diry = 0;
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


void GameServer::debugMakeVirus(int clientid) {
    QWebSocket *pClient = m_clients.at(clientid);
    sendClearMyID(pClient);
    for (int i = 0; i<items.count(); i++) {
        item it = items.at(i);
        if (it.player == clientid) {
            it.id = newitemid;
            it.colorB = 255;
            it.colorG = 255;
            it.colorR = 255;
            it.isVirus = true;
            newitemid++;
            items.replace(i, it);
            //sendUpdate(clientid);
            sendNewMyID(pClient, it.id);
        }
    }
}

void GameServer::sendSpectateView(QWebSocket *pClient, float x, float y, float scale)
{
    QByteArray message;
    message.clear();
    message.append(17); //message id
    addFloat(&message, x);
    addFloat(&message, y);
    addFloat(&message, scale);
    pClient->sendBinaryMessage(message);
}

void GameServer::splitCellsForPlayer(int clientid, bool all)
{
    client player = clients.at(clientid);
    int ic = items.count();
    int counter = 0;
    for (int i = 0; i<ic; i++) {
        item it = items.at(i);
        if (it.player == clientid) {
            counter++;
            if (counter < 16) splitCell(m_clients.at(clientid), i, all);
        }
    }
}

void GameServer::splitCell(QWebSocket *pClient, int itemid, bool all)
{
    item it = items.at(itemid);
        if (it.size>100) {
            int amount = 1;
            if (all) amount = 10; //Amount is going to be used for forced splits (when hit by virus)
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
            newitem.targetx = -1;
            newitem.targety = -1;
            newitem.angle = 0;
            newitem.dirx = 0;
            newitem.diry = 0;
            newitemid++;
            items.append(newitem);
            sendNewMyID(pClient, newitem.id);
            it.size = newitem.size;
            items.replace(itemid, it);
        }
}
