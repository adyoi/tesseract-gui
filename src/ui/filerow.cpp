/*
 * filerow.cpp — see filerow.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui/filerow.h"
#include "ui/settings.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>

namespace tessgui {

namespace {
constexpr int kLabelWidth = 110;
const QString kKeyFilter = QObject::tr("Tesseract keys (*.pub *.key);;All files (*)");
const QString kAllFilter = QObject::tr("All files (*)");
} /* namespace */

FileRow::FileRow(const QString &label, Kind kind, QWidget *parent)
    : QWidget(parent), m_kind(kind), m_label(label) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *caption = new QLabel(label, this);
    caption->setMinimumWidth(kLabelWidth);
    caption->setObjectName(QStringLiteral("rowLabel"));
    layout->addWidget(caption);
    m_caption = caption;

    m_edit = new QLineEdit(this);
    m_edit->setClearButtonEnabled(true);
    layout->addWidget(m_edit, 1);

    m_browse = new QToolButton(this);
    m_browse->setText(QStringLiteral("..."));
    m_browse->setToolTip(tr("Browse"));
    m_browse->setAutoRaise(false);
    layout->addWidget(m_browse);

    m_filter = (kind == MultiOpenFile) ? kKeyFilter : kAllFilter;
    connect(m_browse, &QToolButton::clicked, this, &FileRow::browse);
    connect(m_edit, &QLineEdit::textChanged, this, [this] {
        refreshTooltip();
        emit changed();
    });
    connect(m_edit, &QLineEdit::editingFinished, this, &FileRow::changed);
}

void FileRow::setLabel(const QString &label) {
    m_label = label;
    if (m_caption) m_caption->setText(label);
}

void FileRow::setPlaceholder(const QString &text) {
    m_edit->setPlaceholderText(text);
}

QString FileRow::path() const {
    const QStringList list = paths();
    return list.isEmpty() ? QString() : list.first();
}

QStringList FileRow::paths() const {
    QStringList out;
    const QStringList raw = m_edit->text().split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &item : raw) {
        const QString trimmed = item.trimmed();
        if (!trimmed.isEmpty()) out << trimmed;
    }
    return out;
}

void FileRow::setPath(const QString &path) {
    m_edit->setText(path);
}

void FileRow::setPaths(const QStringList &paths) {
    m_edit->setText(paths.join(QStringLiteral("; ")));
}

void FileRow::refreshTooltip() {
    const QStringList list = paths();
    m_edit->setToolTip(list.join(QStringLiteral("\n")));
}

void FileRow::browse() {
    GuiSettings settings = GuiSettings::load();
    QString start = m_edit->text();
    if (start.contains(QLatin1Char(';'))) start = start.section(QLatin1Char(';'), 0, 0);
    start = start.trimmed();
    if (start.isEmpty()) {
        start = settings.lastDirectory;
    } else {
        const QFileInfo info(start);
        start = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    }
    if (start.isEmpty() || !QDir(start).exists()) start = QDir::homePath();

    const QString title = m_dialogTitle.isEmpty() ? m_label : m_dialogTitle;
    QStringList picked;

    switch (m_kind) {
    case OpenFile: {
        const QString file = QFileDialog::getOpenFileName(this, title, start, m_filter);
        if (!file.isEmpty()) picked << file;
        break;
    }
    case MultiOpenFile: {
        picked = QFileDialog::getOpenFileNames(this, title, start, m_filter);
        break;
    }
    case SaveFile: {
        const QString file = QFileDialog::getSaveFileName(this, title, start, m_filter);
        if (!file.isEmpty()) picked << file;
        break;
    }
    case OpenDirectory: {
        const QString dir = QFileDialog::getExistingDirectory(this, title, start, QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (!dir.isEmpty()) picked << dir;
        break;
    }
    }

    if (picked.isEmpty()) return;

    if (!picked.first().isEmpty()) {
        settings.lastDirectory = QFileInfo(picked.first()).absolutePath();
        settings.save();
    }
    setPaths(picked);
    emit changed();
}

} /* namespace tessgui */
