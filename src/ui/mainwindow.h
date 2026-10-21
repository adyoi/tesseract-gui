/*
 * mainwindow.h — the main form: File | Edit | Tools | Help, the Encrypt /
 * Decrypt switch and the four rows of the sketch.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TESSGUI_MAINWINDOW_H
#define TESSGUI_MAINWINDOW_H

#include <QMainWindow>

#include "ui/settings.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

namespace tessgui {

class FileRow;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    enum class Mode { Encrypt, Decrypt };

    explicit MainWindow(QWidget *parent = nullptr);

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    void setSourceFile(const QString &path);
    void setDestinationFile(const QString &path);
    void setKeyPaths(const QString &publicKey, const QString &privateKey);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onModeChanged();
    void onSourceChanged();
    void onRun();
    void onKeygen();
    void onOptions();
    void onAbout();
    void onAboutQt();

private:
    void buildMenus();
    void buildForm();
    void applySettings();
    void suggestDestination();
    void runEncrypt();
    void runDecrypt();
    void report(const QString &message, bool ok);
    void warn(const QString &message);

    Mode m_mode = Mode::Encrypt;
    GuiSettings m_settings;

    QRadioButton *m_encryptRadio = nullptr;
    QRadioButton *m_decryptRadio = nullptr;

    FileRow *m_source = nullptr;
    FileRow *m_destination = nullptr;
    FileRow *m_keyPublic = nullptr;
    FileRow *m_keyPrivate = nullptr;

    QCheckBox *m_passphraseCheck = nullptr;
    QWidget *m_passphraseRow = nullptr;
    QLineEdit *m_passphraseEdit = nullptr;

    QPushButton *m_keygenButton = nullptr;
    QPushButton *m_runButton = nullptr;
    QLabel *m_status = nullptr;
};

} /* namespace tessgui */

#endif /* TESSGUI_MAINWINDOW_H */
