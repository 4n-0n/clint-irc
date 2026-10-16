#!/usr/bin/env python3
"""Minimal IRC server for integration-testing the clint client.

Accepts one connection, logs every received line to LOG_FILE (env, default
mock_server.log), and drives a registration handshake:
  NICK/USER -> 001 welcome + PING
  PONG      -> (logged, no reply)
  JOIN      -> 353 NAMES + 366 END OF NAMES
"""
import os
import socket
import sys
import threading
import time

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 6667
LOG = os.environ.get("MOCK_LOG", "mock_server.log")

def log(line):
    with open(LOG, "a", encoding="utf-8") as f:
        f.write(line + "\n")

def send(sock, line):
    log(f">>> {line}")
    sock.sendall((line + "\r\n").encode("utf-8"))

def main():
    open(LOG, "w").close()  # truncate
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, PORT))
    srv.listen(1)
    log(f"listening on {HOST}:{PORT}")

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
                elif cmd == "JOIN":
                    chan = parts[1] if len(parts) > 1 else ""
                    send(conn, f":srv 353 {nick} = {chan} :{nick} @opnick +voice")
                    send(conn, f":srv 366 {nick} {chan} :End of /NAMES list.")
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
