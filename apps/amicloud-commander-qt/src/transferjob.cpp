/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#include "transferjob.h"

extern "C" {
#include "cmd.h"
#include "panel.h"
#include "localfs.h"
#include "ui_hooks.h"
}

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QAbstractButton>
#include <QMetaObject>
#include <cstring>

/* Only one TransferJob runs at a time (MainWindow enforces this), so a
 * single static pointer is enough to bridge the plain-C ui_hooks
 * function-pointer interface (no per-call userdata) back to whichever
 * TransferJob instance is currently in run(). */
static TransferJob *g_activeJob = nullptr;

extern "C" {
static void bridge_notify(int level, const char *msg)
{
    if (g_activeJob) g_activeJob->hookNotify(level, msg);
}
static int bridge_confirm(const char *msg)
{
    return (g_activeJob && g_activeJob->hookConfirm(msg)) ? 1 : 0;
}
static int bridge_progress(long done, long total, const char *name)
{
    return g_activeJob ? g_activeJob->hookProgress(done, total, name) : 1;
}
static UiOverwrite bridge_overwrite(int is_dir, const char *name,
                                    long src_size, const char *src_date, const char *src_md5,
                                    long dst_size, const char *dst_date, const char *dst_md5)
{
    return g_activeJob ? g_activeJob->hookOverwrite(is_dir, name, src_size, src_date, src_md5,
                                                     dst_size, dst_date, dst_md5)
                       : UI_OW_OVERWRITE;
}
}

TransferJob::TransferJob(Op op, const AcConfig &cfg, QVector<Item> items, QObject *parent)
    : QThread(parent), m_op(op), m_cfg(cfg), m_items(std::move(items))
{
}

void TransferJob::hookNotify(int level, const char *msg)
{
    emit logMessage(level, QString::fromUtf8(msg));
}

int TransferJob::hookProgress(long done, long total, const char *name)
{
    QString qname = name ? QString::fromUtf8(name) : QString();

    if (done == 0) ui_rate_reset(&m_rate);
    ui_rate_sample(&m_rate, done); /* m_rate.bps itself only changes ~1/sec */

    /* Exactly one of the two signals fires per call, matching the Amiga
     * side's g_op==NULL/non-NULL split in mui_progress(): CopyLocalToCloud/
     * CopyCloudToLocal get the byte-accurate overall position, everything
     * else keeps the per-file byte-percentage behaviour, both now with
     * bps attached. Safe without locking since m_ctx/m_rate are only ever
     * touched from this thread (the one TransferJob runs on; MainWindow
     * only reads them via these signals). */
    if (m_usesCopyCtx) {
        /* bytes_done was pre-incremented by the full current-file size in
         * progress_prefix() (panel.c), so subtracting cur_file_size back
         * out and adding the live done reconstructs "bytes completed by
         * every prior file, plus how far the in-flight file actually
         * got" — smooth movement within a file instead of a jump per
         * file (same formula as amicloud-commander-mui's gui.c mui_progress()). */
        long overall = m_ctx.pr.bytes_done - m_ctx.pr.cur_file_size + done;
        if (overall < 0) overall = 0;
        if (m_ctx.pr.bytes_total > 0 && overall > m_ctx.pr.bytes_total) overall = m_ctx.pr.bytes_total;
        emit overallProgressChanged(qint64(overall), qint64(m_ctx.pr.bytes_total),
                                    m_ctx.pr.done, m_ctx.pr.total, qname, m_rate.bps);
    } else {
        emit progressChanged(qint64(done), qint64(total), qname, m_rate.bps);
    }
    return (m_cancelRequested.load() || m_ctx.cancelled) ? 0 : 1;
}

bool TransferJob::hookConfirm(const char *msg)
{
    bool result = false;
    QString text = QString::fromUtf8(msg);
    QMetaObject::invokeMethod(QCoreApplication::instance(), [text, &result]() {
        result = (QMessageBox::question(nullptr, QObject::tr("Bestaetigen"), text) == QMessageBox::Yes);
    }, Qt::BlockingQueuedConnection);
    return result;
}

/* Per-file/per-folder overwrite prompt — ui_hooks.h's UiHooks.overwrite,
 * installed below into the bridge hookset. Called synchronously from
 * inside panel_copy_file()/panel_get_dir()/panel_put_dir() while run() is
 * on the worker thread, so (like hookConfirm()) it has to hop to the GUI
 * thread and block until the user answers. */
