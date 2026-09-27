/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#ifndef SEARCHDIALOG_H
#define SEARCHDIALOG_H

#include <QDialog>

class QLineEdit;
class QRadioButton;
class QCheckBox;

/* Search requester (docs/design/search-plan.md section 5) — modal,
 * shown once per F4/Suchen invocation. cloudSource picks the label of the
 * "whole scope" radio button ("ganze Cloud" vs. "gesamtes Laufwerk") so
 * the same dialog serves both PANEL_CLOUD and PANEL_LOCAL without needing
 * to know which cmd_search()/search_local() call the caller will make. */
class SearchDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SearchDialog(bool cloudSource, QWidget *parent = nullptr);

    QString pattern() const;
    bool wholeScope() const;  /* false = current folder (default) */
    bool dirsToo() const;

private:
    QLineEdit    *m_pattern;
    QRadioButton *m_currentFolder;
    QRadioButton *m_wholeScope;
    QCheckBox    *m_dirsToo;
};

#endif // SEARCHDIALOG_H
