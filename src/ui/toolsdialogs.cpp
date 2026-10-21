/*
 * toolsdialogs.cpp — the dialogs behind the Tools menu.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui/toolsdialogs.h"

#include "backend/tessbackend.h"
#include "ui/filerow.h"
#include "ui/helpers.h"

#include <tesseract/tesseract.h>

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

namespace tessgui {

namespace {

constexpr int kMaxPassphraseAttempts = 3;

QTextEdit *makeOutput(QWidget *parent) {
    auto *output = new QTextEdit(parent);
    output->setReadOnly(true);
    output->setMinimumHeight(150);
    output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    return output;
}

void showResult(QLabel *label, const QString &text, bool ok) {
    label->setStyleSheet(ok ? QStringLiteral("color: #1a7f37;")
                            : QStringLiteral("color: #b3261e;"));
    label->setText(text);
}

} /* namespace */

/* ------------------------------------------------------------------ */
/* Public key from private key                                         */
/* ------------------------------------------------------------------ */

PubkeyDialog::PubkeyDialog(QWidget *parent) : QDialog(parent) {
    m_settings = GuiSettings::load();
    setWindowTitle(tr("Public key from private key"));
    setMinimumWidth(600);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        tr("Recover the public key of a key pair. Works with a locked private "
           "key as well: the passphrase is only needed for the private part."),
        this));

    m_key = new FileRow(tr("Private key"), FileRow::OpenFile, this);
    m_key->setDialogTitle(tr("Select the private key"));
    m_key->setFilter(tr("Private keys (*.key);;All files (*)"));
    layout->addWidget(m_key);

    m_out = new FileRow(tr("Public key"), FileRow::SaveFile, this);
    m_out->setDialogTitle(tr("Where to write the public key"));
    m_out->setFilter(tr("Public keys (*.pub);;All files (*)"));
    layout->addWidget(m_out);

    m_result = new QLabel(this);
    m_result->setWordWrap(true);
    m_result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_result);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton *run = buttons->addButton(tr("Export"), QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    connect(run, &QPushButton::clicked, this, &PubkeyDialog::run);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_key, &FileRow::changed, this, [this] {
        if (m_out->path().isEmpty() && !m_key->path().isEmpty()) {
            QString out = suggestKeyBaseName(m_key->path()) + QStringLiteral(".pub");
            m_out->setPath(out);
        }
    });
}

void PubkeyDialog::setSourceFile(const QString &path) {
    m_key->setPath(path);
    m_out->setPath(suggestKeyBaseName(path) + QStringLiteral(".pub"));
}

void PubkeyDialog::run() {
    const QString keyFile = m_key->path();
    if (keyFile.isEmpty()) {
        showResult(m_result, tr("Choose the private key first."), false);
        return;
    }
    QString outFile = m_out->path();
    if (outFile.isEmpty()) {
        outFile = suggestKeyBaseName(keyFile) + QStringLiteral(".pub");
        m_out->setPath(outFile);
    }
    if (!confirmOverwrite(this, outFile)) return;

    QString passphrase;
    for (int attempt = 0; attempt < kMaxPassphraseAttempts; ++attempt) {
        const KeyResult result = runBlocking<KeyResult>(
            this, tr("Deriving the public key..."),
            [&] { return exportPublicKey(keyFile, passphrase, outFile); });
        if (result.ok) {
            showResult(m_result,
                       tr("Written to %1 — fingerprint <b>%2</b>")
                           .arg(QFileInfo(outFile).fileName(), result.fingerprint),
                       true);
            return;
        }
        if (result.needsPassphrase) {
            bool accepted = false;
            passphrase = askPassphrase(this, keyFile, result.wrongPassphrase, &accepted);
            if (!accepted) break;
            continue;
        }
        showResult(m_result, result.message, false);
        return;
    }
    showResult(m_result, tr("Cancelled."), false);
}

/* ------------------------------------------------------------------ */
/* Sign                                                                */
/* ------------------------------------------------------------------ */

