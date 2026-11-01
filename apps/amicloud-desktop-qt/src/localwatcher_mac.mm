// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
#include "localwatcher.h"

#include <QFileInfo>
#include <QMetaObject>

#import <CoreServices/CoreServices.h>

extern "C" {
#include "sync.h"
}

struct MacWatch
{
    FSEventStreamRef stream = nullptr;
    dispatch_queue_t queue = nullptr;
};

static void fsevents_cb(ConstFSEventStreamRef, void *info, size_t n, void *paths,
                        const FSEventStreamEventFlags *, const FSEventStreamEventId *)
{
    auto *w = static_cast<LocalWatcher *>(info);
    char **p = static_cast<char **>(paths);
    for (size_t i = 0; i < n; i++) w->notify(QString::fromUtf8(p[i]));
}

LocalWatcher::LocalWatcher(QObject *parent) : QObject(parent) {}

LocalWatcher::~LocalWatcher()
{
    watch(QString());
}

void LocalWatcher::watch(const QString &path)
{
    if (m_impl) {
        auto *mw = static_cast<MacWatch *>(m_impl);
        FSEventStreamStop(mw->stream);
        FSEventStreamInvalidate(mw->stream);
        dispatch_sync(mw->queue, ^{});   // no callback is still running on `this`
        FSEventStreamRelease(mw->stream);
        dispatch_release(mw->queue);
        delete mw;
        m_impl = nullptr;
    }
    m_path = path;
    if (path.isEmpty() || !QFileInfo(path).isDir()) return;

    FSEventStreamContext ctx = {0, this, nullptr, nullptr, nullptr};
    CFStringRef cfPath = CFStringCreateWithCString(nullptr, path.toUtf8().constData(),
                                                   kCFStringEncodingUTF8);
    CFArrayRef paths = CFArrayCreate(nullptr, reinterpret_cast<const void **>(&cfPath), 1,
                                     &kCFTypeArrayCallBacks);
    auto *mw = new MacWatch;
    // 1 s latency: the system coalesces bursts; file-level events so the
    // names can be filtered
    mw->stream = FSEventStreamCreate(nullptr, fsevents_cb, &ctx, paths,
                                     kFSEventStreamEventIdSinceNow, 1.0,
                                     kFSEventStreamCreateFlagFileEvents |
                                     kFSEventStreamCreateFlagNoDefer);
    CFRelease(paths);
    CFRelease(cfPath);
    if (!mw->stream) { delete mw; return; }
    mw->queue = dispatch_queue_create("de.planet-rk.amicloud-desktop.fsevents", DISPATCH_QUEUE_SERIAL);
    FSEventStreamSetDispatchQueue(mw->stream, mw->queue);
    if (!FSEventStreamStart(mw->stream)) {
        FSEventStreamInvalidate(mw->stream);
        FSEventStreamRelease(mw->stream);
        dispatch_release(mw->queue);
        delete mw;
        return;
    }
    m_impl = mw;
}

void LocalWatcher::notify(const QString &path)
{
    // own and foreign litter (.amicloudsync*, .DS_Store, *.part ...) never
    // triggers a run; only the changed name itself is looked at
    QString name = path.section(QLatin1Char('/'), -1, -1, QString::SectionSkipEmpty);
    QByteArray b = name.toUtf8();
    if (name.isEmpty() || sync_name_excluded(b.constData())) return;
    QMetaObject::invokeMethod(this, &LocalWatcher::changed, Qt::QueuedConnection);
}
