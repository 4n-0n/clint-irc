#include "history_line_edit.hpp"

#include "completion.hpp"

#include <QEvent>
#include <QKeyEvent>

namespace ui {

HistoryLineEdit::HistoryLineEdit(QWidget* parent) : QLineEdit(parent) {}

void HistoryLineEdit::setHistory(InputHistory* history) {
    m_history = history;
    m_index = history ? history->count() : 0;
    m_draft.clear();
}

void HistoryLineEdit::setCompletionList(const QStringList& candidates) {
    m_completionList = candidates;
    m_completing = false;
}

void HistoryLineEdit::commitHistory(const QString& line) {
    if (m_history) {
        m_history->add(line);
        m_index = m_history->count();
    }
    m_draft.clear();
}

void HistoryLineEdit::insertTextAtCursor(const QString& snippet) {
    const int cursor = cursorPosition();
    const QString current = text();
    setText(current.left(cursor) + snippet + current.mid(cursor));
    setCursorPosition(cursor + snippet.size());
    setFocus();
}

bool HistoryLineEdit::event(QEvent* e) {
    // Intercept Tab BEFORE QWidget's focus traversal consumes it: QWidget::event
    // moves focus on Tab and never delivers it to keyPressEvent, so completion
    // must be handled here. Only intercept when there are candidates; otherwise
    // let Tab move focus as usual (server/query/list tabs).
    if (e->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(e);
        if (keyEvent->key() == Qt::Key_Tab && !keyEvent->modifiers() &&
            !m_completionList.isEmpty()) {
            handleTabCompletion();
            return true; // consumed: focus stays in this widget
        }
    }
    return QLineEdit::event(e);
}

void HistoryLineEdit::keyPressEvent(QKeyEvent* event) {
    // Any key reaching here is not a consumed Tab, so break any completion
    // cycle in progress.
    m_completing = false;

    // History navigation (Up/Down).
    if (m_history && !event->modifiers()) {
        if (event->key() == Qt::Key_Up) {
            const int n = m_history->count();
            if (n > 0) {
                if (m_index == n) {
                    m_draft = text(); // save the in-progress draft once
                }
                if (m_index > 0) {
                    --m_index;
                    setText(m_history->items().at(m_index));
                    setCursorPosition(text().size());
                }
            }
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Down) {
            const int n = m_history->count();
            if (m_index < n) {
                if (m_index + 1 < n) {
                    ++m_index;
                    setText(m_history->items().at(m_index));
                } else {
                    m_index = n; // past the end: restore the draft
                    setText(m_draft);
                }
                setCursorPosition(text().size());
            }
            event->accept();
            return;
        }
    }

    QLineEdit::keyPressEvent(event);
}

void HistoryLineEdit::handleTabCompletion() {
    const QString full = text();
    const int cursor = cursorPosition();

    // Token start immediately before the cursor.
    int wordStart = cursor;
    while (wordStart > 0 && !full.at(wordStart - 1).isSpace()) {
        --wordStart;
    }
    const QString prefix = full.mid(wordStart, cursor - wordStart);

    if (!m_completing) {
        m_completing = true;
        m_completeBase = prefix;
        m_completeIndex = 0;
    }

    // An empty prefix (Tab on an empty/whitespace-bounded word) cycles through
    // the entire nick list, so Tab always completes even when no letters have
    // been typed yet. A non-empty prefix filters to matching nicks.
    const QStringList matches = m_completeBase.isEmpty()
                                    ? m_completionList
                                    : completionMatches(m_completionList, m_completeBase);
    if (matches.isEmpty()) {
        return;
    }

    const QString chosen = matches.at(m_completeIndex % matches.size());
    ++m_completeIndex;

    setText(full.left(wordStart) + chosen + full.mid(cursor));
    setCursorPosition(wordStart + chosen.size());
}

} // namespace ui
