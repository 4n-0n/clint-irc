#pragma once

#include <QLineEdit>
#include <QStringList>

#include "input_history.hpp"

class QEvent;
class QKeyEvent;

namespace ui {

// QLineEdit with Up/Down history navigation (over a shared InputHistory) and
// Tab nick completion (over a per-input candidate list). The history object
// may be shared across many inputs; the completion list and both navigation
// cursors are per-input.
//
// Tab is intercepted in event() (not keyPressEvent) because QWidget::event()
// performs focus traversal on Tab BEFORE delivering the key, so a keyPressEvent
// override would never see it.
class HistoryLineEdit : public QLineEdit {
public:
    explicit HistoryLineEdit(QWidget* parent = nullptr);

    // Attaches the (shared) history and resets the cursor to the draft end.
    void setHistory(InputHistory* history);

    // Sets the candidate list used by Tab completion (typically the clean nick
    // list of the current channel). Empty list disables completion.
    void setCompletionList(const QStringList& candidates);

    // Records `line` into the history and resets the navigation cursor.
    void commitHistory(const QString& line);

    // Inserts `snippet` at the current cursor position (used by click-to-mention:
    // clicking a nick in the chat view pastes it where the caret sits).
    void insertTextAtCursor(const QString& snippet);

protected:
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void handleTabCompletion();

    InputHistory* m_history = nullptr;
    int m_index = 0;  // cursor into history; == count() means "at draft"
    QString m_draft;  // saved in-progress text while navigating

    QStringList m_completionList;
    bool m_completing = false; // true while cycling through completions
    QString m_completeBase;    // original prefix being completed
    int m_completeIndex = 0;   // which candidate to insert next
};

} // namespace ui
