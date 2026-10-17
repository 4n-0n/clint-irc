#include <QApplication>
#include <QEventLoop>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTextEdit>
#include <QTimer>

#include <cstdio>

#include "ui/channel_widget.hpp"

// Click semantics on the chat view (runs offscreen, no network):
//   single click on a nick anchor -> nick is inserted into the input line
//   double click on a nick anchor -> nickActivated() is emitted (caller opens a
//                                    query) and nothing is inserted
//   double click on plain text    -> not consumed (QTextEdit still selects)
//
// The click point is discovered by hit-testing the viewport: ChannelWidget
// exposes no accessor for its chat view, and the anchor must be located the same
// way the user's mouse would be.

namespace {

void waitMs(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void sendMouse(QWidget* target, QEvent::Type type, const QPoint& pos) {
    const QPointF global = target->mapToGlobal(pos);
    QMouseEvent ev(type, QPointF(pos), global, Qt::LeftButton, Qt::LeftButton,
                   Qt::NoModifier);
    QApplication::sendEvent(target, &ev);
}

// Returns the first viewport point whose anchor is exactly `href`.
QPoint findAnchorPoint(QTextEdit* view, const QString& href) {
    QWidget* vp = view->viewport();
    for (int y = 1; y < vp->height(); y += 2) {
        for (int x = 1; x < vp->width(); x += 2) {
            if (view->anchorAt(QPoint(x, y)) == href) {
                return QPoint(x, y);
            }
        }
    }
    return QPoint(-1, -1);
}

// Returns the first point on row `y` right of `fromX` that carries no anchor, so
// the "plain text" case is guaranteed to be measured on real prose.
QPoint findPlainPoint(QTextEdit* view, int y, int fromX) {
    QWidget* vp = view->viewport();
    for (int x = fromX + 1; x < vp->width(); x += 2) {
        if (view->anchorAt(QPoint(x, y)).isEmpty()) {
            return QPoint(x, y);
        }
    }
    return QPoint(-1, -1);
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    ui::ChannelWidget widget(ui::ChannelWidget::Context::Channel,
                            QStringLiteral("#test"));
    widget.resize(900, 600);
    widget.show();
    waitMs(50); // let the layout settle

    widget.appendHtml(QStringLiteral(
        "<span style=\"color:#888;\">alice</span> "
        "<a href=\"nick:alice\" style=\"text-decoration:none;\">alice</a>: hello "
        "<a href=\"nick:bob\" style=\"text-decoration:none;\">bob</a><br>"));
    waitMs(50);

    QTextEdit* chat = widget.findChild<QTextEdit*>();
    // HistoryLineEdit declares no Q_OBJECT (it adds no signals/slots), so it
    // cannot be findChild'd by type; identify it by its unique placeholder.
    QLineEdit* input = nullptr;
    for (QLineEdit* line : widget.findChildren<QLineEdit*>()) {
        if (line->placeholderText().startsWith(QStringLiteral("Type a message"))) {
            input = line;
        }
    }
    bool ok = (chat != nullptr) && (input != nullptr);

    QPoint alicePos(-1, -1);
    QPoint plainPos(-1, -1);
    if (ok) {
        alicePos = findAnchorPoint(chat, QStringLiteral("nick:alice"));
        ok = ok && (alicePos.x() >= 0);
        // A point inside the plain-text run (e.g. "hello"): no anchor there.
        plainPos = ok ? findPlainPoint(chat, alicePos.y(), alicePos.x())
                      : QPoint(-1, -1);
        if (ok && plainPos.x() < 0) {
            std::printf("could not locate a plain-text point\n");
            ok = false;
        }
    }

    QString activated;
    QObject::connect(&widget, &ui::ChannelWidget::nickActivated,
                     [&activated](const QString& nick) { activated = nick; });

    // --- single click: deferred insert, no query -------------------------------
    if (ok) {
        activated.clear();
        sendMouse(chat->viewport(), QEvent::MouseButtonPress, alicePos);
        sendMouse(chat->viewport(), QEvent::MouseButtonRelease, alicePos);
        // Nothing inserted yet: the click may still become a double click.
        const bool deferred = input->text().isEmpty();
        waitMs(QApplication::doubleClickInterval() + 150);
        const bool inserted = (input->text() == QStringLiteral("alice"));
        if (!deferred || !inserted || !activated.isEmpty()) {
            std::printf("single-click: deferred=%d input=\"%s\" activated=\"%s\"\n",
                        static_cast<int>(deferred), qPrintable(input->text()),
                        qPrintable(activated));
            ok = false;
        }
    }

    // --- double click: opens the query, inserts nothing ------------------------
    if (ok) {
        input->clear();
        activated.clear();
        sendMouse(chat->viewport(), QEvent::MouseButtonPress, alicePos);
        sendMouse(chat->viewport(), QEvent::MouseButtonRelease, alicePos);
        sendMouse(chat->viewport(), QEvent::MouseButtonDblClick, alicePos);
        sendMouse(chat->viewport(), QEvent::MouseButtonRelease, alicePos);
        waitMs(QApplication::doubleClickInterval() + 150);
        const bool emitted = (activated == QStringLiteral("alice"));
        const bool untouched = input->text().isEmpty();
        if (!emitted || !untouched) {
            std::printf("double-click: activated=\"%s\" input=\"%s\"\n",
                        qPrintable(activated), qPrintable(input->text()));
            ok = false;
        }
    }

    // --- double click on plain text must stay available to the text editor -----
    if (ok) {
        input->clear();
        activated.clear();
        sendMouse(chat->viewport(), QEvent::MouseButtonPress, plainPos);
        sendMouse(chat->viewport(), QEvent::MouseButtonDblClick, plainPos);
        sendMouse(chat->viewport(), QEvent::MouseButtonRelease, plainPos);
        waitMs(QApplication::doubleClickInterval() + 150);
        if (!activated.isEmpty() || !input->text().isEmpty()) {
            std::printf("plain-text dblclick: activated=\"%s\" input=\"%s\"\n",
                        qPrintable(activated), qPrintable(input->text()));
            ok = false;
        }
    }

    std::printf("%s\n", ok ? "NICK CLICK TEST PASSED" : "NICK CLICK TEST FAILED");
    return ok ? 0 : 1;
}
