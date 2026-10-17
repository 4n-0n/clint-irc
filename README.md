# clint — a minimal IRC client (C++17 / Qt6)

[![CI](https://github.com/4n-0n/clint-irc/actions/workflows/ci.yml/badge.svg)](https://github.com/4n-0n/clint-irc/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A small IRC client with a Qt Widgets GUI, TLS support, colored chat, per-channel
tabs, nick lists, nick-mention highlighting with tab activity markers, clickable
URLs, click-to-mention and double-click-to-query nicks in the chat view, SASL
PLAIN authentication, right-click context menus, configurable /me actions, and
CTCP ACTION/version handling.

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

## Test

```sh
./build/clint_selftest
```

Runs pure-function tests for the IRC parser, encoder, CTCP, HTML formatter,
and fun-actions loader.

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
  tracks channel membership and nick lists, routes incoming messages to
  semantic signals. No UI code.
- `src/irc/connection` — stateful network session (QAbstractSocket/QSslSocket):
  buffering, PING/PONG, registration handshake, explicit state machine.
- `src/irc/` (pure) — `parser`, `encoder`, `ctcp`, `message`, `result`. No
  state, no I/O; unit-tested.

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
