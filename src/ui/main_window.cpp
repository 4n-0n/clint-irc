#include "main_window.hpp"

#include "../app/client_controller.hpp"
#include "../irc/connection_config.hpp"
#include "chat_formatter.hpp"
#include "connect_dialog.hpp"

#include <QCloseEvent>
#include <QApplication>
#include <QDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>

namespace {

// Splits command arguments on whitespace.
QStringList splitArgs(const QString& s) {
    return s.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

} // namespace

namespace ui {

MainWindow::MainWindow(app::ClientController* controller, QWidget* parent)
    : QMainWindow(parent), m_controller(controller) {
    setupUi();
    setupMenus();
    wireController();
}

void MainWindow::setupUi() {
    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setDocumentMode(true);
    setCentralWidget(m_tabs);

    m_serverTab = new ChannelWidget(ChannelWidget::Context::Server, QStringLiteral("server"), this);
    m_serverTab->setHistory(&m_history);
    m_tabs->addTab(m_serverTab, QStringLiteral("server"));
    connect(m_serverTab, &ChannelWidget::inputSubmitted, this, [this](const QString& text) {
        onInput(m_serverTab, text);
    });

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        QWidget* w = m_tabs->widget(index);
        if (w == m_serverTab) {
            return;
        }
        auto* tab = qobject_cast<ChannelWidget*>(w);
        if (!tab) {
            return;
        }
        if (tab->context() == ChannelWidget::Context::Channel) {
            m_controller->partChannel(tab->title());
        }
        m_tabIndex.remove(tab->title().toLower());
        m_tabs->removeTab(index);
        tab->deleteLater();
    });

    // Selecting a tab clears its activity marker (●/★).
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        ChannelWidget* tab = qobject_cast<ChannelWidget*>(m_tabs->widget(index));
        if (!tab) {
            return;
        }
        const QString title = tab->title();
        if (m_tabs->tabText(index) != title) {
            m_tabs->setTabText(index, title);
        }
    });

    statusBar()->showMessage(QStringLiteral("Disconnected"));
    resize(1000, 700);
}

void MainWindow::setupMenus() {
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    m_connectAction = fileMenu->addAction(QStringLiteral("&Connect..."), this, &MainWindow::onConnectAction);
    m_disconnectAction = fileMenu->addAction(QStringLiteral("&Disconnect"), this, &MainWindow::onDisconnectAction);
    m_disconnectAction->setEnabled(false);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("&Quit"), this, &QWidget::close);

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    helpMenu->addAction(QStringLiteral("&About"), this, &MainWindow::onAboutAction);
}

void MainWindow::wireController() {
    connect(m_controller, &app::ClientController::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_controller, &app::ClientController::serverMessage, this, &MainWindow::onServerMessage);
    connect(m_controller, &app::ClientController::channelMessage, this, &MainWindow::onChannelMessage);
    connect(m_controller, &app::ClientController::queryMessage, this, &MainWindow::onQueryMessage);
    connect(m_controller, &app::ClientController::channelHighlighted, this, &MainWindow::onChannelHighlighted);
    connect(m_controller, &app::ClientController::channelJoined, this, &MainWindow::onChannelJoined);
    connect(m_controller, &app::ClientController::channelParted, this, &MainWindow::onChannelParted);
    connect(m_controller, &app::ClientController::nickListChanged, this, &MainWindow::onNickList);
    connect(m_controller, &app::ClientController::ownNickChanged, this, &MainWindow::onOwnNick);
    connect(m_controller, &app::ClientController::errorOccurred, this, &MainWindow::onError);
    connect(m_controller, &app::ClientController::listStarted, this, &MainWindow::onListStarted);
    connect(m_controller, &app::ClientController::listEntry, this, &MainWindow::onListEntry);
    connect(m_controller, &app::ClientController::listEnded, this, &MainWindow::onListEnded);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_controller->isConnected()) {
        m_controller->quit(QStringLiteral("clint exiting"));
    }
    QMainWindow::closeEvent(event);
}

// ---- Controller -> UI ----------------------------------------------------

