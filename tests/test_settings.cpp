#include <QApplication>
#include <QSettings>

#include <cstdio>

#include "irc/connection_config.hpp"
#include "ui/connect_dialog.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("clint"));
    QApplication::setApplicationName(QStringLiteral("clint"));

    // Isolate from the user's real config so this test never clobbers it.
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope,
                       QStringLiteral("/tmp/clint_test_settings"));

    // Seed settings with known values (as a prior accept() would have done).
    {
        QSettings s;
        s.setValue(QStringLiteral("host"), QStringLiteral("irc.example.com"));
        s.setValue(QStringLiteral("port"), 7000);
        s.setValue(QStringLiteral("tls"), false);
        s.setValue(QStringLiteral("nick"), QStringLiteral("tester"));
        s.setValue(QStringLiteral("user"), QStringLiteral("tuser"));
        s.setValue(QStringLiteral("realname"), QStringLiteral("Test Real"));
        s.setValue(QStringLiteral("sasluser"), QStringLiteral("saslme"));
        s.setValue(QStringLiteral("channels"), QStringLiteral("#a #b"));
    }

    // The dialog constructor must load those persisted values.
    ui::ConnectDialog dialog;
    const irc::ConnectionConfig c = dialog.config();

    bool ok = true;
    ok = ok && (c.host == QStringLiteral("irc.example.com"));
    ok = ok && (c.port == 7000);
    ok = ok && (c.useTls == false);
    ok = ok && (c.nick == QStringLiteral("tester"));
    ok = ok && (c.user == QStringLiteral("tuser"));
    ok = ok && (c.realName == QStringLiteral("Test Real"));
    ok = ok && (c.saslUser == QStringLiteral("saslme"));
    ok = ok && (c.autoJoinChannels ==
                QStringList{QStringLiteral("#a"), QStringLiteral("#b")});

    if (!ok) {
        std::printf("host=%s port=%d tls=%d nick=%s user=%s realname=%s channels=%s\n",
                    qPrintable(c.host), c.port, static_cast<int>(c.useTls), qPrintable(c.nick),
                    qPrintable(c.user), qPrintable(c.realName),
                    qPrintable(c.autoJoinChannels.join(QLatin1Char(','))));
    }
    std::printf("%s\n", ok ? "SETTINGS TEST PASSED" : "SETTINGS TEST FAILED");
    return ok ? 0 : 1;
}
