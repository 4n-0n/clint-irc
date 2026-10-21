// Connection-layer test: IPv6 addressing and address-family selection.
//
// Uses real loopback servers (QTcpServer on ::1 and 127.0.0.1) so the whole
// path — endpoint parsing, resolution, socket connect, registration — is
// exercised against an IPv6-only endpoint, which is the case a plain
// "connectToHost(name)" client cannot guarantee.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <cstdio>

#include "irc/connection.hpp"
#include "irc/endpoint.hpp"

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            std::printf("FAIL: %s\n", msg);         \
            ++g_failures;                           \
        } else {                                    \
            std::printf("ok:   %s\n", msg);         \
        }                                           \
    } while (0)

// Pumps the event loop until `predicate` is true or the budget runs out.
template <typename Predicate>
bool waitFor(Predicate predicate, int timeoutMs = 8000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return predicate();
}

bool ipv6LoopbackAvailable() {
    QTcpServer probe;
    const bool ok = probe.listen(QHostAddress::LocalHostIPv6, 0);
    if (ok) {
        probe.close();
    }
    return ok;
}

// A listening loopback server plus the lines it received, for asserting that
// the client actually reached it and started registering.
struct LoopbackServer {
    QTcpServer server;
    QTcpSocket* peer = nullptr;
    QByteArray received;

    bool listen(const QHostAddress& address) {
        if (!server.listen(address, 0)) {
            return false;
        }
        QObject::connect(&server, &QTcpServer::newConnection, [this] {
            peer = server.nextPendingConnection();
            QObject::connect(peer, &QTcpSocket::readyRead, [this] {
                received += peer->readAll();
            });
            QObject::connect(peer, &QTcpSocket::disconnected, [this] {
                if (peer) {
                    peer->deleteLater();
                    peer = nullptr;
                }
            });
        });
        return true;
    }

    quint16 port() const { return server.serverPort(); }
};

void testIpv6LiteralConnects() {
    LoopbackServer loopback;
    if (!loopback.listen(QHostAddress::LocalHostIPv6)) {
        std::printf("SKIP: no IPv6 loopback on this host\n");
        return;
    }

    irc::IrcConnection conn;
    QStringList errors;
    QObject::connect(&conn, &irc::IrcConnection::errorOccurred,
                     [&errors](const QString& msg) { errors << msg; });

    irc::ConnectionConfig cfg;
    cfg.host = QStringLiteral("[::1]"); // bracketed literal, as users paste it
    cfg.port = loopback.port();
    cfg.nick = QStringLiteral("v6tester");
    cfg.realName = QStringLiteral("v6tester");
    conn.connectToServer(cfg);

    CHECK(waitFor([&] { return loopback.peer != nullptr; }),
          "connects to an IPv6-only loopback server via [::1]");
    CHECK(waitFor([&] { return conn.state() == irc::ConnectionState::Registering; }),
          "reaches Registering over IPv6");
    CHECK(waitFor([&] { return loopback.received.contains("NICK v6tester"); }),
          "IPv6 server receives NICK");
    CHECK(conn.peerEndpoint().startsWith(QStringLiteral("[::1]:")),
          "peer endpoint is reported as [::1]:port");
    CHECK(errors.isEmpty(), "no errors on a successful IPv6 connect");

    conn.disconnectFromServer();
    CHECK(waitFor([&] { return conn.state() == irc::ConnectionState::Disconnected; }),
          "disconnects cleanly");
}

void testHostPortInHostString() {
    LoopbackServer loopback;
    if (!loopback.listen(QHostAddress::LocalHostIPv6)) {
        std::printf("SKIP: no IPv6 loopback on this host\n");
        return;
    }

    irc::IrcConnection conn;
    irc::ConnectionConfig cfg;
    cfg.host = QStringLiteral("[::1]:%1").arg(loopback.port());
    cfg.port = 1; // deliberately wrong: the port in the host string must win
    cfg.nick = QStringLiteral("v6tester");
    cfg.realName = QStringLiteral("v6tester");
    conn.connectToServer(cfg);

    CHECK(waitFor([&] { return loopback.peer != nullptr; }),
          "[::1]:port in the host string overrides the port field");
    conn.disconnectFromServer();
    waitFor([&] { return conn.state() == irc::ConnectionState::Disconnected; });
}

