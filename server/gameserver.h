#ifndef GAMESERVER_H
#define GAMESERVER_H

#include <QtCore/QObject>
#include <QtCore/QList>
#include <QtCore/QByteArray>
#include <stdint.h>
#include <QTimer>
#include <cmath>

QT_FORWARD_DECLARE_CLASS(QWebSocketServer)
QT_FORWARD_DECLARE_CLASS(QWebSocket)

class GameServer : public QObject
{
    Q_OBJECT
public:
    explicit GameServer(quint16 port, bool debug = false, QObject *parent = Q_NULLPTR);
    ~GameServer();

Q_SIGNALS:
    void closed();

private Q_SLOTS:
    void onNewConnection();
    void processTextMessage(QString message);
    void processBinaryMessage(QByteArray message);
    void socketDisconnected();
    void sendHighscore(QWebSocket *pClient);
    void sendTeamHighscore(QWebSocket *pClient);
    void sendUpdate(int clientid);
    void sendCamera(QWebSocket *pClient, double minx, double maxx, double miny, double maxy);
    void sendNewMyID(QWebSocket *pClient, uint32_t itemid);
    void sendClearMyID(QWebSocket *pClient);
    void addFloat(QByteArray* input, float value);
    void addDouble(QByteArray* input, double value);
    void addString(QByteArray* input, QString value);
    void game();
    void updateHighscore();
    void deletePlayersItems(int clientid);
    void createFood();
    void createVirus();
    void createMass(uint8_t cR, uint8_t cG, uint8_t cB, float x, float y, float targetx, float targety, float size);
    void debugMakeVirus(int clientid);
    void sendSpectateView(QWebSocket *pClient, float x, float y, float scale);
    void splitCellsForPlayer(int clientid, bool all = 0);
    void splitCell(QWebSocket *pClient, int itemid, bool all = 0);

private:
    QWebSocketServer *m_pWebSocketServer;
    QList<QWebSocket *> m_clients;
    bool m_debug;
    uint8_t randomByte();
    double weighttosize(double weight);
    double sizetoweight(double size);
};

#endif // GAMESERVER_H
