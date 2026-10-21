/*
 * textcryptdialog.h — Tools -> Encrypt text / Decrypt text.
 *
 * The string half of tesseract-crypt: armored output you can paste into a
 * mail, a chat or a configuration file.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TESSGUI_TEXTCRYPTDIALOG_H
#define TESSGUI_TEXTCRYPTDIALOG_H

#include <QDialog>

#include "ui/settings.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace tessgui {

class FileRow;

class TextCryptDialog : public QDialog {
    Q_OBJECT

public:
    enum class Mode { Encrypt, Decrypt };

    explicit TextCryptDialog(Mode mode, QWidget *parent = nullptr);

private slots:
    void run();
    void copyOutput();
    void updateEnabled();

private:
    Mode m_mode;
    GuiSettings m_settings;

    QPlainTextEdit *m_input = nullptr;
    QPlainTextEdit *m_output = nullptr;
    FileRow *m_keyPublic = nullptr;
    FileRow *m_keyPrivate = nullptr;
    QCheckBox *m_passphraseCheck = nullptr;
    QLineEdit *m_passphraseEdit = nullptr;
    QLabel *m_result = nullptr;
    QPushButton *m_runButton = nullptr;
};

} /* namespace tessgui */

#endif /* TESSGUI_TEXTCRYPTDIALOG_H */
