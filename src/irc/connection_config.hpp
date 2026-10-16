#pragma once

#include <QString>
#include <QStringList>

namespace irc {

// Immutable connection parameters. Built once (e.g. from a dialog or CLI
// args), then passed to IrcConnection::connectToServer.
struct ConnectionConfig {
    QString host;
    quint16 port = 6667;
    bool useTls = false;
    QString nick;
    QString user;        // empty -> fall back to nick
    QString realName;
    QString password;    // optional server password (PASS)
    QString saslUser;    // SASL PLAIN auth identity; empty -> no SASL
    QString saslPassword;// SASL PLAIN password (only used when saslUser set)
    QStringList autoJoinChannels; // e.g. "#chan1", "#chan2"

    // Username to send in the USER command; defaults to nick when unset.
    [[nodiscard]] QString effectiveUser() const {
        return user.isEmpty() ? nick : user;
    }
};

} // namespace irc
