/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#ifndef TRANSFERJOB_H
#define TRANSFERJOB_H

#include <QThread>
#include <QVector>
#include <QString>
#include <atomic>

extern "C" {
#include "amicloud.h"
#include "panel.h"
#include "ui_hooks.h"
}

/* Runs one Commander operation (F5 copy, F6 rename/move, F7 mkdir, F8
 * delete) off the GUI thread — the core is blocking, see GUI-Commander-
 * plan section 6, point 3. Installs itself as the active ui_hooks target
 * for the duration of run(); only one TransferJob may run at a time
 * (MainWindow enforces this), matching the plan's single worker thread. */
class TransferJob : public QThread
{
    Q_OBJECT
public:
    enum Op {
        CopyLocalToCloud,
        CopyCloudToLocal,
        CopyLocalToLocal,
        RenameCloud,
        MoveLocalToCloud,
        MoveCloudToLocal,
        DeleteCloud,
        DeleteLocal,
        MkdirCloud,
        MkdirLocal
    };

    struct Item {
        QString src;
        QString dst;   /* unused for Delete/Mkdir */
        bool    isDir;
        /* Only meaningful for CopyLocalToCloud/CopyCloudToLocal, filled by
         * MainWindow::doCopy() from the already-loaded source/destination
         * panel listings (copy-flow rework — see panel.h's CopyCtx). name
         * is the bare filename (PanelEntry::name), used for the overwrite
         * dialog and progress text instead of re-deriving it from src.
         * smeta describes the incoming entry; dmeta/hasDst describe a
         * same-named file already at the destination, if any (nullopt via
         * hasDst=false rather than a real optional<> to keep this a plain
         * aggregate). */
        QString  name;
        FileMeta smeta{};
        FileMeta dmeta{};
        bool     hasDst = false;
    };

    TransferJob(Op op, const AcConfig &cfg, QVector<Item> items, QObject *parent = nullptr);

    void requestCancel() { m_cancelRequested.store(true); }

    /* Called only from the ui_hooks C bridge functions (ui_hooks.h has no
     * per-call userdata, so those bridges look up the single active job
     * via a static pointer set at the top of run()). */
    void hookNotify(int level, const char *msg);
    int  hookProgress(long done, long total, const char *name);
    bool hookConfirm(const char *msg);
    UiOverwrite hookOverwrite(int isDir, const char *name,
                             long srcSize, const char *srcDate, const char *srcMd5,
                             long dstSize, const char *dstDate, const char *dstMd5);

signals:
    void logMessage(int level, QString msg);
    /* Current file's own byte progress + name + throughput — emitted for
     * every Op except CopyLocalToCloud/CopyCloudToLocal (see
     * hookProgress()). bps comes from m_rate, sampled every call but
     * itself only recomputed ~1/sec (see ui_hooks.h's UiRateMeter). */
    void progressChanged(qint64 done, qint64 total, QString name, long bps);
    /* Byte-accurate overall position across the whole multi-file copy
     * (bytesDone/bytesTotal) plus the coarser file counter
     * (fileDone/fileTotal, still shown as "(x/y)" text) — emitted instead
     * of progressChanged while m_usesCopyCtx. bytesDone is exact, not a
     * per-file jump: see hookProgress()'s use of m_ctx.pr.cur_file_size. */
    void overallProgressChanged(qint64 bytesDone, qint64 bytesTotal,
                                int fileDone, int fileTotal, QString name, long bps);
    void jobFinished(int okCount, int errCount, bool cancelled);

protected:
    void run() override;

private:
    bool runOne(const Item &it);
    bool copyLocalToLocal(const QString &src, const QString &dst, bool isDir);
    bool deleteLocalRecursive(const QString &path);

    Op                m_op;
    AcConfig          m_cfg;
    QVector<Item>     m_items;
    std::atomic<bool> m_cancelRequested{false};

    /* Overwrite policy + x/y progress state spanning the whole job — see
     * panel.h's CopyCtx doc comment on why "…All" must stick across every
     * item, not just within one. Only initialized with ask=1 for
     * CopyLocalToCloud/CopyCloudToLocal (m_usesCopyCtx); every other Op
     * keeps ask=0, matching this job's pre-existing silent-overwrite
     * behaviour for Move/Rename/Delete/Mkdir. */
    CopyCtx           m_ctx{};
    bool              m_usesCopyCtx = false;
    UiRateMeter       m_rate{};
};

#endif // TRANSFERJOB_H
