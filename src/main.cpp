#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

#include "app/client_controller.hpp"
#include "irc/connection.hpp"
#include "irc/connection_config.hpp"
#include "ui/main_window.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("clint"));
    QApplication::setApplicationName(QStringLiteral("clint"));
    QApplication::setApplicationVersion(QStringLiteral("0.2.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("clint - a minimal IRC client"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption serverOpt(QStringList{"s", "server"},
                                 QStringLiteral("IRC server host"), QStringLiteral("host"));
    QCommandLineOption portOpt(QStringList{"p", "port"},
                               QStringLiteral("IRC server port"), QStringLiteral("port"),
                               QStringLiteral("6667"));
    QCommandLineOption tlsOpt(QStringList{"tls"}, QStringLiteral("Use TLS/SSL"));
    QCommandLineOption nickOpt(QStringList{"n", "nick"},
                               QStringLiteral("Nickname"), QStringLiteral("nick"),
                               QStringLiteral("clint_user"));
    QCommandLineOption channelOpt(QStringList{"c", "channel"},
                                  QStringLiteral("Auto-join channel (repeatable)"),
                                  QStringLiteral("channel"));
    QCommandLineOption saslUserOpt(QStringList{"sasl-user"},
                                   QStringLiteral("SASL PLAIN auth identity"),
                                   QStringLiteral("user"));
    QCommandLineOption saslPassOpt(QStringList{"sasl-password"},
                                   QStringLiteral("SASL PLAIN password"),
                                   QStringLiteral("password"));
    QCommandLineOption listOpt(QStringList{"list"},
                               QStringLiteral("Request the channel list after connecting"));
    QCommandLineOption quitAfterOpt(QStringList{"quit-after"},
                                    QStringLiteral("Quit after N ms (for testing)"),
                                    QStringLiteral("ms"), QStringLiteral("0"));
    parser.addOption(serverOpt);
    parser.addOption(portOpt);
    parser.addOption(tlsOpt);
    parser.addOption(nickOpt);
    parser.addOption(channelOpt);
    parser.addOption(saslUserOpt);
    parser.addOption(saslPassOpt);
    parser.addOption(listOpt);
    parser.addOption(quitAfterOpt);
    parser.process(app);

    app::ClientController controller;
    ui::MainWindow window(&controller);
    window.show();

    // Establish the /list hook BEFORE connecting, so the Connected transition
    // is never missed.
    if (parser.isSet(listOpt)) {
        QObject::connect(&controller, &app::ClientController::stateChanged, &controller,
                         [&controller](irc::ConnectionState state) {
                             if (state == irc::ConnectionState::Connected) {
                                 controller.listChannels(QString());
                             }
                         });
    }

    if (parser.isSet(serverOpt)) {
        irc::ConnectionConfig cfg;
        cfg.host = parser.value(serverOpt);
        cfg.port = static_cast<quint16>(parser.value(portOpt).toUInt());
        cfg.useTls = parser.isSet(tlsOpt);
        cfg.nick = parser.value(nickOpt);
        cfg.realName = cfg.nick;
        cfg.saslUser = parser.value(saslUserOpt);
        cfg.saslPassword = parser.value(saslPassOpt);
        cfg.autoJoinChannels = parser.values(channelOpt);
        controller.connectToServer(cfg);
    }

    const int quitAfter = parser.value(quitAfterOpt).toInt();
    if (quitAfter > 0) {
        QTimer::singleShot(quitAfter, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
