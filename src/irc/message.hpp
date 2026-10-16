#pragma once

#include <QByteArray>
#include <QList>

namespace irc {

// A message prefix "nick!user@host" split into its components.
// Empty fields indicate absence (e.g. a bare host has nick+host, no user).
struct Prefix {
    QByteArray nick;
    QByteArray user;
    QByteArray host;
};

// An IRC protocol message (RFC 1459 / RFC 2812).
//
// Invariants:
//  - `raw` is the exact line received, WITHOUT the trailing CRLF.
//  - `command` is non-empty.
//  - `params` are the already-split parameters; trailing ':' semantics are
//    re-applied by the encoder (see encoder.hpp), not stored here.
struct Message {
    QByteArray raw;        // full original line (no CRLF)
    QByteArray prefixRaw;  // "nick!user@host" or empty
    Prefix prefix;         // parsed prefix (all-empty when absent)
    QByteArray command;    // "PRIVMSG", "001", "PING", ...
    QList<QByteArray> params;

    // True if params contains an element at `index`.
    [[nodiscard]] bool hasParam(int index) const {
        return index >= 0 && index < params.size();
    }
};

} // namespace irc
