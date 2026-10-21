/*
 * optionsdialog.cpp — Edit -> Options.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui/optionsdialog.h"

#include "backend/tessbackend.h"
#include "ui/filerow.h"

#include <tesseract/tesseract.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace tessgui {

OptionsDialog::OptionsDialog(QWidget *parent) : QDialog(parent) {
    m_settings = GuiSettings::load();

    setWindowTitle(tr("Options"));
    setMinimumWidth(600);

    auto *layout = new QVBoxLayout(this);

    /* ---------------------------------------------------------- encrypt */
    auto *encryptBox = new QGroupBox(tr("Encrypt"), this);
    auto *encryptLayout = new QVBoxLayout(encryptBox);

    m_armor = new QCheckBox(tr("Write armored output (PEM-like base64 text)"), encryptBox);
    m_armor->setChecked(m_settings.armor);
    encryptLayout->addWidget(m_armor);

    auto *chunkLayout = new QFormLayout;
    m_chunkKiB = new QSpinBox(encryptBox);
    m_chunkKiB->setRange(0, 16384);
    m_chunkKiB->setSingleStep(4);
    m_chunkKiB->setSuffix(tr(" KiB"));
    m_chunkKiB->setSpecialValueText(tr("default (64 KiB)"));
    m_chunkKiB->setValue(static_cast<int>(m_settings.chunkSize / 1024));
    m_chunkKiB->setToolTip(tr("Plaintext bytes per authenticated chunk, %1-%2 KiB.")
                               .arg(TESS_MIN_CHUNK / 1024)
                               .arg(TESS_MAX_CHUNK / 1024));
    chunkLayout->addRow(tr("Chunk size"), m_chunkKiB);
    encryptLayout->addLayout(chunkLayout);

    m_sign = new QCheckBox(tr("Sign messages when a sender key is available"), encryptBox);
    m_sign->setChecked(m_settings.signWhenPossible);
    encryptLayout->addWidget(m_sign);

    m_senderKey = new FileRow(tr("Sender key"), FileRow::OpenFile, encryptBox);
    m_senderKey->setDialogTitle(tr("Private key used to sign the messages"));
    m_senderKey->setFilter(tr("Private keys (*.key);;All files (*)"));
    m_senderKey->setPlaceholder(tr("your .key file (optional)"));
    m_senderKey->setPath(m_settings.senderKey);
    encryptLayout->addWidget(m_senderKey);
    layout->addWidget(encryptBox);

    /* ---------------------------------------------------------- decrypt */
    auto *decryptBox = new QGroupBox(tr("Decrypt"), this);
    auto *decryptLayout = new QVBoxLayout(decryptBox);

    m_requireSigner = new QCheckBox(tr("Only accept messages signed by this key"), decryptBox);
    m_requireSigner->setChecked(m_settings.requireSigner);
    decryptLayout->addWidget(m_requireSigner);

    m_signerKey = new FileRow(tr("Signer key"), FileRow::OpenFile, decryptBox);
    m_signerKey->setDialogTitle(tr("Public key the signature must match"));
    m_signerKey->setFilter(tr("Public keys (*.pub);;All files (*)"));
    m_signerKey->setPlaceholder(tr("expected signer .pub file"));
    m_signerKey->setPath(m_settings.signerKey);
    decryptLayout->addWidget(m_signerKey);
    layout->addWidget(decryptBox);

    /* ----------------------------------------------------------- keygen */
    auto *keygenBox = new QGroupBox(tr("Key generation"), this);
    auto *keygenLayout = new QVBoxLayout(keygenBox);

    m_lockKeys = new QCheckBox(tr("Protect new private keys with a passphrase by default"),
                               keygenBox);
    m_lockKeys->setChecked(m_settings.lockNewKeys);
    keygenLayout->addWidget(m_lockKeys);

    auto *kdfLayout = new QFormLayout;
    m_ops = new QSpinBox(keygenBox);
    m_ops->setRange(0, 64);
    m_ops->setSpecialValueText(tr("default"));
    m_ops->setValue(static_cast<int>(m_settings.kdfOps));
    m_mem = new QSpinBox(keygenBox);
    m_mem->setRange(0, 4096);
    m_mem->setSuffix(tr(" MiB"));
    m_mem->setSpecialValueText(tr("default"));
    m_mem->setValue(static_cast<int>(m_settings.kdfMemMiB));
    kdfLayout->addRow(tr("Argon2id passes"), m_ops);
    kdfLayout->addRow(tr("Argon2id memory"), m_mem);
    keygenLayout->addLayout(kdfLayout);
    layout->addWidget(keygenBox);

    /* -------------------------------------------------------- interface */
    auto *uiBox = new QGroupBox(tr("Interface"), this);
    auto *uiLayout = new QVBoxLayout(uiBox);
    m_confirmOverwrite = new QCheckBox(tr("Ask before replacing an existing file"), uiBox);
    m_confirmOverwrite->setChecked(m_settings.confirmOverwrite);
    uiLayout->addWidget(m_confirmOverwrite);
    layout->addWidget(uiBox);

    auto *note = new QLabel(
        tr("The armored output and the chunk size follow the message format; "
           "the sender key is only used when a recipient key is selected."),
        this);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->addButton(tr("Restore defaults"), QDialogButtonBox::ResetRole);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &OptionsDialog::saveAndClose);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::clicked, this, [this, buttons](QAbstractButton *button) {
        if (buttons->buttonRole(button) == QDialogButtonBox::ResetRole) restoreDefaults();
    });
    connect(m_sign, &QCheckBox::toggled, this, &OptionsDialog::updateEnabled);
    connect(m_requireSigner, &QCheckBox::toggled, this, &OptionsDialog::updateEnabled);
    connect(m_lockKeys, &QCheckBox::toggled, this, &OptionsDialog::updateEnabled);
    updateEnabled();
}

