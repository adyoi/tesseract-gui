/*
 * optionsdialog.h — Edit -> Options: the defaults the main form uses.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TESSGUI_OPTIONSDIALOG_H
#define TESSGUI_OPTIONSDIALOG_H

#include <QDialog>

#include "ui/settings.h"

class QCheckBox;
class QSpinBox;

namespace tessgui {

class FileRow;

class OptionsDialog : public QDialog {
    Q_OBJECT

public:
    explicit OptionsDialog(QWidget *parent = nullptr);

private slots:
    void restoreDefaults();
    void saveAndClose();
    void updateEnabled();

private:
    GuiSettings m_settings;
    QCheckBox *m_armor = nullptr;
    QSpinBox *m_chunkKiB = nullptr;
    QCheckBox *m_sign = nullptr;
    FileRow *m_senderKey = nullptr;
    QCheckBox *m_requireSigner = nullptr;
    FileRow *m_signerKey = nullptr;
    QCheckBox *m_lockKeys = nullptr;
    QSpinBox *m_ops = nullptr;
    QSpinBox *m_mem = nullptr;
    QCheckBox *m_confirmOverwrite = nullptr;
};

} /* namespace tessgui */

#endif /* TESSGUI_OPTIONSDIALOG_H */
