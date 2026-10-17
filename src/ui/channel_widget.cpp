#include "channel_widget.hpp"

#include "history_line_edit.hpp"
#include "input_history.hpp"

#include "../irc/prefix.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QDesktopServices>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace ui {

ChannelWidget::ChannelWidget(Context context, const QString& title, QWidget* parent)
    : QWidget(parent), m_context(context), m_title(title) {
    setupUi();
}

void ChannelWidget::setHistory(InputHistory* history) {
    m_input->setHistory(history);
}

void ChannelWidget::setupUi() {
    m_chatView = new QTextEdit(this);
    m_chatView->setReadOnly(true);
    m_chatView->setAcceptRichText(true);
    m_chatView->setUndoRedoEnabled(false);
    m_chatView->setStyleSheet(
        QStringLiteral("QTextEdit { background:#1e1e1e; color:#d4d4d4; border:none; }"));

    m_nickFilter = new QLineEdit(this);
    m_nickFilter->setPlaceholderText(QStringLiteral("Filter nicks"));
    m_nickFilter->setClearButtonEnabled(true);
    m_nickFilter->setStyleSheet(
        QStringLiteral("QLineEdit { background:#252526; color:#d4d4d4;"
                       " border:1px solid #3c3c3c; padding:3px; }"));

    m_nickList = new QListWidget(this);
    m_nickList->setStyleSheet(
        QStringLiteral("QListWidget { background:#252526; color:#d4d4d4; border:none; }"));

    // Right-hand column: filter box stacked above the nick list. Hidden for
    // non-channel contexts (server/query/list tabs have no nick list).
    auto* nickColumn = new QWidget(this);
    auto* nickLayout = new QVBoxLayout(nickColumn);
    nickLayout->setContentsMargins(0, 0, 0, 0);
    nickLayout->setSpacing(0);
    nickLayout->addWidget(m_nickFilter, 0);
    nickLayout->addWidget(m_nickList, 1);
    nickColumn->setMaximumWidth(180);
    nickColumn->setVisible(m_context == Context::Channel);

    m_input = new HistoryLineEdit(this);
    m_input->setPlaceholderText(QStringLiteral("Type a message, or /command (try /help)"));
    m_input->setStyleSheet(
        QStringLiteral("QLineEdit { background:#252526; color:#d4d4d4;"
                       " border:1px solid #3c3c3c; padding:4px; }"));
    // A list view is read-only: no input box.
    m_input->setVisible(m_context != Context::List);

    auto* chatAndNicks = new QHBoxLayout;
    chatAndNicks->addWidget(m_chatView, 1);
    chatAndNicks->addWidget(nickColumn, 0);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addLayout(chatAndNicks, 1);
    root->addWidget(m_input, 0);

    connect(m_input, &QLineEdit::returnPressed, this, [this] {
        const QString text = m_input->text();
        m_input->clear();
        if (!text.trimmed().isEmpty()) {
            m_input->commitHistory(text);
            emit inputSubmitted(text);
        }
    });

    // Clicking a nick in the chat view inserts it into the input at the cursor;
    // double-clicking it opens a query. Mouse events land on the viewport (not
    // the scroll area), so the filter is installed there; anchorAt() resolves
    // which link (if any) was clicked.
    m_chatView->viewport()->installEventFilter(this);

    // Deferred single-click insert: armed on release, cancelled by a dblclick.
    // The interval matches Qt's own double-click detection window so a click
    // that Qt will promote to a double click never inserts the nick first.
    m_nickClickTimer = new QTimer(this);
    m_nickClickTimer->setSingleShot(true);
    m_nickClickTimer->setInterval(QApplication::doubleClickInterval());
    connect(m_nickClickTimer, &QTimer::timeout, this, [this] {
        const QString nick = m_pendingNick;
        m_pendingNick.clear();
        if (!nick.isEmpty()) {
            m_input->insertTextAtCursor(nick);
        }
    });

    connect(m_nickFilter, &QLineEdit::textChanged, this, &ChannelWidget::applyNickFilter);

    // Double-clicking a nick emits it (clean form) to the caller.
    connect(m_nickList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) {
                const QString nick = item->data(Qt::UserRole).toString();
                if (!nick.isEmpty()) {
                    emit nickActivated(nick);
                }
            });

    // Right-clicking a nick opens a context menu of actions.
    m_nickList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_nickList, &QListWidget::customContextMenuRequested, this,
            &ChannelWidget::showNickContextMenu);

    setFocusProxy(m_input);
}

