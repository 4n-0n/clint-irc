#include "connection.hpp"

#include <QHostAddress>
#include <QHostInfo>
#include <QSslSocket>
#include <QStringList>
#include <QTimer>

#include "encoder.hpp"
#include "endpoint.hpp"
#include "parser.hpp"

namespace irc {

namespace {

// Default socket factory: a TLS-capable socket. Plain TCP is also supported
// by QSslSocket via connectToHost(); the config decides which path is taken.
QAbstractSocket* defaultSocketFactory() {
    return new QSslSocket();
}

// How long to wait for a CAP * LS reply before abandoning SASL negotiation
// and registering plainly. Guards against servers that ignore CAP LS.
constexpr int kSaslTimeoutMs = 5000;

// Per-address budget for TCP connect + TLS handshake. Without it a stale or
// firewalled record (a dead IPv4 next to a working IPv6 one, typically) would
// keep the whole connect stalled in the kernel's SYN retries.
constexpr int kConnectTimeoutMs = 15000;

bool isNumericCommand(const QByteArray& command) {
    if (command.size() != 3) {
        return false;
    }
    for (char c : command) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

// True if `list` (a space-separated capability list, e.g. "sasl multi-prefix")
// contains `cap`, allowing the "cap=value" form (e.g. "sasl=PLAIN,EXTERNAL").
bool capListContains(const QByteArray& list, const QByteArray& cap) {
    const QList<QByteArray> tokens = list.split(' ');
    for (const QByteArray& t : tokens) {
        if (t == cap) {
            return true;
        }
        if (t.size() > cap.size() && t.startsWith(cap) && t.at(cap.size()) == '=') {
            return true;
        }
    }
    return false;
}

// Effective family of a resolved address. IPv4-mapped IPv6 addresses
// ("::ffff:192.0.2.1") are IPv4 for every practical purpose, including
// family filtering, since that is how a v4 host may come back from getaddrinfo.
QAbstractSocket::NetworkLayerProtocol transportFamily(const QHostAddress& address) {
    if (address.protocol() == QAbstractSocket::IPv6Protocol &&
        address.toString().startsWith(QLatin1String("::ffff:"))) {
        return QAbstractSocket::IPv4Protocol;
    }
    return address.protocol();
}

bool addressMatchesFamily(const QHostAddress& address, AddressFamily family) {
    const auto protocol = transportFamily(address);
    switch (family) {
    case AddressFamily::IPv4:
        return protocol == QAbstractSocket::IPv4Protocol;
    case AddressFamily::IPv6:
        return protocol == QAbstractSocket::IPv6Protocol;
    case AddressFamily::Any:
        break;
    }
    return protocol == QAbstractSocket::IPv4Protocol ||
           protocol == QAbstractSocket::IPv6Protocol;
}

QString describeAddress(const QHostAddress& address, quint16 port) {
    QHostAddress shown = address;
    if (transportFamily(address) == QAbstractSocket::IPv4Protocol &&
        address.protocol() == QAbstractSocket::IPv6Protocol) {
        // Show a v4-mapped address as plain IPv4.
        const QString text = address.toString();
        QHostAddress plain;
        if (plain.setAddress(text.mid(7))) {
            shown = plain;
        }
    }
    return formatEndpoint(shown.toString(), port);
}

} // namespace

IrcConnection::IrcConnection(QObject* parent)
    : IrcConnection(defaultSocketFactory, parent) {}

IrcConnection::IrcConnection(SocketFactory factory, QObject* parent)
    : QObject(parent), m_factory(std::move(factory)) {
    // Safety net: if the server never answers CAP LS, abandon SASL and
    // register plainly after the timeout.
    m_saslTimer = new QTimer(this);
    m_saslTimer->setSingleShot(true);
    connect(m_saslTimer, &QTimer::timeout, this, [this] { finishSasl(); });

    m_attemptTimer = new QTimer(this);
    m_attemptTimer->setSingleShot(true);
    connect(m_attemptTimer, &QTimer::timeout, this, &IrcConnection::onAttemptTimeout);
}

void IrcConnection::connectToServer(const ConnectionConfig& config) {
    teardown();
    setState(ConnectionState::Disconnected); // normalize baseline

    // Accept "host", "host:port", "[v6]", "[v6]:port" and bare v6 literals, so
    // a server string can be pasted straight in. An explicit port in the host
    // string wins over the separate port field.
    const Endpoint endpoint = parseEndpoint(config.host);
    if (endpoint.host.isEmpty()) {
        emit errorOccurred(QStringLiteral("No server host given."));
        return;
    }

    m_config = config;
    m_config.host = endpoint.host;
    if (endpoint.port != 0) {
        m_config.port = endpoint.port;
    }

    m_saslState = SaslState::Disabled;
    m_failedAttempts.clear();
    m_peerEndpoint.clear();
    setState(ConnectionState::Connecting);
    startResolution();
}

void IrcConnection::teardown() {
    ++m_generation; // any in-flight lookup callback is now stale
    if (m_lookupId != -1) {
        QHostInfo::abortHostLookup(m_lookupId);
        m_lookupId = -1;
    }
    m_attemptTimer->stop();
    m_saslTimer->stop();
    dropSocket();
    m_buffer.clear();
    m_addresses.clear();
    m_addressIndex = 0;
    m_currentAttempt.clear();
    m_peerEndpoint.clear();
    m_saslState = SaslState::Disabled;
}

void IrcConnection::dropSocket() {
    if (!m_socket) {
        return;
    }
    m_socket->disconnect(this);
    m_socket->abort();
    m_socket->deleteLater();
    m_socket = nullptr;
}

void IrcConnection::startResolution() {
    QHostAddress literal;
    if (literal.setAddress(m_config.host)) {
        // No DNS for literal addresses: go straight to the connect attempt.
        if (!addressMatchesFamily(literal, m_config.family)) {
            failConnect(QStringLiteral("%1 is not an %2 address.")
                            .arg(m_config.host, addressFamilyLabel(m_config.family)));
            return;
        }
        m_addresses = {literal};
        m_addressIndex = 0;
        tryNextAddress();
        return;
    }

    const quint64 generation = m_generation;
    m_lookupId = QHostInfo::lookupHost(m_config.host, this, [this, generation](
                                                                  const QHostInfo& info) {
        if (generation != m_generation) {
            return; // superseded by a newer connect/teardown
        }
        m_lookupId = -1;
        onLookupFinished(info);
    });
}

void IrcConnection::onLookupFinished(const QHostInfo& info) {
    if (info.error() != QHostInfo::NoError) {
        failConnect(QStringLiteral("Cannot resolve %1: %2")
                        .arg(m_config.host, info.errorString()));
        return;
    }

    QList<QHostAddress> usable;
    for (const QHostAddress& address : info.addresses()) {
        if (addressMatchesFamily(address, m_config.family)) {
            usable << address;
        }
    }

    if (usable.isEmpty()) {
        failConnect(QStringLiteral("%1 has no %2 address.")
                        .arg(m_config.host, addressFamilyLabel(m_config.family)));
        return;
    }

    // Keep the resolver's order: getaddrinfo is already sorted by destination
    // preference (RFC 6724), which puts IPv6 first on a dual-stack host.
    m_addresses = usable;
    m_addressIndex = 0;
    tryNextAddress();
}

void IrcConnection::tryNextAddress() {
    if (m_addressIndex >= m_addresses.size()) {
        const QString tried = m_failedAttempts.isEmpty()
                                  ? QStringLiteral("no address available")
                                  : m_failedAttempts.join(QStringLiteral("; "));
        failConnect(QStringLiteral("Could not connect to %1:%2 (%3) — %4")
                        .arg(m_config.host)
                        .arg(m_config.port)
                        .arg(addressFamilyLabel(m_config.family), tried));
        return;
    }

    const QHostAddress address = m_addresses.at(m_addressIndex++);
    m_currentAttempt = describeAddress(address, m_config.port);

    m_socket = m_factory();
    if (!m_socket) {
        attemptFailed(QStringLiteral("socket factory returned null"));
        return;
    }
    m_socket->setParent(this);

    connect(m_socket, &QAbstractSocket::connected, this, &IrcConnection::onConnected);
    connect(m_socket, &QAbstractSocket::disconnected, this, &IrcConnection::onDisconnected);
    connect(m_socket, &QAbstractSocket::readyRead, this, &IrcConnection::onReadyRead);
    connect(m_socket, &QAbstractSocket::errorOccurred, this, &IrcConnection::onSocketError);

    if (auto* ssl = qobject_cast<QSslSocket*>(m_socket)) {
        connect(ssl, &QSslSocket::sslErrors, this, &IrcConnection::onSslErrors);
        connect(ssl, &QSslSocket::encrypted, this, &IrcConnection::onEncrypted);
        // IRC servers commonly present self-signed certs; accept them.
        ssl->setPeerVerifyMode(QSslSocket::VerifyNone);
        // TLS identity (SNI, and the verification name if it were enabled)
        // stays the *host name* even though we connect to a resolved address:
        // for an IPv6 literal that is the literal itself, never a bracket form.
        ssl->setPeerVerifyName(m_config.host);
    } else if (m_config.useTls) {
        attemptFailed(QStringLiteral("socket does not support TLS"));
        return;
    }

    m_socket->connectToHost(address, m_config.port);
    m_attemptTimer->start(kConnectTimeoutMs);
}

void IrcConnection::attemptFailed(const QString& reason) {
    const QString attempt = m_currentAttempt.isEmpty() ? m_config.host : m_currentAttempt;
    m_failedAttempts << QStringLiteral("%1: %2").arg(attempt, reason);

    // With a single candidate the final summary says it all; with several,
    // each failure is worth showing so a dual-stack diagnosis is possible.
    if (m_addresses.size() > 1) {
        emit errorOccurred(
            QStringLiteral("Connection attempt to %1 failed: %2").arg(attempt, reason));
    }

    m_attemptTimer->stop();
    dropSocket();
    tryNextAddress();
}

void IrcConnection::failConnect(const QString& summary) {
    m_attemptTimer->stop();
    dropSocket();
    m_failedAttempts.clear();
    m_addresses.clear();
    m_addressIndex = 0;
    setState(ConnectionState::Disconnected);
    emit errorOccurred(summary);
}

void IrcConnection::disconnectFromServer(const QString& quitMessage) {
    // Still resolving, or the TCP/TLS handshake never completed: nothing to
    // say goodbye with, just drop it.
    if (m_state == ConnectionState::Connecting || !m_socket ||
        m_socket->state() != QAbstractSocket::ConnectedState) {
        teardown();
        setState(ConnectionState::Disconnected);
        return;
    }

    // Capture locally: disconnectFromHost()/abort() may synchronously emit
    // disconnected(), which nulls m_socket via onDisconnected(). Never touch
    // m_socket after initiating the close; use the local pointer instead.
    QAbstractSocket* socket = m_socket;

    if (m_state == ConnectionState::Connected) {
        QByteArray line = "QUIT";
        if (!quitMessage.isEmpty()) {
            QByteArray msg = quitMessage.toUtf8();
            msg.replace('\r', ' '); // sanitize: no CRLF injection
            msg.replace('\n', ' ');
            line += " :";
            line += msg;
        }
        socket->write(withCrlf(line));
        socket->flush();
        socket->disconnectFromHost(); // graceful: flush then close
    } else {
        socket->abort(); // not registered: close immediately
    }

    // If the socket closed synchronously, onDisconnected() already ran and
    // nulled m_socket; otherwise fall through to the disconnected() signal.
    if (m_socket && m_socket->state() == QAbstractSocket::UnconnectedState) {
        onDisconnected();
    }
}

bool IrcConnection::sendRaw(const QByteArray& line) {
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        return false;
    }
    m_socket->write(withCrlf(line));
    m_socket->flush();
    return true;
}

bool IrcConnection::sendCommand(const QByteArray& command,
                                const QList<QByteArray>& params) {
    return sendRaw(encodeCommand(command, params));
}

void IrcConnection::onConnected() {
    // TCP is up. With TLS the handshake still has to run before any IRC line
    // may be written, so registration waits for onEncrypted().
    if (m_config.useTls) {
        if (auto* ssl = qobject_cast<QSslSocket*>(m_socket)) {
            ssl->startClientEncryption();
            return; // handshake outcome arrives via encrypted()/errorOccurred()
        }
        attemptFailed(QStringLiteral("socket does not support TLS"));
        return;
    }
    onTransportReady();
}

void IrcConnection::onEncrypted() {
    onTransportReady();
}

void IrcConnection::onTransportReady() {
    if (!m_socket) {
        return;
    }
    m_attemptTimer->stop();
    m_peerEndpoint = describeAddress(m_socket->peerAddress(), m_config.port);
    emit transportConnected(m_peerEndpoint);

    // Begin IRC registration handshake.
    if (!m_config.password.isEmpty()) {
        sendRaw("PASS " + m_config.password.toUtf8());
    }

    if (m_config.saslUser.isEmpty()) {
        sendRegistration(); // no SASL configured: register immediately
    } else {
        // SASL configured: negotiate IRCv3 capabilities before NICK/USER.
        m_saslState = SaslState::CapLs;
        sendRaw("CAP LS 302");
        m_saslTimer->start(kSaslTimeoutMs);
    }

    setState(ConnectionState::Registering);
}

void IrcConnection::sendRegistration() {
    sendCommand("NICK", {m_config.nick.toUtf8()});
    sendCommand("USER", {
        m_config.effectiveUser().toUtf8(),
        "0", // mode
        "*", // unused
        m_config.realName.toUtf8(),
    });
}

void IrcConnection::onDisconnected() {
    dropSocket();
    m_buffer.clear();
    m_saslTimer->stop();
    m_attemptTimer->stop();
    m_saslState = SaslState::Disabled;
    m_peerEndpoint.clear();
    setState(ConnectionState::Disconnected);
}

void IrcConnection::onReadyRead() {
    if (!m_socket) {
        return;
    }
    m_buffer += m_socket->readAll();
    processBuffer();
}

void IrcConnection::processBuffer() {
    int newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (line.isEmpty()) {
            continue;
        }
        handleLine(line);
    }
}

