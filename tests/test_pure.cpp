#include <QByteArray>
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>

#include <cstdio>

#include "irc/ctcp.hpp"
#include "irc/encoder.hpp"
#include "irc/endpoint.hpp"
#include "irc/parser.hpp"
#include "irc/prefix.hpp"
#include "ui/chat_formatter.hpp"
#include "ui/completion.hpp"
#include "ui/fun_actions.hpp"
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
    {
        // IRCv3 message tags in front of the prefix (servers may send them even
        // when the client did not request any).
        auto r = parseLine(QByteArrayLiteral(
            "@time=2026-10-21T10:00:00Z;msgid=abc123 :alice!u@h PRIVMSG #c :hi there"));
        CHECK(r.isOk(), "parse IRCv3-tagged message");
        if (r.isOk()) {
            CHECK(r.value().tagsRaw == "time=2026-10-21T10:00:00Z;msgid=abc123",
                  "tagged line: tags captured");
            CHECK(r.value().command == "PRIVMSG", "tagged line: command");
            CHECK(r.value().prefix.nick == "alice", "tagged line: prefix");
            CHECK(r.value().params.size() == 2 && r.value().params[1] == "hi there",
                  "tagged line: params");
        }
        auto bad = parseLine(QByteArrayLiteral("@only=tags"));
        CHECK(bad.isErr() && bad.error() == ParseError::NoCommand,
              "tags without a command -> NoCommand");
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
    {
        // Budget for the free-text parameter: 510 - ("PRIVMSG" + " #chan" + " :").
        CHECK(trailingParamBudget("PRIVMSG", {"#chan"}) == 495,
              "trailingParamBudget PRIVMSG #chan");
        CHECK(trailingParamBudget("NOTICE", {"nick"}) == 510 - 6 - 5 - 2,
              "trailingParamBudget NOTICE nick");
    }
    {
        // Splitting: every chunk fits, and word boundaries are preferred, so
        // re-joining with single spaces reproduces the original text.
        QStringList words;
        for (int i = 0; i < 40; ++i) {
            words << QStringLiteral("word%1").arg(i);
        }
        const QByteArray text = words.join(QLatin1Char(' ')).toUtf8();
        const QList<QByteArray> chunks = splitText(text, 40);
        CHECK(chunks.size() > 1, "splitText splits a long message");
        bool fits = true;
        bool wholeWords = true;
        for (const QByteArray& c : chunks) {
            fits = fits && c.size() <= 40 && !c.isEmpty();
            wholeWords = wholeWords && !c.startsWith(' ') && !c.endsWith(' ');
        }
        CHECK(fits, "splitText chunks fit the budget");
        CHECK(wholeWords, "splitText chunks are trimmed");
        CHECK(chunks.join(' ') == text, "splitText round-trips at word boundaries");

        // A single word longer than the budget is hard-cut, never clipped away.
        QByteArray word;
        word.fill('a', 95);
        const QList<QByteArray> hard = splitText(word, 40);
        CHECK(hard.size() == 3, "splitText hard-cuts an over-long word");
        CHECK(hard.join("") == word, "splitText hard cut preserves all bytes");

        // UTF-8: the cut backs off to a character boundary.
        QByteArray utf8;
        utf8.fill('a', 38);
        utf8 += QString::fromUtf8("\u00e8").toUtf8(); // "è" = 2 bytes
        utf8 += " tail";
        const QList<QByteArray> utf8Chunks = splitText(utf8, 40);
        bool valid = true;
        for (const QByteArray& c : utf8Chunks) {
            valid = valid && (QString::fromUtf8(c).toUtf8() == c);
        }
        CHECK(valid, "splitText never splits a UTF-8 sequence");

        CHECK(splitText(QByteArray(), 40).size() == 1 &&
                  splitText(QByteArray(), 40).first().isEmpty(),
              "splitText empty text -> one empty chunk");
        CHECK(splitText("abc", 0).size() == 1 && splitText("abc", 0).first() == "abc",
              "splitText non-positive budget -> whole text");
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

    // --- mode prefix updates (MODE +o/-o/+v/-v on the nick list) ---
    {
        CHECK(applyPrefixSymbol(QString(), QStringLiteral("@+"), QLatin1Char('@'), true) ==
                  QStringLiteral("@"),
              "applyPrefixSymbol adds to empty");
        CHECK(applyPrefixSymbol(QStringLiteral("+"), QStringLiteral("@+"), QLatin1Char('@'),
                                true) == QStringLiteral("@+"),
              "applyPrefixSymbol ranks the new symbol");
        CHECK(applyPrefixSymbol(QStringLiteral("@+"), QStringLiteral("@+"), QLatin1Char('@'),
                                false) == QStringLiteral("+"),
              "applyPrefixSymbol removes one, keeps the other");
        CHECK(applyPrefixSymbol(QStringLiteral("@"), QStringLiteral("@+"), QLatin1Char('@'),
                                true) == QStringLiteral("@"),
              "applyPrefixSymbol add is idempotent");
        CHECK(applyPrefixSymbol(QStringLiteral("@"), QStringLiteral("@+"), QLatin1Char('+'),
                                false) == QStringLiteral("@"),
              "applyPrefixSymbol removing an absent symbol is a no-op");
        CHECK(applyPrefixSymbol(QStringLiteral("+@"), QStringLiteral("@+"), QLatin1Char('+'),
                                true) == QStringLiteral("@+"),
              "applyPrefixSymbol re-ranks existing symbols");
        CHECK(applyPrefixSymbol(QStringLiteral("%"), QStringLiteral("~&@%+"),
                                QLatin1Char('@'), true) == QStringLiteral("@%"),
              "applyPrefixSymbol honours the server's symbol order");
    }

    // --- nick colors: stable, deterministic, case-insensitive ---
    {
        CHECK(ui::nickColor(QStringLiteral("alice")) ==
                  ui::nickColor(QStringLiteral("ALICE")),
              "nickColor is case-insensitive");
        // Pinned to the FNV-1a palette index: qHash() is seeded per process and
        // would make colors change on every launch.
        CHECK(ui::nickColor(QStringLiteral("alice")) == QStringLiteral("#ef6f6c"),
              "nickColor is deterministic across runs");
        CHECK(ui::nickColor(QString()) == QStringLiteral("#cccccc"),
              "nickColor empty nick");
    }

    // --- formatter ---
    {
        const QString h = ui::htmlEscape(QStringLiteral("<a>&\"b\""));
        CHECK(h == "&lt;a&gt;&amp;&quot;b&quot;", "htmlEscape");
    }
    {
        const QString h = ui::linkify(QStringLiteral("see https://example.com/x?a=1&b=2 now"));
        CHECK(h.contains(QStringLiteral("<a href=\"https://example.com/x?a=1&amp;b=2\"")),
              "linkify wraps URL and escapes ampersands");
        CHECK(!ui::linkify(QStringLiteral("plain text")).contains(QStringLiteral("<a ")),
              "linkify no-op without URL");
        CHECK(ui::linkify(QStringLiteral("go to https://ex.com."))
                  .contains(QStringLiteral("https://ex.com</a>.")),
              "linkify strips trailing period");
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

    // --- fun /me actions loader ---
    {
        const auto defs = ui::defaultFunActions();
        CHECK(!defs.isEmpty(), "default fun actions non-empty");

        QTemporaryFile f(QStringLiteral("/tmp/clint_actions_XXXXXX"));
        if (f.open()) {
            f.write("# comment\n\nWave | waves hello\nDance | dances\nNoSeparator\n");
            f.flush();
            const auto loaded = ui::loadFunActions(f.fileName());
            CHECK(loaded.size() == 3, "loadFunActions parses entries, skips comments/blanks");
            if (loaded.size() == 3) {
                CHECK(loaded[0].label == QStringLiteral("Wave") &&
                          loaded[0].text == QStringLiteral("waves hello"),
                      "loadFunActions label+text split");
                CHECK(loaded[1].label == QStringLiteral("Dance") &&
                          loaded[1].text == QStringLiteral("dances"),
                      "loadFunActions second entry");
                CHECK(loaded[2].label == QStringLiteral("NoSeparator") &&
                          loaded[2].text == QStringLiteral("NoSeparator"),
                      "loadFunActions no-separator line uses text as label");
            }
        } else {
            CHECK(false, "temp file open");
        }
        CHECK(ui::loadFunActions(QStringLiteral("/nonexistent/clint_actions.conf")).isEmpty(),
              "loadFunActions empty on missing file");
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

    // --- endpoint parsing (IPv6 literals, host:port, brackets) ---
    {
        // Plain names and name:port.
        auto ep = parseEndpoint(QStringLiteral("irc.example.net"));
        CHECK(ep.host == QStringLiteral("irc.example.net") && ep.port == 0 &&
                  !ep.isAddressLiteral,
              "endpoint plain host");
        ep = parseEndpoint(QStringLiteral("irc.example.net:6697"));
        CHECK(ep.host == QStringLiteral("irc.example.net") && ep.port == 6697,
              "endpoint host:port");

        // Bare IPv6 literal: colons belong to the address, never to a port.
        ep = parseEndpoint(QStringLiteral("2001:db8::1"));
        CHECK(ep.host == QStringLiteral("2001:db8::1") && ep.port == 0 && ep.isIpv6 &&
                  ep.isAddressLiteral,
              "endpoint bare IPv6 literal");
        ep = parseEndpoint(QStringLiteral("::1"));
        CHECK(ep.host == QStringLiteral("::1") && ep.isIpv6, "endpoint ::1 loopback");

        // Bracketed literal, with and without port: brackets are stripped so
        // the socket layer receives a connectable address.
        ep = parseEndpoint(QStringLiteral("[2001:db8::1]"));
        CHECK(ep.host == QStringLiteral("2001:db8::1") && ep.port == 0 && ep.isIpv6,
              "endpoint bracketed IPv6 literal");
        ep = parseEndpoint(QStringLiteral("[2001:db8::1]:6697"));
        CHECK(ep.host == QStringLiteral("2001:db8::1") && ep.port == 6697 && ep.isIpv6,
              "endpoint bracketed IPv6 literal + port");
        ep = parseEndpoint(QStringLiteral(" [::1]:6667 "));
        CHECK(ep.host == QStringLiteral("::1") && ep.port == 6667,
              "endpoint brackets + port, whitespace trimmed");

        // Zone/scope id survives (link-local servers).
        ep = parseEndpoint(QStringLiteral("[fe80::1%eth0]:6697"));
        CHECK(ep.host == QStringLiteral("fe80::1%eth0") && ep.port == 6697 && ep.isIpv6,
              "endpoint zone id");

        // IPv4 literal, and a non-numeric suffix that is not a port.
        ep = parseEndpoint(QStringLiteral("192.0.2.1:7000"));
        CHECK(ep.host == QStringLiteral("192.0.2.1") && ep.port == 7000 &&
                  ep.isAddressLiteral && !ep.isIpv6,
              "endpoint IPv4 literal + port");
        ep = parseEndpoint(QStringLiteral("irc.example.net:ircd"));
        CHECK(ep.host == QStringLiteral("irc.example.net:ircd") && ep.port == 0,
              "endpoint non-numeric port kept as host");
        ep = parseEndpoint(QStringLiteral(""));
        CHECK(ep.host.isEmpty() && ep.port == 0, "endpoint empty input");

        // Display form brackets IPv6 literals, leaves names alone.
        CHECK(formatEndpoint(QStringLiteral("::1"), 6697) == QStringLiteral("[::1]:6697"),
              "formatEndpoint brackets IPv6");
        CHECK(formatEndpoint(QStringLiteral("2001:db8::1"), 6667) ==
                  QStringLiteral("[2001:db8::1]:6667"),
              "formatEndpoint brackets full IPv6");
        CHECK(formatEndpoint(QStringLiteral("irc.example.net"), 6697) ==
                  QStringLiteral("irc.example.net:6697"),
              "formatEndpoint leaves hostnames unbracketed");
        CHECK(isIpv6Literal(QStringLiteral("fe80::1%eth0")), "isIpv6Literal zone id");
        CHECK(!isIpv6Literal(QStringLiteral("irc.example.net")), "isIpv6Literal name");
        CHECK(!isIpv6Literal(QStringLiteral("192.0.2.1")), "isIpv6Literal IPv4");
    }

    std::printf("\n%s (%d failures)\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
