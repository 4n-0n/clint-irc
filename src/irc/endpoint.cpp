#include "endpoint.hpp"

#include <QHostAddress>
#include <QStringList>

namespace irc {

namespace {

// Parses a decimal port in 1..65535; returns 0 when it is not a valid port.
quint16 parsePort(const QString& text) {
    bool ok = false;
    const uint value = text.trimmed().toUInt(&ok);
    if (!ok || value == 0 || value > 65535) {
        return 0;
    }
    return static_cast<quint16>(value);
}

bool isLiteral(const QString& host, bool* ipv6 = nullptr) {
    QHostAddress address;
    if (!address.setAddress(host)) {
        if (ipv6) {
            *ipv6 = false;
        }
        return false;
    }
    const bool v6 = (address.protocol() == QAbstractSocket::IPv6Protocol);
    if (ipv6) {
        *ipv6 = v6;
    }
    return true;
}

} // namespace

Endpoint parseEndpoint(const QString& raw) {
    Endpoint ep;
    const QString text = raw.trimmed();
    if (text.isEmpty()) {
        return ep;
    }

    // Bracketed form: [address] or [address]:port. Anything after the closing
    // bracket other than ":port" is ignored.
    if (text.startsWith(QLatin1Char('['))) {
        const int close = text.indexOf(QLatin1Char(']'));
        if (close > 1) {
            ep.host = text.mid(1, close - 1).trimmed();
            const QString rest = text.mid(close + 1).trimmed();
            if (rest.startsWith(QLatin1Char(':'))) {
                ep.port = parsePort(rest.mid(1));
            }
            bool v6 = false;
            ep.isAddressLiteral = isLiteral(ep.host, &v6);
            ep.isIpv6 = v6;
            if (ep.host.isEmpty()) {
                return Endpoint{};
            }
            return ep;
        }
    }

    // Exactly one colon: "host:port". Zero colons: just a host.
    const int colons = static_cast<int>(text.count(QLatin1Char(':')));
    if (colons == 1) {
        const int sep = text.indexOf(QLatin1Char(':'));
        const QString maybePort = text.mid(sep + 1);
        const quint16 port = parsePort(maybePort);
        if (port != 0) {
            ep.host = text.left(sep).trimmed();
            ep.port = port;
        } else {
            // Not a port (e.g. "example.com:ircd"): keep the whole string and
            // let resolution report the failure.
            ep.host = text;
        }
    } else {
        // Zero colons, or several (a bare IPv6 literal, which has no port).
        ep.host = text;
    }

    bool v6 = false;
    ep.isAddressLiteral = isLiteral(ep.host, &v6);
    ep.isIpv6 = v6;
    return ep;
}

QString formatEndpoint(const QString& host, quint16 port) {
    QString hostText = host.trimmed();
    if (isIpv6Literal(hostText) && !hostText.startsWith(QLatin1Char('['))) {
        hostText = QLatin1Char('[') + hostText + QLatin1Char(']');
    }
    return QStringLiteral("%1:%2").arg(hostText).arg(port);
}

bool isIpv6Literal(const QString& s) {
    bool v6 = false;
    return isLiteral(s.trimmed(), &v6) && v6;
}

} // namespace irc
