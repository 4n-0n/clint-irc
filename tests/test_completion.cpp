#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

#include "ui/history_line_edit.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // A container with two focusable widgets: without the fix, Tab would move
    // focus from `input` to `other` and never complete the nick.
    QWidget container;
    auto* layout = new QVBoxLayout(&container);
    auto* input = new ui::HistoryLineEdit(&container);
    auto* other = new QLineEdit(&container);
    layout->addWidget(input);
    layout->addWidget(other);
    container.show();

    input->setCompletionList({QStringLiteral("opnick"), QStringLiteral("operator")});
    input->setText(QStringLiteral("op"));
    input->setCursorPosition(2);
    input->setFocus();
    QApplication::processEvents();

    auto pressTab = [&]() {
        QKeyEvent e(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QApplication::sendEvent(input, &e);
    };
    // A non-Tab key resets the completion cycle (Left arrow reaches
    // keyPressEvent without altering the text).
    auto resetCompletion = [&]() {
        QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
        QApplication::sendEvent(input, &left);
    };

    pressTab();
    const QString after1 = input->text();
    const bool focus1 = input->hasFocus();

    pressTab(); // cycle to the next match
    const QString after2 = input->text();
    const bool focus2 = input->hasFocus();

    // Empty-prefix completion: Tab with no letters typed cycles all nicks.
    input->setText(QString());
    input->setCursorPosition(0);
    resetCompletion();
    pressTab();
    const QString empty1 = input->text();
    pressTab();
    const QString empty2 = input->text();

    bool ok = true;
    ok = ok && (after1 == QStringLiteral("opnick"));   // first match
    ok = ok && (after2 == QStringLiteral("operator")); // cycled to next match
    ok = ok && focus1 && focus2;                       // focus never moved
    ok = ok && (empty1 == QStringLiteral("opnick"));   // empty prefix -> first nick
    ok = ok && (empty2 == QStringLiteral("operator")); // empty prefix -> cycle

    std::printf("after1=%s after2=%s empty1=%s empty2=%s focus1=%d focus2=%d\n",
                qPrintable(after1), qPrintable(after2), qPrintable(empty1),
                qPrintable(empty2), static_cast<int>(focus1), static_cast<int>(focus2));
    std::printf("%s\n", ok ? "COMPLETION TEST PASSED" : "COMPLETION TEST FAILED");
    return ok ? 0 : 1;
}
