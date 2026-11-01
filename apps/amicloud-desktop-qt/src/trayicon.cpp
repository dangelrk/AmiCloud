// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
#include "trayicon.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

static QPainterPath cloudPath(qreal s)
{
    // in a 22x22 grid, scaled by s
    QPainterPath p;
    p.setFillRule(Qt::WindingFill);
    p.addEllipse(QRectF(3 * s, 8 * s, 8 * s, 8 * s));
    p.addEllipse(QRectF(7 * s, 4 * s, 10 * s, 10 * s));
    p.addEllipse(QRectF(12.5 * s, 8.5 * s, 7.5 * s, 7.5 * s));
    p.addRoundedRect(QRectF(6 * s, 11 * s, 11 * s, 5 * s), 1 * s, 1 * s);
    return p.simplified();
}

static void drawState(QPainter &g, SyncController::State st, qreal s)
{
    g.setCompositionMode(QPainter::CompositionMode_Clear);
    QPen pen(Qt::black, 1.6 * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    switch (st) {
    case SyncController::Syncing: {
        // up arrow left, down arrow right
        g.setPen(pen);
        g.setBrush(Qt::NoBrush);
        g.drawLine(QPointF(9 * s, 14.5 * s), QPointF(9 * s, 8.5 * s));
        g.drawLine(QPointF(7 * s, 10.5 * s), QPointF(9 * s, 8.5 * s));
        g.drawLine(QPointF(11 * s, 10.5 * s), QPointF(9 * s, 8.5 * s));
        g.drawLine(QPointF(14 * s, 8.5 * s), QPointF(14 * s, 14.5 * s));
        g.drawLine(QPointF(12 * s, 12.5 * s), QPointF(14 * s, 14.5 * s));
        g.drawLine(QPointF(16 * s, 12.5 * s), QPointF(14 * s, 14.5 * s));
        break;
    }
    case SyncController::Paused:
        g.setPen(Qt::NoPen);
        g.setBrush(Qt::black);
        g.drawRect(QRectF(9 * s, 8.5 * s, 1.8 * s, 6 * s));
        g.drawRect(QRectF(12.4 * s, 8.5 * s, 1.8 * s, 6 * s));
        break;
    case SyncController::AuthError:
    case SyncController::Error:
        g.setPen(Qt::NoPen);
        g.setBrush(Qt::black);
        g.drawRoundedRect(QRectF(10.6 * s, 7.5 * s, 1.8 * s, 5 * s), 0.8 * s, 0.8 * s);
        g.drawEllipse(QRectF(10.6 * s, 13.2 * s, 1.8 * s, 1.8 * s));
        break;
    default:
        break;
    }
}

QIcon makeTrayIcon(SyncController::State st)
{
    QIcon icon;
    for (int scale : {1, 2}) {
        const qreal s = scale;
        QPixmap pm(int(22 * s), int(22 * s));
        pm.fill(Qt::transparent);
        QPainter g(&pm);
        g.setRenderHint(QPainter::Antialiasing);
        QPainterPath cloud = cloudPath(s);
        bool outline = st == SyncController::Offline || st == SyncController::NotConfigured;
        if (outline) {
            g.setPen(QPen(Qt::black, 1.5 * s));
            g.setBrush(Qt::NoBrush);
            g.drawPath(cloud);
        } else {
            g.setPen(Qt::NoPen);
            g.setBrush(Qt::black);
            g.drawPath(cloud);
            drawState(g, st, s);
        }
        g.end();
        pm.setDevicePixelRatio(s);
        icon.addPixmap(pm);
    }
    icon.setIsMask(true);
    return icon;
}
