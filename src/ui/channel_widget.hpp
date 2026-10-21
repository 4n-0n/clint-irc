#pragma once

#include <QElapsedTimer>
#include <QWidget>

class QLabel;
class QListWidget;
class QLineEdit;
class QTextEdit;
class QTimer;

namespace ui {

class HistoryLineEdit;
class InputHistory;

// A single tab: server status, a channel, a private query, or a read-only
// channel-list view. Dumb render widget: it formats nothing itself; the caller
// supplies HTML and the nick list. It only emits raw input for the caller to
// interpret.
class ChannelWidget : public QWidget {
    Q_OBJECT
public:
    enum class Context { Server, Channel, Query, List };

    explicit ChannelWidget(Context context, const QString& title,
                           QWidget* parent = nullptr);

    [[nodiscard]] Context context() const { return m_context; }
    [[nodiscard]] QString title() const { return m_title; }

    // Injects the (shared) command history into the input line.
    void setHistory(InputHistory* history);

    void appendHtml(const QString& html);
    void clear();
    void setNickList(const QStringList& nicks);
    // Channel topic shown above the chat. An empty topic hides the bar. Only
    // channel tabs have one; other contexts ignore the call.
    void setTopic(const QString& topic);

signals:
    void inputSubmitted(const QString& text);
    // Emitted when the user activates a clean nick: double-click on the nick
    // list, or double-click on a nick anchor in the chat view. The caller
    // decides the action (the main window opens a query tab).
    void nickActivated(const QString& nick);
    // Emitted when the user picks an action from a nick's context menu.
    // `action` is a semantic token ("query", "whois", "kick", "op", ...);
    // `nick` is the clean nick. The caller resolves it against the tab.
    void nickContextAction(const QString& action, const QString& nick);
    // Emitted when the user clicks a channel name in the channel-list tab. The
    // caller joins it.
    void channelActivated(const QString& channel);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUi();
    void applyNickFilter();
    void showNickContextMenu(const QPoint& viewportPos);
    void showChatContextMenu(const QPoint& globalPos);
    // Clean nick whose "nick:" anchor covers `viewportPos`, or an empty string
    // when that point is not on a nick.
    [[nodiscard]] QString nickAt(const QPoint& viewportPos) const;
    // Left-button handling on the chat viewport. Click semantics:
    //   single click on a nick -> insert it into the input at the caret
    //   double click on a nick -> emit nickActivated() (caller opens a query)
    // Qt delivers press/release/dblclick/release for a double click, so a single
    // click must be deferred by QApplication::doubleClickInterval(); a dblclick
    // cancels the pending insert, and the release closing that pair is ignored
    // via m_nickDblClick (a timestamp, so it self-expires instead of sticking).
    // Returns true when the release was consumed (nick click, link click, or the
    // trailing release of a double click); false lets QTextEdit handle it
    // normally so plain-text selection keeps working.
    bool onChatMouseRelease(const QPoint& viewportPos);
    // Returns true when the double click landed on a nick (consumed, query
    // emitted); false lets QTextEdit select the word as usual.
    bool onChatMouseDoubleClick(const QPoint& viewportPos);

    Context m_context;
    QString m_title;
    QTextEdit* m_chatView = nullptr;
    QLabel* m_topicLabel = nullptr;
    QLineEdit* m_nickFilter = nullptr;
    QListWidget* m_nickList = nullptr;
    HistoryLineEdit* m_input = nullptr;
    QStringList m_nicks;       // unfiltered display-form nick list (prefix + nick)
    QStringList m_cleanNicks;  // parallel list: mode prefix stripped

    QTimer* m_nickClickTimer = nullptr; // fires the deferred single-click insert
    QString m_pendingNick;              // nick awaiting that deferred insert
    QElapsedTimer m_nickDblClick;       // start of the last nick double-click
};

} // namespace ui
