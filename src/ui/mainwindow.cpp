/*
 * mainwindow.cpp — the main form.
 *
 * The window is the sketch: a menu bar, the Encrypt/Decrypt switch, the
 * Source / Destination / Key Public / Key Private rows and the Keygen
 * button.  Everything that does not fit on the sketch (detached signatures,
 * inspection, text mode, format options) lives in the Tools menu and in
 * Edit -> Options.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui/mainwindow.h"

#include "backend/tessbackend.h"
#include "ui/aboutdialog.h"
#include "ui/filerow.h"
#include "ui/helpers.h"
#include "ui/keygendialog.h"
#include "ui/optionsdialog.h"
#include "ui/textcryptdialog.h"
#include "ui/toolsdialogs.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>

namespace tessgui {

namespace {

constexpr int kMaxPassphraseAttempts = 3;

} /* namespace */

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    m_settings = GuiSettings::load();

    setWindowTitle(tr("Tesseract Crypt"));
    setAcceptDrops(true);
    setMinimumWidth(620);

    /* One label carries every status message: a single place to look, and it
     * survives the dialogs that open on top of it. */
    m_status = new QLabel(this);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addWidget(m_status, 1);

    buildMenus();
    buildForm();
    applySettings();
    setMode(Mode::Encrypt);

    m_status->setText(tr("Ready"));
}

/* ------------------------------------------------------------------ */
/* Menus                                                               */
/* ------------------------------------------------------------------ */

void MainWindow::buildMenus() {
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    QAction *exit = fileMenu->addAction(tr("E&xit"));
    exit->setShortcut(QKeySequence::Quit);
    connect(exit, &QAction::triggered, this, &QWidget::close);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction *options = editMenu->addAction(tr("&Options..."));
    options->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    connect(options, &QAction::triggered, this, &MainWindow::onOptions);

    QMenu *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    QAction *keygen = toolsMenu->addAction(tr("&Key pair..."));
    keygen->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));
    connect(keygen, &QAction::triggered, this, &MainWindow::onKeygen);

    connect(toolsMenu->addAction(tr("Public key from private key...")), &QAction::triggered, this,
            [this] {
                PubkeyDialog dialog(this);
                if (!m_source->path().isEmpty()) dialog.setSourceFile(m_source->path());
                dialog.exec();
            });
    connect(toolsMenu->addAction(tr("Sign a file...")), &QAction::triggered, this, [this] {
        SignDialog dialog(this);
        if (!m_source->path().isEmpty()) dialog.setSourceFile(m_source->path());
        dialog.exec();
    });
    connect(toolsMenu->addAction(tr("Verify a signature...")), &QAction::triggered, this, [this] {
        VerifyDialog dialog(this);
        if (!m_source->path().isEmpty()) dialog.setSourceFile(m_source->path());
        dialog.exec();
    });
    connect(toolsMenu->addAction(tr("Inspect a message...")), &QAction::triggered, this, [this] {
        InspectDialog dialog(this);
        if (!m_source->path().isEmpty()) dialog.setSourceFile(m_source->path());
        dialog.exec();
    });
    connect(toolsMenu->addAction(tr("Key information...")), &QAction::triggered, this, [this] {
        InfoDialog dialog(this);
        if (!m_source->path().isEmpty()) dialog.setSourceFile(m_source->path());
        dialog.exec();
    });

    toolsMenu->addSeparator();
    connect(toolsMenu->addAction(tr("Encrypt text...")), &QAction::triggered, this,
            [this] { TextCryptDialog dialog(TextCryptDialog::Mode::Encrypt, this); dialog.exec(); });
    connect(toolsMenu->addAction(tr("Decrypt text...")), &QAction::triggered, this,
            [this] { TextCryptDialog dialog(TextCryptDialog::Mode::Decrypt, this); dialog.exec(); });

    toolsMenu->addSeparator();
    connect(toolsMenu->addAction(tr("Batch encrypt from directory...")), &QAction::triggered, this, &MainWindow::onBatchEncrypt);
    connect(toolsMenu->addAction(tr("Batch decrypt from directory...")), &QAction::triggered, this, &MainWindow::onBatchDecrypt);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    connect(helpMenu->addAction(tr("&About")), &QAction::triggered, this, &MainWindow::onAbout);
    connect(helpMenu->addAction(tr("About &Qt")), &QAction::triggered, this, &MainWindow::onAboutQt);
}

