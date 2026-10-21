// Session-layer test: ClientController against a fake in-process IRC server.
//
// Exercises the behaviour that lives above the parser and below the UI:
//   - channel topic tracking (332, TOPIC, 331) -> channelTopicChanged
//   - MODE +o/-o/+v/-v updates the nick list prefixes
//   - long chat/action messages are split into several legal-sized lines
//   - a 433 while registered reports the error instead of renaming us
//
// Qt Core + Network only (no widgets, no display).
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QStringList>

#include <cstdio>

#include "app/client_controller.hpp"
#include "irc/connection_config.hpp"
#include "irc/encoder.hpp"

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

template <typename Predicate>
bool waitFor(Predicate predicate, int timeoutMs = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return predicate();
}

// Pumps the event loop for `ms` (used after a send: the socket writes are
// queued, so the loop has to run once before the peer sees them).
void pump(int ms = 250) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

// A loopback server that records everything the client sends and lets the test
// push raw lines back.
struct FakeServer {
    QTcpServer server;
    QTcpSocket* peer = nullptr;
    QByteArray received;

    bool listen() {
        if (!server.listen(QHostAddress::LocalHost, 0)) {
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

    void send(const QByteArray& line) {
        if (peer) {
            peer->write(line + "\r\n");
            peer->flush();
        }
    }

    // Complete lines received so far (CRLF stripped).
    QStringList lines() const {
        const QString text = QString::fromUtf8(received);
        QStringList out;
        for (const QString& l : text.split(QStringLiteral("\r\n"), Qt::SkipEmptyParts)) {
            out << l;
        }
        return out;
    }

    void clearReceived() { received.clear(); }
};

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    FakeServer server;
    if (!server.listen()) {
        std::printf("could not listen on loopback\n");
        return 0; // environment problem, not a client failure
    }

    app::ClientController controller;
    QStringList serverErrors;
    QStringList nickLists;
    QStringList topics;
    QString lastNick;

    QObject::connect(&controller, &app::ClientController::errorOccurred,
                     [&serverErrors](const QString& m) { serverErrors << m; });
    QObject::connect(&controller, &app::ClientController::nickListChanged,
                     [&nickLists](const QString&, const QStringList& nicks) {
                         nickLists << nicks.join(QLatin1Char(' '));
                     });
    QObject::connect(&controller, &app::ClientController::channelTopicChanged,
                     [&topics](const QString&, const QString& topic) { topics << topic; });
    QObject::connect(&controller, &app::ClientController::ownNickChanged,
                     [&lastNick](const QString& n) { lastNick = n; });

    irc::ConnectionConfig cfg;
    cfg.host = QStringLiteral("127.0.0.1");
    cfg.port = server.port();
    cfg.nick = QStringLiteral("tester");
    cfg.realName = QStringLiteral("tester");
    controller.connectToServer(cfg);

    CHECK(waitFor([&] { return server.peer != nullptr; }), "client connects");
    CHECK(waitFor([&] { return server.received.contains("NICK tester"); }),
          "client sends NICK");

    // Register, advertise PREFIX, and echo our own JOIN.
    server.send(":srv 001 tester :Welcome");
    server.send(":srv 005 tester PREFIX=(ov)@+ CHANTYPES=# :are supported");
    server.send(":tester!u@h JOIN #test");
    CHECK(waitFor([&] { return controller.channels().contains(QStringLiteral("#test")); }),
          "self-join creates the channel");

    // NAMES then prefix changes.
    server.send(":srv 353 tester = #test :@alice +bob tester");
    server.send(":srv 366 tester #test :End of /NAMES list.");
    CHECK(waitFor([&] { return !nickLists.isEmpty(); }), "nick list arrives");
    CHECK(!nickLists.isEmpty() &&
              nickLists.last() == QStringLiteral("@alice +bob tester"),
          "nick list keeps the server's prefixes");

    nickLists.clear();
    server.send(":srv MODE #test +o bob");
    CHECK(waitFor([&] { return !nickLists.isEmpty(); }), "MODE +o refreshes the nick list");
    CHECK(!nickLists.isEmpty() && nickLists.last() == QStringLiteral("@alice @bob tester"),
          "MODE +o adds '@' (and ranks it)");

    nickLists.clear();
    server.send(":srv MODE #test -o alice");
    CHECK(waitFor([&] { return !nickLists.isEmpty(); }), "MODE -o refreshes the nick list");
    CHECK(!nickLists.isEmpty() && nickLists.last() == QStringLiteral("alice @bob tester"),
          "MODE -o removes '@'");

    nickLists.clear();
    server.send(":srv MODE #test +v alice");
    CHECK(waitFor([&] { return !nickLists.isEmpty(); }), "MODE +v refreshes the nick list");
    CHECK(!nickLists.isEmpty() && nickLists.last() == QStringLiteral("+alice @bob tester"),
          "MODE +v adds '+'");

    // Topic: bar content, change, and reset. Joining already emitted an empty
    // topic, so start counting from here.
    topics.clear();
    server.send(":srv 332 tester #test :Welcome to #test");
    CHECK(waitFor([&] { return topics.size() >= 1; }), "332 sets the topic");
    CHECK(topics.size() >= 1 && topics.last() == QStringLiteral("Welcome to #test"),
          "topic text is delivered");

    server.send(":alice!u@h TOPIC #test :New topic");
    CHECK(waitFor([&] { return topics.size() >= 2; }), "TOPIC updates the topic");
    CHECK(topics.size() >= 2 && topics.last() == QStringLiteral("New topic"),
          "TOPIC carries the new text");

    server.send(":srv 331 tester #test :No topic is set");
    CHECK(waitFor([&] { return topics.size() >= 3; }), "331 clears the topic");
    CHECK(topics.size() >= 3 && topics.last().isEmpty(), "331 delivers an empty topic");

    // A mention highlights the line and raises channelHighlighted.
    QString lastChannelHtml;
    int highlights = 0;
    QObject::connect(&controller, &app::ClientController::channelMessage,
                     [&lastChannelHtml](const QString&, const QString& html) {
                         lastChannelHtml = html;
                     });
    QObject::connect(&controller, &app::ClientController::channelHighlighted,
                     [&highlights](const QString&) { ++highlights; });
    server.send(":alice!u@h PRIVMSG #test :hello tester, look here");
    CHECK(waitFor([&] { return !lastChannelHtml.isEmpty(); }), "channel message arrives");
    CHECK(highlights == 1, "a nick mention raises channelHighlighted");
    CHECK(lastChannelHtml.contains(QStringLiteral("background-color")),
          "a mentioning line is render-highlighted");
    server.send(":alice!u@h PRIVMSG #test :unrelated chatter");
    CHECK(waitFor([&] { return lastChannelHtml.contains(QStringLiteral("unrelated")); }),
          "plain channel message arrives");
    CHECK(highlights == 1, "a non-mention does not raise channelHighlighted");

    // Long message splitting: several lines, each within the IRC limit, and the
    // text arrives complete when the payloads are joined back with spaces.
    QStringList words;
    for (int i = 0; i < 120; ++i) {
        words << QStringLiteral("word%1").arg(i % 10);
    }
    const QString longText = words.join(QLatin1Char(' '));
    server.clearReceived();
    controller.sendChat(QStringLiteral("#test"), longText);
    pump();

    const QStringList chatLines = server.lines();
    bool sizeOk = !chatLines.isEmpty();
    QStringList payloads;
    for (const QString& line : chatLines) {
        if (!line.startsWith(QStringLiteral("PRIVMSG #test :"))) {
            sizeOk = false;
            break;
        }
        sizeOk = sizeOk && (line.toUtf8().size() + 2 <= irc::kMaxLineLength);
        payloads << line.mid(QStringLiteral("PRIVMSG #test :").size());
    }
    CHECK(sizeOk, "every split PRIVMSG line is a legal IRC line");
    CHECK(chatLines.size() > 1, "a long message becomes several PRIVMSG lines");
    CHECK(payloads.join(QLatin1Char(' ')) == longText,
          "split lines reassemble to the original text");

    // Same for an ACTION: every chunk stays CTCP-framed.
    server.clearReceived();
    controller.sendAction(QStringLiteral("#test"), longText);
    pump();
    const QStringList actionLines = server.lines();
    bool framed = actionLines.size() > 1;
    for (const QString& line : actionLines) {
        const int prefixLen = QStringLiteral("PRIVMSG #test :").size();
        const QString body = line.mid(prefixLen);
        // "\x01" is kept in its own literal: in "\x01ACTION" the compiler
        // would read the following hex digits as part of the escape.
        framed = framed && body.startsWith(QStringLiteral("\x01") +
                                           QStringLiteral("ACTION ")) &&
                 body.endsWith(QStringLiteral("\x01"));
        framed = framed && (line.toUtf8().size() + 2 <= irc::kMaxLineLength);
    }
    CHECK(framed, "a long ACTION is split into valid CTCP chunks");

    // 433 while registered: report it, never rename the user behind their back.
    server.clearReceived();
    serverErrors.clear();
    lastNick.clear();
    server.send(":srv 433 tester wanted :Nickname is already in use.");
    CHECK(waitFor([&] { return !serverErrors.isEmpty(); }),
          "433 while registered reports an error");
    CHECK(!serverErrors.isEmpty() &&
              serverErrors.last().contains(QStringLiteral("already in use")),
          "the 433 error names the problem");
    const bool renick = server.received.contains("NICK");
    CHECK(!renick, "433 does not silently rename the user");
    CHECK(lastNick.isEmpty(), "own nick unchanged after 433");

    controller.quit(QStringLiteral("bye"));
    waitFor([&] { return controller.state() == irc::ConnectionState::Disconnected; });
    CHECK(waitFor([&] { return server.received.contains("QUIT"); }), "QUIT is sent");

    std::printf("\n%s (%d failures)\n",
                g_failures == 0 ? "ALL CONTROLLER TESTS PASSED" : "CONTROLLER TESTS FAILED",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