void ChannelWidget::appendHtml(const QString& html) {
    // Auto-scroll only when the user was already near the bottom; if they have
    // scrolled up to read history, leave the viewport where it is so new
    // messages don't yank them back down.
    QScrollBar* bar = m_chatView->verticalScrollBar();
    const bool atBottom = (bar->value() >= bar->maximum() - 4);
    m_chatView->append(html);
    if (atBottom) {
        bar->setValue(bar->maximum());
    }
}

void ChannelWidget::clear() {
    m_chatView->clear();
}

void ChannelWidget::setNickList(const QStringList& nicks) {
    m_nicks = nicks;
    m_cleanNicks.clear();
    m_cleanNicks.reserve(nicks.size());
    for (const QString& n : nicks) {
        // Strip mode-prefix chars for tab completion (RFC 2812 standard set).
        m_cleanNicks << irc::splitNickPrefix(n, QStringLiteral("~&@%+!")).nick;
    }
    m_input->setCompletionList(m_cleanNicks);
    applyNickFilter();
}

void ChannelWidget::applyNickFilter() {
    const QString needle = m_nickFilter->text().trimmed();

    // Preserve the current selection across the full rebuild: an incoming
    // message (which triggers setNickList -> applyNickFilter) must not clear
    // the nick the user has selected. The item's UserRole holds the clean nick.
    const QString selected = m_nickList->currentItem()
                                 ? m_nickList->currentItem()->data(Qt::UserRole).toString()
                                 : QString();

    // Preserve the scroll position too. clear() + re-add (and setCurrentItem,
    // which scrolls to its item) both reset the viewport to the top, which made
    // the list jump on every join/part/message. Restoring the value afterwards
    // keeps the user's place; the rebuild may only shorten the range, so the
    // value is re-clamped.
    QScrollBar* bar = m_nickList->verticalScrollBar();
    const int scroll = bar->value();

    m_nickList->clear();
    QListWidgetItem* selectedItem = nullptr;
    for (int i = 0; i < m_nicks.size(); ++i) {
        const QString& nick = m_nicks.at(i);
        if (needle.isEmpty() || nick.contains(needle, Qt::CaseInsensitive)) {
            auto* item = new QListWidgetItem(nick); // display form (prefix + nick)
            item->setData(Qt::UserRole, m_cleanNicks.at(i));
            m_nickList->addItem(item);
            if (!selected.isEmpty() && m_cleanNicks.at(i) == selected) {
                selectedItem = item;
            }
        }
    }
    if (selectedItem) {
        m_nickList->setCurrentItem(selectedItem); // may scroll; undone below
    }
    bar->setValue(qMin(scroll, bar->maximum()));
}

QString ChannelWidget::nickAt(const QPoint& viewportPos) const {
    // Nick anchors carry the custom "nick:<percent-encoded>" scheme.
    const QString href = m_chatView->anchorAt(viewportPos);
    if (!href.startsWith(QLatin1String("nick:"))) {
        return {};
    }
    return QUrl::fromPercentEncoding(href.mid(5).toUtf8());
}

bool ChannelWidget::onChatMouseRelease(const QPoint& viewportPos) {
    // The release that closes a double-click pair must not re-arm the pending
    // insert (Qt sends press/release/dblclick/release). The timestamp is checked
    // instead of a flag so it can never stay armed if the trailing release is
    // swallowed elsewhere (e.g. the tab switched away on the dblclick).
    if (m_nickDblClick.isValid() &&
        m_nickDblClick.elapsed() < QApplication::doubleClickInterval()) {
        return true;
    }

    const QString nick = nickAt(viewportPos);
    if (!nick.isEmpty()) {
        m_pendingNick = nick;
        m_nickClickTimer->start();
        return true;
    }

    const QString href = m_chatView->anchorAt(viewportPos);
    if (href.startsWith(QLatin1String("http://")) || href.startsWith(QLatin1String("https://"))) {
        QDesktopServices::openUrl(QUrl(href));
        return true;
    }
    return false; // plain text: not ours
}