QString MainWindow::stateText(irc::ConnectionState state) const {
    switch (state) {
    case irc::ConnectionState::Connecting:  return QStringLiteral("Connecting...");
    case irc::ConnectionState::Registering: return QStringLiteral("Registering...");
    case irc::ConnectionState::Connected:   return QStringLiteral("Connected");
    case irc::ConnectionState::Disconnected: return QStringLiteral("Disconnected");
    }
    return QString();
}

void MainWindow::onStateChanged(irc::ConnectionState state) {
    statusBar()->showMessage(stateText(state));
    m_connectAction->setEnabled(state == irc::ConnectionState::Disconnected);
    m_disconnectAction->setEnabled(state != irc::ConnectionState::Disconnected);

    if (state == irc::ConnectionState::Disconnected) {
        closeTabsExceptServer();
        appendToServer(formatSystem(QStringLiteral("Disconnected")));
    } else if (state == irc::ConnectionState::Connected) {
        appendToServer(formatSystem(QStringLiteral("Connected")));
    }
}

void MainWindow::onServerMessage(const QString& html) {
    appendToServer(html);
}

void MainWindow::onChannelMessage(const QString& channel, const QString& html) {
    ChannelWidget* tab =
        findOrCreateTab(ChannelWidget::Context::Channel, channel, channel.toLower());
    tab->appendHtml(html);
    markActivity(tab, false);
}

void MainWindow::onQueryMessage(const QString& peer, const QString& html) {
    ChannelWidget* tab =
        findOrCreateTab(ChannelWidget::Context::Query, peer, peer.toLower());
    tab->appendHtml(html);
    markActivity(tab, false);
}

void MainWindow::onChannelHighlighted(const QString& channel) {
    if (ChannelWidget* tab = findTab(channel.toLower())) {
        markActivity(tab, true);
    }
    // Flash the taskbar entry to draw attention to the mention.
    QApplication::alert(this);
}

void MainWindow::onChannelJoined(const QString& channel) {
    ChannelWidget* tab =
        findOrCreateTab(ChannelWidget::Context::Channel, channel, channel.toLower());
    tab->appendHtml(formatSystem(QStringLiteral("Joined %1").arg(channel)));
}

void MainWindow::onChannelParted(const QString& channel) {
    const QString key = channel.toLower();
    if (ChannelWidget* tab = findTab(key)) {
        m_tabIndex.remove(key);
        m_tabs->removeTab(m_tabs->indexOf(tab));
        tab->deleteLater();
    }
    appendToServer(formatSystem(QStringLiteral("Left %1").arg(channel)));
}

void MainWindow::onNickList(const QString& channel, const QStringList& nicks) {
    if (ChannelWidget* tab = findTab(channel.toLower())) {
        tab->setNickList(nicks);
    }
}

void MainWindow::onOwnNick(const QString& nick) {
    setWindowTitle(QStringLiteral("clint - %1").arg(nick));
}

void MainWindow::onError(const QString& message) {
    appendToServer(formatSystem(QStringLiteral("Error: %1").arg(message)));
}

void MainWindow::onListStarted() {
    ChannelWidget* tab = findOrCreateTab(ChannelWidget::Context::List,
                                         QStringLiteral("channel list"),
                                         QStringLiteral("channel list"));
    tab->clear();
    tab->appendHtml(formatSystem(QStringLiteral("Fetching channel list...")));
}

void MainWindow::onListEntry(const QString& channel, int users, const QString& topic) {
    ChannelWidget* tab = findTab(QStringLiteral("channel list"));
    if (!tab) {
        return;
    }
    const QString line =
        QStringLiteral("<span style=\"color:#61afef\">%1</span>"
                       " <span style=\"color:#9aa0a6\">(%2)</span>"
                       " <span style=\"color:#d4d4d4\">%3</span>")
            .arg(htmlEscape(channel))
            .arg(users)
            .arg(htmlEscape(topic));
    tab->appendHtml(line);
}

void MainWindow::onListEnded(int total) {
    ChannelWidget* tab = findTab(QStringLiteral("channel list"));
    if (!tab) {
        return;
    }
    tab->appendHtml(formatSystem(QStringLiteral("%1 channel(s)").arg(total)));
}

