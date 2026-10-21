#pragma once

#include <QByteArray>
#include <QList>

namespace irc {

// Maximum IRC line length in bytes INCLUDING the trailing CRLF
// (RFC 2812 section 2.3).
constexpr int kMaxLineLength = 512;

// Maximum line content in bytes, CRLF excluded.
constexpr int kMaxContentLength = kMaxLineLength - 2;

// Encodes a command + params into a single IRC line (no CRLF), then clips it
// to fit the 512-byte limit (RFC 2812 section 2.3): if the line exceeds 510
// content bytes, the trailing parameter is shortened at a UTF-8 boundary.
// Pure function. Applies the trailing-':' rule: the last param is encoded as
// a trailing param if it is empty, contains a space, or starts with ':'.
QByteArray encodeCommand(const QByteArray& command, const QList<QByteArray>& params);

// Bytes available for a free-text trailing parameter when `command` and the
// middle params (e.g. the target of a PRIVMSG) are fixed. Accounts for the
// separating spaces and the ':' that prefixes the trailing param.
// Returns a negative number when the fixed part alone already overflows.
int trailingParamBudget(const QByteArray& command,
                        const QList<QByteArray>& middleParams,
                        int maxContent = kMaxContentLength);

// Splits `text` into chunks of at most `budget` bytes so each chunk fits in one
// IRC line. Chunks break at whitespace when there is one inside the window,
// otherwise they are hard-cut, and never inside a UTF-8 multibyte sequence.
// Splitting at whitespace drops it; the chunks therefore read back as the
// original text when the caller re-joins them with single spaces. An empty
// text yields one empty chunk, and a non-positive budget yields the whole text
// as a single chunk (the caller still has something to send).
QList<QByteArray> splitText(const QByteArray& text, int budget);

// Appends CRLF to a line. Kept separate so callers can reason about framing.
QByteArray withCrlf(const QByteArray& line);

} // namespace irc
