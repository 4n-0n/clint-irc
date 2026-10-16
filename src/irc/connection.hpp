#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QSslError>

#include <functional>

#include "connection_config.hpp"
#include "message.hpp"

class QTimer;

namespace irc {

// Explicit connection lifecycle states.
enum class ConnectionState {
    Disconnected, // no active socket
    Connecting,   // TCP/TLS handshake in progress
    Registering,  // connected; NICK/USER sent; waiting for 001
    Connected,    // fully registered and usable
};

// Owns the IRC network session. Drives a QAbstractSocket (TLS or plain),
// buffers partial lines, handles PING/PONG, and emits parsed messages plus
// state changes. Contains no UI code.
//
// Dependency injection: the socket is supplied by a factory function
// (defaults to creating a QSslSocket, which also serves plain TCP).
// Tests can inject a mock socket.
class IrcConnection : public QObject {
    Q_OBJECT
public:
    using SocketFactory = std::function<QAbstractSocket*()>;

    explicit IrcConnection(QObject* parent = nullptr);
    explicit IrcConnection(SocketFactory factory, QObject* parent = nullptr);

    void connectToServer(const ConnectionConfig& config);
    // Sends QUIT (if connected) then closes. `quitMessage` is sanitized.
    void disconnectFromServer(const QString& quitMessage = QString());

    // Sends a raw line (CRLF appended). Returns false if not connected.
    bool sendRaw(const QByteArray& line);
    // Sends a command with params (trailing-':' handled by the encoder).
    bool sendCommand(const QByteArray& command,
                     const QList<QByteArray>& params = {});

    [[nodiscard]] ConnectionState state() const { return m_state; }
    [[nodiscard]] const ConnectionConfig& config() const { return m_config; }

signals:
    void stateChanged(ConnectionState newState);
    void messageReceived(const irc::Message& message);
    void rawLineReceived(const QByteArray& line); // debug hook
    // Renamed from "error" to avoid clashing with QObject::error().
    void errorOccurred(const QString& message);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError err);
    void onSslErrors(const QList<QSslError>& errors);

private:
    // SASL PLAIN negotiation substates (only entered when SASL is configured).
    enum class SaslState {
        Disabled,     // no SASL configured; skip negotiation
        CapLs,        // sent CAP LS 302; awaiting the server's CAP * LS
        CapReq,       // sent CAP REQ :sasl; awaiting CAP * ACK/NAK
        Authenticate, // sent AUTHENTICATE PLAIN; awaiting AUTHENTICATE +
        AuthPayload,  // sent AUTHENTICATE <b64>; awaiting 903/904/905/906
        Done,         // negotiation finished (success or failure); registering
    };

    void setState(ConnectionState state);
    void processBuffer();
    void handleLine(const QByteArray& line);

    // Sends NICK + USER (the registration handshake), shared by the plain and
    // SASL paths.
    void sendRegistration();
    // Advances the SASL state machine for a CAP/AUTHENTICATE/numeric message.
    void handleSasl(const Message& msg);
    // Wraps up SASL (CAP END + NICK/USER), regardless of outcome.
    void finishSasl();

    SocketFactory m_factory;
    QAbstractSocket* m_socket = nullptr;
    ConnectionState m_state = ConnectionState::Disconnected;
    ConnectionConfig m_config;
    QByteArray m_buffer; // accumulates partial lines
    SaslState m_saslState = SaslState::Disabled;
    QTimer* m_saslTimer = nullptr; // aborts a stalled CAP negotiation
};

} // namespace irc