// ---- Menu actions --------------------------------------------------------

void MainWindow::onConnectAction() {
    ConnectDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    irc::ConnectionConfig cfg = dialog.config();
    if (cfg.host.isEmpty() || cfg.nick.isEmpty()) {
        appendToServer(formatSystem(QStringLiteral("Host and nick are required.")));
        return;
    }
    closeTabsExceptServer();
    appendToServer(formatSystem(QStringLiteral("Connecting to %1:%2 ...")
                                    .arg(cfg.host)
                                    .arg(cfg.port)));
    m_controller->connectToServer(cfg);
}

void MainWindow::onDisconnectAction() {
    m_controller->quit();
}

void MainWindow::onAboutAction() {
    QMessageBox::about(this, QStringLiteral("About clint"),
        QStringLiteral("<b>clint</b> 0.1.0<br>"
                       "A minimal IRC client built with C++17 and Qt6."));
}

// ---- Tab management ------------------------------------------------------

ChannelWidget* MainWindow::findOrCreateTab(ChannelWidget::Context ctx,
                                           const QString& title, const QString& key) {
    if (m_tabIndex.contains(key)) {
        return m_tabIndex.value(key);
    }
    auto* tab = new ChannelWidget(ctx, title, this);
    tab->setHistory(&m_history);
    m_tabIndex.insert(key, tab);
    m_tabs->addTab(tab, title);
    connect(tab, &ChannelWidget::inputSubmitted, this, [this, tab](const QString& text) {
        onInput(tab, text);
    });
    connect(tab, &ChannelWidget::nickActivated, this, &MainWindow::openQueryForNick);
    m_tabs->setCurrentWidget(tab);
    return tab;
}

ChannelWidget* MainWindow::findTab(const QString& key) const {
    return m_tabIndex.value(key, nullptr);
}

void MainWindow::closeTabsExceptServer() {
    QList<QWidget*> toDelete;
    for (int i = m_tabs->count() - 1; i >= 0; --i) {
        QWidget* w = m_tabs->widget(i);
        if (w != m_serverTab) {
            m_tabs->removeTab(i);
            toDelete << w;
        }
    }
    m_tabIndex.clear();
    for (QWidget* w : toDelete) {
        w->deleteLater();
    }
}

void MainWindow::appendToServer(const QString& html) {
    m_serverTab->appendHtml(html);
}

void MainWindow::openQueryForNick(const QString& nick) {
    // Reuse an existing query tab if present, then bring it to the front.
    ChannelWidget* tab =
        findOrCreateTab(ChannelWidget::Context::Query, nick, nick.toLower());
    m_tabs->setCurrentWidget(tab);
}

void MainWindow::markActivity(ChannelWidget* tab, bool highlight) {
    const int idx = m_tabs->indexOf(tab);
    if (idx < 0 || m_tabs->currentIndex() == idx) {
        return; // already visible (or gone): no marker needed
    }
    const QString title = tab->title();
    const QString current = m_tabs->tabText(idx);

    // A mention (★) outranks plain activity (●); never downgrade it.
    const bool alreadyStar = current.startsWith(QStringLiteral("\u2605 "));
    if (alreadyStar && !highlight) {
        return;
    }
    if (highlight) {
        m_tabs->setTabText(idx, QStringLiteral("\u2605 ") + title);
    } else {
        m_tabs->setTabText(idx, QStringLiteral("\u25cf ") + title);
    }
}

// ---- Input handling ------------------------------------------------------

void MainWindow::onInput(ChannelWidget* tab, const QString& text) {
    if (text.startsWith(QLatin1Char('/'))) {
        handleSlashCommand(tab, text);
        return;
    }

    if (tab->context() == ChannelWidget::Context::Server) {
        appendToServer(formatSystem(
            QStringLiteral("Use /join #channel first; you cannot chat in the server tab.")));
        return;
    }

    m_controller->sendChat(tab->title(), text);
    tab->appendHtml(formatOwnMessage(m_controller->nick(), text));
}

