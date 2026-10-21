# clint — a minimal IRC client (C++17 / Qt6)

[![CI](https://github.com/4n-0n/clint-irc/actions/workflows/ci.yml/badge.svg)](https://github.com/4n-0n/clint-irc/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A small IRC client with a Qt Widgets GUI, TLS support, IPv4/IPv6 (including
IPv6-only servers), colored chat, per-channel
tabs, nick lists, nick-mention highlighting with tab activity markers, clickable
URLs, click-to-mention and double-click-to-query nicks in the chat view, SASL
PLAIN authentication, right-click context menus, configurable /me actions, a
per-channel topic bar, and CTCP ACTION/version handling.

Other niceties:

- Long messages and /me actions are split into several IRC lines instead of
  being truncated at the 512-byte limit, breaking at word boundaries and never
  inside a UTF-8 character.
- Nick colors are derived from a stable hash, so a nick keeps the same color
  across sessions.
- The nick list follows mode changes: `MODE +o/-o/+v/-v` updates the prefixes
  without waiting for a new NAMES.
- `/list` entries are clickable: one click joins the channel.
- Private messages mark their tab like a mention and flash the taskbar entry
  when the window is in the background.
- `/clear` empties the current tab, `/close` closes it, `/join #chan key`
  passes a channel key.

## Build

```sh
cmake -B build -S .
cmake --build build -j"$(nproc)"
```

## Run

```sh
./build/clint
```

Or connect directly from the command line:

```sh
./build/clint --server irc.libera.chat --port 6697 --tls --nick mynick --channel '#chan'
```

With SASL PLAIN authentication:

```sh
./build/clint --server irc.libera.chat --port 6697 --tls --nick mynick \
  --sasl-user myaccount --sasl-password secret --channel '#chan'
```

## IPv6

IPv6-only servers work out of the box. `--server` accepts a hostname, an IPv4
address, or an IPv6 literal — bracketed or bare, with or without a port:

```sh
./build/clint --server ::1 --port 6667 --nick mynick
./build/clint --server '[2001:db8::1]' --port 6697 --tls --nick mynick
./build/clint --server '[2001:db8::1]:6697' --tls --nick mynick   # port in the string
./build/clint --server irc6.example.net --ipv6 --nick mynick      # AAAA-only name
```

- `--ipv6` / `--ipv4` (or the **Protocol** dropdown in the connect dialog)
  restrict the attempt to one address family. With **Auto** the resolver's
  preference order is used, which is IPv6-first on a dual-stack host, and a
  failed address falls back to the next one.
- Brackets are only required when a port is appended: `2001:db8::1:6697` is a
  valid address, so it is read as one, not as host + port.
- Every resolved address is tried in turn with a 15 s budget each, so a stale
  or firewalled record (an IPv4 entry next to a working IPv6 one) no longer
  stalls the connect; the failure summary lists what was tried.
- Link-local servers take a zone id: `[fe80::1%eth0]:6697`.
- TLS over IPv6 keeps the host name for SNI; an IPv6 literal connects as the
  literal itself.

To check a build against an IPv6-only server locally:

```sh
python3 tests/mock_irc_server.py 6667 --host ::1 &
./build/clint --server '[::1]' --port 6667 --nick mynick
```

## Test

```sh
ctest --test-dir build --output-on-failure
```

Runs pure-function tests (IRC parser including IRCv3 message tags, encoder and
message splitting, CTCP, HTML formatter and stable nick colors, fun-actions
loader, endpoint parsing), the settings round-trip, and:

- `clint_channel_extras_test` — topic bar visibility and a click on a
  channel-list entry emits a join.
- `clint_controller_test` — the session layer against an in-process fake
  server: topic tracking, MODE prefix updates, mention highlighting, long
  message splitting, 433 handling, clean QUIT.
- `clint_connect_test` — connects to real loopback servers to verify the
  IPv6 path (bracketed literals, `host:port`, family selection, failover,
  disconnect during resolution).

## Fun /me actions

The "Actions" menu (and toolbar "Fun" button) sends preconfigured `/me`
actions. The list is read from `~/.config/clint/actions.conf` (falling back to
built-in defaults when missing). Format — one action per line, `Label | text`:

```
Wave | waves hello
Dance | does a happy dance
```

`#` starts a comment; a line without `|` uses the whole line as label and text.
See `examples/actions.conf` for a full example.

## Architecture

Layering, top to bottom:

- `src/ui/` — Qt Widgets UI. `main_window` manages tabs and slash commands;
  `channel_widget` is a dumb render widget; `chat_formatter` is pure HTML
  formatting.
- `src/app/client_controller` — session/state layer: owns the connection,
  tracks channel membership, nick lists, nick prefixes and topics, routes
  incoming messages to semantic signals, and splits long outgoing messages.
  No UI code.
- `src/irc/connection` — stateful network session (QAbstractSocket/QSslSocket):
  buffering, PING/PONG, registration handshake, explicit state machine. Resolves
  the host with `QHostInfo`, filters by address family, and tries every
  resolved address in turn.
- `src/irc/` (pure) — `parser`, `encoder`, `ctcp`, `endpoint` (host/port/IPv6
  parsing), `message`, `result`. No state, no I/O; unit-tested.

Design principles:

- Pure functions (protocol, formatting) are separated from stateful classes
  (connection, controller, widgets).
- Errors in expected flow use a `Result<T,E>` pattern; exceptions only for
  truly exceptional cases.
- Explicit state enums (`ConnectionState`) instead of implicit flags.
- Dependency injection: `IrcConnection` receives its socket via a factory,
  so tests can substitute a mock.

## License

[MIT](LICENSE)