/* ------------------------------------------------------------------ */
/* Form                                                                */
/* ------------------------------------------------------------------ */

void MainWindow::buildForm() {
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);

    /* [x] Encrypt   [ ] Decrypt */
    auto *modeRow = new QHBoxLayout;
    m_encryptRadio = new QRadioButton(tr("Encrypt"), central);
    m_decryptRadio = new QRadioButton(tr("Decrypt"), central);
    m_encryptRadio->setChecked(true);
    modeRow->addWidget(m_encryptRadio);
    modeRow->addWidget(m_decryptRadio);
    modeRow->addStretch(1);
    layout->addLayout(modeRow);

    layout->addSpacing(4);

    /* Source / Destination */
    m_source = new FileRow(tr("Source"), FileRow::OpenFile, central);
    m_source->setDialogTitle(tr("Select the file to process"));
    layout->addWidget(m_source);

    m_destination = new FileRow(tr("Destination"), FileRow::SaveFile, central);
    m_destination->setDialogTitle(tr("Select where to write the result"));
    layout->addWidget(m_destination);

    /* Key Public (encrypt) / Key Private (decrypt) */
    m_keyPublic = new FileRow(tr("Key Public"), FileRow::MultiOpenFile, central);
    m_keyPublic->setDialogTitle(tr("Select one or more recipient public keys"));
    m_keyPublic->setPlaceholder(tr("recipient .pub key (several allowed)"));
    layout->addWidget(m_keyPublic);

    m_keyPrivate = new FileRow(tr("Key Private"), FileRow::OpenFile, central);
    m_keyPrivate->setDialogTitle(tr("Select your private key"));
    m_keyPrivate->setPlaceholder(tr("your .key file"));
    layout->addWidget(m_keyPrivate);

    /* optional passphrase mode */
    m_passphraseRow = new QWidget(central);
    auto *passLayout = new QHBoxLayout(m_passphraseRow);
    passLayout->setContentsMargins(0, 0, 0, 0);
    passLayout->setSpacing(6);
    m_passphraseCheck = new QCheckBox(tr("Use a passphrase"), m_passphraseRow);
    m_passphraseCheck->setMinimumWidth(110);
    m_passphraseEdit = new QLineEdit(m_passphraseRow);
    m_passphraseEdit->setEchoMode(QLineEdit::Password);
    m_passphraseEdit->setPlaceholderText(tr("passphrase (asked for again when needed)"));
    passLayout->addWidget(m_passphraseCheck);
    passLayout->addWidget(m_passphraseEdit, 1);
    layout->addWidget(m_passphraseRow);

    /* PE split + batch options */
    auto *peRow = new QHBoxLayout;
    m_peCheck = new QCheckBox(tr("Use PE-split for large files"), central);
    m_peSpin = new QSpinBox(central);
    m_peSpin->setRange(1, 1024);
    m_peSpin->setValue(256);
    m_peSpin->setSuffix(tr(" MiB"));
    m_peSpin->setEnabled(false);
    peRow->addWidget(m_peCheck);
    peRow->addWidget(m_peSpin);
    peRow->addStretch(1);
    layout->addLayout(peRow);
    connect(m_peCheck, &QCheckBox::toggled, m_peSpin, &QSpinBox::setEnabled);

    m_subdirsCheck = new QCheckBox(tr("Include subdirectories (batch)"), central);
    layout->addWidget(m_subdirsCheck);

    layout->addSpacing(10);

    /* [ Keygen ]                                    [ Encrypt ] */
    auto *buttonRow = new QHBoxLayout;
    m_keygenButton = new QPushButton(tr("Keygen"), central);
    m_runButton = new QPushButton(tr("Encrypt"), central);
    m_runButton->setDefault(true);
    m_runButton->setMinimumWidth(140);
    buttonRow->addWidget(m_keygenButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_runButton);
    layout->addLayout(buttonRow);

    setCentralWidget(central);

    connect(m_encryptRadio, &QRadioButton::toggled, this, &MainWindow::onModeChanged);
    connect(m_keygenButton, &QPushButton::clicked, this, &MainWindow::onKeygen);
    connect(m_runButton, &QPushButton::clicked, this, &MainWindow::onRun);
    connect(m_source, &FileRow::changed, this, &MainWindow::onSourceChanged);
    connect(m_passphraseCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_passphraseEdit->setEnabled(on);
        m_keyPublic->setEnabled(m_mode != Mode::Encrypt || !on);
        m_keyPrivate->setEnabled(m_mode != Mode::Decrypt || !on);
    });
    m_passphraseEdit->setEnabled(false);
}