void MainWindow::handleSlashCommand(ChannelWidget* tab, const QString& text) {
    const QString body = text.mid(1);
    const int sp = body.indexOf(QLatin1Char(' '));
    const QString cmd = (sp < 0 ? body : body.left(sp)).trimmed().toLower();
    const QString rest = (sp < 0 ? QString() : body.mid(sp + 1)).trimmed();

    if (cmd.isEmpty()) {
        return;
    }

    if (cmd == QLatin1String("help")) {
        showHelp();
    } else if (cmd == QLatin1String("me")) {
        if (tab->context() == ChannelWidget::Context::Server) {
            appendToServer(formatSystem(QStringLiteral("Not in a channel or query.")));
            return;
        }
        m_controller->sendAction(tab->title(), rest);
        tab->appendHtml(formatOwnAction(m_controller->nick(), rest));
    } else if (cmd == QLatin1String("msg") || cmd == QLatin1String("query") ||
               cmd == QLatin1String("privmsg")) {
        const int sp2 = rest.indexOf(QLatin1Char(' '));
        const QString target = (sp2 < 0 ? rest : rest.left(sp2)).trimmed();
        const QString msg = (sp2 < 0 ? QString() : rest.mid(sp2 + 1)).trimmed();
        if (target.isEmpty()) {
            appendToServer(formatSystem(QStringLiteral("Usage: /msg <nick> <message>")));
            return;
        }
        m_controller->sendChat(target, msg);
        ChannelWidget* qt =
            findOrCreateTab(ChannelWidget::Context::Query, target, target.toLower());
        qt->appendHtml(formatOwnMessage(m_controller->nick(), msg));
    } else if (cmd == QLatin1String("notice")) {
        const int sp2 = rest.indexOf(QLatin1Char(' '));
        const QString target = (sp2 < 0 ? rest : rest.left(sp2)).trimmed();
        const QString msg = (sp2 < 0 ? QString() : rest.mid(sp2 + 1)).trimmed();
        if (target.isEmpty() || msg.isEmpty()) {
            appendToServer(formatSystem(QStringLiteral("Usage: /notice <target> <message>")));
            return;
        }
        m_controller->sendNotice(target, msg);
    } else if (cmd == QLatin1String("join") || cmd == QLatin1String("j")) {
        const QString chan = rest.section(QLatin1Char(' '), 0, 0);
        if (chan.isEmpty()) {
            appendToServer(formatSystem(QStringLiteral("Usage: /join #channel")));
            return;
        }
        m_controller->joinChannel(chan);
    } else if (cmd == QLatin1String("part") || cmd == QLatin1String("leave")) {
        if (tab->context() != ChannelWidget::Context::Channel) {
            appendToServer(formatSystem(QStringLiteral("Not in a channel.")));
            return;
        }
        m_controller->partChannel(tab->title(), rest);
    } else if (cmd == QLatin1String("quit") || cmd == QLatin1String("exit")) {
        m_controller->quit(rest);
    } else if (cmd == QLatin1String("nick")) {
        if (rest.isEmpty()) {
            appendToServer(formatSystem(QStringLiteral("Usage: /nick <newnick>")));
            return;
        }
        m_controller->setNick(rest);
    } else if (cmd == QLatin1String("topic")) {
        if (tab->context() != ChannelWidget::Context::Channel) {
            appendToServer(formatSystem(QStringLiteral("Not in a channel.")));
            return;
        }
        m_controller->setTopic(tab->title(), rest);
    } else if (cmd == QLatin1String("kick")) {
        if (tab->context() != ChannelWidget::Context::Channel) {
            appendToServer(formatSystem(QStringLiteral("Not in a channel.")));
            return;
        }
        const int sp2 = rest.indexOf(QLatin1Char(' '));
        const QString victim = (sp2 < 0 ? rest : rest.left(sp2)).trimmed();
        const QString reason = (sp2 < 0 ? QString() : rest.mid(sp2 + 1)).trimmed();
        if (victim.isEmpty()) {
            appendToServer(formatSystem(QStringLiteral("Usage: /kick <nick> [reason]")));
            return;
        }
        QStringList args{tab->title(), victim};
        if (!reason.isEmpty()) {
            args << reason;
        }
        m_controller->sendCommand(QStringLiteral("KICK"), args);
    } else if (cmd == QLatin1String("invite")) {
        const QString victim = rest.section(QLatin1Char(' '), 0, 0);
        if (victim.isEmpty()) {
            appendToServer(formatSystem(QStringLiteral("Usage: /invite <nick>")));
            return;
        }
        m_controller->sendCommand(QStringLiteral("INVITE"), {victim, tab->title()});
    } else if (cmd == QLatin1String("mode")) {
        if (tab->context() == ChannelWidget::Context::Channel) {
            m_controller->sendCommand(QStringLiteral("MODE"), {tab->title(), rest});
        } else if (!rest.isEmpty()) {
            m_controller->sendCommand(QStringLiteral("MODE"), {rest});
        } else {
            appendToServer(formatSystem(QStringLiteral("Usage: /mode +flags [nick]")));
        }
    } else if (cmd == QLatin1String("away")) {
        m_controller->sendCommand(QStringLiteral("AWAY"),
                                  rest.isEmpty() ? QStringList() : QStringList{rest});
    } else if (cmd == QLatin1String("whois")) {
        m_controller->sendCommand(QStringLiteral("WHOIS"), {rest});
    } else if (cmd == QLatin1String("who")) {
        m_controller->sendCommand(QStringLiteral("WHO"), splitArgs(rest));
    } else if (cmd == QLatin1String("whowas")) {
        m_controller->sendCommand(QStringLiteral("WHOWAS"), splitArgs(rest));
    } else if (cmd == QLatin1String("names")) {
        m_controller->sendCommand(QStringLiteral("NAMES"), splitArgs(rest));
    } else if (cmd == QLatin1String("motd")) {
        m_controller->sendCommand(QStringLiteral("MOTD"), splitArgs(rest));
    } else if (cmd == QLatin1String("info")) {
        m_controller->sendCommand(QStringLiteral("INFO"), splitArgs(rest));
    } else if (cmd == QLatin1String("admin")) {
        m_controller->sendCommand(QStringLiteral("ADMIN"), splitArgs(rest));
    } else if (cmd == QLatin1String("stats")) {
        m_controller->sendCommand(QStringLiteral("STATS"), splitArgs(rest));
    } else if (cmd == QLatin1String("links")) {
        m_controller->sendCommand(QStringLiteral("LINKS"), splitArgs(rest));
    } else if (cmd == QLatin1String("lusers")) {
        m_controller->sendCommand(QStringLiteral("LUSERS"), splitArgs(rest));
    } else if (cmd == QLatin1String("userhost")) {
        m_controller->sendCommand(QStringLiteral("USERHOST"), splitArgs(rest));
    } else if (cmd == QLatin1String("ison")) {
        m_controller->sendCommand(QStringLiteral("ISON"), splitArgs(rest));
    } else if (cmd == QLatin1String("knock")) {
        m_controller->sendCommand(QStringLiteral("KNOCK"), splitArgs(rest));
    } else if (cmd == QLatin1String("ping")) {
        const int sp2 = rest.indexOf(QLatin1Char(' '));
        const QString target = (sp2 < 0 ? rest : rest.left(sp2)).trimmed();
        const QString arg = (sp2 < 0 ? QString() : rest.mid(sp2 + 1)).trimmed();
        if (target.isEmpty()) {
            appendToServer(formatSystem(QStringLiteral("Usage: /ping <target>")));
            return;
        }
        m_controller->sendCtcp(target, QStringLiteral("PING"), arg);
    } else if (cmd == QLatin1String("version")) {
        const QString target = rest.section(QLatin1Char(' '), 0, 0);
        if (target.isEmpty()) {
            appendToServer(formatSystem(QStringLiteral("Usage: /version <target>")));
            return;
        }
        m_controller->sendCtcp(target, QStringLiteral("VERSION"));
    } else if (cmd == QLatin1String("time")) {
        const QString target = rest.section(QLatin1Char(' '), 0, 0);
        if (target.isEmpty()) {
            appendToServer(formatSystem(QStringLiteral("Usage: /time <target>")));
            return;
        }
        m_controller->sendCtcp(target, QStringLiteral("TIME"));
    } else if (cmd == QLatin1String("cap")) {
        m_controller->sendCommand(QStringLiteral("CAP"), splitArgs(rest));
    } else if (cmd == QLatin1String("list")) {
        if (!m_controller->isConnected()) {
            appendToServer(formatSystem(QStringLiteral("Not connected.")));
            return;
        }
        m_controller->listChannels(rest);
    } else if (cmd == QLatin1String("raw") || cmd == QLatin1String("quote")) {
        m_controller->sendRaw(rest);
    } else if (cmd == QLatin1String("ctcp")) {
        const int sp2 = rest.indexOf(QLatin1Char(' '));
        const QString target = (sp2 < 0 ? rest : rest.left(sp2)).trimmed();
        const QString payload = (sp2 < 0 ? QString() : rest.mid(sp2 + 1)).trimmed();
        if (target.isEmpty() || payload.isEmpty()) {
            appendToServer(formatSystem(QStringLiteral("Usage: /ctcp <target> <payload>")));
            return;
        }
        m_controller->sendCommand(QStringLiteral("PRIVMSG"),
                                  {target, QStringLiteral("\x01") + payload + QStringLiteral("\x01")});
    } else {
        appendToServer(formatSystem(
            QStringLiteral("Unknown command: /%1 (try /help)").arg(cmd)));
    }
}

