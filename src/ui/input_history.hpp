#pragma once

#include <QString>
#include <QStringList>

namespace ui {

// Global command-line history, shared across all tabs. Stateful: it owns the
// list of past entries and enforces a size cap. Navigation cursor state lives
// in HistoryLineEdit (per-input), NOT here, so sharing one InputHistory across
// many inputs is safe.
class InputHistory {
public:
    static constexpr int kMaxEntries = 500;

    // Appends `line` unless it exactly duplicates the previous entry.
    void add(const QString& line) {
        if (!line.isEmpty() && (m_items.isEmpty() || m_items.last() != line)) {
            m_items.append(line);
            if (m_items.size() > kMaxEntries) {
                m_items.removeFirst();
            }
        }
    }

    [[nodiscard]] const QStringList& items() const { return m_items; }
    [[nodiscard]] int count() const { return m_items.size(); }

private:
    QStringList m_items;
};

} // namespace ui
