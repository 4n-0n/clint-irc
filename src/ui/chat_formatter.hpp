#pragma once

#include "../irc/message.hpp"

#include <QString>

namespace ui {

// Deterministic color for a nick (stable across sessions, used for sender
// highlighting). Pure function of the nick string.
QString nickColor(const QString& nick);

// Escapes text for safe HTML embedding. Pure function.
QString htmlEscape(const QString& text);

// True if `text` mentions `nick` as a whole word (case-insensitive, '_'
// treated as part of a nick). Used to detect channel highlights. Pure.
bool containsNick(const QString& text, const QString& nick);

// Current-time timestamp string "[HH:mm:ss]". Reads the system clock (the
// single impure input to the formatters below).
QString timestampNow();

// Formatting entry points. Each returns one line of HTML (no <br>/<p>) ready
// for QTextEdit::append(). All are pure functions of (message, clock) — they
// hold no state.
QString formatServerLine(const irc::Message& msg);  // numerics, server notices
QString formatChannelLine(const irc::Message& msg, bool highlight = false); // channel events/chat
QString formatQueryLine(const irc::Message& msg);   // private messages from a peer

// Local echo helpers (our own outgoing messages, which the server does not
// echo back to us).
QString formatOwnMessage(const QString& nick, const QString& text);
QString formatOwnAction(const QString& nick, const QString& text);
QString formatOwnNotice(const QString& nick, const QString& text);

// Local status text (e.g. "Joined #chan"), rendered as "*** ...".
QString formatSystem(const QString& text);

} // namespace ui
