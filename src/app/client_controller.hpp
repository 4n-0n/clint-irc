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
    void joinChannel(const QString& channel);
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
    void serverMessage(const QString& html);
    void channelMessage(const QString& channel, const QString& html);
    void queryMessage(const QString& peer, const QString& html);
    // Emitted when a channel message mentions our own nick (in addition to
    // channelMessage, whose HTML is already highlight-styled). Lets the UI
    // escalate the tab marker and alert the user.
    void channelHighlighted(const QString& channel);
    void channelJoined(const QString& channel);
    void channelParted(const QString& channel);
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
    QString m_prefixSymbols = QStringLiteral("@+"); // ISUPPORT PREFIX (RFC default)
    int m_listCount = 0;
};

} // namespace app
