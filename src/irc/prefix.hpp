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

// Adds or removes one mode-prefix symbol on a nick's prefix string (the result
// of splitNickPrefix). The result is re-ordered by `prefixSymbols`, which lists
// the symbols highest-rank first ("~&@%+"), so a nick that gains '+o' keeps '@'
// ahead of '+'. Symbols the server does not list are dropped. Pure function.
//
// Example: applyPrefixSymbol("+", "@+", '@', true)  -> "@+"
//          applyPrefixSymbol("@+", "@+", '@', false) -> "+"
inline QString applyPrefixSymbol(const QString& prefix, const QString& prefixSymbols,
                                 QChar symbol, bool add) {
    QString chars = prefix;
    if (add) {
        if (!chars.contains(symbol)) {
            chars += symbol;
        }
    } else {
        chars.remove(symbol);
    }

    QString ranked;
    ranked.reserve(chars.size());
    for (const QChar s : prefixSymbols) {
        if (chars.contains(s)) {
            ranked += s;
        }
    }
    return ranked;
}

} // namespace irc
