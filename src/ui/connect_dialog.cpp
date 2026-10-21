#include "connect_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include "../irc/endpoint.hpp"

namespace ui {

namespace {

int familyToIndex(irc::AddressFamily family) {
    switch (family) {
    case irc::AddressFamily::IPv4:
        return 1;
    case irc::AddressFamily::IPv6:
        return 2;
    case irc::AddressFamily::Any:
        break;
    }
    return 0;
}

irc::AddressFamily indexToFamily(int index) {
    switch (index) {
    case 1:
        return irc::AddressFamily::IPv4;
    case 2:
        return irc::AddressFamily::IPv6;
    default:
        return irc::AddressFamily::Any;
    }
}

} // namespace

ConnectDialog::ConnectDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Connect to IRC"));

    // Load persisted values; fall back to defaults on first run.
    QSettings settings;

    m_host = new QLineEdit(
        settings.value(QStringLiteral("host"), QStringLiteral("irc.libera.chat")).toString(),
        this);
    m_host->setPlaceholderText(QStringLiteral("irc.example.net, [2001:db8::1] or [::1]:6697"));
    m_host->setToolTip(QStringLiteral(
        "Hostname, IPv4 address, IPv6 literal (brackets optional, e.g.\n"
        "[2001:db8::1]) or host:port."));
    m_port = new QSpinBox(this);
    m_port->setRange(1, 65535);
    m_port->setValue(settings.value(QStringLiteral("port"), 6697).toInt());
    m_tls = new QCheckBox(QStringLiteral("Use TLS/SSL"), this);
    m_tls->setChecked(settings.value(QStringLiteral("tls"), true).toBool());
    m_family = new QComboBox(this);
    m_family->addItem(QStringLiteral("Auto (IPv6 preferred, IPv4 fallback)"));
    m_family->addItem(QStringLiteral("IPv4 only"));
    m_family->addItem(QStringLiteral("IPv6 only"));
    m_family->setToolTip(QStringLiteral(
        "Restrict the connect attempt to one address family.\n"
        "Use \"IPv6 only\" for a server that has no IPv4 address."));
    m_family->setCurrentIndex(familyToIndex(
        static_cast<irc::AddressFamily>(settings.value(QStringLiteral("family"), 0).toInt())));

    m_nick = new QLineEdit(
        settings.value(QStringLiteral("nick"), QStringLiteral("clint_user")).toString(), this);
    m_user = new QLineEdit(settings.value(QStringLiteral("user")).toString(), this);
    m_user->setPlaceholderText(QStringLiteral("defaults to nick"));
    m_realName = new QLineEdit(
        settings.value(QStringLiteral("realname"), QStringLiteral("Clint IRC User")).toString(),
        this);
    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(QStringLiteral("optional server password (not saved)"));
    m_saslUser = new QLineEdit(
        settings.value(QStringLiteral("sasluser")).toString(), this);
    m_saslUser->setPlaceholderText(QStringLiteral("account name (enables SASL)"));
    m_saslPassword = new QLineEdit(this);
    m_saslPassword->setEchoMode(QLineEdit::Password);
    m_saslPassword->setPlaceholderText(QStringLiteral("SASL password (not saved)"));
    m_channels = new QLineEdit(
        settings.value(QStringLiteral("channels"), QStringLiteral("#clint")).toString(), this);
    m_channels->setPlaceholderText(QStringLiteral("#chan1 #chan2"));

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Server:"), m_host);
    form->addRow(QStringLiteral("Port:"), m_port);
    form->addRow(QStringLiteral("Protocol:"), m_family);
    form->addRow(QString(), m_tls);
    form->addRow(QStringLiteral("Nick:"), m_nick);
    form->addRow(QStringLiteral("Username:"), m_user);
    form->addRow(QStringLiteral("Real name:"), m_realName);
    form->addRow(QStringLiteral("Password:"), m_password);
    form->addRow(QStringLiteral("SASL user:"), m_saslUser);
    form->addRow(QStringLiteral("SASL password:"), m_saslPassword);
    form->addRow(QStringLiteral("Auto-join:"), m_channels);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(buttons);
}

void ConnectDialog::accept() {
    // Normalize "host:port" / bracketed literals before persisting, so the
    // saved host is always a plain connectable string.
    const irc::Endpoint endpoint = irc::parseEndpoint(m_host->text());
    if (!endpoint.host.isEmpty()) {
        m_host->setText(endpoint.host);
    }
    if (endpoint.port != 0) {
        m_port->setValue(endpoint.port);
    }

    // Persist settings on OK. The password is deliberately NOT saved: a
    // plaintext secret at rest is a needless risk; re-enter it each session.
    QSettings settings;
    settings.setValue(QStringLiteral("host"), m_host->text().trimmed());
    settings.setValue(QStringLiteral("port"), m_port->value());
    settings.setValue(QStringLiteral("tls"), m_tls->isChecked());
    settings.setValue(QStringLiteral("family"), m_family->currentIndex());
    settings.setValue(QStringLiteral("nick"), m_nick->text().trimmed());
    settings.setValue(QStringLiteral("user"), m_user->text().trimmed());
    settings.setValue(QStringLiteral("realname"), m_realName->text().trimmed());
    settings.setValue(QStringLiteral("sasluser"), m_saslUser->text().trimmed());
    settings.setValue(QStringLiteral("channels"), m_channels->text().trimmed());
    QDialog::accept();
}

irc::ConnectionConfig ConnectDialog::config() const {
    irc::ConnectionConfig c;

    // Accept the same strings the CLI does: "host", "host:port", "[v6]",
    // "[v6]:port" and bare IPv6 literals.
    const irc::Endpoint endpoint = irc::parseEndpoint(m_host->text());
    c.host = endpoint.host;

    c.port = static_cast<quint16>(m_port->value());
    if (endpoint.port != 0) {
        c.port = endpoint.port;
    }

    c.useTls = m_tls->isChecked();
    c.family = indexToFamily(m_family->currentIndex());
    // A literal pins the family: connect to exactly that address.
    if (endpoint.isAddressLiteral) {
        c.family = endpoint.isIpv6 ? irc::AddressFamily::IPv6 : irc::AddressFamily::IPv4;
    }
    c.nick = m_nick->text().trimmed();
    c.user = m_user->text().trimmed();
    c.realName = m_realName->text().trimmed();
    c.password = m_password->text();
    c.saslUser = m_saslUser->text().trimmed();
    c.saslPassword = m_saslPassword->text();
    c.autoJoinChannels =
        m_channels->text().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    return c;
}

} // namespace ui
