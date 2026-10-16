#include "ctcp.hpp"

namespace irc {
namespace ctcp {

std::optional<QByteArray> extractPayload(const QByteArray& body) {
    if (body.size() < 2) {
        return std::nullopt;
    }
    if (body.front() != '\x01') {
        return std::nullopt;
    }
    const int last = body.size() - 1;
    if (body.at(last) != '\x01') {
        return std::nullopt;
    }
    return body.mid(1, body.size() - 2);
}

Parsed parse(const QByteArray& payload) {
    const int space = payload.indexOf(' ');
    if (space < 0) {
        return Parsed{payload, {}};
    }
    return Parsed{payload.left(space), payload.mid(space + 1)};
}

QByteArray encode(const QByteArray& tag, const QByteArray& argument) {
    QByteArray out;
    out += '\x01';
    out += tag;
    if (!argument.isEmpty()) {
        out += ' ';
        out += argument;
    }
    out += '\x01';
    return out;
}

} // namespace ctcp
} // namespace irc
