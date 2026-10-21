#include <QApplication>
#include <QEventLoop>
#include <QLabel>
#include <QMouseEvent>
#include <QTextEdit>
#include <QTimer>

#include <cstdio>

#include "ui/channel_widget.hpp"

// Channel-tab extras (runs offscreen, no network):
//   setTopic()        -> the topic bar above the chat shows the text, and is
//                        hidden again when the topic is cleared
//   click "join:"     -> channelActivated() is emitted with the decoded name
//                        (this is how a click in the /list tab joins)

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

// First viewport point whose anchor is exactly `href`.
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

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    ui::ChannelWidget widget(ui::ChannelWidget::Context::Channel,
                             QStringLiteral("#test"));
    widget.resize(900, 600);
    widget.show();
    waitMs(50); // let the layout settle

    bool ok = true;

    // --- topic bar ------------------------------------------------------------
    auto* topic = widget.findChild<QLabel*>(QStringLiteral("topicLabel"));
    if (!topic) {
        std::printf("topic label not found\n");
        ok = false;
    }
    QTextEdit* chat = widget.findChild<QTextEdit*>();

    if (ok) {
        if (topic->isVisible()) {
            std::printf("topic bar visible before any topic\n");
            ok = false;
        }
        widget.setTopic(QStringLiteral("Welcome to #test — be nice"));
        waitMs(20);
        if (!topic->isVisible() || topic->text() != QStringLiteral("Welcome to #test — be nice")) {
            std::printf("topic bar did not show the topic (text=\"%s\" visible=%d)\n",
                        qPrintable(topic->text()), static_cast<int>(topic->isVisible()));
            ok = false;
        }
        // The chat area must shrink, not the chat content: append still works.
        if (ok) {
            chat->clear();
            widget.appendHtml(QStringLiteral("hello"));
            if (!chat->toPlainText().contains(QStringLiteral("hello"))) {
                std::printf("chat append failed with a topic bar visible\n");
                ok = false;
            }
        }
        widget.setTopic(QString());
        waitMs(20);
        if (topic->isVisible() || !topic->text().isEmpty()) {
            std::printf("topic bar did not hide on an empty topic\n");
            ok = false;
        }
    }

    // --- click a channel name to join ----------------------------------------
    if (ok) {
        chat->clear();
        widget.appendHtml(QStringLiteral(
            "<a href=\"join:%23general\" style=\"text-decoration:none;\">#general</a> "
            "<span>(42)</span>"));
        waitMs(30);

        const QPoint point = findAnchorPoint(chat, QStringLiteral("join:%23general"));
        if (point.x() < 0) {
            std::printf("join anchor not found\n");
            ok = false;
        } else {
            QString activated;
            QObject::connect(&widget, &ui::ChannelWidget::channelActivated,
                             [&activated](const QString& channel) { activated = channel; });

            sendMouse(chat->viewport(), QEvent::MouseButtonPress, point);
            sendMouse(chat->viewport(), QEvent::MouseButtonRelease, point);
            waitMs(QApplication::doubleClickInterval() + 100);

            if (activated != QStringLiteral("#general")) {
                std::printf("join click activated \"%s\"\n", qPrintable(activated));
                ok = false;
            }
        }
    }

    std::printf("%s\n", ok ? "CHANNEL EXTRAS TEST PASSED" : "CHANNEL EXTRAS TEST FAILED");
    return ok ? 0 : 1;
}
