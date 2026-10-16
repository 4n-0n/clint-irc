#pragma once

#include <QByteArray>
#include <optional>

namespace irc {
namespace ctcp {

// CTCP messages ride inside PRIVMSG/NOTICE bodies, framed by \x01
// (SOH) characters: "\x01TAG [argument]\x01".

// Extracts a CTCP payload from a PRIVMSG/NOTICE body.
// Returns std::nullopt if the body is not CTCP-framed.
// Pure function.
std::optional<QByteArray> extractPayload(const QByteArray& body);

// A parsed CTCP payload "TAG [argument]".
struct Parsed {
    QByteArray tag;      // upper-case tag, e.g. "ACTION"
    QByteArray argument; // empty when absent
};

// Parses a payload into tag + argument. Pure function.
Parsed parse(const QByteArray& payload);

// Builds a CTCP payload from tag + optional argument. Pure function.
QByteArray encode(const QByteArray& tag, const QByteArray& argument = {});

} // namespace ctcp
} // namespace irc