void MainWindow::setMode(Mode mode) {
    m_mode = mode;
    const bool encrypt = (mode == Mode::Encrypt);
    if (encrypt) {
        m_encryptRadio->setChecked(true);
    } else {
        m_decryptRadio->setChecked(true);
    }

    /* Key Private only appears in Decrypt, Key Public only in Encrypt. */
    m_keyPublic->setVisible(encrypt);
    m_keyPrivate->setVisible(!encrypt);
    m_keyPublic->setEnabled(encrypt && !m_passphraseCheck->isChecked());
    m_keyPrivate->setEnabled(!encrypt && !m_passphraseCheck->isChecked());

    m_passphraseCheck->setText(encrypt ? tr("Encrypt with a passphrase")
                                       : tr("Decrypt with a passphrase"));
    m_runButton->setText(encrypt ? tr("Encrypt") : tr("Decrypt"));
    setWindowTitle(encrypt ? tr("Tesseract Crypt — Encrypt")
                           : tr("Tesseract Crypt — Decrypt"));
    suggestDestination();
}

void MainWindow::onModeChanged() {
    setMode(m_encryptRadio->isChecked() ? Mode::Encrypt : Mode::Decrypt);
    m_status->setStyleSheet(QString());
    m_status->setText(m_mode == Mode::Encrypt
                          ? tr("Encrypting for the owners of the selected public keys")
                          : tr("Decrypting with your private key"));
}

void MainWindow::onSourceChanged() {
    suggestDestination();
}

void MainWindow::suggestDestination() {
    if (m_source == nullptr || m_destination == nullptr) return;
    const QString source = m_source->path();
    if (source.isEmpty()) return;
    const QString current = m_destination->path();
    if (!current.isEmpty()) return; /* never overwrite a choice the user made */
    m_destination->setPath(m_mode == Mode::Encrypt ? suggestEncryptedName(source)
                                                   : suggestDecryptedName(source));
}

void MainWindow::setSourceFile(const QString &path) {
    m_source->setPath(path);
    m_destination->setPath(QString());
    suggestDestination();
}

void MainWindow::setDestinationFile(const QString &path) {
    m_destination->setPath(path);
}

void MainWindow::setKeyPaths(const QString &publicKey, const QString &privateKey) {
    if (!publicKey.isEmpty()) m_keyPublic->setPaths(QStringList{publicKey});
    if (!privateKey.isEmpty()) m_keyPrivate->setPath(privateKey);
}

void MainWindow::applySettings() {
    m_settings = GuiSettings::load();
    m_passphraseCheck->setChecked(false);
    m_passphraseEdit->clear();
    if (m_peCheck) m_peCheck->setChecked(m_settings.usePe);
    if (m_peSpin) m_peSpin->setValue(m_settings.peChunkMiB == 0 ? 256 : m_settings.peChunkMiB);
    if (m_subdirsCheck) m_subdirsCheck->setChecked(m_settings.includeSubdirs);
    if (!m_settings.senderKey.isEmpty()) {
        m_status->setStyleSheet(QString());
        m_status->setText(tr("Messages are signed with %1")
                              .arg(QFileInfo(m_settings.senderKey).fileName()));
    }
}

/* ------------------------------------------------------------------ */
/* Actions                                                             */
/* ------------------------------------------------------------------ */

void MainWindow::onRun() {
    if (m_mode == Mode::Encrypt) {
        runEncrypt();
    } else {
        runDecrypt();
    }
}

