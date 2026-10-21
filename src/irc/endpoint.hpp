#pragma once

#include <QString>

namespace irc {

// Result of parsing a user-entered server string.
struct Endpoint {
    QString host;              // IPv6 literals are returned *without* brackets
    quint16 port = 0;          // 0 = not specified; caller keeps its own default
    bool isAddressLiteral = false; // host is an IPv4/IPv6 literal, not a name
    bool isIpv6 = false;       // host is an IPv6 literal
};

// Parses a server string into host + optional port. Accepted forms:
//
//   irc.example.com            host, no port
//   irc.example.com:6697       host + port
//   2001:db8::1                IPv6 literal, no port (colons are the address)
//   [2001:db8::1]              bracketed IPv6 literal, no port
//   [2001:db8::1]:6697         bracketed IPv6 literal + port
//   [fe80::1%eth0]:6697        with a zone/scope id
//
// Brackets are required around an IPv6 literal when a port is appended:
// "2001:db8::1:6697" is a perfectly valid address, so it cannot be read as
// host + port. Whitespace is trimmed throughout.
Endpoint parseEndpoint(const QString& raw);

// Renders host:port for display, bracketing IPv6 literals ("[::1]:6697").
QString formatEndpoint(const QString& host, quint16 port);

// True if `s` is a bare IPv6 literal (brackets optional, scope id allowed).
bool isIpv6Literal(const QString& s);

} // namespace irc
