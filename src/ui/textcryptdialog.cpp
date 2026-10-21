/*
 * textcryptdialog.cpp — Tools -> Encrypt text / Decrypt text.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui/textcryptdialog.h"

#include "backend/tessbackend.h"
#include "ui/filerow.h"
#include "ui/helpers.h"

#include <QCheckBox>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace tessgui {

namespace {

constexpr int kMaxPassphraseAttempts = 3;

QPlainTextEdit *makeEditor(QWidget *parent, bool readOnly, int height) {
    auto *edit = new QPlainTextEdit(parent);
    edit->setReadOnly(readOnly);
    edit->setMinimumHeight(height);
    if (readOnly) edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    return edit;
}

} /* namespace */

TextCryptDialog::TextCryptDialog(Mode mode, QWidget *parent)
    : QDialog(parent), m_mode(mode) {
    m_settings = GuiSettings::load();
    const bool encrypt = (mode == Mode::Encrypt);
    setWindowTitle(encrypt ? tr("Encrypt text") : tr("Decrypt text"));
    setMinimumWidth(640);

    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        encrypt ? tr("Type the text to protect. The result is armored "
                     "(PEM-like text) and can be pasted anywhere.")
                : tr("Paste an armored message produced by Tesseract Crypt."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    layout->addWidget(new QLabel(encrypt ? tr("Plaintext") : tr("Armored message"), this));
    m_input = makeEditor(this, false, 110);
    layout->addWidget(m_input);

    if (encrypt) {
        m_keyPublic = new FileRow(tr("Key Public"), FileRow::MultiOpenFile, this);
        m_keyPublic->setDialogTitle(tr("Recipient public keys"));
        m_keyPublic->setPlaceholder(tr("recipient .pub key (several allowed)"));
        layout->addWidget(m_keyPublic);
    } else {
        m_keyPrivate = new FileRow(tr("Key Private"), FileRow::OpenFile, this);
        m_keyPrivate->setDialogTitle(tr("Private key"));
        m_keyPrivate->setPlaceholder(tr("your .key file"));
        layout->addWidget(m_keyPrivate);
    }

    auto *passRow = new QWidget(this);
    auto *passLayout = new QHBoxLayout(passRow);
    passLayout->setContentsMargins(0, 0, 0, 0);
    passLayout->setSpacing(6);
    m_passphraseCheck = new QCheckBox(tr("Use a passphrase"), passRow);
    m_passphraseCheck->setMinimumWidth(110);
    m_passphraseEdit = new QLineEdit(passRow);
    m_passphraseEdit->setEchoMode(QLineEdit::Password);
    m_passphraseEdit->setEnabled(false);
    passLayout->addWidget(m_passphraseCheck);
    passLayout->addWidget(m_passphraseEdit, 1);
    layout->addWidget(passRow);

    QString hint;
    if (encrypt && !m_settings.senderKey.isEmpty()) {
        hint = tr("The message is signed with %1.")
                   .arg(QFileInfo(m_settings.senderKey).fileName());
    } else if (!encrypt && m_settings.requireSigner && !m_settings.signerKey.isEmpty()) {
        hint = tr("Only messages signed by %1 are accepted.")
                   .arg(QFileInfo(m_settings.signerKey).fileName());
    }
    if (!hint.isEmpty()) {
        auto *note = new QLabel(hint, this);
        note->setWordWrap(true);
        layout->addWidget(note);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_runButton = buttons->addButton(encrypt ? tr("Encrypt") : tr("Decrypt"),
                                     QDialogButtonBox::AcceptRole);
    QPushButton *copy = buttons->addButton(tr("Copy result"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    layout->addWidget(new QLabel(tr("Result"), this));
    m_output = makeEditor(this, true, 150);
    layout->addWidget(m_output);

    m_result = new QLabel(this);
    m_result->setWordWrap(true);
    layout->addWidget(m_result);

    connect(m_runButton, &QPushButton::clicked, this, &TextCryptDialog::run);
    connect(copy, &QPushButton::clicked, this, &TextCryptDialog::copyOutput);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_passphraseCheck, &QCheckBox::toggled, this, &TextCryptDialog::updateEnabled);
    updateEnabled();
}

void TextCryptDialog::updateEnabled() {
    const bool usePassphrase = m_passphraseCheck->isChecked();
    m_passphraseEdit->setEnabled(usePassphrase);
    if (m_keyPublic) m_keyPublic->setEnabled(!usePassphrase);
    if (m_keyPrivate) m_keyPrivate->setEnabled(!usePassphrase);
}

void TextCryptDialog::copyOutput() {
    QGuiApplication::clipboard()->setText(m_output->toPlainText());
    m_result->setStyleSheet(QString());
    m_result->setText(tr("Result copied to the clipboard."));
}

void TextCryptDialog::run() {
    const QString input = m_input->toPlainText();
    if (input.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Nothing to process."));
        return;
    }

    const bool encrypt = (m_mode == Mode::Encrypt);
    const bool usePassphrase = m_passphraseCheck->isChecked();
    QStringList recipients;
    QString keyFile;
    QString passphrase = m_passphraseEdit->text();

    if (usePassphrase) {
        if (passphrase.isEmpty()) {
            bool accepted = false;
            passphrase = askNewPassphrase(this, windowTitle(), &accepted);
            if (!accepted || passphrase.isEmpty()) return;
            m_passphraseEdit->setText(passphrase);
        }
    } else if (encrypt) {
        recipients = m_keyPublic->paths();
        if (recipients.isEmpty()) {
            QMessageBox::warning(this, windowTitle(),
                                 tr("Choose a recipient key or tick \"Use a passphrase\"."));
            return;
        }
    } else {
        keyFile = m_keyPrivate->path();
        if (keyFile.isEmpty()) {
            QMessageBox::warning(this, windowTitle(),
                                 tr("Choose your private key or tick \"Use a passphrase\"."));
            return;
        }
    }

    const QString senderKey = encrypt ? m_settings.senderKey : QString();
    const QString signer = (!encrypt && m_settings.requireSigner) ? m_settings.signerKey
                                                                  : QString();
    const bool sign = encrypt && m_settings.signWhenPossible && !recipients.isEmpty() &&
                      !senderKey.isEmpty();
    const quint32 chunk = m_settings.chunkSize;

    QString keyPassphrase;  /* for a locked key file */
    for (int attempt = 0; attempt < kMaxPassphraseAttempts; ++attempt) {
        if (encrypt) {
            const TextResult result = runBlocking<TextResult>(
                this, tr("Encrypting..."), [&] {
                    return sealText(input, recipients, senderKey, keyPassphrase, sign,
                                    usePassphrase, passphrase, chunk);
                });
            if (result.ok) {
                m_output->setPlainText(result.text);
                m_result->setStyleSheet(QStringLiteral("color: #1a7f37;"));
                m_result->setText(result.message);
                return;
            }
            if (result.needsPassphrase && !senderKey.isEmpty()) {
                bool accepted = false;
                keyPassphrase = askPassphrase(this, senderKey, result.wrongPassphrase, &accepted);
                if (!accepted) break;
                continue;
            }
            m_result->setStyleSheet(QStringLiteral("color: #b3261e;"));
            m_result->setText(result.message);
            return;
        }

        const TextResult result = runBlocking<TextResult>(
            this, tr("Decrypting..."), [&] {
                return openText(input, keyFile, keyPassphrase, signer, usePassphrase,
                                passphrase);
            });
        if (result.ok) {
            m_output->setPlainText(result.text);
            m_result->setStyleSheet(QStringLiteral("color: #1a7f37;"));
            m_result->setText(result.message);
            return;
        }
        if (result.needsPassphrase && !keyFile.isEmpty()) {
            bool accepted = false;
            keyPassphrase = askPassphrase(this, keyFile, result.wrongPassphrase, &accepted);
            if (!accepted) break;
            continue;
        }
        m_result->setStyleSheet(QStringLiteral("color: #b3261e;"));
        m_result->setText(result.message);
        return;
    }
    m_result->setStyleSheet(QString());
    m_result->setText(tr("Cancelled."));
}

} /* namespace tessgui */