UiOverwrite TransferJob::hookOverwrite(int isDir, const char *name,
                                       long srcSize, const char *srcDate, const char *srcMd5,
                                       long dstSize, const char *dstDate, const char *dstMd5)
{
    UiOverwrite result = UI_OW_CANCEL;
    QString text;

    if (isDir) {
        text = QObject::tr("Verzeichnis \"%1\" existiert bereits.\nInhalt zusammenfuehren?")
              .arg(QString::fromUtf8(name));
    } else {
        QString srcExtra = (srcMd5 && srcMd5[0]) ? QObject::tr(", MD5 %1").arg(QString::fromUtf8(srcMd5)) : QString();
        QString dstExtra = (dstMd5 && dstMd5[0]) ? QObject::tr(", MD5 %1").arg(QString::fromUtf8(dstMd5)) : QString();
        text = QObject::tr("\"%1\" existiert bereits.\n\nNeu: %2 Bytes, %3%4\nVorhanden: %5 Bytes, %6%7")
              .arg(QString::fromUtf8(name))
              .arg(srcSize).arg(QString::fromUtf8(srcDate ? srcDate : "")).arg(srcExtra)
              .arg(dstSize).arg(QString::fromUtf8(dstDate ? dstDate : "")).arg(dstExtra);
    }

    QMetaObject::invokeMethod(QCoreApplication::instance(), [text, &result]() {
        QMessageBox box(QMessageBox::Question, QObject::tr("Ueberschreiben?"), text,
                        QMessageBox::NoButton, nullptr);
        QPushButton *bOverwrite = box.addButton(QObject::tr("Ueberschreiben"), QMessageBox::AcceptRole);
        QPushButton *bSkip      = box.addButton(QObject::tr("Ueberspringen"), QMessageBox::RejectRole);
        QPushButton *bAll       = box.addButton(QObject::tr("Alle ueberschreiben"), QMessageBox::YesRole);
        QPushButton *bSkipAll   = box.addButton(QObject::tr("Alle ueberspringen"), QMessageBox::NoRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(bOverwrite);
        box.exec();

        QAbstractButton *clicked = box.clickedButton();
        if (clicked == bOverwrite)      result = UI_OW_OVERWRITE;
        else if (clicked == bSkip)      result = UI_OW_SKIP;
        else if (clicked == bAll)       result = UI_OW_OVERWRITE_ALL;
        else if (clicked == bSkipAll)   result = UI_OW_SKIP_ALL;
        else                            result = UI_OW_CANCEL; /* Cancel or window closed */
    }, Qt::BlockingQueuedConnection);

    return result;
}

bool TransferJob::copyLocalToLocal(const QString &src, const QString &dst, bool isDir)
{
    if (isDir) {
        QDir().mkpath(dst);
        QDir dir(src);
        bool ok = true;
        for (const QFileInfo &fi : dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
            QString childDst = dst + "/" + fi.fileName();
            if (fi.isDir()) ok = copyLocalToLocal(fi.filePath(), childDst, true) && ok;
            else {
                emit progressChanged(0, -1, fi.fileName(), 0);
                if (QFile::exists(childDst)) QFile::remove(childDst);
                ok = QFile::copy(fi.filePath(), childDst) && ok;
            }
        }
        return ok;
    }
    emit progressChanged(0, -1, QFileInfo(src).fileName(), 0);
    if (QFile::exists(dst)) QFile::remove(dst);
    return QFile::copy(src, dst);
}

bool TransferJob::deleteLocalRecursive(const QString &path)
{
    QFileInfo fi(path);
    if (fi.isDir()) return QDir(path).removeRecursively();
    return QFile::remove(path);
}

bool TransferJob::runOne(const Item &it)
{
    QByteArray src = it.src.toUtf8();
    QByteArray dst = it.dst.toUtf8();

    switch (m_op) {
    case CopyLocalToCloud:
        /* Existence check + overwrite decision now happen inline, per
         * conflict, inside panel_copy_file()/panel_put_dir() via m_ctx —
         * see run()'s scan phase for the byte/file-count pre-pass and
         * doCopy() (mainwindow.cpp) for where it.smeta/dmeta come from.
         * m_ctx.pr.ok/err already tally the outcome, so the bool this
         * returns is unused for this Op — see run(). */
        if (it.isDir) {
            panel_put_dir(&m_cfg, src.constData(), dst.constData(), &m_ctx);
        } else {
            QByteArray name = it.name.toUtf8();
            panel_copy_file(PANEL_LOCAL, nullptr, src.constData(),
                            PANEL_CLOUD, &m_cfg, dst.constData(),
                            name.constData(), &it.smeta, it.hasDst ? &it.dmeta : nullptr, &m_ctx);
        }
        return true;

    case CopyCloudToLocal:
        if (it.isDir) {
            panel_get_dir(&m_cfg, src.constData(), dst.constData(), &m_ctx);
        } else {
            QByteArray name = it.name.toUtf8();
            panel_copy_file(PANEL_CLOUD, &m_cfg, src.constData(),
                            PANEL_LOCAL, nullptr, dst.constData(),
                            name.constData(), &it.smeta, it.hasDst ? &it.dmeta : nullptr, &m_ctx);
        }
        return true;

    case CopyLocalToLocal:
        return copyLocalToLocal(it.src, it.dst, it.isDir);

    case RenameCloud:
        return cmd_rename(&m_cfg, src.constData(), dst.constData()) == 0;

    case MoveLocalToCloud: {
        /* m_ctx.ask is 0 for this Op (see run()) — a move's destination is
         * freshly derived from the source name, no user-facing overwrite
         * decision to make, same as the Amiga side's do_rename_move(). */
        bool ok;
        if (it.isDir) {
            int errBefore = m_ctx.pr.err;
            panel_put_dir(&m_cfg, src.constData(), dst.constData(), &m_ctx);
            ok = m_ctx.pr.err == errBefore;
        } else {
            ok = cmd_put(&m_cfg, src.constData(), dst.constData()) == 0;
        }
        if (ok) ok = deleteLocalRecursive(it.src);
        return ok;
    }

    case MoveCloudToLocal: {
        bool ok;
        if (it.isDir) {
            int errBefore = m_ctx.pr.err;
            panel_get_dir(&m_cfg, src.constData(), dst.constData(), &m_ctx);
            ok = m_ctx.pr.err == errBefore;
        } else {
            ok = cmd_get(&m_cfg, src.constData(), dst.constData()) == 0;
        }
        if (ok) ok = (it.isDir ? cmd_del_recursive(&m_cfg, src.constData())
                               : cmd_del(&m_cfg, src.constData())) == 0;
        return ok;
    }

    case DeleteCloud:
        return (it.isDir ? cmd_del_recursive(&m_cfg, src.constData())
                          : cmd_del(&m_cfg, src.constData())) == 0;

    case DeleteLocal:
        return deleteLocalRecursive(it.src);

    case MkdirCloud:
        return cmd_mkdir(&m_cfg, src.constData()) == 0;

    case MkdirLocal:
        return QDir().mkpath(it.src);
    }
    return false;
}

void TransferJob::run()
{
    g_activeJob = this;
    UiHooks hooks = { bridge_notify, bridge_confirm, bridge_progress, bridge_overwrite };
    ui_set_hooks(&hooks);
    ui_rate_reset(&m_rate);

    m_usesCopyCtx = (m_op == CopyLocalToCloud || m_op == CopyCloudToLocal);
    copyctx_init(&m_ctx, m_usesCopyCtx ? 1 : 0);

    if (m_usesCopyCtx) {
        /* Scan phase: total file count + bytes across the whole item list,
         * so the progress dialog can show a real x/y from the first file
         * onward (GUI-Commander copy-flow rework) — same two-pass shape as
         * the Amiga Commander's do_copy(). No extra network round-trips:
         * uploads count the local tree for free, downloads walk the same
         * `list` calls the transfer itself needs. */
        for (const Item &it : m_items) {
            if (it.isDir) {
                QByteArray src = it.src.toUtf8();
                if (m_op == CopyLocalToCloud) count_local_tree(src.constData(), &m_ctx.pr);
                else                          count_remote_tree(&m_cfg, src.constData(), &m_ctx.pr);
            } else {
                m_ctx.pr.total++;
                m_ctx.pr.bytes_total += it.smeta.size;
            }
        }
    }

    int okCount = 0, errCount = 0;
    bool cancelled = false;

    /* Cloud -> local: server names that would be one file at the
     * destination (case, umlauts on macOS/Windows) are not fetched -- the
     * second would overwrite the first, and a move would then delete
     * both on the server. Counted as errors. */
    QVector<unsigned char> skip(m_items.size(), 0);
    if ((m_op == CopyCloudToLocal || m_op == MoveCloudToLocal) && m_items.size() > 1) {
        QVector<QByteArray> names;
        QVector<const char *> un;
        for (const Item &it : m_items) names.append(it.src.section('/', -1).toUtf8());
        for (const QByteArray &n : names) un.append(n.constData());
        QByteArray dir = QFileInfo(m_items.first().dst).absolutePath().toUtf8();
        int k = panel_local_clashes(un.constData(), int(un.size()), dir.constData(), skip.data());
        if (k > 0) {
            if (m_usesCopyCtx) m_ctx.pr.err += k;
            else               errCount += k;
        }
    }

    for (int i = 0; i < m_items.size(); i++) {
        const Item &it = m_items.at(i);
        if (m_cancelRequested.load() || m_ctx.cancelled) { cancelled = true; break; }
        if (skip.at(i)) continue;
        if (m_usesCopyCtx) {
            runOne(it); /* ok/err/skip already tallied in m_ctx.pr, see below */
        } else if (runOne(it)) {
            okCount++;
        } else {
            errCount++;
        }
    }

    if (m_usesCopyCtx) {
        okCount   = m_ctx.pr.ok;
        errCount  = m_ctx.pr.err;
        cancelled = cancelled || m_ctx.cancelled;
    }

    ui_set_hooks(nullptr);
    g_activeJob = nullptr;

    emit jobFinished(okCount, errCount, cancelled);
}
