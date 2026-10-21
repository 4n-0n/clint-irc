#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QHostAddress>
#include <QHostInfo>
#include <QList>
#include <QObject>
#include <QSslError>
#include <QStringList>

#include <functional>

#include "connection_config.hpp"
#include "message.hpp"

class QTimer;

namespace irc {

// Explicit connection lifecycle states.
enum class ConnectionState {
    Disconnected, // no active socket
    Connecting,   // resolving / TCP connect / TLS handshake in progress
    Registering,  // connected; NICK/USER sent; waiting for 001
    Connected,    // fully registered and usable
};

// Owns the IRC network session. Drives a QAbstractSocket (TLS or plain),
// buffers partial lines, handles PING/PONG, and emits parsed messages plus
// state changes. Contains no UI code.
//
// Address handling: the host is parsed by parseEndpoint() (bracketed IPv6
// literals and "host:port" are accepted), resolved asynchronously with
// QHostInfo, and every resolved address is tried in turn until one accepts the
// connection. That is what makes IPv6-only servers work regardless of whether
// they are reached by literal or by a AAAA-only name, and it keeps a stale
// (unreachable) IPv4 record from stalling the connect indefinitely:
// per-address budget is kConnectTimeoutMs.
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
    // Remote endpoint actually connected to ("[::1]:6697"), empty before that.
    [[nodiscard]] QString peerEndpoint() const { return m_peerEndpoint; }

signals:
    void stateChanged(ConnectionState newState);
    void messageReceived(const irc::Message& message);
    void rawLineReceived(const QByteArray& line); // debug hook
    // Renamed from "error" to avoid clashing with QObject::error().
    void errorOccurred(const QString& message);
    // Emitted once the transport is up and the remote address is known, before
    // registration is sent. Lets the UI show "[::1]:6697 (IPv6)".
    void transportConnected(const QString& peerEndpoint);

private slots:
    void onConnected();
    void onEncrypted();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError err);
    void onSslErrors(const QList<QSslError>& errors);
    void onAttemptTimeout();

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

    // --- address resolution / connect attempts -----------------------------
    // Starts resolution (literal addresses bypass DNS entirely).
    void startResolution();
    void onLookupFinished(const QHostInfo& info);
    // Opens a socket to the next resolved address, or reports failure when the
    // list is exhausted.
    void tryNextAddress();
    // Records why the current attempt failed and moves on.
    void attemptFailed(const QString& reason);
    // Drops an unusable socket without emitting disconnected().
    void dropSocket();
    // Aborts resolution, closes the socket and clears per-session state.
    void teardown();
    // Reports the accumulated failure and goes back to Disconnected.
    void failConnect(const QString& summary);

    // --- registration ------------------------------------------------------
    // Sends NICK + USER (the registration handshake), shared by the plain and
    // SASL paths.
    void sendRegistration();
    // Advances the SASL state machine for a CAP/AUTHENTICATE/numeric message.
    void handleSasl(const Message& msg);
    // Wraps up SASL (CAP END + NICK/USER), regardless of outcome.
    void finishSasl();
    // Transport (TCP, plus TLS when enabled) is up: begin registration.
    void onTransportReady();

    SocketFactory m_factory;
    QAbstractSocket* m_socket = nullptr;
    ConnectionState m_state = ConnectionState::Disconnected;
    ConnectionConfig m_config;
    QByteArray m_buffer; // accumulates partial lines
    SaslState m_saslState = SaslState::Disabled;
    QTimer* m_saslTimer = nullptr;   // aborts a stalled CAP negotiation
    QTimer* m_attemptTimer = nullptr; // per-address connect budget

    QList<QHostAddress> m_addresses; // resolved, family-filtered, in DNS order
    int m_addressIndex = 0;
    QStringList m_failedAttempts;    // "<address>: <reason>" per failed attempt
    QString m_currentAttempt;        // human-readable address being tried
    QString m_peerEndpoint;          // address actually connected to
    int m_lookupId = -1;             // pending QHostInfo lookup, -1 when none
    quint64 m_generation = 0;        // invalidates stale DNS callbacks
};

} // namespace irc
