/*
 * aboutdialog.h — the "About Tesseract Crypt" box.
 *
 * It shows the logo of tesseract-crypt, the version of the library and of
 * Qt, and the short description of the program.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TESSGUI_ABOUTDIALOG_H
#define TESSGUI_ABOUTDIALOG_H

class QMessageBox;
class QWidget;

namespace tessgui {

/** Build the about box; the caller owns it. */
QMessageBox *createAboutBox(QWidget *parent);

/** Show the about box as a modal dialog. */
void showAboutDialog(QWidget *parent);

} /* namespace tessgui */

#endif /* TESSGUI_ABOUTDIALOG_H */
