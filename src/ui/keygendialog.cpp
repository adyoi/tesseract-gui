/*
 * keygendialog.cpp — key pair generation, with the same KDF knobs as
 * `tesseract-crypt keygen --passphrase --kdf-ops N --kdf-mem MB`.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui/keygendialog.h"

#include "backend/tessbackend.h"
#include "ui/filerow.h"
#include "ui/helpers.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace tessgui {

KeygenDialog::KeygenDialog(QWidget *parent) : QDialog(parent) {
    m_settings = GuiSettings::load();

    setWindowTitle(tr("Key pair"));
    setMinimumWidth(560);

    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("Creates a private key (<b>.key</b>) and the matching public key "
           "(<b>.pub</b>). Give the .pub to anyone who wants to write to you; "
           "keep the .key to yourself."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_base = new FileRow(tr("Base name"), FileRow::SaveFile, this);
    m_base->setDialogTitle(tr("Choose the name of the key pair"));
    m_base->setFilter(tr("Key files (*.key *.pub);;All files (*)"));
    m_base->setPlaceholder(tr("path/mykey — also writes path/mykey.pub"));
    QString start = m_settings.lastDirectory;
    if (start.isEmpty()) start = QDir::homePath();
    m_base->setPath(QDir(start).filePath(QStringLiteral("mykey")));
    layout->addWidget(m_base);

    auto *protection = new QGroupBox(tr("Protection"), this);
    auto *protectionLayout = new QVBoxLayout(protection);

    m_lock = new QCheckBox(tr("Protect the private key with a passphrase "
                              "(Argon2id)"),
                           protection);
    m_lock->setChecked(m_settings.lockNewKeys);
    protectionLayout->addWidget(m_lock);

    auto *passLayout = new QFormLayout;
    m_passphrase = new QLineEdit(protection);
    m_passphrase->setEchoMode(QLineEdit::Password);
    m_confirm = new QLineEdit(protection);
    m_confirm->setEchoMode(QLineEdit::Password);
    passLayout->addRow(tr("Passphrase"), m_passphrase);
    passLayout->addRow(tr("Confirm"), m_confirm);
    protectionLayout->addLayout(passLayout);

    auto *kdfLayout = new QFormLayout;
    m_ops = new QSpinBox(protection);
    m_ops->setRange(0, 64);
    m_ops->setSpecialValueText(tr("default"));
    m_ops->setValue(static_cast<int>(m_settings.kdfOps));
    m_mem = new QSpinBox(protection);
    m_mem->setRange(0, 4096);
    m_mem->setSuffix(tr(" MiB"));
    m_mem->setSpecialValueText(tr("default"));
    m_mem->setValue(static_cast<int>(m_settings.kdfMemMiB));
    kdfLayout->addRow(tr("KDF passes"), m_ops);
    kdfLayout->addRow(tr("KDF memory"), m_mem);
    protectionLayout->addLayout(kdfLayout);
    layout->addWidget(protection);

    m_result = new QLabel(this);
    m_result->setWordWrap(true);
    m_result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_result);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton *generate = buttons->addButton(tr("Generate"), QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);

    connect(generate, &QPushButton::clicked, this, &KeygenDialog::generate);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_lock, &QCheckBox::toggled, this, &KeygenDialog::updateEnabled);
    updateEnabled();
}

void KeygenDialog::updateEnabled() {
    const bool on = m_lock->isChecked();
    m_passphrase->setEnabled(on);
    m_confirm->setEnabled(on);
    m_ops->setEnabled(on);
    m_mem->setEnabled(on);
}

void KeygenDialog::generate() {
    QString base = m_base->path().trimmed();
    if (base.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Choose a base name first."));
        return;
    }
    base = suggestKeyBaseName(base);
    const QString privatePath = base + QStringLiteral(".key");
    const QString publicPath = base + QStringLiteral(".pub");

    const bool lock = m_lock->isChecked();
    QString passphrase;
    if (lock) {
        passphrase = m_passphrase->text();
        if (passphrase.isEmpty()) {
            QMessageBox::warning(this, windowTitle(),
                                 tr("The passphrase is empty: type one or untick the box."));
            return;
        }
        if (passphrase != m_confirm->text()) {
            QMessageBox::warning(this, windowTitle(), tr("The passphrases do not match."));
            return;
        }
    }
    if (!confirmOverwrite(this, privatePath) || !confirmOverwrite(this, publicPath)) return;

    const quint32 ops = static_cast<quint32>(m_ops->value());
    const quint64 mem =
        static_cast<quint64>(m_mem->value()) * 1024ull * 1024ull;

    const KeygenResult result = runBlocking<KeygenResult>(
        this, tr("Generating the key pair..."),
        [&] { return keygen(base, lock, passphrase, ops, mem); });

    if (!result.ok) {
        m_result->setStyleSheet(QStringLiteral("color: #b3261e;"));
        m_result->setText(tr("Failed: %1").arg(result.message));
        return;
    }

    m_result->setStyleSheet(QStringLiteral("color: #1a7f37;"));
    m_result->setText(tr("Private key: %1<br>Public key: %2<br>Fingerprint: <b>%3</b>")
                          .arg(result.privatePath, result.publicPath, result.fingerprint));

    GuiSettings settings = GuiSettings::load();
    settings.lastDirectory = QFileInfo(result.privatePath).absolutePath();
    settings.save();
}

} /* namespace tessgui */
