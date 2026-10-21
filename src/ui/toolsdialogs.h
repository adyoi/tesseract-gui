/*
 * toolsdialogs.h — the dialogs behind the Tools menu.
 *
 * These are the features that do not fit on the main form: deriving a public
 * key from a private one, detached signatures, verification, message
 * inspection and key information.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TESSGUI_TOOLSDIALOGS_H
#define TESSGUI_TOOLSDIALOGS_H

#include <QDialog>

#include "ui/settings.h"

class QLabel;
class QTextEdit;

namespace tessgui {

class FileRow;

/** Pubkey: private key in, public key file out. */
class PubkeyDialog : public QDialog {
    Q_OBJECT

public:
    explicit PubkeyDialog(QWidget *parent = nullptr);
    void setSourceFile(const QString &path);

private slots:
    void run();

private:
    GuiSettings m_settings;
    FileRow *m_key = nullptr;
    FileRow *m_out = nullptr;
    QLabel *m_result = nullptr;
};

/** Sign: detached Ed25519ph signature of a file. */
class SignDialog : public QDialog {
    Q_OBJECT

public:
    explicit SignDialog(QWidget *parent = nullptr);
    void setSourceFile(const QString &path);

private slots:
    void run();
    void updateOutput();

private:
    GuiSettings m_settings;
    FileRow *m_key = nullptr;
    FileRow *m_input = nullptr;
    FileRow *m_output = nullptr;
    QLabel *m_result = nullptr;
};

/** Verify: check a detached signature against a public key. */
class VerifyDialog : public QDialog {
    Q_OBJECT

public:
    explicit VerifyDialog(QWidget *parent = nullptr);
    void setSourceFile(const QString &path);

private slots:
    void run();

private:
    FileRow *m_input = nullptr;
    FileRow *m_signature = nullptr;
    FileRow *m_key = nullptr;
    QLabel *m_result = nullptr;
};

/** Inspect: read the header of a message (file or armored). */
class InspectDialog : public QDialog {
    Q_OBJECT

public:
    explicit InspectDialog(QWidget *parent = nullptr);
    void setSourceFile(const QString &path);

private slots:
    void run();

private:
    FileRow *m_input = nullptr;
    QTextEdit *m_output = nullptr;
};

/** Info: what kind of key is this file, and what is its fingerprint? */
class InfoDialog : public QDialog {
    Q_OBJECT

public:
    explicit InfoDialog(QWidget *parent = nullptr);
    void setSourceFile(const QString &path);

private slots:
    void run();

private:
    FileRow *m_key = nullptr;
    QTextEdit *m_output = nullptr;
};

} /* namespace tessgui */

#endif /* TESSGUI_TOOLSDIALOGS_H */
