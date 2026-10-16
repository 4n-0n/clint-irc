#pragma once

#include <QString>

namespace irc {

// Result of splitting an ISUPPORT-PREFIX-decorated NAMES entry.
struct NickWithPrefix {
    QString prefix; // mode prefix chars, e.g. "@", "+", "" — highest first
    QString nick;   // remaining clean nick
};

// Splits leading mode-prefix characters (per the ISUPPORT PREFIX symbols,
// e.g. "@+" or "@+%~&") off a NAMES entry. Pure function.
//
// Example: splitNickPrefix("@opnick", "@+") -> { "@", "opnick" }
inline NickWithPrefix splitNickPrefix(const QString& raw, const QString& prefixSymbols) {
    NickWithPrefix out;
    out.nick = raw;
    while (!out.nick.isEmpty() && prefixSymbols.contains(out.nick.at(0))) {
        out.prefix += out.nick.at(0);
        out.nick.remove(0, 1);
    }
    return out;
}

} // namespace irc