void IrcConnection::handleLine(const QByteArray& line) {
    emit rawLineReceived(line);

    auto result = parseLine(line);
    if (result.isErr()) {
        emit errorOccurred(QStringLiteral("Unparseable line: %1")
                               .arg(QString::fromUtf8(line)));
        return;
    }

    Message msg = std::move(result.value());

    // PING: reply with the exact token text (RFC 2812: echo everything after
    // "PING" back in a PONG). Rebuilt from the parsed params rather than the
    // raw line, so IRCv3 tags in front of the command cannot leak into it.
    if (msg.command == "PING") {
        sendCommand("PONG", msg.params);
    }

    // Route CAP/AUTHENTICATE/SASL numerics into the negotiation state machine
    // while it is still in progress.
    if (m_saslState != SaslState::Done && m_saslState != SaslState::Disabled) {
        handleSasl(msg);
    }

    // Registration completes on the 001 welcome.
    if (msg.command == "001" && m_state == ConnectionState::Registering) {
        setState(ConnectionState::Connected);
        for (const QString& channel : m_config.autoJoinChannels) {
            sendCommand("JOIN", {channel.toUtf8()});
        }
    }

    emit messageReceived(msg);
}

void IrcConnection::handleSasl(const Message& msg) {
    const QByteArray& cmd = msg.command;

    if (cmd == "CAP") {
        if (m_saslState == SaslState::CapLs) {
            // "CAP * LS :cap1 cap2 ..." -> request sasl if offered.
            if (capListContains(msg.params.value(2), "sasl")) {
                m_saslState = SaslState::CapReq;
                sendRaw("CAP REQ :sasl");
            } else {
                finishSasl(); // server has no SASL: register plainly
            }
            return;
        }
        if (m_saslState == SaslState::CapReq) {
            // "CAP * ACK/NAK :sasl"
            const bool ack = (msg.params.value(1) == "ACK");
            if (ack && capListContains(msg.params.value(2), "sasl")) {
                m_saslState = SaslState::Authenticate;
                sendRaw("AUTHENTICATE PLAIN");
            } else {
                finishSasl();
            }
            return;
        }
        return;
    }

    if (cmd == "AUTHENTICATE") {
        // "AUTHENTICATE +" -> send the base64 SASL PLAIN payload
        // (RFC 4616: authzid NUL authcid NUL passwd; authzid empty here).
        if (m_saslState == SaslState::Authenticate && msg.params.value(0) == "+") {
            m_saslState = SaslState::AuthPayload;
            QByteArray payload;
            payload += '\0';
            payload += m_config.saslUser.toUtf8();
            payload += '\0';
            payload += m_config.saslPassword.toUtf8();
            sendRaw("AUTHENTICATE " + payload.toBase64());
        }
        return;
    }

    if (isNumericCommand(cmd)) {
        const int code = cmd.toInt();
        if (code == 900 || code == 903) { // RPL_LOGGEDIN / RPL_SASLSUCCESS
            finishSasl();
        } else if (code == 904 || code == 905 || code == 906 || code == 907) {
            // SASL failed; report it, then fall back to plain registration.
            emit errorOccurred(QStringLiteral("SASL authentication failed (%1)")
                                   .arg(QString::fromUtf8(cmd)));
            finishSasl();
        }
    }
}