void MainWindow::runEncrypt() {
    const QString source = m_source->path();
    if (source.isEmpty()) {
        warn(tr("Choose the file to encrypt first."));
        return;
    }
    if (!QFileInfo::exists(source)) {
        warn(tr("%1 does not exist.").arg(source));
        return;
    }
    QString destination = m_destination->path();
    if (destination.isEmpty()) {
        destination = suggestEncryptedName(source);
        m_destination->setPath(destination);
    }
    const QFileInfo out(destination);
    if (out.exists() && out.isDir()) {
        warn(tr("%1 is a directory.").arg(destination));
        return;
    }
    const bool usePassphrase = m_passphraseCheck->isChecked();
    QStringList recipients;
    QString passphrase;
    if (usePassphrase) {
        passphrase = m_passphraseEdit->text();
        if (passphrase.isEmpty()) {
            bool accepted = false;
            passphrase = askNewPassphrase(this, tr("Encrypt with a passphrase"), &accepted);
            if (!accepted || passphrase.isEmpty()) return;
            m_passphraseEdit->setText(passphrase);
        }
    } else {
        recipients = m_keyPublic->paths();
        if (recipients.isEmpty()) {
            warn(tr("Choose at least one recipient public key, or tick \"Use a passphrase\"."));
            return;
        }
        for (const QString &recipient : recipients) {
            if (!QFileInfo::exists(recipient)) {
                warn(tr("%1 does not exist.").arg(recipient));
                return;
            }
        }
    }
    if (m_settings.confirmOverwrite && !confirmOverwrite(this, destination)) return;

    const bool sign = m_settings.signWhenPossible && !recipients.isEmpty() &&
                      !m_settings.senderKey.isEmpty();
    const quint32 chunk = m_settings.chunkSize;
    const bool armor = m_settings.armor;
    const QString senderKey = m_settings.senderKey;
    const QString fileName = QFileInfo(source).fileName();

    /* PE split: a container of sealed chunks, marked with "PE" so the
     * decrypt side recognises it without an extra flag. */
    const bool usePe = m_peCheck && m_peCheck->isChecked();
    const quint32 peChunkMiB = m_peSpin ? static_cast<quint32>(m_peSpin->value()) : 256u;

    QString senderPassphrase;
    for (int attempt = 0; attempt < kMaxPassphraseAttempts; ++attempt) {
        const Result result = runBlocking<Result>(
            this, tr("Encrypting %1...").arg(fileName), [&] {
                if (usePe) {
                    return sealFilePe(source, destination, recipients, senderKey,
                                      senderPassphrase, sign, usePassphrase, passphrase,
                                      chunk, armor, peChunkMiB);
                }
                return sealFile(source, destination, recipients, senderKey, senderPassphrase,
                                sign, usePassphrase, passphrase, chunk, armor);
            });
        if (result.ok) {
            report(result.message, true);
            return;
        }
        if (result.needsPassphrase && !senderKey.isEmpty()) {
            bool accepted = false;
            senderPassphrase =
                askPassphrase(this, senderKey, result.wrongPassphrase, &accepted);
            if (!accepted) break;
            continue;
        }
        warn(result.message);
        return;
    }
    m_status->setText(tr("Encryption cancelled"));
}

void MainWindow::runDecrypt() {
    const QString source = m_source->path();
    if (source.isEmpty()) {
        warn(tr("Choose the file to decrypt first."));
        return;
    }
    if (!QFileInfo::exists(source)) {
        warn(tr("%1 does not exist.").arg(source));
        return;
    }
    QString destination = m_destination->path();
    if (destination.isEmpty()) {
        destination = suggestDecryptedName(source);
        m_destination->setPath(destination);
    }
    if (m_settings.confirmOverwrite && !confirmOverwrite(this, destination)) return;

    const bool usePassphrase = m_passphraseCheck->isChecked();
    QString keyFile;
    QString passphrase;
    if (usePassphrase) {
        passphrase = m_passphraseEdit->text();
        if (passphrase.isEmpty()) {
            bool accepted = false;
            passphrase = askNewPassphrase(this, tr("Decrypt with a passphrase"), &accepted);
            if (!accepted || passphrase.isEmpty()) return;
            m_passphraseEdit->setText(passphrase);
        }
    } else {
        keyFile = m_keyPrivate->path();
        if (keyFile.isEmpty()) {
            warn(tr("Choose your private key, or tick \"Use a passphrase\"."));
            return;
        }
        if (!QFileInfo::exists(keyFile)) {
            warn(tr("%1 does not exist.").arg(keyFile));
            return;
        }
    }

    const QString signer = m_settings.requireSigner ? m_settings.signerKey : QString();
    const QString fileName = QFileInfo(source).fileName();

    QString keyPassphrase;
    for (int attempt = 0; attempt < kMaxPassphraseAttempts; ++attempt) {
        const OpenResult result = runBlocking<OpenResult>(
            this, tr("Decrypting %1...").arg(fileName), [&] {
                return openFile(source, destination, keyFile, keyPassphrase, signer,
                                usePassphrase, passphrase);
            });
        if (result.ok) {
            report(result.message, true);
            return;
        }
        if (result.needsPassphrase && !keyFile.isEmpty()) {
            bool accepted = false;
            keyPassphrase = askPassphrase(this, keyFile, result.wrongPassphrase, &accepted);
            if (!accepted) break;
            continue;
        }
        warn(result.message);
        return;
    }
    m_status->setText(tr("Decryption cancelled"));
}

