#pragma once

#include "../irc/connection_config.hpp"

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QSpinBox;

namespace ui {

// Modal dialog collecting connection parameters. Returns a fully-populated
// ConnectionConfig via config().
class ConnectDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConnectDialog(QWidget* parent = nullptr);

    [[nodiscard]] irc::ConnectionConfig config() const;

    // Persists the current fields to QSettings before closing.
    void accept() override;

private:
    QLineEdit* m_host;
    QSpinBox* m_port;
    QCheckBox* m_tls;
    QLineEdit* m_nick;
    QLineEdit* m_user;
    QLineEdit* m_realName;
    QLineEdit* m_password;
    QLineEdit* m_saslUser;
    QLineEdit* m_saslPassword;
    QLineEdit* m_channels;
};

} // namespace ui
