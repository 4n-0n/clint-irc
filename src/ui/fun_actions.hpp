#pragma once

#include <QList>
#include <QString>

namespace ui {

// A preconfigured /me action: a menu label and the action text sent after
// "/me ".
struct FunAction {
    QString label;
    QString text;
};

// Built-in defaults, used when no config file exists. Pure function.
inline QList<FunAction> defaultFunActions() {
    return {
        {QStringLiteral("Wave"), QStringLiteral("waves hello")},
        {QStringLiteral("Dance"), QStringLiteral("does a happy dance")},
        {QStringLiteral("Hug"), QStringLiteral("hugs everyone")},
        {QStringLiteral("Slap"), QStringLiteral("gives a friendly slap")},
        {QStringLiteral("Facepalm"), QStringLiteral("facepalms")},
        {QStringLiteral("Coffee"), QStringLiteral("brews a fresh pot of coffee")},
        {QStringLiteral("Party"), QStringLiteral("throws confetti everywhere")},
        {QStringLiteral("Shrug"), QStringLiteral("shrugs")},
        {QStringLiteral("Laugh"), QStringLiteral("laughs out loud")},
        {QStringLiteral("Wink"), QStringLiteral("winks")},
    };
}

// Parses an actions config file. Format: one action per line, "Label | text";
// '#' starts a comment and blank lines are ignored. Returns the parsed list,
// which is EMPTY on a missing/unreadable file (the caller falls back to
// defaults). Pure function of the file contents.
QList<FunAction> loadFunActions(const QString& path);

} // namespace ui
