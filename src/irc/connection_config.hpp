#pragma once

#include <QString>
#include <QStringList>

namespace irc {

// Which address family to resolve and connect to. `Any` keeps the resolver's
// order (which is DNS-preference sorted, usually IPv6 first on dual-stack
// hosts); IPv4/IPv6 restrict the attempt to that family only, which is what
// an IPv6-only (or IPv4-only) server needs.
enum class AddressFamily {
    Any,
    IPv4,
    IPv6,
};

// Immutable connection parameters. Built once (e.g. from a dialog or CLI
// args), then passed to IrcConnection::connectToServer.
struct ConnectionConfig {
    QString host;        // hostname, or IPv4/IPv6 literal without brackets
    quint16 port = 6667;
    bool useTls = false;
    AddressFamily family = AddressFamily::Any;
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

// Human-readable label for an address family, used in error/status messages.
inline QString addressFamilyLabel(AddressFamily family) {
    switch (family) {
    case AddressFamily::IPv4:
        return QStringLiteral("IPv4");
    case AddressFamily::IPv6:
        return QStringLiteral("IPv6");
    case AddressFamily::Any:
        break;
    }
    return QStringLiteral("IPv4/IPv6");
}

} // namespace irc
