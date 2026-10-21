/*
 * iconexport.h — write a Windows icon file.
 *
 * The icon of the executable is built from the same artwork the window
 * uses.  The file is written once and kept in the repository, so that a
 * checkout does not depend on a converter being installed.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TESSGUI_ICONEXPORT_H
#define TESSGUI_ICONEXPORT_H

#include <QString>

#include <initializer_list>

class QIcon;

namespace tessgui {

/**
 * Write @p icon as a Windows .ico file with the given square sizes.
 * Returns an empty string on success, a message otherwise.
 */
QString exportIconFile(const QIcon &icon, const QString &path,
                        std::initializer_list<int> sizes = {16, 24, 32, 48, 64, 128, 256});

} /* namespace tessgui */

#endif /* TESSGUI_ICONEXPORT_H */
