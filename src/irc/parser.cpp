#include "parser.hpp"

#include <cstring>

namespace irc {

Prefix parsePrefix(const QByteArray& raw) {
    Prefix p;
    if (raw.isEmpty()) {
        return p;
    }

    const int bang = raw.indexOf('!');
    const int at = raw.indexOf('@');

    if (bang >= 0) {
        p.nick = raw.left(bang);
        if (at > bang) {
            p.user = raw.mid(bang + 1, at - bang - 1);
            p.host = raw.mid(at + 1);
        } else {
            p.user = raw.mid(bang + 1);
        }
    } else if (at >= 0) {
        p.nick = raw.left(at);
        p.host = raw.mid(at + 1);
    } else {
        p.nick = raw;
    }

    return p;
}

Result<Message, ParseError> parseLine(const QByteArray& line) {
    if (line.isEmpty()) {
        return Result<Message, ParseError>::err(ParseError::EmptyLine);
    }

    Message msg;
    msg.raw = line;

    const char* p = line.constData();
    const char* end = p + line.size();

    // 1. Optional prefix: a line starting with ':' whose prefix runs to the
    //    next space.
    if (p < end && *p == ':') {
        ++p;
        const char* space = static_cast<const char*>(std::memchr(p, ' ', end - p));
        if (space == nullptr) {
            // Line is only a prefix, no command -> malformed.
            return Result<Message, ParseError>::err(ParseError::NoCommand);
        }
        msg.prefixRaw = QByteArray(p, static_cast<int>(space - p));
        p = space + 1;
    }

    // 2. Command token: up to the next space or end of line.
    const char* space = static_cast<const char*>(std::memchr(p, ' ', end - p));
    if (space == nullptr) {
        msg.command = QByteArray(p, static_cast<int>(end - p));
    } else {
        msg.command = QByteArray(p, static_cast<int>(space - p));
        p = space + 1;
    }

    if (msg.command.isEmpty()) {
        return Result<Message, ParseError>::err(ParseError::NoCommand);
    }

    // 3. Parameters: middle params are space-separated; a trailing param
    //    starts with ':' and runs to end of line (may contain spaces).
    while (p < end) {
        while (p < end && *p == ' ') {
            ++p;
        }
        if (p >= end) {
            break;
        }

        if (*p == ':') {
            ++p;
            msg.params.append(QByteArray(p, static_cast<int>(end - p)));
            break;
        }

        const char* nextSpace = static_cast<const char*>(std::memchr(p, ' ', end - p));
        if (nextSpace == nullptr) {
            msg.params.append(QByteArray(p, static_cast<int>(end - p)));
            break;
        }
        msg.params.append(QByteArray(p, static_cast<int>(nextSpace - p)));
        p = nextSpace + 1;
    }

    msg.prefix = parsePrefix(msg.prefixRaw);

    return Result<Message, ParseError>::ok(std::move(msg));
}

} // namespace irc
