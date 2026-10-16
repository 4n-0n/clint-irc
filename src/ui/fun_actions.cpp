#include "fun_actions.hpp"

#include <QFile>
#include <QTextStream>

namespace ui {

QList<FunAction> loadFunActions(const QString& path) {
    QList<FunAction> out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return out; // missing/unreadable -> caller uses defaults
    }

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const int bar = line.indexOf(QLatin1Char('|'));
        if (bar < 0) {
            // No separator: label == text.
            out.append(FunAction{line, line});
            continue;
        }
        const QString label = line.left(bar).trimmed();
        const QString text = line.mid(bar + 1).trimmed();
        if (!label.isEmpty() && !text.isEmpty()) {
            out.append(FunAction{label, text});
        }
    }
    return out;
}

} // namespace ui
