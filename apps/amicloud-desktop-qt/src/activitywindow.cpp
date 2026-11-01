// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
#include "activitywindow.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

extern "C" {
#include "ui_hooks.h"
}

ActivityWindow::ActivityWindow(SyncController *ctl, QWidget *parent)
    : QWidget(parent, Qt::Window), m_ctl(ctl)
{
    setWindowTitle(tr("AmiCloud Desktop – Aktivität"));
    auto *v = new QVBoxLayout(this);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    v->addWidget(m_status);

    auto *row = new QHBoxLayout;
    m_filter = new QComboBox;
    m_filter->addItem(tr("Alles"));
    m_filter->addItem(tr("Warnungen und Fehler"));
    m_filter->addItem(tr("Nur Fehler"));
    auto *clear = new QPushButton(tr("Leeren"));
    row->addWidget(new QLabel(tr("Zeigen:")));
    row->addWidget(m_filter);
    row->addStretch();
    row->addWidget(clear);
    v->addLayout(row);

    m_list = new QTreeWidget;
    m_list->setColumnCount(2);
    m_list->setHeaderLabels({tr("Zeit"), tr("Meldung")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setWordWrap(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    v->addWidget(m_list, 1);

    connect(m_filter, &QComboBox::currentIndexChanged, this, &ActivityWindow::rebuild);
    connect(clear, &QPushButton::clicked, m_ctl, &SyncController::clearLog);
    connect(m_ctl, &SyncController::logAdded, this, &ActivityWindow::add);
    connect(m_ctl, &SyncController::logCleared, this, &ActivityWindow::rebuild);
    connect(m_ctl, &SyncController::stateChanged, this, &ActivityWindow::updateStatus);
    connect(m_ctl, &SyncController::progressChanged, this, &ActivityWindow::updateStatus);

    resize(760, 460);
    rebuild();
    updateStatus();
}

void ActivityWindow::add(const LogEntry &e)
{
    int f = m_filter->currentIndex();
    if ((f == 1 && e.level == UI_INFO) || (f == 2 && e.level != UI_ERROR)) return;
    QScrollBar *sb = m_list->verticalScrollBar();
    bool atEnd = sb->value() >= sb->maximum() - 2;
    auto *it = new QTreeWidgetItem(m_list);
    it->setText(0, e.time.toString(QStringLiteral("dd.MM. HH:mm:ss")));
    it->setText(1, e.text);
    it->setToolTip(1, e.text);
    if (e.level == UI_ERROR)
        it->setIcon(1, style()->standardIcon(QStyle::SP_MessageBoxCritical));
    else if (e.level == UI_WARN)
        it->setIcon(1, style()->standardIcon(QStyle::SP_MessageBoxWarning));
    if (atEnd) m_list->scrollToBottom();
}

void ActivityWindow::rebuild()
{
    m_list->clear();
    for (const LogEntry &e : m_ctl->log()) add(e);
    m_list->scrollToBottom();
}

void ActivityWindow::updateStatus()
{
    QString t = QStringLiteral("<b>%1</b>").arg(m_ctl->statusText().toHtmlEscaped());
    if (!m_ctl->progressText().isEmpty())
        t += QStringLiteral("<br>") + m_ctl->progressText().toHtmlEscaped();
    m_status->setText(t);
}