void testIpv6OnlyRejectsIpv4Literal() {
    irc::IrcConnection conn;
    QStringList errors;
    QObject::connect(&conn, &irc::IrcConnection::errorOccurred,
                     [&errors](const QString& msg) { errors << msg; });

    irc::ConnectionConfig cfg;
    cfg.host = QStringLiteral("127.0.0.1");
    cfg.port = 6667;
    cfg.family = irc::AddressFamily::IPv6;
    cfg.nick = QStringLiteral("v6tester");
    conn.connectToServer(cfg);

    CHECK(waitFor([&] { return !errors.isEmpty(); }),
          "IPv6-only selection rejects an IPv4 literal");
    CHECK(!errors.isEmpty() && errors.first().contains(QStringLiteral("IPv6")),
          "rejection message names the requested family");
    CHECK(conn.state() == irc::ConnectionState::Disconnected,
          "stays Disconnected after a family mismatch");
}

void testUnresolvableHostIsReported() {
    irc::IrcConnection conn;
    QStringList errors;
    QObject::connect(&conn, &irc::IrcConnection::errorOccurred,
                     [&errors](const QString& msg) { errors << msg; });

    irc::ConnectionConfig cfg;
    cfg.host = QStringLiteral("no-such-host.invalid");
    cfg.port = 6667;
    cfg.nick = QStringLiteral("v6tester");
    conn.connectToServer(cfg);

    CHECK(waitFor([&] { return !errors.isEmpty(); }),
          "unresolvable host reports an error");
    CHECK(conn.state() == irc::ConnectionState::Disconnected,
          "unresolvable host ends Disconnected");
}

void testFallsBackToIpv4() {
    // Dual-stack style: several addresses for one name, the first attempt goes
    // nowhere (nothing listening on the v6 port) and a later one works. On a
    // single-family host this degrades into a plain connect test.
    LoopbackServer v4;
    QTcpServer v6;
    if (!v4.listen(QHostAddress::LocalHost)) {
        std::printf("SKIP: no IPv4 loopback on this host\n");
        return;
    }
    const bool haveV6 = v6.listen(QHostAddress::LocalHostIPv6, 0);
    const quint16 v6Port = haveV6 ? v6.serverPort() : 0;
    if (haveV6) {
        v6.close(); // nothing listening on the v6 port any more
    }

    irc::IrcConnection conn;
    irc::ConnectionConfig cfg;
    cfg.host = QStringLiteral("localhost"); // resolves to both families
    cfg.port = v4.port();
    cfg.nick = QStringLiteral("v6tester");
    cfg.realName = QStringLiteral("v6tester");
    conn.connectToServer(cfg);

    const bool connected = waitFor([&] { return v4.peer != nullptr; }, 12000);
    if (!haveV6) {
        std::printf("note: single-family host, failover not exercised\n");
    }
    CHECK(connected, "falls back to a working address when the first fails");
    Q_UNUSED(v6Port);
    conn.disconnectFromServer();
    waitFor([&] { return conn.state() == irc::ConnectionState::Disconnected; });
}

void testAbortDuringResolution() {
    // Disconnecting while the lookup is still in flight must not leave the
    // object wedged in Connecting, and the stale callback must be ignored.
    irc::IrcConnection conn;
    int stateChanges = 0;
    QObject::connect(&conn, &irc::IrcConnection::stateChanged,
                     [&stateChanges](irc::ConnectionState) { ++stateChanges; });

    irc::ConnectionConfig cfg;
    cfg.host = QStringLiteral("no-such-host.invalid");
    cfg.port = 6667;
    cfg.nick = QStringLiteral("v6tester");
    conn.connectToServer(cfg);
    conn.disconnectFromServer();

    CHECK(conn.state() == irc::ConnectionState::Disconnected,
          "disconnect during resolution ends Disconnected");
    // Give the aborted lookup callback a chance to (wrongly) fire.
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 400) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    CHECK(conn.state() == irc::ConnectionState::Disconnected,
          "stale lookup callback does not revive the connection");
    Q_UNUSED(stateChanges);
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    testIpv6LiteralConnects();
    testHostPortInHostString();
    testIpv6OnlyRejectsIpv4Literal();
    testUnresolvableHostIsReported();
    testFallsBackToIpv4();
    testAbortDuringResolution();

    std::printf("\n%s (%d failures)\n",
                g_failures == 0 ? "ALL CONNECT TESTS PASSED" : "CONNECT TESTS FAILED",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
