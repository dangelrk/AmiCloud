// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
//
// setupwizard.h — first start (plan 6.2): server address -> login with
// an app password (created in the web interface, the wizard opens the
// page) -> local folder -> start folder and selection.
#pragma once

#include "coreworker.h"

#include <QWizard>

class QCheckBox;
class QLabel;
class QLineEdit;
class FolderTree;
class SyncController;

class SetupWizard : public QWizard
{
    Q_OBJECT
public:
    explicit SetupWizard(SyncController *ctl, QWidget *parent = nullptr);

    Account account() const;
    bool verifySsl() const;

    // pages
    QLineEdit *serverEdit = nullptr;
    QCheckBox *insecureBox = nullptr;
    QLineEdit *userEdit = nullptr;
    QLineEdit *passwordEdit = nullptr;
    QLineEdit *localEdit = nullptr;
    QLineEdit *remoteEdit = nullptr;
    FolderTree *tree = nullptr;

protected:
    void accept() override;

private:
    SyncController *m_ctl;
};
