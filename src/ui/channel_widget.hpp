#pragma once

#include <QWidget>

class QListWidget;
class QLineEdit;
class QTextEdit;

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

signals:
    void inputSubmitted(const QString& text);
    // Emitted when the user double-clicks a nick in the list. `nick` is the
    // clean form (mode prefix stripped); the caller decides the action (the
    // main window opens a query tab).
    void nickActivated(const QString& nick);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUi();
    void applyNickFilter();

    Context m_context;
    QString m_title;
    QTextEdit* m_chatView = nullptr;
    QLineEdit* m_nickFilter = nullptr;
    QListWidget* m_nickList = nullptr;
    HistoryLineEdit* m_input = nullptr;
    QStringList m_nicks;       // unfiltered display-form nick list (prefix + nick)
    QStringList m_cleanNicks;  // parallel list: mode prefix stripped
};

} // namespace ui
