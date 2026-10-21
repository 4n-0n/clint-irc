#!/usr/bin/env python3
"""Minimal IRC server for integration-testing the clint client.

Binds 127.0.0.1 by default; pass --host ::1 (or set MOCK_HOST=::1) to serve an
IPv6-only endpoint, which is what the client's IPv6 path needs to be tested
against.
Accepts one connection, logs every received line to LOG_FILE (env, default
mock_server.log), and drives a registration handshake:
  NICK/USER -> 001 welcome + PING
  PONG      -> (logged, no reply)
  JOIN      -> 353 NAMES + 366 END OF NAMES

Also implements IRCv3 SASL PLAIN when the client negotiates it:
  CAP LS 302       -> CAP * LS :sasl multi-prefix
  CAP REQ :sasl    -> CAP * ACK :sasl
  AUTHENTICATE PLAIN -> AUTHENTICATE +
  AUTHENTICATE <b64> -> 903 RPL_SASLSUCCESS (payload decoded + logged)
  CAP END          -> (no reply)
"""
import base64
import os
import socket
import sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
host_arg = None
for i, a in enumerate(sys.argv):
    if a == "--host" and i + 1 < len(sys.argv):
        host_arg = sys.argv[i + 1]

HOST = host_arg or os.environ.get("MOCK_HOST", "127.0.0.1")
PORT = int(args[0]) if args else 6667
LOG = os.environ.get("MOCK_LOG", "mock_server.log")

def log(line):
    with open(LOG, "a", encoding="utf-8") as f:
        f.write(line + "\n")

def send(sock, line):
    log(f">>> {line}")
    sock.sendall((line + "\r\n").encode("utf-8"))

def main():
    open(LOG, "w").close()  # truncate
    # Pick the socket family from the bind address, so ::1 really is IPv6-only.
    family, socktype, proto, _name, sockaddr = socket.getaddrinfo(
        HOST, PORT, socket.AF_UNSPEC, socket.SOCK_STREAM, 0, socket.AI_PASSIVE
    )[0]
    srv = socket.socket(family, socktype, proto)
    if family == socket.AF_INET6:
        srv.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(sockaddr)
    srv.listen(1)
    log(f"listening on [{HOST}]:{PORT}" if family == socket.AF_INET6
        else f"listening on {HOST}:{PORT}")

    conn, addr = srv.accept()
    log(f"accepted {addr}")
    conn.settimeout(10)
    buf = b""
    saw_nick = saw_user = False
    sent_welcome = False
    nick = ""

    try:
        while True:
            data = conn.recv(4096)
            if not data:
                break
            buf += data
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = raw.rstrip(b"\r").decode("utf-8", "replace")
                log(f"<<< {line}")
                parts = line.split()
                if not parts:
                    continue
                cmd = parts[0].upper()
                if cmd == "NICK":
                    nick = parts[1] if len(parts) > 1 else ""
                    saw_nick = True
                elif cmd == "USER":
                    saw_user = True
                elif cmd == "PONG":
                    pass
                elif cmd == "CAP":
                    sub = parts[1].upper() if len(parts) > 1 else ""
                    if sub == "LS":
                        send(conn, ":srv CAP * LS :sasl multi-prefix")
                    elif sub == "REQ":
                        send(conn, ":srv CAP * ACK :sasl")
                    # CAP END -> no reply
                elif cmd == "AUTHENTICATE":
                    if len(parts) > 1 and parts[1] == "PLAIN":
                        send(conn, "AUTHENTICATE +")
                    elif len(parts) > 1:
                        try:
                            decoded = base64.b64decode(parts[1])
                            log(f"    sasl payload: {decoded!r}")
                        except Exception as e:  # noqa: BLE001
                            log(f"    sasl decode error: {e}")
                        send(conn, ":srv 903 * :SASL authentication successful")
                elif cmd == "JOIN":
                    chan = parts[1] if len(parts) > 1 else ""
                    send(conn, f":srv 332 {nick} {chan} :Welcome to {chan} - be nice")
                    send(conn, f":srv 353 {nick} = {chan} :{nick} @opnick +voice")
                    send(conn, f":srv 366 {nick} {chan} :End of /NAMES list.")
                    # Exercise the client's MODE handling: op a nick in the list.
                    send(conn, f":srv MODE {chan} +o opnick")
                    send(conn, f":srv MODE {chan} +v {nick}")
                    send(conn, f":srv TOPIC {chan} :Welcome to {chan} - be nice")
                elif cmd == "LIST":
                    send(conn, f":srv 321 {nick} Channel :Users Name")
                    send(conn, f":srv 322 {nick} #general 42 :General discussion")
                    send(conn, f":srv 322 {nick} #dev 7 :Development channel")
                    send(conn, f":srv 323 {nick} :End of /LIST")
                elif cmd == "QUIT":
                    break

                if saw_nick and saw_user and not sent_welcome:
                    sent_welcome = True
                    send(conn, f":srv 001 {nick} :Welcome to the mock network")
                    send(conn, f":srv 002 {nick} :Your host is mock")
                    send(conn, f":srv 003 {nick} :This server was created now")
                    send(conn, f":srv 004 {nick} mock 0.1 o o")
                    send(conn, "PING :mocktoken123")
    except socket.timeout:
        log("timeout (client did not QUIT)")

    conn.close()
    srv.close()
    log("closed")

if __name__ == "__main__":
    main()
