#pragma once

#include "message.hpp"
#include "result.hpp"

namespace irc {

// Reasons a line cannot be parsed into a Message.
enum class ParseError {
    EmptyLine,  // line is empty after CRLF stripping
    NoCommand,  // no command token present (e.g. a lone prefix)
};

// Parses a single IRC line (CRLF already stripped) into a Message.
// Pure function: no state, no I/O, no side effects.
Result<Message, ParseError> parseLine(const QByteArray& line);

// Parses a raw prefix "nick!user@host" into its components.
// Pure function. Empty raw -> all-empty Prefix.
Prefix parsePrefix(const QByteArray& raw);

} // namespace irc
