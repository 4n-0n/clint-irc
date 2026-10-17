#include <QApplication>
#include <QEventLoop>
#include <QListWidget>
#include <QScrollBar>
#include <QTimer>

#include <cstdio>

#include "ui/channel_widget.hpp"

// The nick list is rebuilt from scratch every time the caller pushes a new nick
// set (every join/part/quit/nick-change triggers setNickList). The user's
// selected nick must survive that rebuild AND the viewport must not move: no
// jump to the top, no scroll to the selected row. This runs offscreen.

namespace {

void waitMs(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

QStringList makeNicks(int count) {
    QStringList nicks;
    for (int i = 0; i < count; ++i) {
        nicks << QStringLiteral("user%1").arg(i, 3, 10, QLatin1Char('0'));
    }
    return nicks;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    ui::ChannelWidget widget(ui::ChannelWidget::Context::Channel,
                            QStringLiteral("#test"));
    widget.resize(500, 400);
    widget.show();
    waitMs(50);

    QListWidget* list = widget.findChild<QListWidget*>();
    bool ok = (list != nullptr);

    int scrolledTo = 0;
    QString selectedNick;
    if (ok) {
        widget.setNickList(makeNicks(200));
        waitMs(50);
        QScrollBar* bar = list->verticalScrollBar();
        ok = ok && (bar->maximum() > 20); // the list must actually be scrollable

        if (ok) {
            // User selects a row far down, then scrolls back up to keep reading.
            list->setCurrentRow(120);
            selectedNick = list->currentItem()->text();
            bar->setValue(20);
            scrolledTo = bar->value();

            // A new nick set arrives (someone joins): same rebuild path.
            QStringList nicks = makeNicks(200);
            nicks << QStringLiteral("newcomer");
            widget.setNickList(nicks);
            waitMs(50);

            const int after = bar->value();
            const QString stillSelected =
                list->currentItem() ? list->currentItem()->text() : QString();
            if (after != scrolledTo) {
                std::printf("scroll position moved: %d -> %d\n", scrolledTo, after);
                ok = false;
            }
            if (stillSelected != selectedNick) {
                std::printf("selection lost: \"%s\" -> \"%s\"\n",
                            qPrintable(selectedNick), qPrintable(stillSelected));
                ok = false;
            }
        }
    }

    // The scroll value must also stay sane when the rebuild shortens the list.
    if (ok) {
        QScrollBar* bar = list->verticalScrollBar();
        bar->setValue(bar->maximum());
        const int before = bar->value();
        widget.setNickList(makeNicks(10)); // nearly everyone parts
        waitMs(50);
        const int after = bar->value();
        if (after > bar->maximum() || after < 0) {
            std::printf("out-of-range scroll after shrink: %d (max %d, was %d)\n",
                        after, bar->maximum(), before);
            ok = false;
        }
    }

    std::printf("%s\n", ok ? "NICK LIST SCROLL TEST PASSED" : "NICK LIST SCROLL TEST FAILED");
    return ok ? 0 : 1;
}