bool ChannelWidget::onChatMouseDoubleClick(const QPoint& viewportPos) {
    const QString nick = nickAt(viewportPos);
    if (nick.isEmpty()) {
        return false; // plain text: let QTextEdit select the word as usual
    }
    // A double click supersedes the single-click insert that is still pending.
    m_nickClickTimer->stop();
    m_pendingNick.clear();
    m_nickDblClick.start();
    emit nickActivated(nick);
    return true;
}

bool ChannelWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_chatView->viewport()) {
        if (event->type() == QEvent::ContextMenu) {
            const auto* ctx = static_cast<QContextMenuEvent*>(event);
            showChatContextMenu(ctx->globalPos());
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton && onChatMouseRelease(mouse->pos())) {
                return true; // consumed: nick/link click, or a double-click tail
            }
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton && onChatMouseDoubleClick(mouse->pos())) {
                return true; // consumed: the word must not be selected as well
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ChannelWidget::showNickContextMenu(const QPoint& viewportPos) {
    QListWidgetItem* item = m_nickList->itemAt(viewportPos);
    if (!item) {
        return;
    }
    const QString nick = item->data(Qt::UserRole).toString();
    if (nick.isEmpty()) {
        return;
    }

    QMenu menu(this);
    // Each action forwards a semantic token + the clean nick to the caller,
    // which owns all protocol/state decisions (this widget stays dumb).
    auto add = [&](const QString& label, const QString& action) {
        QAction* a = menu.addAction(label);
        connect(a, &QAction::triggered, this, [this, action, nick] {
            emit nickContextAction(action, nick);
        });
    };

    add(QStringLiteral("Query"), QStringLiteral("query"));
    menu.addSeparator();
    add(QStringLiteral("Whois"), QStringLiteral("whois"));
    add(QStringLiteral("CTCP Version"), QStringLiteral("ctcp-version"));
    add(QStringLiteral("CTCP Ping"), QStringLiteral("ctcp-ping"));
    add(QStringLiteral("CTCP Time"), QStringLiteral("ctcp-time"));
    menu.addSeparator();
    add(QStringLiteral("Kick"), QStringLiteral("kick"));
    add(QStringLiteral("Op (+o)"), QStringLiteral("op"));
    add(QStringLiteral("Deop (-o)"), QStringLiteral("deop"));
    add(QStringLiteral("Voice (+v)"), QStringLiteral("voice"));
    add(QStringLiteral("Devoice (-v)"), QStringLiteral("devoice"));

    menu.exec(m_nickList->viewport()->mapToGlobal(viewportPos));
}

void ChannelWidget::showChatContextMenu(const QPoint& globalPos) {
    // Resolve the anchor under the cursor (viewport coords).
    const QPoint vp = m_chatView->viewport()->mapFromGlobal(globalPos);
    const QString href = m_chatView->anchorAt(vp);
    const bool isLink =
        href.startsWith(QLatin1String("http://")) || href.startsWith(QLatin1String("https://"));

    QMenu menu(this);

    QAction* copy = menu.addAction(QStringLiteral("Copy"));
    copy->setEnabled(m_chatView->textCursor().hasSelection());
    connect(copy, &QAction::triggered, m_chatView, &QTextEdit::copy);

    if (isLink) {
        QAction* open = menu.addAction(QStringLiteral("Open Link"));
        connect(open, &QAction::triggered, this, [href] {
            QDesktopServices::openUrl(QUrl(href));
        });
        QAction* copyLink = menu.addAction(QStringLiteral("Copy Link"));
        connect(copyLink, &QAction::triggered, this, [href] {
            QApplication::clipboard()->setText(href);
        });
    }

    menu.addSeparator();
    QAction* selectAll = menu.addAction(QStringLiteral("Select All"));
    connect(selectAll, &QAction::triggered, m_chatView, &QTextEdit::selectAll);
    QAction* clearAction = menu.addAction(QStringLiteral("Clear"));
    connect(clearAction, &QAction::triggered, m_chatView, &QTextEdit::clear);

    menu.exec(globalPos);
}

} // namespace ui