void OptionsDialog::updateEnabled() {
    m_senderKey->setEnabled(m_sign->isChecked());
    m_signerKey->setEnabled(m_requireSigner->isChecked());
    m_ops->setEnabled(m_lockKeys->isChecked());
    m_mem->setEnabled(m_lockKeys->isChecked());
}

void OptionsDialog::restoreDefaults() {
    const GuiSettings defaults;
    m_armor->setChecked(defaults.armor);
    m_chunkKiB->setValue(static_cast<int>(defaults.chunkSize / 1024));
    m_sign->setChecked(defaults.signWhenPossible);
    m_senderKey->setPath(defaults.senderKey);
    m_requireSigner->setChecked(defaults.requireSigner);
    m_signerKey->setPath(defaults.signerKey);
    m_lockKeys->setChecked(defaults.lockNewKeys);
    m_ops->setValue(static_cast<int>(defaults.kdfOps));
    m_mem->setValue(static_cast<int>(defaults.kdfMemMiB));
    m_confirmOverwrite->setChecked(defaults.confirmOverwrite);
    updateEnabled();
}

void OptionsDialog::saveAndClose() {
    GuiSettings settings;
    settings.armor = m_armor->isChecked();
    settings.chunkSize = static_cast<quint32>(m_chunkKiB->value()) * 1024u;
    settings.signWhenPossible = m_sign->isChecked();
    settings.senderKey = m_senderKey->path();
    settings.requireSigner = m_requireSigner->isChecked();
    settings.signerKey = m_signerKey->path();
    settings.lockNewKeys = m_lockKeys->isChecked();
    settings.kdfOps = static_cast<quint32>(m_ops->value());
    settings.kdfMemMiB = static_cast<quint32>(m_mem->value());
    settings.confirmOverwrite = m_confirmOverwrite->isChecked();
    settings.lastDirectory = m_settings.lastDirectory;
    settings.save();
    accept();
}

} /* namespace tessgui */
