#include "chat_formatter.hpp"

#include "../irc/ctcp.hpp"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QTime>
#include <QUrl>

namespace ui {

namespace {

// Fixed palette for nick coloring (one-dark-ish pastels).
const char* const kPalette[] = {
    "#e06c75", "#d19a66", "#e5c07b", "#98c379", "#56b6c2",
    "#61afef", "#c678dd", "#ef6f6c", "#7fdbca", "#f2b8c6",
    "#a3be8c", "#b48ead", "#88c0d0", "#d08770", "#ebcb8b", "#bf616a",
};

// FNV-1a over the lowercased UTF-8 bytes. qHash() must not be used here: Qt
// seeds it randomly per process, which would give a nick a different color on
// every launch. Case is folded so "Alice" and "alice" match, and the result is
// stable across platforms and runs.
quint32 stableNickHash(const QString& nick) {
    quint32 hash = 2166136261u;
    for (const char c : nick.toCaseFolded().toUtf8()) {
        hash ^= static_cast<quint8>(c);
        hash *= 16777619u;
    }
    return hash;
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

QString timestampSpan() {
    return QStringLiteral("<span style=\"color:#6a6a6a\">[%1]</span> ")
        .arg(timestampNow());
}

// Renders a nick as a clickable anchor carrying the nick under the custom
// "nick:" URL scheme. The UI layer intercepts anchor clicks and inserts the
// nick into the input line at the cursor. Pure function.
QString nickAnchor(const QString& nick, const QString& color) {
    const QString href = QString::fromLatin1(QUrl::toPercentEncoding(nick));
    return QStringLiteral("<a href=\"nick:%1\" style=\"color:%2; text-decoration:none;\">%3</a>")
        .arg(href, color, htmlEscape(nick));
}

QString nickSpan(const QString& nick) {
    return nickAnchor(nick, nickColor(nick));
}

// Renders a nick in the highlight style used when the user is mentioned:
// a bright bold nick with a subtle background on the whole message line.
QString nickHighlightSpan(const QString& nick) {
    const QString href = QString::fromLatin1(QUrl::toPercentEncoding(nick));
    return QStringLiteral(
               "<a href=\"nick:%1\" style=\"color:#e5c07b; font-weight:bold; text-decoration:none;\">%2</a>")
        .arg(href, htmlEscape(nick));
}

// Renders a CTCP payload into a human-readable fragment; returns empty string
// if the body is not CTCP. `nick` is the sender (may be empty).
QString renderCtcp(const QByteArray& body, const QString& nick, bool* consumed) {
    *consumed = false;
    const auto payload = irc::ctcp::extractPayload(body);
    if (!payload) {
        return QString();
    }
    *consumed = true;
    const auto parsed = irc::ctcp::parse(*payload);
    const QString tag = QString::fromUtf8(parsed.tag);
    const QString arg = QString::fromUtf8(parsed.argument);

    if (tag.compare(QStringLiteral("ACTION"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("<span style=\"color:#56b6c2\">* %1 %2</span>")
            .arg(nickAnchor(nick, QStringLiteral("#56b6c2")), htmlEscape(arg));
    }
    QString text = QStringLiteral("[CTCP %1").arg(htmlEscape(tag));
    if (!arg.isEmpty()) {
        text += QStringLiteral(" %1").arg(htmlEscape(arg));
    }
    if (!nick.isEmpty()) {
        text += QStringLiteral(" from %1").arg(htmlEscape(nick));
    }
    text += QStringLiteral("]");
    return QStringLiteral("<span style=\"color:#9aa0a6\">%1</span>").arg(text);
}

} // namespace

QString nickColor(const QString& nick) {
    if (nick.isEmpty()) {
        return QStringLiteral("#cccccc");
    }
    const size_t n = sizeof(kPalette) / sizeof(kPalette[0]);
    return QString::fromLatin1(kPalette[stableNickHash(nick) % n]);
}

QString htmlEscape(const QString& text) {
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        switch (c.unicode()) {
        case '&': out += QStringLiteral("&amp;"); break;
        case '<': out += QStringLiteral("&lt;"); break;
        case '>': out += QStringLiteral("&gt;"); break;
        case '"': out += QStringLiteral("&quot;"); break;
        default:  out += c; break;
        }
    }
    return out;
}

namespace {

// Matches http:// and https:// URLs up to the first whitespace or a character
// that cannot legally appear in a URL (angle brackets, quotes). Trailing
// sentence punctuation is trimmed by the caller, not here.
const QRegularExpression& urlPattern() {
    static const QRegularExpression re(
        QStringLiteral("(https?://[^\\s<>\"']+)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

} // namespace

QString linkify(const QString& text) {
    QString out;
    out.reserve(text.size());
    int last = 0;
    QRegularExpressionMatchIterator it = urlPattern().globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const int start = m.capturedStart();

        // Escaped text preceding this URL.
        out += htmlEscape(text.mid(last, start - last));

        // Drop trailing punctuation that is almost never part of the URL; the
        // trimmed chars are re-emitted (escaped) in the following segment.
        QString url = m.captured();
        while (url.size() > 1 && QStringLiteral(".,;:!?").contains(url.back())) {
            url.chop(1);
        }

        out += QStringLiteral(
                   "<a href=\"%1\" style=\"color:#61afef; text-decoration:underline;\">%2</a>")
                   .arg(htmlEscape(url), htmlEscape(url));
        last = start + url.size();
    }
    out += htmlEscape(text.mid(last));
    return out;
}

QString timestampNow() {
    return QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
}

namespace {

// Word characters for nick-matching: '_' is valid in IRC nicks, so it counts
// as part of a nick (\"foo\" must not match inside \"foo_bar\").
bool isNickWordChar(QChar c) {
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

} // namespace

bool containsNick(const QString& text, const QString& nick) {
    if (nick.isEmpty() || text.isEmpty()) {
        return false;
    }
    const int nickLen = nick.size();
    int idx = 0;
    while ((idx = text.indexOf(nick, idx, Qt::CaseInsensitive)) >= 0) {
        const bool leftOk = (idx == 0) || !isNickWordChar(text.at(idx - 1));
        const int end = idx + nickLen;
        const bool rightOk = (end >= text.size()) || !isNickWordChar(text.at(end));
        if (leftOk && rightOk) {
            return true;
        }
        idx = end; // advance past this (non-boundary) occurrence
    }
    return false;
}

QString formatServerLine(const irc::Message& msg) {
    QString out = timestampSpan();

    if (isNumericCommand(msg.command)) {
        const int code = msg.command.toInt();
        QString body;
        switch (code) {
        case 353: { // RPL_NAMREPLY: [nick, "=", channel, "name1 name2 ..."]
            if (msg.params.size() >= 4) {
                body = QStringLiteral("Names for %1: %2")
                           .arg(QString::fromUtf8(msg.params[2]),
                                QString::fromUtf8(msg.params[3]));
            } else {
                body = QString::fromUtf8(msg.raw);
            }
            break;
        }
        case 332: { // RPL_TOPIC: [nick, channel, topic]
            if (msg.params.size() >= 3) {
                body = QStringLiteral("Topic for %1: %2")
                           .arg(QString::fromUtf8(msg.params[1]),
                                QString::fromUtf8(msg.params[2]));
            } else {
                body = QString::fromUtf8(msg.raw);
            }
            break;
        }
        case 366:
            body = QStringLiteral("End of NAMES");
            break;
        case 311: { // RPL_WHOISUSER: [me, nick, user, host, *, realname]
            if (msg.params.size() >= 6) {
                body = QStringLiteral("%1 is %2@%3 * %4")
                           .arg(QString::fromUtf8(msg.params[1]), QString::fromUtf8(msg.params[2]),
                                QString::fromUtf8(msg.params[3]), QString::fromUtf8(msg.params[5]));
            } else {
                body = QString::fromUtf8(msg.raw);
            }
            break;
        }
        case 312: { // RPL_WHOISSERVER: [me, nick, server, info]
            if (msg.params.size() >= 4) {
                body = QStringLiteral("%1 on %2 (%3)")
                           .arg(QString::fromUtf8(msg.params[1]), QString::fromUtf8(msg.params[2]),
                                QString::fromUtf8(msg.params[3]));
            } else {
                body = QString::fromUtf8(msg.raw);
            }
            break;
        }
        case 317: { // RPL_WHOISIDLE: [me, nick, idle_seconds, signon, info]
            if (msg.params.size() >= 4) {
                body = QStringLiteral("%1 idle %2 seconds")
                           .arg(QString::fromUtf8(msg.params[1]), QString::fromUtf8(msg.params[2]));
            } else {
                body = QString::fromUtf8(msg.raw);
            }
            break;
        }
        case 319: { // RPL_WHOISCHANNELS: [me, nick, channels]
            if (msg.params.size() >= 3) {
                body = QStringLiteral("%1 on channels %2")
                           .arg(QString::fromUtf8(msg.params[1]), QString::fromUtf8(msg.params[2]));
            } else {
                body = QString::fromUtf8(msg.raw);
            }
            break;
        }
        case 318:
            body = QStringLiteral("End of WHOIS");
            break;
        case 352: { // RPL_WHOREPLY: [me, chan, user, host, server, nick, flags, hopcount realname]
            if (msg.params.size() >= 8) {
                body = QStringLiteral("%1 %2@%3 %4 %5 %6")
                           .arg(QString::fromUtf8(msg.params[1]), QString::fromUtf8(msg.params[2]),
                                QString::fromUtf8(msg.params[3]), QString::fromUtf8(msg.params[5]),
                                QString::fromUtf8(msg.params[6]), QString::fromUtf8(msg.params[7]));
            } else {
                body = QString::fromUtf8(msg.raw);
            }
            break;
        }
        case 315:
            body = QStringLiteral("End of WHO");
            break;
        case 314: { // RPL_WHOWASUSER: [me, nick, user, host, *, realname]
            if (msg.params.size() >= 6) {
                body = QStringLiteral("%1 was %2@%3 * %4")
                           .arg(QString::fromUtf8(msg.params[1]), QString::fromUtf8(msg.params[2]),
                                QString::fromUtf8(msg.params[3]), QString::fromUtf8(msg.params[5]));
            } else {
                body = QString::fromUtf8(msg.raw);
            }
            break;
        }
        case 369:
            body = QStringLiteral("End of WHOWAS");
            break;
        default: {
            // Strip the leading target nick (params[0]) and join the rest.
            QStringList parts;
            const int start = msg.params.isEmpty() ? 0 : 1;
            for (int i = start; i < msg.params.size(); ++i) {
                parts << QString::fromUtf8(msg.params[i]);
            }
            body = parts.join(QLatin1Char(' '));
            break;
        }
        }
        out += QStringLiteral("<span style=\"color:#9aa0a6\">%1</span>").arg(htmlEscape(body));
        return out;
    }

    if (msg.command == "ERROR") {
        const QString text = msg.params.isEmpty() ? QString()
                                                  : QString::fromUtf8(msg.params.last());
        out += QStringLiteral("<span style=\"color:#e06c75\">ERROR: %1</span>")
                   .arg(htmlEscape(text));
        return out;
    }

    if (msg.command == "NOTICE") {
        const QString text = msg.params.isEmpty() ? QString()
                                                  : QString::fromUtf8(msg.params.last());
        if (!msg.prefix.nick.isEmpty()) {
            out += QStringLiteral("-%1- %2")
                       .arg(nickSpan(QString::fromUtf8(msg.prefix.nick)), htmlEscape(text));
        } else {
            out += QStringLiteral("-server- %1").arg(htmlEscape(text));
        }
        return out;
    }

    // Fallback: the raw line.
    out += htmlEscape(QString::fromUtf8(msg.raw));
    return out;
}

QString formatChannelLine(const irc::Message& msg, bool highlight) {
    QString out = timestampSpan();
    const QString nick = QString::fromUtf8(msg.prefix.nick);

    if (msg.command == "PRIVMSG" || msg.command == "NOTICE") {
        const bool isNotice = (msg.command == "NOTICE");
        const QString body = msg.params.size() >= 2
                                 ? QString::fromUtf8(msg.params[1])
                                 : QString();

        bool consumed = false;
        const QString ctcp = renderCtcp(msg.params.value(1), nick, &consumed);
        if (consumed) {
            return out + ctcp;
        }

        // A mention highlights the sender nick in bold yellow and draws a
        // subtle background under the whole message line.
        QString line;
        if (isNotice) {
            line = QStringLiteral("-%1- %2").arg(nickSpan(nick), linkify(body));
        } else if (highlight) {
            line = QStringLiteral("<span style=\"background-color:#4d2b2b;\">&lt;%1&gt; %2</span>")
                       .arg(nickHighlightSpan(nick), linkify(body));
        } else {
            line = QStringLiteral("&lt;%1&gt; %2").arg(nickSpan(nick), linkify(body));
        }
        return out + line;
    }

    if (msg.command == "JOIN") {
        out += QStringLiteral("<span style=\"color:#98c379\">--&gt; %1 has joined</span>")
                   .arg(htmlEscape(nick));
        return out;
    }
    if (msg.command == "PART") {
        const QString reason = msg.params.size() >= 2
                                   ? QString::fromUtf8(msg.params[1])
                                   : QString();
        out += QStringLiteral("<span style=\"color:#98c379\">&lt;-- %1 has left")
                   .arg(htmlEscape(nick));
        if (!reason.isEmpty()) {
            out += QStringLiteral(" (%1)").arg(htmlEscape(reason));
        }
        out += QStringLiteral("</span>");
        return out;
    }
    if (msg.command == "QUIT") {
        const QString reason = msg.params.isEmpty() ? QString()
                                                    : QString::fromUtf8(msg.params.last());
        out += QStringLiteral("<span style=\"color:#98c379\">&lt;-- %1 has quit")
                   .arg(htmlEscape(nick));
        if (!reason.isEmpty()) {
            out += QStringLiteral(" (%1)").arg(htmlEscape(reason));
        }
        out += QStringLiteral("</span>");
        return out;
    }
    if (msg.command == "KICK") {
        const QString victim = msg.params.size() >= 2
                                   ? QString::fromUtf8(msg.params[1])
                                   : QString();
        const QString reason = msg.params.size() >= 3
                                   ? QString::fromUtf8(msg.params[2])
                                   : QString();
        out += QStringLiteral("<span style=\"color:#e06c75\">%1 was kicked by %2")
                   .arg(htmlEscape(victim), htmlEscape(nick));
        if (!reason.isEmpty()) {
            out += QStringLiteral(" (%1)").arg(htmlEscape(reason));
        }
        out += QStringLiteral("</span>");
        return out;
    }
    if (msg.command == "NICK") {
        const QString newNick = msg.params.isEmpty() ? QString()
                                                     : QString::fromUtf8(msg.params[0]);
        out += QStringLiteral("<span style=\"color:#9aa0a6\">%1 is now known as %2</span>")
                   .arg(htmlEscape(nick), htmlEscape(newNick));
        return out;
    }
    if (msg.command == "MODE") {
        QStringList rest;
        for (int i = 1; i < msg.params.size(); ++i) {
            rest << QString::fromUtf8(msg.params[i]);
        }
        out += QStringLiteral("<span style=\"color:#9aa0a6\">%1 sets mode %2</span>")
                   .arg(htmlEscape(nick), htmlEscape(rest.join(QLatin1Char(' '))));
        return out;
    }
    if (msg.command == "TOPIC") {
        const QString topic = msg.params.size() >= 2
                                  ? QString::fromUtf8(msg.params[1])
                                  : QString();
        out += QStringLiteral("<span style=\"color:#9aa0a6\">%1 changed the topic to: %2</span>")
                   .arg(htmlEscape(nick), htmlEscape(topic));
        return out;
    }

    out += htmlEscape(QString::fromUtf8(msg.raw));
    return out;
}

QString formatQueryLine(const irc::Message& msg) {
    QString out = timestampSpan();
    const QString nick = QString::fromUtf8(msg.prefix.nick);

    if (msg.command == "PRIVMSG" || msg.command == "NOTICE") {
        const bool isNotice = (msg.command == "NOTICE");
        const QString body = msg.params.size() >= 2
                                 ? QString::fromUtf8(msg.params[1])
                                 : QString();

        bool consumed = false;
        const QString ctcp = renderCtcp(msg.params.value(1), nick, &consumed);
        if (consumed) {
            return out + ctcp;
        }

        if (isNotice) {
            out += QStringLiteral("-%1- %2").arg(nickSpan(nick), linkify(body));
        } else {
            out += QStringLiteral("&lt;%1&gt; %2").arg(nickSpan(nick), linkify(body));
        }
        return out;
    }

    out += htmlEscape(QString::fromUtf8(msg.raw));
    return out;
}

QString formatOwnMessage(const QString& nick, const QString& text) {
    return timestampSpan() +
           QStringLiteral("<span style=\"color:#e0e0e0\">&lt;%1&gt;</span> %2")
               .arg(nickSpan(nick), linkify(text));
}

QString formatOwnAction(const QString& nick, const QString& text) {
    return timestampSpan() +
           QStringLiteral("<span style=\"color:#56b6c2\">* %1 %2</span>")
               .arg(htmlEscape(nick), htmlEscape(text));
}

QString formatOwnNotice(const QString& nick, const QString& text) {
    return timestampSpan() +
           QStringLiteral("-%1- %2").arg(nickSpan(nick), htmlEscape(text));
}

QString formatSystem(const QString& text) {
    return timestampSpan() +
           QStringLiteral("<span style=\"color:#9aa0a6\">*** %1</span>")
               .arg(htmlEscape(text));
}

} // namespace ui
