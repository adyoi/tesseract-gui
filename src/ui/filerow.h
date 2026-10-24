/*
 * filerow.h — the "label [ path ] [...]" line used by every form.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TESSGUI_FILEROW_H
#define TESSGUI_FILEROW_H

#include <QStringList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QToolButton;

namespace tessgui {

/**
 * One line of a form: a fixed width label, the path field and a browse
 * button.  Public key lines accept several files at once, which is how
 * multi-recipient messages are produced; the paths are then separated by
 * "; " in the field.
 */
class FileRow : public QWidget {
    Q_OBJECT

public:
    enum Kind {
        OpenFile,      /* pick one existing file        */
        MultiOpenFile, /* pick several existing files   */
        SaveFile,      /* pick the file to write        */
        OpenDirectory, /* pick a directory               */
    };

    FileRow(const QString &label, Kind kind, QWidget *parent = nullptr);

    QLineEdit *edit() const { return m_edit; }

    QString label() const { return m_label; }
    void setLabel(const QString &label);

    QString path() const;
    QStringList paths() const;
    void setPath(const QString &path);
    void setPaths(const QStringList &paths);

    void setDialogTitle(const QString &title) { m_dialogTitle = title; }
    void setFilter(const QString &filter) { m_filter = filter; }
    void setPlaceholder(const QString &text);

signals:
    void changed();

private:
    void browse();
    void refreshTooltip();

    Kind m_kind;
    QString m_label;
    QString m_dialogTitle;
    QString m_filter;
    QLabel *m_caption = nullptr;
    QLineEdit *m_edit = nullptr;
    QToolButton *m_browse = nullptr;
};

} /* namespace tessgui */

#endif /* TESSGUI_FILEROW_H */
