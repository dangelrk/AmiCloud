/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#ifndef CONNECTDIALOG_H
#define CONNECTDIALOG_H

#include <QDialog>

extern "C" {
#include "amicloud.h"
}

class QLineEdit;
class QCheckBox;

/* Shown only when the loaded config (~/.amicloudrc et al.) is missing or
 * incomplete — GUI-Commander-plan section 6, point 1. Editing an already-
 * complete config happens by hand-editing the rc file for now (v1 scope). */
class ConnectDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ConnectDialog(const AcConfig &initial, QWidget *parent = nullptr);
    AcConfig result() const;

private:
    QLineEdit *m_server;
    QLineEdit *m_user;
    QLineEdit *m_pass;
    QLineEdit *m_basepath;
    QCheckBox *m_verifySsl;
};

#endif // CONNECTDIALOG_H
