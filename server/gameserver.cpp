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
    QString name;
};
struct client
{
    QString name;
    double mousex;
    double mousey;
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
QList<uint32_t> cleans;

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
    for (uint32_t i = 0; i<5; i++) {
        item newitem;
        newitem.x = 200+i*500;
        newitem.y = 200+i*500;
        newitem.size = 100+100*i;
        newitem.velocityx = 0;
        newitem.velocityy = 0;
        newitem.isVirus = randomByte()/128;
        newitem.name = QString::number(i+1);
        newitem.colorR = randomByte();
        newitem.colorG = randomByte();
        newitem.colorB = randomByte();
        newitem.player = -1;
        newitem.id = newitemid;
        newitemid++;
        items.append(newitem);
    }
    //Start game
    QTimer::singleShot(50, this, SLOT(game()));
    QTimer::singleShot(250, this, SLOT(updateHighscore()));
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
            double min = 0, max = 10000;
            float change = 40-it.size/40;
            qDebug() << "change: "<<QString::number(change);
            if ((player.mousex < it.x) && (it.x>min)) it.x-=change; //Basic movement
            if ((player.mousex > it.x) && (it.x<max)) it.x+=change; //Needs to be replaced by algorythm with item velocity
            if ((player.mousey < it.y) && (it.y>min)) it.y-=change;
            if ((player.mousey > it.y) && (it.y<max)) it.y+=change;
            items.replace(i, it);
        }
    }

    //Send game state to every player
    for (int i = 0; i<m_clients.count(); i++) {
        sendUpdate(m_clients.at(i));
    }
    QTimer::singleShot(50, this, SLOT(game()));
}

void GameServer::updateHighscore()
{
    highscores.clear();
    highscore temp;
    temp.name = "";
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
        //qDebug() << "Totalsize player "<<QString::number(i)<<" is "<<QString::number(score);
        for (int k = 0; k<10; k++) {
            //qDebug() << "Pos "<<QString::number(k)<<" is "<<QString::number(highscores.at(k).size);
            if (score>highscores.at(k).size) {
                for (int l = k; l<9; l++) {
                    highscores.replace(l+1, highscores.at(l));
                }
                temp.name = player.name;
                temp.size = score;
                highscores.replace(k, temp);
                //qDebug() << "Pos "<<QString::number(k)<<" is replaced ("<<QString::number(temp.size)<<").";
                break;
            }
        }
    }
    for (int i = 0; i<m_clients.count(); i++) {
        sendHighscore(m_clients.at(i));
    }
    QTimer::singleShot(250, this, SLOT(updateHighscore()));
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
    clients.append(newuser);
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
            newitem.size = 33;
            newitem.isVirus = 0;
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

            //Voor eerste test:
            client cl = clients.at(clientid);
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

            /* Debug code voor cleanup functie in client */

            //cleans.append(items.at(0).id);

            /*for (int i = 0; i<items.count(); i++) {
                item it = items.at(i);
                if (it.player==clientid) {
                    it.size = it.size + 0.5;
                    qDebug() << "size is now "<<QString::number(it.size);
                    items.replace(i, it);
                }
            }*/
            int j = items.count();
            for (int i = 0; i<j; i++) {
                item it = items.at(i);
                if (it.player==clientid && it.size>100) {
                    item newitem;
                    newitem.x = it.x+it.size+2*randomByte()-2*randomByte();
                    newitem.y = it.y+it.size+2*randomByte()-2*randomByte();
                    newitem.size = it.size/2;
                    newitem.velocityx = 0;
                    newitem.velocityy = 0;
                    newitem.isVirus = 0;
                    newitem.name = it.name;
                    newitem.colorR = it.colorR;
                    newitem.colorG = it.colorG;
                    newitem.colorB = it.colorB;
                    newitem.player = it.player;
                    newitem.id = newitemid;
                    newitemid++;
                    items.append(newitem);
                    sendNewMyID(pClient, newitem.id);
                   // it.size = it.size/2;
                    //qDebug() << "Newsize = "<<QString::number(it.size);
                    //items.replace(i, it);
                    //cleans.append(it.id); //Trigger update for old cell
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
            if (items.count()>1) {
                for (int i = 0; i<items.count(); i++) {
                    item it = items.at(i);
                    if (it.player==clientid) {
                        kill k;
                        k.victim = 0;
                        k.attacker = i;
                        kills.append(k);
                        qDebug() << "Kill created.";
                        break;
                    }
                }
            }
            break;
        }
        case 254: {
            break;
        }
        case 255: {
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

void GameServer::sendUpdate(QWebSocket *pClient)
{
    QByteArray message;
    message.clear();
    char data[100];

    uint16_t numkills = kills.count();
    data[0] = 16;
    data[1] = *((char*) &numkills);
    data[2] = *((char*) &numkills+1);
    message.append(data, 3);

    /* Voor iedere kill in deze update */
    for (uint16_t i = 0; i<numkills; i++) {
        uint32_t aid = kills.at(i).attacker;
        uint32_t vid = kills.at(i).victim;
        uint32_t attacker = items.at(aid).id;
        uint32_t victim = items.at(vid).id;
        qDebug() << "Found kill between items: "<<QString::number(attacker)<<" killed "<<QString::number(victim);
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
        a.size = a.size + v.size*0.8;
        items.replace(aid, a);
        items.removeAt(vid);
    }
    kills.clear();

    for (int i = 0; i<items.count(); i++){
        item currentitem = items.at(i);
        uint32_t itemnr = currentitem.id;
        //qDebug() << "Sending item with id "<<QString::number(itemnr);
        data[0] = *((char*) &(itemnr));
        data[1] = *((char*) &(itemnr)+1);
        data[2] = *((char*) &(itemnr)+2);
        data[3] = *((char*) &(itemnr)+3);
        message.append(data, 4);
        //if (i==0) break;
        addFloat(&message, currentitem.x);
        addFloat(&message, currentitem.y);
        addFloat(&message, currentitem.size);
        //qDebug() <<"X = "<<QString::number(currentitem.x)<<", Y = "<<QString::number(currentitem.y)<<", Size = "<<QString::number(currentitem.size);
        data[0] = currentitem.colorR;//R
        message.append(data, 1);
        data[0] = currentitem.colorG;//G
        message.append(data, 1);
        data[0] = currentitem.colorB;//B
        message.append(data, 1);
        data[0] = currentitem.isVirus;//virus
        message.append(data, 1);
        QString name = currentitem.name;  //"["+QString::number(currentitem.size)+"] "+
        addString(&message, name);
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