void MainWindow::onKeygen() {
    KeygenDialog dialog(this);
    dialog.exec();
}

void MainWindow::onOptions() {
    OptionsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        applySettings();
        report(tr("Options saved"), true);
    }
}

void MainWindow::onAbout() {
    tessgui::showAboutDialog(this);
}

void MainWindow::onAboutQt() {
    QMessageBox::aboutQt(this, tr("About Qt"));
}



void MainWindow::onBatchEncrypt() {
    m_settings = GuiSettings::load();
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select directory to encrypt"), m_settings.batchInputDir.isEmpty() ? m_settings.lastDirectory : m_settings.batchInputDir, QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) return;
    m_settings.batchInputDir = dir;
    QString outDir = QFileDialog::getExistingDirectory(this, tr("Select output directory"), m_settings.batchOutputDir.isEmpty() ? dir : m_settings.batchOutputDir, QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (outDir.isEmpty()) outDir = dir;
    m_settings.batchOutputDir = outDir;
    bool recursive = m_subdirsCheck ? m_subdirsCheck->isChecked() : m_settings.includeSubdirs;
    m_settings.includeSubdirs = recursive;
    bool usePe = m_peCheck ? m_peCheck->isChecked() : m_settings.usePe;
    m_settings.usePe = usePe;
    quint32 peMb = m_peSpin ? (quint32)m_peSpin->value() : m_settings.peChunkMiB;
    m_settings.peChunkMiB = peMb;
    m_settings.save();

    bool usePassphraseMode = m_passphraseCheck && m_passphraseCheck->isChecked();
    QStringList recipients;
    if (usePassphraseMode) {
        QString pass = m_passphraseEdit->text();
        if (pass.isEmpty()) {
            bool accepted = false;
            pass = askNewPassphrase(this, tr("Batch encrypt with a passphrase"), &accepted);
            if (!accepted || pass.isEmpty()) return;
            m_passphraseEdit->setText(pass);
        }
    } else {
        recipients = m_keyPublic ? m_keyPublic->paths() : QStringList();
        if (recipients.isEmpty()) { QMessageBox::warning(this, tr("Tesseract Crypt"), tr("Select at least one recipient public key, or tick \\\"Use a passphrase\\\".")); return; }
        for (const QString &r : recipients) {
            if (!QFileInfo::exists(r)) { QMessageBox::warning(this, tr("Tesseract Crypt"), tr("%1 does not exist.").arg(r)); return; }
        }
    }
    QString senderKey = (m_settings.signWhenPossible && !m_settings.senderKey.isEmpty()) ? m_settings.senderKey : QString();
    QString senderPass;
    if (!senderKey.isEmpty()) senderPass = QString();

    QDir d(dir);
    QList<QFileInfo> files;
    if (recursive) {
        QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) files << QFileInfo(it.next());
    } else {
        files = d.entryInfoList(QDir::Files);
    }
    if (files.isEmpty()) { QMessageBox::information(this, tr("Tesseract Crypt"), tr("No files found.")); return; }

    QProgressDialog prog(tr("Encrypting..."), tr("Cancel"), 0, files.size(), this);
    prog.setWindowModality(Qt::WindowModal);
    int done = 0;
    for (const auto &fi : files) {
        if (prog.wasCanceled()) break;
        prog.setLabelText(fi.fileName());
        prog.setValue(done);
        QString in = fi.absoluteFilePath();
        QString rel = d.relativeFilePath(fi.absolutePath());
        QString outPath = outDir;
        if (recursive && !rel.isEmpty() && rel != ".") {
            QDir(outDir).mkpath(rel);
            outPath = QDir(outDir).filePath(rel + "/" + fi.fileName() + ".enc");
        } else {
            outPath = QDir(outDir).filePath(fi.fileName() + ".enc");
        }
        QString pass = usePassphraseMode ? m_passphraseEdit->text() : QString();
        Result r;
        if (usePe) {
            r = sealFilePe(in, outPath, recipients, senderKey, senderPass,
                           m_settings.signWhenPossible && !senderKey.isEmpty(),
                           usePassphraseMode, pass, m_settings.chunkSize,
                           m_settings.armor, peMb);
        } else {
            r = sealFile(in, outPath, recipients, senderKey, senderPass,
                          m_settings.signWhenPossible && !senderKey.isEmpty(),
                          usePassphraseMode, pass, m_settings.chunkSize,
                          m_settings.armor);
        }
        if (!r.ok) { m_status->setText(r.message); }
        done++;
    }
    prog.setValue(files.size());
    m_status->setText(tr("Batch encrypt done"));
}

