/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#ifndef INFODIALOG_H
#define INFODIALOG_H

#include <QDialog>

extern "C" {
#include "amicloud.h"
#include "panel.h"
}

/* F3 info dialog (GUI-Commander-plan section 3.2) — cloud side reads
 * cmd_info_raw(), local side reads localfs_stat(). Deliberately just a
 * read-only field dump, no editing (protection/comment editing stays a
 * shell-only feature for v1, see plan section 10). */
class InfoDialog : public QDialog
{
    Q_OBJECT
public:
    static void showFor(PanelSource src, const AcConfig &cfg,
                        const QString &fullPath, const PanelEntry &entry,
                        QWidget *parent);

private:
    InfoDialog(QWidget *parent = nullptr);
};

#endif // INFODIALOG_H
