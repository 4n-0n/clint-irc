#include <QByteArray>
#include <QCoreApplication>
#include <QString>

#include <cstdio>

#include "irc/ctcp.hpp"
#include "irc/encoder.hpp"
#include "irc/parser.hpp"
#include "irc/prefix.hpp"
#include "ui/chat_formatter.hpp"
#include "ui/completion.hpp"
#include "ui/input_history.hpp"

static int g_failures = 0;

#define CHECK(cond, msg)                                          \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s\n", msg);                       \
            ++g_failures;                                         \
        } else {                                                  \
            std::printf("ok:   %s\n", msg);                       \
        }                                                         \
    } while (0)

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    using namespace irc;

    // --- parser ---
    {
        auto r = parseLine(QByteArrayLiteral(":nick!user@host PRIVMSG #chan :hello world"));
        CHECK(r.isOk(), "parse PRIVMSG with prefix + trailing");
        if (r.isOk()) {
            CHECK(r.value().prefix.nick == "nick", "prefix nick");
            CHECK(r.value().prefix.user == "user", "prefix user");
            CHECK(r.value().prefix.host == "host", "prefix host");
            CHECK(r.value().command == "PRIVMSG", "command");
            CHECK(r.value().params.size() == 2, "param count");
            CHECK(r.value().params[0] == "#chan", "param[0]");
            CHECK(r.value().params[1] == "hello world", "trailing param");
        }
    }
    {
        auto r = parseLine(QByteArrayLiteral("PING :token123"));
        CHECK(r.isOk() && r.value().command == "PING" &&
              r.value().params.size() == 1 && r.value().params[0] == "token123",
              "parse PING");
    }
    {
        auto r = parseLine(QByteArrayLiteral("001 nick :Welcome"));
        CHECK(r.isOk() && r.value().command == "001", "parse numeric 001");
    }
    {
        auto r = parseLine(QByteArrayLiteral(""));
        CHECK(r.isErr() && r.error() == ParseError::EmptyLine, "empty line -> EmptyLine");
    }
    {
        auto r = parseLine(QByteArrayLiteral(":prefix-only"));
        CHECK(r.isErr() && r.error() == ParseError::NoCommand, "prefix-only -> NoCommand");
    }

    // --- encoder ---
    {
        const QByteArray l = encodeCommand("PRIVMSG", {"#chan", "hello world"});
        CHECK(l == "PRIVMSG #chan :hello world", "encoder trailing colon");
    }
    {
        const QByteArray l = encodeCommand("JOIN", {"#chan"});
        CHECK(l == "JOIN #chan", "encoder no colon");
    }
    {
        const QByteArray l = encodeCommand("PRIVMSG", {"#chan", ""});
        CHECK(l == "PRIVMSG #chan :", "encoder empty trailing param");
    }
    {
        // ASCII overflow: clip to exactly 510 content bytes, colon preserved.
        QByteArray text;
        text.fill('a', 490);
        text += " hello world"; // space forces a trailing ':' separator
        const QByteArray l = encodeCommand("PRIVMSG", {"#chan", text});
        CHECK(l.size() == 510, "encoder clips long line to 510 bytes");
        CHECK(l.startsWith("PRIVMSG #chan :"), "encoder clip preserves colon");
    }
    {
        // UTF-8 overflow: clip must not split a multibyte sequence.
        QByteArray text;
        text.fill('a', 495);
        text += QString::fromUtf8("\u00e9").toUtf8(); // "é" = 2 bytes
        const QByteArray l = encodeCommand("PRIVMSG", {"#chan", text});
        CHECK(l.size() <= 510, "utf8 clip respects 510 limit");
        CHECK(QString::fromUtf8(l).toUtf8() == l, "utf8 clip does not split multibyte");
    }

    // --- ctcp ---
    {
        const QByteArray payload =
            QByteArrayLiteral("\x01") + "ACTION waves" + QByteArrayLiteral("\x01");
        auto p = ctcp::extractPayload(payload);
        CHECK(p.has_value() && *p == "ACTION waves", "ctcp extract");
        if (p.has_value()) {
            const auto parsed = ctcp::parse(*p);
            CHECK(parsed.tag == "ACTION" && parsed.argument == "waves", "ctcp parse");
        }
        CHECK(ctcp::encode("ACTION", "waves") == payload, "ctcp encode");
    }

    // --- prefix splitting (ISUPPORT PREFIX) ---
    {
        const auto np = splitNickPrefix(QStringLiteral("@opnick"), QStringLiteral("@+"));
        CHECK(np.prefix == "@" && np.nick == "opnick", "prefix '@' stripped");
    }
    {
        const auto np = splitNickPrefix(QStringLiteral("+voice"), QStringLiteral("@+"));
        CHECK(np.prefix == "+" && np.nick == "voice", "prefix '+' stripped");
    }
    {
        const auto np = splitNickPrefix(QStringLiteral("plain"), QStringLiteral("@+"));
        CHECK(np.prefix.isEmpty() && np.nick == "plain", "no prefix");
    }
    {
        const auto np = splitNickPrefix(QStringLiteral("@+both"), QStringLiteral("@+"));
        CHECK(np.prefix == "@+" && np.nick == "both", "multiple prefixes");
    }
    {
        const auto np = splitNickPrefix(QStringLiteral("%halfop"), QStringLiteral("@+%"));
        CHECK(np.prefix == "%" && np.nick == "halfop", "custom prefix set");
    }

    // --- formatter ---
    {
        const QString h = ui::htmlEscape(QStringLiteral("<a>&\"b\""));
        CHECK(h == "&lt;a&gt;&amp;&quot;b&quot;", "htmlEscape");
    }
    {
        auto r = parseLine(QByteArrayLiteral(":alice!u@h PRIVMSG #c :hi"));
        const QString html = ui::formatChannelLine(r.value());
        CHECK(html.contains("alice"), "channel line contains sender nick");
        CHECK(html.contains("hi"), "channel line contains text");
    }
    {
        auto r = parseLine(QByteArrayLiteral(":srv 311 me alice user host * :Alice Real"));
        const QString html = ui::formatServerLine(r.value());
        CHECK(html.contains("alice is user@host"), "WHOIS 311 rendering");
    }
    {
        auto r = parseLine(QByteArrayLiteral(":srv 315 me #chan :End of /WHO list."));
        const QString html = ui::formatServerLine(r.value());
        CHECK(html.contains("End of WHO"), "End of WHO rendering");
    }

    // --- nick mention detection ---
    {
        CHECK(ui::containsNick(QStringLiteral("hey alice, how are you?"), QStringLiteral("alice")),
              "containsNick basic");
        CHECK(ui::containsNick(QStringLiteral("ALICE is here"), QStringLiteral("alice")),
              "containsNick case-insensitive");
        CHECK(ui::containsNick(QStringLiteral("alice: ping"), QStringLiteral("alice")),
              "containsNick punctuation boundary");
        CHECK(!ui::containsNick(QStringLiteral("hello alicex"), QStringLiteral("alice")),
              "containsNick rejects suffix words");
        CHECK(!ui::containsNick(QStringLiteral("hello malice"), QStringLiteral("alice")),
              "containsNick rejects embedded substring");
        CHECK(!ui::containsNick(QStringLiteral("hello alice"), QString()),
              "containsNick empty nick -> false");
    }

    // --- input history (global, dedup + cap) ---
    {
        ui::InputHistory h;
        h.add(QStringLiteral("one"));
        h.add(QStringLiteral("one")); // consecutive duplicate -> dropped
        h.add(QStringLiteral("two"));
        CHECK(h.count() == 2, "history dedup consecutive");
        CHECK(h.items().at(0) == "one" && h.items().at(1) == "two", "history order");
    }

    // --- tab completion matching ---
    {
        const QStringList nicks{QStringLiteral("alice"), QStringLiteral("albert"),
                                QStringLiteral("bob"), QStringLiteral("opnick")};
        const auto m = ui::completionMatches(nicks, QStringLiteral("al"));
        CHECK(m == QStringList({QStringLiteral("alice"), QStringLiteral("albert")}),
              "completion matches prefix");
        const auto m2 = ui::completionMatches(nicks, QStringLiteral("AL"));
        CHECK(m2 == QStringList({QStringLiteral("alice"), QStringLiteral("albert")}),
              "completion case-insensitive");
        CHECK(ui::completionMatches(nicks, QStringLiteral("zz")).isEmpty(),
              "completion no match");
        CHECK(ui::completionMatches(nicks, QString()).isEmpty(),
              "completion empty prefix");
    }

    std::printf("\n%s (%d failures)\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
