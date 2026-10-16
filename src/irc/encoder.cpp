#include "encoder.hpp"

#include <algorithm>

namespace irc {

namespace {

// Largest payload a single IRC line may carry: 512 bytes including CRLF
// (RFC 2812 section 2.3) leaves 510 bytes of content.
constexpr int kMaxContentLength = kMaxLineLength - 2;

// Reduces `maxLen` so the cut does not split a UTF-8 multibyte sequence:
// while the byte at the cut position is a continuation byte (10xxxxxx), step
// back to its lead byte.
int utf8SafeLength(const QByteArray& data, int maxLen) {
    int n = static_cast<int>(std::min<qsizetype>(data.size(), maxLen));
    while (n > 0 && n < data.size() &&
           (static_cast<unsigned char>(data.at(n)) & 0xC0) == 0x80) {
        --n;
    }
    return n;
}

} // namespace

QByteArray encodeCommand(const QByteArray& command, const QList<QByteArray>& params) {
    QByteArray out = command;

    for (int i = 0; i < params.size(); ++i) {
        out += ' ';
        const QByteArray& p = params.at(i);
        const bool isLast = (i == params.size() - 1);
        const bool needsTrailing =
            isLast && (p.isEmpty() || p.contains(' ') || p.startsWith(':'));
        if (needsTrailing) {
            out += ':';
        }
        out += p;
    }

    // Enforce the RFC line limit by clipping from the end. Clipping the tail
    // preserves the command, earlier params, and any trailing-':' separator;
    // only the final (free-text) parameter is shortened.
    if (out.size() > kMaxContentLength) {
        out.truncate(utf8SafeLength(out, kMaxContentLength));
    }

    return out;
}

QByteArray withCrlf(const QByteArray& line) {
    QByteArray out = line;
    out += "\r\n";
    return out;
}

} // namespace irc
