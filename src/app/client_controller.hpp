#pragma once

#include "../irc/connection.hpp"
#include "../irc/connection_config.hpp"

#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>

namespace app {

// Session/state layer between the raw IRC connection and the UI.
// Owns the connection, tracks channel membership and nick lists, and routes
// incoming messages to semantic signals. Contains no UI code.
//
// Channel lookups are case-insensitive: keys are lowercased, while the
// display name preserves the server's original casing.
class ClientController : public QObject {
    Q_OBJECT
public:
    explicit ClientController(QObject* parent = nullptr);

    // Connection lifecycle.
    void connectToServer(const irc::ConnectionConfig& config);
    void quit(const QString& message = QString());
    [[nodiscard]] bool isConnected() const;

    // Outgoing primitives (thin wrappers over the connection).
    void sendChat(const QString& target, const QString& text);
    void sendAction(const QString& target, const QString& text);
    void sendNotice(const QString& target, const QString& text);
    void sendCtcp(const QString& target, const QString& tag, const QString& argument = QString());
    // `key` is the channel key (+k); sent only when non-empty.
    void joinChannel(const QString& channel, const QString& key = QString());
    void partChannel(const QString& channel, const QString& reason = QString());
    void setNick(const QString& nick);
    void setTopic(const QString& channel, const QString& topic);
    void listChannels(const QString& filter = QString());
    void sendCommand(const QString& command, const QStringList& args);
    void sendRaw(const QString& line);

    // State accessors (for the UI).
    [[nodiscard]] QString nick() const { return m_nick; }
    [[nodiscard]] irc::ConnectionState state() const { return m_conn.state(); }
    [[nodiscard]] QStringList channels() const;

signals:
    void stateChanged(irc::ConnectionState state);
    // Transport up; `peer` is the resolved remote endpoint ("[::1]:6697"). Lets
    // the UI show which address an IPv6-only (or dual-stack) host reached.
    void transportConnected(const QString& peer);
    void serverMessage(const QString& html);
    void channelMessage(const QString& channel, const QString& html);
    void queryMessage(const QString& peer, const QString& html);
    // Emitted when a channel message mentions our own nick (in addition to
    // channelMessage, whose HTML is already highlight-styled). Lets the UI
    // escalate the tab marker and alert the user.
    void channelHighlighted(const QString& channel);
    void channelJoined(const QString& channel);
    void channelParted(const QString& channel);
    // Topic of a channel: set on join (332), changed by TOPIC, cleared by 331
    // or by leaving. The UI shows it above the chat instead of as a chat line.
    void channelTopicChanged(const QString& channel, const QString& topic);
    void nickListChanged(const QString& channel, const QStringList& nicks);
    void ownNickChanged(const QString& nick);
    void errorOccurred(const QString& message);
    // Channel list (/list) events.
    void listStarted();
    void listEntry(const QString& channel, int users, const QString& topic);
    void listEnded(int total);

private:
    // A channel member: clean nick plus its mode prefix symbols (e.g. "@", "+").
    struct NickEntry {
        QString nick;
        QString prefix;
    };

    struct ChannelState {
        QString displayName;
        QString topic;
        QList<NickEntry> nicks; // sorted by nick (case-insensitive)
    };

    void onMessage(const irc::Message& msg);
    void onStateChanged(irc::ConnectionState state);
    void onSocketError(const QString& message);

    void handleNumeric(const irc::Message& msg);
    void handlePrivmsg(const irc::Message& msg);
    void handleNotice(const irc::Message& msg);
    void handleJoin(const irc::Message& msg);
    void handlePart(const irc::Message& msg);
    void handleQuit(const irc::Message& msg);
    void handleKick(const irc::Message& msg);
    void handleNick(const irc::Message& msg);
    void handleMode(const irc::Message& msg);
    void handleTopic(const irc::Message& msg);
    // Applies a MODE line's prefix changes (+o/-o/+v/-v, ...) to the channel's
    // nick list. Returns true when at least one nick's prefix changed.
    bool applyChannelModes(const QString& key, const QByteArray& modes,
                           const QList<QByteArray>& args);
    void setChannelTopic(const QString& key, const QString& topic);

    // Returns true if the body was a CTCP request we replied to (and displayed).
    bool handleCtcpRequest(const irc::Message& msg);

    [[nodiscard]] QString channelKey(const QString& name) const;

    void addNick(const QString& key, const QString& nick, const QString& prefix);
    bool removeNick(const QString& key, const QString& nick);
    QStringList renameNick(const QString& from, const QString& to);
    void emitNickList(const QString& key);

    irc::IrcConnection m_conn;
    QString m_nick;
    QHash<QString, ChannelState> m_channels; // key (lowercase) -> state
    QString m_prefixSymbols = QStringLiteral("@+"); // ISUPPORT PREFIX symbols
    QString m_prefixModes = QStringLiteral("ov");   // ISUPPORT PREFIX letters
    int m_nickRetries = 0; // automatic nick suffixes sent during registration
    int m_listCount = 0;
};

} // namespace app
