#include "client_controller.hpp"

#include "../irc/ctcp.hpp"
#include "../irc/prefix.hpp"
#include "../ui/chat_formatter.hpp"

#include <QDateTime>

#include <algorithm>

namespace app {

namespace {

// True if the string is an IRC channel name (prefix char per RFC 2812).
bool isChannelName(const QString& name) {
    return !name.isEmpty() && QStringLiteral("#&+!").contains(name.at(0));
}

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

} // namespace

ClientController::ClientController(QObject* parent) : QObject(parent) {
    connect(&m_conn, &irc::IrcConnection::messageReceived, this, &ClientController::onMessage);
    connect(&m_conn, &irc::IrcConnection::stateChanged, this, &ClientController::onStateChanged);
    connect(&m_conn, &irc::IrcConnection::errorOccurred, this, &ClientController::onSocketError);
}

void ClientController::connectToServer(const irc::ConnectionConfig& config) {
    m_nick = config.nick;
    m_channels.clear();
    m_prefixSymbols = QStringLiteral("@+"); // RFC 1459 default; updated by 005
    m_conn.connectToServer(config);
}

void ClientController::quit(const QString& message) {
    m_conn.disconnectFromServer(message);
}

bool ClientController::isConnected() const {
    return m_conn.state() == irc::ConnectionState::Connected;
}

QStringList ClientController::channels() const {
    QStringList out;
    out.reserve(m_channels.size());
    for (const ChannelState& st : m_channels) {
        out << st.displayName;
    }
    return out;
}

// ---- Outgoing primitives -------------------------------------------------

void ClientController::sendChat(const QString& target, const QString& text) {
    m_conn.sendCommand("PRIVMSG", {target.toUtf8(), text.toUtf8()});
}

void ClientController::sendAction(const QString& target, const QString& text) {
    m_conn.sendCommand("PRIVMSG",
                       {target.toUtf8(), irc::ctcp::encode("ACTION", text.toUtf8())});
}

void ClientController::sendNotice(const QString& target, const QString& text) {
    m_conn.sendCommand("NOTICE", {target.toUtf8(), text.toUtf8()});
}

void ClientController::sendCtcp(const QString& target, const QString& tag,
                                const QString& argument) {
    m_conn.sendCommand("PRIVMSG",
                       {target.toUtf8(), irc::ctcp::encode(tag.toUtf8(), argument.toUtf8())});
}

void ClientController::joinChannel(const QString& channel) {
    m_conn.sendCommand("JOIN", {channel.toUtf8()});
}

void ClientController::partChannel(const QString& channel, const QString& reason) {
    if (reason.isEmpty()) {
        m_conn.sendCommand("PART", {channel.toUtf8()});
    } else {
        m_conn.sendCommand("PART", {channel.toUtf8(), reason.toUtf8()});
    }
}

void ClientController::setNick(const QString& nick) {
    m_conn.sendCommand("NICK", {nick.toUtf8()});
}

void ClientController::setTopic(const QString& channel, const QString& topic) {
    if (topic.isEmpty()) {
        m_conn.sendCommand("TOPIC", {channel.toUtf8()});
    } else {
        m_conn.sendCommand("TOPIC", {channel.toUtf8(), topic.toUtf8()});
    }
}

void ClientController::listChannels(const QString& filter) {
    if (filter.isEmpty()) {
        m_conn.sendCommand("LIST", {});
    } else {
        m_conn.sendCommand("LIST", {filter.toUtf8()});
    }
}

void ClientController::sendCommand(const QString& command, const QStringList& args) {
    QList<QByteArray> params;
    params.reserve(args.size());
    for (const QString& a : args) {
        params << a.toUtf8();
    }
    m_conn.sendCommand(command.toUtf8(), params);
}

void ClientController::sendRaw(const QString& line) {
    m_conn.sendRaw(line.toUtf8());
}

// ---- Incoming routing ----------------------------------------------------

void ClientController::onMessage(const irc::Message& msg) {
    const QByteArray& cmd = msg.command;

    if (isNumericCommand(cmd)) {
        handleNumeric(msg);
        return;
    }
    if (cmd == "PRIVMSG") { handlePrivmsg(msg); return; }
    if (cmd == "NOTICE") { handleNotice(msg); return; }
    if (cmd == "JOIN") { handleJoin(msg); return; }
    if (cmd == "PART") { handlePart(msg); return; }
    if (cmd == "QUIT") { handleQuit(msg); return; }
    if (cmd == "KICK") { handleKick(msg); return; }
    if (cmd == "NICK") { handleNick(msg); return; }
    if (cmd == "MODE") { handleMode(msg); return; }
    if (cmd == "TOPIC") { handleTopic(msg); return; }
    if (cmd == "ERROR") {
        emit errorOccurred(QString::fromUtf8(msg.params.value(0)));
        return;
    }

    emit serverMessage(ui::formatServerLine(msg));
}

void ClientController::onStateChanged(irc::ConnectionState state) {
    if (state == irc::ConnectionState::Disconnected) {
        m_channels.clear();
    }
    emit stateChanged(state);
}

void ClientController::onSocketError(const QString& message) {
    emit errorOccurred(message);
}

void ClientController::handleNumeric(const irc::Message& msg) {
    const int code = msg.command.toInt();

    switch (code) {
    case 1: { // RPL_WELCOME: [nick, "Welcome ..."]
        if (!msg.params.isEmpty()) {
            m_nick = QString::fromUtf8(msg.params[0]);
        }
        emit ownNickChanged(m_nick);
        emit serverMessage(ui::formatServerLine(msg));
        break;
    }
    case 5: { // RPL_ISUPPORT: parse "PREFIX=(ov)@+" into the prefix symbols.
        for (int i = 1; i < msg.params.size(); ++i) {
            const QByteArray token = msg.params[i];
            if (token.startsWith("PREFIX=")) {
                const QByteArray value = token.mid(7); // after "PREFIX="
                const int close = value.indexOf(')');
                if (close >= 0) {
                    m_prefixSymbols = QString::fromUtf8(value.mid(close + 1));
                }
            }
        }
        emit serverMessage(ui::formatServerLine(msg));
        break;
    }
    case 332: { // RPL_TOPIC: [nick, channel, topic]
        if (msg.params.size() >= 3) {
            const QString key = channelKey(QString::fromUtf8(msg.params[1]));
            if (m_channels.contains(key)) {
                const QString chan = m_channels.value(key).displayName;
                const QString topic = QString::fromUtf8(msg.params[2]);
                emit channelMessage(chan, ui::formatSystem(
                    QStringLiteral("Topic is: %1").arg(topic)));
            }
        }
        break;
    }
    case 353: { // RPL_NAMREPLY: [nick, "=", channel, "name1 name2 ..."]
        if (msg.params.size() >= 4) {
            const QString key = channelKey(QString::fromUtf8(msg.params[2]));
            if (m_channels.contains(key)) {
                const QStringList names =
                    QString::fromUtf8(msg.params[3]).split(' ', Qt::SkipEmptyParts);
                for (const QString& raw : names) {
                    const irc::NickWithPrefix np = irc::splitNickPrefix(raw, m_prefixSymbols);
                    if (!np.nick.isEmpty()) {
                        addNick(key, np.nick, np.prefix);
                    }
                }
                emitNickList(key);
            }
        }
        break;
    }
    case 433: { // ERR_NICKNAMEINUSE: auto-append '_' and retry once.
        m_nick += QStringLiteral("_");
        m_conn.sendCommand("NICK", {m_nick.toUtf8()});
        emit ownNickChanged(m_nick);
        emit serverMessage(ui::formatServerLine(msg));
        break;
    }
    case 321: // RPL_LISTSTART
        m_listCount = 0;
        emit listStarted();
        break;
    case 322: { // RPL_LIST: [nick, channel, user count, topic]
        if (msg.params.size() >= 3) {
            const QString channel = QString::fromUtf8(msg.params[1]);
            const int users = QString::fromUtf8(msg.params[2]).toInt();
            const QString topic = msg.params.size() >= 4
                                      ? QString::fromUtf8(msg.params[3])
                                      : QString();
            ++m_listCount;
            emit listEntry(channel, users, topic);
        }
        break;
    }
    case 323: // RPL_LISTEND
        emit listEnded(m_listCount);
        break;
    default:
        emit serverMessage(ui::formatServerLine(msg));
        break;
    }
}

void ClientController::handlePrivmsg(const irc::Message& msg) {
    if (msg.params.size() < 2) {
        return;
    }
    const QString target = QString::fromUtf8(msg.params[0]);

    if (target.compare(m_nick, Qt::CaseInsensitive) == 0) {
        if (handleCtcpRequest(msg)) {
            return;
        }
    }

    if (isChannelName(target)) {
        const QString key = channelKey(target);
        const QString chan = m_channels.contains(key) ? m_channels.value(key).displayName
                                                      : target;
        const QString text = QString::fromUtf8(msg.params[1]);
        // Highlight when someone else mentions our nick in a channel.
        const QString fromNick = QString::fromUtf8(msg.prefix.nick);
        const bool highlight =
            !m_nick.isEmpty() && fromNick.compare(m_nick, Qt::CaseInsensitive) != 0 &&
            ui::containsNick(text, m_nick);
        emit channelMessage(chan, ui::formatChannelLine(msg, highlight));
        if (highlight) {
            emit channelHighlighted(chan);
        }
    } else {
        emit queryMessage(QString::fromUtf8(msg.prefix.nick), ui::formatQueryLine(msg));
    }
}

bool ClientController::handleCtcpRequest(const irc::Message& msg) {
    const auto payload = irc::ctcp::extractPayload(msg.params.value(1));
    if (!payload) {
        return false;
    }
    const auto parsed = irc::ctcp::parse(*payload);
    const QString tag = QString::fromUtf8(parsed.tag).toUpper();
    const QString from = QString::fromUtf8(msg.prefix.nick);

    if (tag == QStringLiteral("VERSION")) {
        sendNotice(from, QString::fromUtf8(irc::ctcp::encode("VERSION", "clint 0.2.0")));
    } else if (tag == QStringLiteral("PING")) {
        sendNotice(from, QString::fromUtf8(irc::ctcp::encode("PING", parsed.argument)));
    } else if (tag == QStringLiteral("TIME")) {
        const QByteArray now = QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8();
        sendNotice(from, QString::fromUtf8(irc::ctcp::encode("TIME", now)));
    }

    emit queryMessage(from, ui::formatQueryLine(msg));
    return true;
}

void ClientController::handleNotice(const irc::Message& msg) {
    if (msg.params.size() < 2) {
        return;
    }
    const QString target = QString::fromUtf8(msg.params[0]);

    if (!msg.prefix.nick.isEmpty() && target.compare(m_nick, Qt::CaseInsensitive) == 0) {
        emit queryMessage(QString::fromUtf8(msg.prefix.nick), ui::formatQueryLine(msg));
    } else {
        emit serverMessage(ui::formatServerLine(msg));
    }
}

void ClientController::handleJoin(const irc::Message& msg) {
    if (msg.params.isEmpty()) {
        return;
    }
    const QString chan = QString::fromUtf8(msg.params[0]);
    const QString key = channelKey(chan);
    const QString nick = QString::fromUtf8(msg.prefix.nick);

    if (nick.compare(m_nick, Qt::CaseInsensitive) == 0) {
        ChannelState st;
        st.displayName = chan;
        m_channels.insert(key, st);
        emit channelJoined(chan);
    } else if (m_channels.contains(key)) {
        addNick(key, nick, QString());
        emitNickList(key);
        emit channelMessage(m_channels.value(key).displayName, ui::formatChannelLine(msg));
    }
}

void ClientController::handlePart(const irc::Message& msg) {
    if (msg.params.isEmpty()) {
        return;
    }
    const QString chan = QString::fromUtf8(msg.params[0]);
    const QString key = channelKey(chan);
    const QString nick = QString::fromUtf8(msg.prefix.nick);

    if (nick.compare(m_nick, Qt::CaseInsensitive) == 0) {
        if (m_channels.remove(key) > 0) {
            emit channelParted(chan);
        }
    } else if (m_channels.contains(key)) {
        removeNick(key, nick);
        emitNickList(key);
        emit channelMessage(m_channels.value(key).displayName, ui::formatChannelLine(msg));
    }
}

void ClientController::handleQuit(const irc::Message& msg) {
    const QString nick = QString::fromUtf8(msg.prefix.nick);
    if (nick.isEmpty()) {
        return;
    }
    for (auto it = m_channels.begin(); it != m_channels.end(); ++it) {
        if (removeNick(it.key(), nick)) {
            emitNickList(it.key());
            emit channelMessage(it->displayName, ui::formatChannelLine(msg));
        }
    }
}

void ClientController::handleKick(const irc::Message& msg) {
    if (msg.params.size() < 2) {
        return;
    }
    const QString chan = QString::fromUtf8(msg.params[0]);
    const QString victim = QString::fromUtf8(msg.params[1]);
    const QString key = channelKey(chan);

    if (victim.compare(m_nick, Qt::CaseInsensitive) == 0) {
        if (m_channels.remove(key) > 0) {
            emit channelParted(chan);
        }
    } else if (m_channels.contains(key)) {
        removeNick(key, victim);
        emitNickList(key);
        emit channelMessage(m_channels.value(key).displayName, ui::formatChannelLine(msg));
    }
}

void ClientController::handleNick(const irc::Message& msg) {
    if (msg.params.isEmpty()) {
        return;
    }
    const QString from = QString::fromUtf8(msg.prefix.nick);
    const QString to = QString::fromUtf8(msg.params[0]);

    if (from.compare(m_nick, Qt::CaseInsensitive) == 0) {
        m_nick = to;
        emit ownNickChanged(m_nick);
    }

    const QStringList affected = renameNick(from, to);
    for (const QString& key : affected) {
        emitNickList(key);
        emit channelMessage(m_channels.value(key).displayName, ui::formatChannelLine(msg));
    }
}

void ClientController::handleMode(const irc::Message& msg) {
    if (msg.params.size() < 2) {
        return;
    }
    const QString target = QString::fromUtf8(msg.params[0]);
    if (isChannelName(target)) {
        const QString key = channelKey(target);
        const QString chan = m_channels.contains(key) ? m_channels.value(key).displayName
                                                      : target;
        emit channelMessage(chan, ui::formatChannelLine(msg));
    } else {
        emit serverMessage(ui::formatServerLine(msg));
    }
}

void ClientController::handleTopic(const irc::Message& msg) {
    if (msg.params.size() < 2) {
        return;
    }
    const QString chan = QString::fromUtf8(msg.params[0]);
    const QString key = channelKey(chan);
    if (m_channels.contains(key)) {
        emit channelMessage(m_channels.value(key).displayName, ui::formatChannelLine(msg));
    } else {
        emit serverMessage(ui::formatServerLine(msg));
    }
}

// ---- Channel state helpers ----------------------------------------------

QString ClientController::channelKey(const QString& name) const {
    return name.toLower();
}

void ClientController::addNick(const QString& key, const QString& nick, const QString& prefix) {
    if (!m_channels.contains(key)) {
        return;
    }
    QList<NickEntry>& list = m_channels[key].nicks;
    for (const NickEntry& e : list) {
        if (e.nick.compare(nick, Qt::CaseInsensitive) == 0) {
            return;
        }
    }
    list.append(NickEntry{nick, prefix});
    std::sort(list.begin(), list.end(), [](const NickEntry& a, const NickEntry& b) {
        return a.nick.compare(b.nick, Qt::CaseInsensitive) < 0;
    });
}

bool ClientController::removeNick(const QString& key, const QString& nick) {
    if (!m_channels.contains(key)) {
        return false;
    }
    QList<NickEntry>& list = m_channels[key].nicks;
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].nick.compare(nick, Qt::CaseInsensitive) == 0) {
            list.removeAt(i);
            return true;
        }
    }
    return false;
}

QStringList ClientController::renameNick(const QString& from, const QString& to) {
    QStringList affected;
    for (auto it = m_channels.begin(); it != m_channels.end(); ++it) {
        for (int i = 0; i < it->nicks.size(); ++i) {
            if (it->nicks[i].nick.compare(from, Qt::CaseInsensitive) == 0) {
                it->nicks[i].nick = to;
                affected << it.key();
                break;
            }
        }
    }
    return affected;
}

void ClientController::emitNickList(const QString& key) {
    if (!m_channels.contains(key)) {
        return;
    }
    QStringList display;
    display.reserve(m_channels[key].nicks.size());
    for (const NickEntry& e : m_channels[key].nicks) {
        display << (e.prefix + e.nick);
    }
    emit nickListChanged(m_channels.value(key).displayName, display);
}

} // namespace app
