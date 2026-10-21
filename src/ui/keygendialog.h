/*
 * keygendialog.h — create a key pair (tess_keygen / tess_keygen_locked).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TESSGUI_KEYGENDIALOG_H
#define TESSGUI_KEYGENDIALOG_H

#include <QDialog>

#include "ui/settings.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace tessgui {

class FileRow;

class KeygenDialog : public QDialog {
    Q_OBJECT

public:
    explicit KeygenDialog(QWidget *parent = nullptr);

private slots:
    void generate();
    void updateEnabled();

private:
    GuiSettings m_settings;
    FileRow *m_base = nullptr;
    QCheckBox *m_lock = nullptr;
    QLineEdit *m_passphrase = nullptr;
    QLineEdit *m_confirm = nullptr;
    QSpinBox *m_ops = nullptr;
    QSpinBox *m_mem = nullptr;
    QLabel *m_result = nullptr;
};

} /* namespace tessgui */

#endif /* TESSGUI_KEYGENDIALOG_H */
