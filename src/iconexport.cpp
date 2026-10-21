/*
 * iconexport.cpp — write a Windows icon file.
 *
 * The file is an ICONDIR followed by one directory entry and one PNG image
 * per size.  Windows Vista and newer read PNG images inside an icon file,
 * which keeps the file small and the picture sharp.
 *
 * SPDX-License-Identifier: MIT
 */
#include "iconexport.h"

#include <QBuffer>
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QList>

namespace tessgui {

QString exportIconFile(const QIcon &icon, const QString &path, std::initializer_list<int> sizes) {
    const QList<int> wanted(sizes.begin(), sizes.end());
    if (wanted.isEmpty()) return QObject::tr("no icon size was given");

    QList<QByteArray> images;
    for (int size : wanted) {
        const QImage image = icon.pixmap(size, size).toImage();
        if (image.isNull()) return QObject::tr("the icon has no image");

        QByteArray png;
        QBuffer buffer(&png);
        if (!buffer.open(QIODevice::WriteOnly)) return QObject::tr("the image buffer failed");
        if (!image.save(&buffer, "PNG")) return QObject::tr("the PNG image could not be encoded");
        images.append(png);
    }

    QByteArray file;
    const auto appendByte = [&file](quint8 value) { file.append(char(value)); };
    const auto appendU32 = [&file](quint32 value) {
        file.append(char(value & 0xff));
        file.append(char((value >> 8) & 0xff));
        file.append(char((value >> 16) & 0xff));
        file.append(char((value >> 24) & 0xff));
    };

    const int count = images.size();
    appendByte(0); /* reserved */
    appendByte(0);
    appendByte(1); /* type: icon */
    appendByte(0);
    appendByte(quint8(count & 0xff));
    appendByte(quint8((count >> 8) & 0xff));

    quint32 offset = quint32(6 + 16 * count);
    for (int i = 0; i < count; ++i) {
        const int size = wanted.at(i);
        const quint8 dimension = quint8(size >= 256 ? 0 : size); /* 0 means 256 */
        appendByte(dimension);
        appendByte(dimension);
        appendByte(0); /* colours */
        appendByte(0); /* reserved */
        appendByte(1); /* planes */
        appendByte(0);
        appendByte(32); /* bits per pixel */
        appendByte(0);
        appendU32(quint32(images.at(i).size()));
        appendU32(offset);
        offset += quint32(images.at(i).size());
    }
    for (const QByteArray &image : images) file.append(image);

    QFile handle(path);
    if (!handle.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QObject::tr("%1 could not be written: %2").arg(path, handle.errorString());
    }
    if (handle.write(file) != file.size()) {
        return QObject::tr("%1 could not be written completely").arg(path);
    }
    handle.close();
    return QString();
}

} /* namespace tessgui */
