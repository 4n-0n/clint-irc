#pragma once

#include <QByteArray>
#include <QList>

namespace irc {

// Maximum IRC line length in bytes INCLUDING the trailing CRLF
// (RFC 2812 section 2.3).
constexpr int kMaxLineLength = 512;

// Encodes a command + params into a single IRC line (no CRLF), then clips it
// to fit the 512-byte limit (RFC 2812 section 2.3): if the line exceeds 510
// content bytes, the trailing parameter is shortened at a UTF-8 boundary.
// Pure function. Applies the trailing-':' rule: the last param is encoded as
// a trailing param if it is empty, contains a space, or starts with ':'.
QByteArray encodeCommand(const QByteArray& command, const QList<QByteArray>& params);

// Appends CRLF to a line. Kept separate so callers can reason about framing.
QByteArray withCrlf(const QByteArray& line);

} // namespace irc