SignDialog::SignDialog(QWidget *parent) : QDialog(parent) {
    m_settings = GuiSettings::load();
    setWindowTitle(tr("Sign a file"));
    setMinimumWidth(600);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        tr("Writes a detached Ed25519ph signature of the file, in armored "
           "text. The file itself is not modified."),
        this));

    m_key = new FileRow(tr("Private key"), FileRow::OpenFile, this);
    m_key->setDialogTitle(tr("Key used to sign"));
    m_key->setFilter(tr("Private keys (*.key);;All files (*)"));
    m_key->setPath(m_settings.senderKey);
    layout->addWidget(m_key);

    m_input = new FileRow(tr("File"), FileRow::OpenFile, this);
    m_input->setDialogTitle(tr("File to sign"));
    layout->addWidget(m_input);

    m_output = new FileRow(tr("Signature"), FileRow::SaveFile, this);
    m_output->setDialogTitle(tr("Where to write the signature"));
    m_output->setFilter(tr("Signatures (*.sig);;All files (*)"));
    layout->addWidget(m_output);

    m_result = new QLabel(this);
    m_result->setWordWrap(true);
    m_result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_result);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton *run = buttons->addButton(tr("Sign"), QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    connect(run, &QPushButton::clicked, this, &SignDialog::run);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_input, &FileRow::changed, this, &SignDialog::updateOutput);
}

void SignDialog::setSourceFile(const QString &path) {
    m_input->setPath(path);
    m_output->setPath(suggestSignatureName(path));
}

void SignDialog::updateOutput() {
    if (m_output->path().isEmpty() && !m_input->path().isEmpty()) {
        m_output->setPath(suggestSignatureName(m_input->path()));
    }
}

void SignDialog::run() {
    const QString input = m_input->path();
    if (m_key->path().isEmpty() || input.isEmpty()) {
        showResult(m_result, tr("Choose the private key and the file to sign."), false);
        return;
    }
    QString output = m_output->path();
    if (output.isEmpty()) {
        output = suggestSignatureName(input);
        m_output->setPath(output);
    }
    if (!confirmOverwrite(this, output)) return;

    QString passphrase;
    for (int attempt = 0; attempt < kMaxPassphraseAttempts; ++attempt) {
        const Result result = runBlocking<Result>(
            this, tr("Signing %1...").arg(QFileInfo(input).fileName()),
            [&] { return signFile(input, m_key->path(), passphrase, output); });
        if (result.ok) {
            showResult(m_result, result.message, true);
            return;
        }
        if (result.needsPassphrase) {
            bool accepted = false;
            passphrase = askPassphrase(this, m_key->path(), result.wrongPassphrase, &accepted);
            if (!accepted) break;
            continue;
        }
        showResult(m_result, result.message, false);
        return;
    }
    showResult(m_result, tr("Cancelled."), false);
}

/* ------------------------------------------------------------------ */
/* Verify                                                              */
/* ------------------------------------------------------------------ */

VerifyDialog::VerifyDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Verify a signature"));
    setMinimumWidth(600);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        tr("Check that a signature belongs to a file and to the expected key."), this));

    m_input = new FileRow(tr("File"), FileRow::OpenFile, this);
    m_input->setDialogTitle(tr("File that was signed"));
    layout->addWidget(m_input);

    m_signature = new FileRow(tr("Signature"), FileRow::OpenFile, this);
    m_signature->setDialogTitle(tr("Detached signature"));
    m_signature->setFilter(tr("Signatures (*.sig);;All files (*)"));
    layout->addWidget(m_signature);

    m_key = new FileRow(tr("Key Public"), FileRow::OpenFile, this);
    m_key->setDialogTitle(tr("Public key of the expected signer"));
    m_key->setFilter(tr("Public keys (*.pub);;All files (*)"));
    layout->addWidget(m_key);

    m_result = new QLabel(this);
    m_result->setWordWrap(true);
    layout->addWidget(m_result);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton *run = buttons->addButton(tr("Verify"), QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    connect(run, &QPushButton::clicked, this, &VerifyDialog::run);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_input, &FileRow::changed, this, [this] {
        if (m_signature->path().isEmpty() && !m_input->path().isEmpty()) {
            m_signature->setPath(suggestSignatureName(m_input->path()));
        }
    });
}

void VerifyDialog::setSourceFile(const QString &path) {
    m_input->setPath(path);
    m_signature->setPath(suggestSignatureName(path));
}

void VerifyDialog::run() {
    if (m_input->path().isEmpty() || m_signature->path().isEmpty() ||
        m_key->path().isEmpty()) {
        showResult(m_result, tr("Choose the file, its signature and the public key."), false);
        return;
    }
    const Result result = runBlocking<Result>(
        this, tr("Verifying..."), [&] {
            return verifyFile(m_input->path(), m_signature->path(), m_key->path());
        });
    showResult(m_result, result.ok ? tr("Signature is valid.") : result.message, result.ok);
}