void IrcConnection::finishSasl() {
    if (m_saslState == SaslState::Done || m_saslState == SaslState::Disabled) {
        return; // idempotent
    }
    m_saslTimer->stop();
    m_saslState = SaslState::Done;
    sendRaw("CAP END");
    sendRegistration();
}

void IrcConnection::onAttemptTimeout() {
    if (m_state != ConnectionState::Connecting) {
        return;
    }
    attemptFailed(QStringLiteral("timed out after %1 s").arg(kConnectTimeoutMs / 1000));
}

void IrcConnection::onSocketError(QAbstractSocket::SocketError err) {
    Q_UNUSED(err);
    if (!m_socket) {
        return;
    }
    const QString reason = m_socket->errorString();

    if (m_state == ConnectionState::Connecting && m_addresses.size() > 0) {
        // Still hunting for a working address: record and try the next one.
        if (m_socket->state() == QAbstractSocket::UnconnectedState) {
            attemptFailed(reason);
        }
        return;
    }

    emit errorOccurred(reason);

    // A failed connection attempt never emits disconnected(); normalize here.
    if (m_socket && m_socket->state() == QAbstractSocket::UnconnectedState &&
        m_state == ConnectionState::Registering) {
        onDisconnected();
    }
}

void IrcConnection::onSslErrors(const QList<QSslError>& errors) {
    QStringList messages;
    for (const QSslError& e : errors) {
        messages << e.errorString();
    }
    emit errorOccurred(QStringLiteral("TLS: %1").arg(messages.join("; ")));
    if (auto* ssl = qobject_cast<QSslSocket*>(m_socket)) {
        ssl->ignoreSslErrors(); // VerifyNone already set; this is belt-and-suspenders
    }
}

void IrcConnection::setState(ConnectionState state) {
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(state);
}

} // namespace irc