void MainWindow::showHelp() {
    const QString help = QStringLiteral(
        "<b>Commands:</b><br>"
        "/join #channel &nbsp;&nbsp; Join a channel<br>"
        "/list [filter] &nbsp;&nbsp; List channels<br>"
        "/part [reason] &nbsp;&nbsp; Leave the current channel<br>"
        "/msg &lt;nick&gt; &lt;msg&gt; &nbsp;&nbsp; Private message<br>"
        "/me &lt;action&gt; &nbsp;&nbsp; Send an action<br>"
        "/notice &lt;target&gt; &lt;msg&gt; &nbsp;&nbsp; Send a notice<br>"
        "/nick &lt;newnick&gt; &nbsp;&nbsp; Change nick<br>"
        "/topic [new topic] &nbsp;&nbsp; Show/set topic<br>"
        "/kick &lt;nick&gt; [reason] &nbsp;&nbsp; Kick a user<br>"
        "/invite &lt;nick&gt; &nbsp;&nbsp; Invite a user<br>"
        "/mode +flags [nick] &nbsp;&nbsp; Set modes<br>"
        "/who [mask] &nbsp;&nbsp; List users<br>"
        "/whois &lt;nick&gt; &nbsp;&nbsp; User info<br>"
        "/whowas &lt;nick&gt; &nbsp;&nbsp; Nick history<br>"
        "/names [channel] &nbsp;&nbsp; List channel members<br>"
        "/motd &nbsp;&nbsp; Message of the day<br>"
        "/userhost &lt;nicks&gt; &nbsp;&nbsp; User hosts<br>"
        "/ison &lt;nicks&gt; &nbsp;&nbsp; Check online status<br>"
        "/knock &lt;channel&gt; &nbsp;&nbsp; Knock on a channel<br>"
        "/away [message] &nbsp;&nbsp; Set away status<br>"
        "/ping &lt;target&gt; &nbsp;&nbsp; CTCP ping<br>"
        "/version &lt;target&gt; &nbsp;&nbsp; CTCP version<br>"
        "/time &lt;target&gt; &nbsp;&nbsp; CTCP time<br>"
        "/info /admin /stats /links /lusers /cap &nbsp;&nbsp; Server queries<br>"
        "/ctcp &lt;target&gt; &lt;payload&gt; &nbsp;&nbsp; Raw CTCP<br>"
        "/raw &lt;line&gt; &nbsp;&nbsp; Send a raw line<br>"
        "/quit [message] &nbsp;&nbsp; Disconnect");
    appendToServer(QStringLiteral("<span style=\"color:#9aa0a6\">%1</span>").arg(help));
}

} // namespace ui