/* ------------------------------------------------------------------ */
/* Inspect                                                             */
/* ------------------------------------------------------------------ */

InspectDialog::InspectDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Inspect a message"));
    setMinimumWidth(600);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        tr("Show the header of an encrypted message without the key: recipients, "
           "mode, signature and sizes."),
        this));

    m_input = new FileRow(tr("Message"), FileRow::OpenFile, this);
    m_input->setDialogTitle(tr("Encrypted file or armored message"));
    layout->addWidget(m_input);

    m_output = makeOutput(this);
    layout->addWidget(m_output);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton *run = buttons->addButton(tr("Inspect"), QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    connect(run, &QPushButton::clicked, this, &InspectDialog::run);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void InspectDialog::setSourceFile(const QString &path) {
    m_input->setPath(path);
}

void InspectDialog::run() {
    const QString input = m_input->path();
    if (input.isEmpty()) {
        m_output->setPlainText(tr("Choose a message file first."));
        return;
    }
    const InspectResult result = runBlocking<InspectResult>(
        this, tr("Reading the header..."), [&] { return inspectFile(input); });
    if (!result.ok) {
        m_output->setPlainText(result.message);
        return;
    }

    QStringList lines;
    lines << tr("format version : %1").arg(result.version)
          << tr("suite          : %1").arg(result.suite)
          << tr("mode           : %1")
                 .arg(result.mode == TESS_MODE_PUBLICKEY ? tr("public-key") : tr("passphrase"));
    if (result.mode == TESS_MODE_PUBLICKEY) {
        lines << tr("recipients     : %1").arg(result.recipientCount);
    }
    lines << tr("signed         : %1").arg(result.signedFlag ? tr("yes") : tr("no"));
    if (result.signedFlag) {
        lines << tr("sender key     : %1").arg(result.senderFingerprint);
    }
    lines << tr("chunk size     : %1 bytes").arg(result.chunkSize)
          << tr("plaintext len  : %1")
                 .arg(result.lengthKnown ? humanSize(result.plaintextLen)
                                        : tr("unknown"));
    m_output->setPlainText(lines.join(QLatin1Char('\n')));
}

/* ------------------------------------------------------------------ */
/* Key information                                                     */
/* ------------------------------------------------------------------ */

InfoDialog::InfoDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Key information"));
    setMinimumWidth(600);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        tr("What kind of key is this file, is it locked, and what is its "
           "fingerprint?"),
        this));

    m_key = new FileRow(tr("Key"), FileRow::OpenFile, this);
    m_key->setDialogTitle(tr("Key file"));
    m_key->setFilter(tr("Keys (*.key *.pub);;All files (*)"));
    layout->addWidget(m_key);

    m_output = makeOutput(this);
    m_output->setMinimumHeight(90);
    layout->addWidget(m_output);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton *run = buttons->addButton(tr("Show"), QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    connect(run, &QPushButton::clicked, this, &InfoDialog::run);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void InfoDialog::setSourceFile(const QString &path) {
    m_key->setPath(path);
}

void InfoDialog::run() {
    const QString keyFile = m_key->path();
    if (keyFile.isEmpty()) {
        m_output->setPlainText(tr("Choose a key file first."));
        return;
    }

    /* A locked key needs its passphrase before the public half can be read
     * (same behaviour as `tesseract-crypt info`). */
    QString passphrase;
    for (int attempt = 0; attempt < kMaxPassphraseAttempts; ++attempt) {
        const KeyResult result = runBlocking<KeyResult>(
            this, tr("Reading the key..."), [&] { return keyInfo(keyFile, passphrase); });
        if (result.ok) {
            const QString kind = !result.isSecret ? tr("public")
                                                  : (result.isLocked ? tr("private (locked)")
                                                                     : tr("private (unlocked)"));
            m_output->setPlainText(tr("key file    : %1\ntype        : %2\nfingerprint : %3")
                                       .arg(keyFile, kind, result.fingerprint));
            return;
        }
        if (result.needsPassphrase) {
            bool accepted = false;
            passphrase = askPassphrase(this, keyFile, result.wrongPassphrase, &accepted);
            if (!accepted) break;
            continue;
        }
        m_output->setPlainText(result.message);
        return;
    }
    m_output->setPlainText(tr("Cancelled."));
}

} /* namespace tessgui */
