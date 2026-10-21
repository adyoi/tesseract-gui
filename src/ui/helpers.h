/*
 * helpers.h — small shared pieces of interface behaviour.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TESSGUI_HELPERS_H
#define TESSGUI_HELPERS_H

#include <QEventLoop>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QString>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

#include <functional>

namespace tessgui {

/**
 * Run a blocking operation on a worker thread while the interface stays
 * alive: encryption and decryption of large files can take a while, and the
 * library reports no progress, so the dialog is deliberately unquantified.
 */
template <typename R>
R runBlocking(QWidget *parent, const QString &label, const std::function<R()> &op) {
    QProgressDialog progress(label, QString(), 0, 0, parent);
    progress.setWindowTitle(QObject::tr("Working"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setCancelButton(nullptr);
    progress.setMinimumDuration(0);
    progress.show();

    QFutureWatcher<R> watcher;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<R>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run(op));
    loop.exec();
    progress.close();
    return watcher.result();
}

/** Ask for a key passphrase.  *accepted is false when the user cancelled. */
inline QString askPassphrase(QWidget *parent, const QString &keyFile, bool retry,
                             bool *accepted) {
    const QString name = QFileInfo(keyFile).fileName().isEmpty() ? keyFile
                                                                : QFileInfo(keyFile).fileName();
    const QString label =
        retry ? QObject::tr("Wrong passphrase for %1 — try again:").arg(name)
              : QObject::tr("Passphrase for %1:").arg(name);
    bool ok = false;
    const QString text = QInputDialog::getText(parent, QObject::tr("Passphrase"), label,
                                               QLineEdit::Password, QString(), &ok);
    if (accepted) *accepted = ok;
    return ok ? text : QString();
}

/** Ask for a fresh passphrase twice (key generation, passphrase encryption). */
inline QString askNewPassphrase(QWidget *parent, const QString &title, bool *accepted) {
    bool ok = false;
    const QString first = QInputDialog::getText(parent, title, QObject::tr("Passphrase:"),
                                                QLineEdit::Password, QString(), &ok);
    if (!ok) {
        if (accepted) *accepted = false;
        return QString();
    }
    const QString second = QInputDialog::getText(
        parent, title, QObject::tr("Confirm passphrase:"), QLineEdit::Password, QString(), &ok);
    if (!ok || first != second) {
        if (ok) {
            QMessageBox::warning(parent, title,
                                 QObject::tr("The two passphrases do not match."));
        }
        if (accepted) *accepted = false;
        return QString();
    }
    if (accepted) *accepted = true;
    return first;
}

/** Ask before replacing an existing file (the library would overwrite it). */
inline bool confirmOverwrite(QWidget *parent, const QString &path) {
    if (path.isEmpty() || !QFileInfo::exists(path)) return true;
    const auto answer = QMessageBox::question(
        parent, QObject::tr("Overwrite"),
        QObject::tr("%1 already exists.\n\nReplace it?").arg(QFileInfo(path).fileName()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return answer == QMessageBox::Yes;
}

} /* namespace tessgui */

#endif /* TESSGUI_HELPERS_H */
