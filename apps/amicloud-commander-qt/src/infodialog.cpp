/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#include "infodialog.h"

extern "C" {
#include "cmd.h"
#include "localfs.h"
#include "md5.h"
}

#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>

InfoDialog::InfoDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Info"));
}

void InfoDialog::showFor(PanelSource src, const AcConfig &cfg,
                         const QString &fullPath, const PanelEntry &entry,
                         QWidget *parent)
{
    auto *dlg = new InfoDialog(parent);
    auto *form = new QFormLayout;

    form->addRow(dlg->tr("Name:"), new QLabel(QString::fromUtf8(entry.name)));
    form->addRow(dlg->tr("Pfad:"), new QLabel(fullPath));
    form->addRow(dlg->tr("Typ:"), new QLabel(entry.is_dir ? dlg->tr("Verzeichnis") : dlg->tr("Datei")));

    if (src == PANEL_CLOUD) {
        AcInfo info;
        QByteArray path = fullPath.toUtf8();
        if (cmd_info_raw(&cfg, path.constData(), &info) == 0) {
            form->addRow(dlg->tr("Groesse:"), new QLabel(dlg->tr("%1 Bytes").arg(info.size)));
            form->addRow(dlg->tr("MIME:"), new QLabel(QString::fromUtf8(info.mime)));
            form->addRow(dlg->tr("Erstellt:"), new QLabel(QString::fromUtf8(info.created)));
            form->addRow(dlg->tr("Geaendert:"), new QLabel(QString::fromUtf8(info.modified)));
            if (info.md5[0])
                form->addRow(dlg->tr("MD5:"), new QLabel(QString::fromUtf8(info.md5)));
            if (info.comment[0])
                form->addRow(dlg->tr("Kommentar:"), new QLabel(QString::fromUtf8(info.comment)));
        } else {
            form->addRow(new QLabel(dlg->tr("Konnte Info nicht laden.")));
        }
    } else {
        long size = 0, mtime = 0;
        QByteArray path = fullPath.toUtf8();
        if (localfs_stat(path.constData(), &size, &mtime)) {
            form->addRow(dlg->tr("Groesse:"), new QLabel(dlg->tr("%1 Bytes").arg(size)));
            form->addRow(dlg->tr("Geaendert:"), new QLabel(QString::fromUtf8(entry.modified)));
            /* Local entries carry no MD5 from the panel listing (only
             * cloud ones do) -- computed on demand here, same helper/
             * pattern as MainWindow::doChecksum() and panel.c's
             * overwrite-confirm dialog. */
            if (!entry.is_dir) {
                char hex[33] = "";
                if (md5_file_hex(path.constData(), hex))
                    form->addRow(dlg->tr("MD5:"), new QLabel(QString::fromUtf8(hex)));
            }
        } else {
            form->addRow(new QLabel(dlg->tr("Konnte Info nicht laden.")));
        }
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);

    auto *layout = new QVBoxLayout(dlg);
    layout->addLayout(form);
    layout->addWidget(buttons);

    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->exec();
}
