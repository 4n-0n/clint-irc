#pragma once

#include <QString>
#include <QStringList>

namespace ui {

// Returns the candidates that case-insensitively start with `prefix`.
// An empty prefix yields no matches (Tab on an empty word does nothing).
// Pure function.
inline QStringList completionMatches(const QStringList& candidates, const QString& prefix) {
    QStringList matches;
    if (prefix.isEmpty()) {
        return matches;
    }
    matches.reserve(candidates.size());
    for (const QString& candidate : candidates) {
        if (candidate.startsWith(prefix, Qt::CaseInsensitive)) {
            matches << candidate;
        }
    }
    return matches;
}

} // namespace ui
