#pragma once

#include "../irc/connection.hpp"

#include "channel_widget.hpp"
#include "fun_actions.hpp"
#include "input_history.hpp"

#include <QHash>
#include <QMainWindow>

class QAction;
class QMenu;
class QTabWidget;

namespace app { class ClientController; }

namespace ui {

// Main window: tab management, slash-command parsing, and menu/status glue.
// All IRC protocol/state logic lives in app::ClientController; this class only
// renders its signals and translates user input into controller calls.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(app::ClientController* controller, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupUi();
    void setupMenus();
    void wireController();

    // Controller -> UI event handlers.
    void onStateChanged(irc::ConnectionState state);
    void onServerMessage(const QString& html);
    void onChannelMessage(const QString& channel, const QString& html);
    void onQueryMessage(const QString& peer, const QString& html);
    void onChannelHighlighted(const QString& channel);
    void onChannelJoined(const QString& channel);
    void onChannelParted(const QString& channel);
    void onNickList(const QString& channel, const QStringList& nicks);
    void onOwnNick(const QString& nick);
    void onError(const QString& message);
    void onListStarted();
    void onListEntry(const QString& channel, int users, const QString& topic);
    void onListEnded(int total);

    // Menu actions.
    void onConnectAction();
    void onDisconnectAction();
    void onAboutAction();

    // Builds the fun /me actions menu from the loaded config (or defaults).
    QMenu* buildFunMenu(QWidget* parent);
    // Sends a fun /me action to the current channel/query tab.
    void onFunAction(const QString& text);

    // Nick context-menu dispatch (from ChannelWidget::nickContextAction).
    void onNickContextAction(const QString& action, const QString& nick);

    // Tab management.
    ChannelWidget* findOrCreateTab(ChannelWidget::Context ctx, const QString& title,
                                   const QString& key);
    ChannelWidget* findTab(const QString& key) const;
    void closeTabsExceptServer();
    void appendToServer(const QString& html);
    void openQueryForNick(const QString& nick);
    // Marks a background tab with an activity indicator (● for new activity,
    // ★ for a mention highlight). Cleared when the tab becomes current.
    void markActivity(ChannelWidget* tab, bool highlight);

    // Input handling.
    void onInput(ChannelWidget* tab, const QString& text);
    void handleSlashCommand(ChannelWidget* tab, const QString& text);
    void showHelp();

    QString stateText(irc::ConnectionState state) const;

    app::ClientController* m_controller;
    QTabWidget* m_tabs;
    ChannelWidget* m_serverTab = nullptr;
    QHash<QString, ChannelWidget*> m_tabIndex; // lowercase key -> tab
    QAction* m_connectAction = nullptr;
    QAction* m_disconnectAction = nullptr;
    QList<FunAction> m_funActions; // fun /me actions (config or defaults)
    InputHistory m_history; // global command history, shared across all tabs
};

} // namespace ui
