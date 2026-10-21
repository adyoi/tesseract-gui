/*
 * aboutdialog.cpp — the "About Tesseract Crypt" box.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui/aboutdialog.h"

#include "backend/tessbackend.h"

#include <QIcon>
#include <QMessageBox>
#include <QPixmap>
#include <QWidget>

namespace tessgui {

QMessageBox *createAboutBox(QWidget *parent) {
    auto *box = new QMessageBox(parent);
    box->setWindowTitle(QObject::tr("About Tesseract Crypt"));
    box->setIconPixmap(QIcon(QStringLiteral(":/logo.svg")).pixmap(64, 64));
    box->setTextFormat(Qt::RichText);
    box->setText(QObject::tr("<h3>Tesseract Crypt — GUI</h3>"
                              "<p>Hybrid public-key string and file encryption, built on the same "
                              "library as the <b>tesseract-crypt</b> command line tool.</p>"
                              "<p>%1<br>Qt %2</p>"
                              "<p>X25519 key exchange, XChaCha20-Poly1305 AEAD, Argon2id "
                              "passphrase keys, Ed25519 signatures.</p>"
                              "<p>Licensed under the MIT license.</p>")
                       .arg(libraryVersion(), QStringLiteral(QT_VERSION_STR)));
    return box;
}

void showAboutDialog(QWidget *parent) {
    QMessageBox *box = createAboutBox(parent);
    box->exec();
    delete box;
}

} /* namespace tessgui */
