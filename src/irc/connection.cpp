#include "connection.hpp"

#include <QSslSocket>
#include <QStringList>

#include "encoder.hpp"
#include "parser.hpp"

namespace irc {

namespace {

// Default socket factory: a TLS-capable socket. Plain TCP is also supported
// by QSslSocket via connectToHost(); the config decides which path is taken.
QAbstractSocket* defaultSocketFactory() {
    return new QSslSocket();
}

} // namespace

IrcConnection::IrcConnection(QObject* parent)
    : IrcConnection(defaultSocketFactory, parent) {}

IrcConnection::IrcConnection(SocketFactory factory, QObject* parent)
    : QObject(parent), m_factory(std::move(factory)) {}

void IrcConnection::connectToServer(const ConnectionConfig& config) {
    // Drop any prior socket; detach its signals so no stale callbacks fire.
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_buffer.clear();
    m_config = config;
    setState(ConnectionState::Disconnected); // normalize baseline

    m_socket = m_factory();
    m_socket->setParent(this);

    connect(m_socket, &QAbstractSocket::connected, this, &IrcConnection::onConnected);
    connect(m_socket, &QAbstractSocket::disconnected, this, &IrcConnection::onDisconnected);
    connect(m_socket, &QAbstractSocket::readyRead, this, &IrcConnection::onReadyRead);
    connect(m_socket, &QAbstractSocket::errorOccurred, this, &IrcConnection::onSocketError);

    if (auto* ssl = qobject_cast<QSslSocket*>(m_socket)) {
        connect(ssl, &QSslSocket::sslErrors, this, &IrcConnection::onSslErrors);
        // IRC servers commonly present self-signed certs; accept them.
        ssl->setPeerVerifyMode(QSslSocket::VerifyNone);
        if (config.useTls) {
            ssl->connectToHostEncrypted(config.host, config.port);
        } else {
            ssl->connectToHost(config.host, config.port);
        }
    } else {
        m_socket->connectToHost(config.host, config.port);
    }

    setState(ConnectionState::Connecting);
}

void IrcConnection::disconnectFromServer(const QString& quitMessage) {
    if (!m_socket) {
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
    // TCP/TLS established. Begin IRC registration handshake.
    if (!m_config.password.isEmpty()) {
        sendRaw("PASS " + m_config.password.toUtf8());
    }
    sendCommand("NICK", {m_config.nick.toUtf8()});
    sendCommand("USER", {
        m_config.effectiveUser().toUtf8(),
        "0", // mode
        "*", // unused
        m_config.realName.toUtf8(),
    });

    setState(ConnectionState::Registering);
}

void IrcConnection::onDisconnected() {
    if (m_socket) {
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_buffer.clear();
    setState(ConnectionState::Disconnected);
}

void IrcConnection::onReadyRead() {
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
    // "PING" back in a PONG).
    if (msg.command == "PING") {
        QByteArray reply = msg.raw;
        reply.replace(0, 4, "PONG");
        sendRaw(reply);
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

void IrcConnection::onSocketError(QAbstractSocket::SocketError err) {
    Q_UNUSED(err);
    emit errorOccurred(m_socket ? m_socket->errorString() : QString());

    // A failed connection attempt never emits disconnected(); normalize here.
    if (m_socket && m_socket->state() == QAbstractSocket::UnconnectedState &&
        (m_state == ConnectionState::Connecting ||
         m_state == ConnectionState::Registering)) {
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