void MainWindow::onBatchDecrypt() {
    m_settings = GuiSettings::load();
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select directory to decrypt"), m_settings.batchInputDir.isEmpty() ? m_settings.lastDirectory : m_settings.batchInputDir, QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) return;
    m_settings.batchInputDir = dir;
    QString outDir = QFileDialog::getExistingDirectory(this, tr("Select output directory"), m_settings.batchOutputDir.isEmpty() ? dir : m_settings.batchOutputDir, QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (outDir.isEmpty()) outDir = dir;
    m_settings.batchOutputDir = outDir;
    bool recursive = m_subdirsCheck ? m_subdirsCheck->isChecked() : m_settings.includeSubdirs;
    m_settings.includeSubdirs = recursive;
    m_settings.save();

    QString keyFile = m_keyPrivate ? m_keyPrivate->path() : QString();
    QString reqSigner;
    if (m_settings.requireSigner && !m_settings.signerKey.isEmpty()) reqSigner = m_settings.signerKey;
    bool usePass = m_passphraseCheck && m_passphraseCheck->isChecked();
    QString pass = usePass ? m_passphraseEdit->text() : QString();
    if (!usePass && keyFile.isEmpty()) { QMessageBox::warning(this, tr("Tesseract Crypt"), tr("Select a private key or use passphrase.")); return; }

    QDir d(dir);
    QList<QFileInfo> files;
    if (recursive) {
        QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) files << QFileInfo(it.next());
    } else {
        files = d.entryInfoList(QDir::Files);
    }
    if (files.isEmpty()) { QMessageBox::information(this, tr("Tesseract Crypt"), tr("No files found.")); return; }

    QProgressDialog prog(tr("Decrypting..."), tr("Cancel"), 0, files.size(), this);
    prog.setWindowModality(Qt::WindowModal);
    int done = 0;
    for (const auto &fi : files) {
        if (prog.wasCanceled()) break;
        prog.setLabelText(fi.fileName());
        prog.setValue(done);
        QString in = fi.absoluteFilePath();
        QString rel = d.relativeFilePath(fi.absolutePath());
        QString base = fi.fileName();
        if (base.endsWith(".enc")) base.chop(4);
        else base += ".dec";
        QString outPath = outDir;
        if (recursive && !rel.isEmpty() && rel != ".") {
            QDir(outDir).mkpath(rel);
            outPath = QDir(outDir).filePath(rel + "/" + base);
        } else {
            outPath = QDir(outDir).filePath(base);
        }
        OpenResult r = openFile(in, outPath, keyFile, QString(), reqSigner, usePass, pass);
        if (!r.ok) { m_status->setText(r.message); }
        done++;
    }
    prog.setValue(files.size());
    m_status->setText(tr("Batch decrypt done"));
}

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

void MainWindow::report(const QString &message, bool ok) {
    m_status->setStyleSheet(ok ? QStringLiteral("color: #1a7f37;")
                               : QStringLiteral("color: #b3261e;"));
    m_status->setText(message);
    m_status->setToolTip(message);
}

void MainWindow::warn(const QString &message) {
    report(message, false);
    QMessageBox::warning(this, tr("Tesseract Crypt"), message);
}

/* ------------------------------------------------------------------ */
/* Drag and drop                                                       */
/* ------------------------------------------------------------------ */

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;
    const QString path = urls.first().toLocalFile();
    if (path.isEmpty()) return;
    setSourceFile(path);
    event->acceptProposedAction();
}

} /* namespace tessgui */
