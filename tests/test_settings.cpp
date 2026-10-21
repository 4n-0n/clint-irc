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
        s.setValue(QStringLiteral("family"), 2); // IPv6 only (combo index)
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
    ok = ok && (c.family == irc::AddressFamily::IPv6);
    ok = ok && (c.autoJoinChannels ==
                QStringList{QStringLiteral("#a"), QStringLiteral("#b")});

    // An IPv6 endpoint typed into the Server field: brackets are stripped and
    // the embedded port wins over the port field, without touching the socket
    // layer's expectations.
    {
        QSettings s;
        s.setValue(QStringLiteral("host"), QStringLiteral("[2001:db8::5]:6697"));
        s.setValue(QStringLiteral("port"), 6667);
        s.setValue(QStringLiteral("family"), 0);
    }
    {
        ui::ConnectDialog v6Dialog;
        const irc::ConnectionConfig v6 = v6Dialog.config();
        ok = ok && (v6.host == QStringLiteral("2001:db8::5"));
        ok = ok && (v6.port == 6697);
        ok = ok && (v6.family == irc::AddressFamily::IPv6);
        if (!(v6.host == QStringLiteral("2001:db8::5") && v6.port == 6697)) {
            std::printf("ipv6 dialog: host=%s port=%d\n", qPrintable(v6.host), v6.port);
        }
    }

    if (!ok) {
        std::printf("host=%s port=%d tls=%d nick=%s user=%s realname=%s channels=%s\n",
                    qPrintable(c.host), c.port, static_cast<int>(c.useTls), qPrintable(c.nick),
                    qPrintable(c.user), qPrintable(c.realName),
                    qPrintable(c.autoJoinChannels.join(QLatin1Char(','))));
    }
    std::printf("%s\n", ok ? "SETTINGS TEST PASSED" : "SETTINGS TEST FAILED");
    return ok ? 0 : 1;
}
