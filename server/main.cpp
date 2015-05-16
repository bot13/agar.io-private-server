#include <QCoreApplication>
#include <QtCore/QCommandLineParser>
#include <QtCore/QCommandLineOption>
#include "gameserver.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);


    QCommandLineParser parser;
    parser.setApplicationDescription("agar.io private server");
    parser.addHelpOption();

    QCommandLineOption dbgOption(QStringList() << "d" << "debug",
            QCoreApplication::translate("main", "Debug output [default: off]."));
    parser.addOption(dbgOption);
    QCommandLineOption portOption(QStringList() << "p" << "port",
            QCoreApplication::translate("main", "Port for server [default: 9000]."),
            QCoreApplication::translate("main", "port"), QLatin1Literal("9000"));
    parser.addOption(portOption);
    parser.process(a);
    bool debug = parser.isSet(dbgOption);
    int port = parser.value(portOption).toInt();

    GameServer *server = new GameServer(port, debug);
    QObject::connect(server, &GameServer::closed, &a, &QCoreApplication::quit);

    return a.exec();
}
