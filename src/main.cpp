/*
 * main.cpp — tesseract-gui: a Qt6 front-end for libtesseract_crypt.
 *
 * Besides the interactive window the program understands three options used
 * by the test/CI path, where no display is available:
 *
 *   --screenshot FILE   render one window to a PNG file and exit
 *   --dialog NAME       which window (main, keygen, options, pubkey, sign,
 *                       verify, inspect, info, text, textdecrypt)
 *   --mode encrypt|decrypt   mode of the main window
 *
 * SPDX-License-Identifier: MIT
 */
#include "backend/tessbackend.h"
#include "iconexport.h"
#include "ui/aboutdialog.h"
#include "ui/keygendialog.h"
#include "ui/mainwindow.h"
#include "ui/optionsdialog.h"
#include "ui/textcryptdialog.h"
#include "ui/toolsdialogs.h"

#include <sodium.h>

#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QStringList>
#include <QWidget>

#include <cstdio>

namespace {

/** The logo of tesseract-crypt, compiled into the program. */
QIcon logoIcon() {
    return QIcon(QStringLiteral(":/logo.svg"));
}

QString argValue(const QStringList &args, const QString &name) {
    const int index = args.indexOf(name);
    if (index < 0 || index + 1 >= args.size()) return QString();
    return args.at(index + 1);
}

} /* namespace */

int main(int argc, char **argv) {
    if (sodium_init() < 0) {
        std::fputs("tesseract-gui: libsodium initialization failed\n", stderr);
        return 2;
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("tesseract-gui"));
    QApplication::setOrganizationName(QStringLiteral("tesseract-crypt"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QApplication::setApplicationDisplayName(QStringLiteral("Tesseract Crypt"));
    QApplication::setWindowIcon(logoIcon());

    const QStringList args = QApplication::arguments();

    /* Write the icon of the executable; the file is kept in the repository. */
    const QString iconPath = argValue(args, QStringLiteral("--export-icon"));
    if (!iconPath.isEmpty()) {
        const QString error = tessgui::exportIconFile(logoIcon(), iconPath);
        if (!error.isEmpty()) {
            std::fprintf(stderr, "tesseract-gui: %s\n", qPrintable(error));
            return 1;
        }
        return 0;
    }

    const QString screenshot = argValue(args, QStringLiteral("--screenshot"));
    if (!screenshot.isEmpty()) {
        const QString name = argValue(args, QStringLiteral("--dialog"));
        const QString mode = argValue(args, QStringLiteral("--mode"));

        QWidget *target = nullptr;
        if (name.isEmpty() || name == QLatin1String("main")) {
            auto *window = new tessgui::MainWindow;
            window->setMode(mode == QLatin1String("decrypt") ? tessgui::MainWindow::Mode::Decrypt
                                                             : tessgui::MainWindow::Mode::Encrypt);
            const QString source = argValue(args, QStringLiteral("--source"));
            const QString destination = argValue(args, QStringLiteral("--destination"));
            if (!source.isEmpty()) window->setSourceFile(source);
            if (!destination.isEmpty()) window->setDestinationFile(destination);
            window->setKeyPaths(argValue(args, QStringLiteral("--key-public")),
                                argValue(args, QStringLiteral("--key-private")));
            target = window;
        } else if (name == QLatin1String("keygen")) {
            target = new tessgui::KeygenDialog;
        } else if (name == QLatin1String("options")) {
            target = new tessgui::OptionsDialog;
        } else if (name == QLatin1String("pubkey")) {
            target = new tessgui::PubkeyDialog;
        } else if (name == QLatin1String("sign")) {
            target = new tessgui::SignDialog;
        } else if (name == QLatin1String("verify")) {
            target = new tessgui::VerifyDialog;
        } else if (name == QLatin1String("inspect")) {
            target = new tessgui::InspectDialog;
        } else if (name == QLatin1String("info")) {
            target = new tessgui::InfoDialog;
        } else if (name == QLatin1String("text")) {
            target = new tessgui::TextCryptDialog(tessgui::TextCryptDialog::Mode::Encrypt);
        } else if (name == QLatin1String("textdecrypt")) {
            target = new tessgui::TextCryptDialog(tessgui::TextCryptDialog::Mode::Decrypt);
        } else if (name == QLatin1String("about")) {
            target = tessgui::createAboutBox(nullptr);
        } else {
            std::fprintf(stderr, "tesseract-gui: unknown --dialog '%s'\n", qPrintable(name));
            return 2;
        }

        target->show();
        app.processEvents();
        app.processEvents();
        const bool saved = target->grab().save(screenshot, "PNG");
        delete target;
        if (!saved) {
            std::fprintf(stderr, "tesseract-gui: cannot write %s\n", qPrintable(screenshot));
            return 1;
        }
        return 0;
    }

    tessgui::MainWindow window;
    window.show();
    return app.exec();
}
