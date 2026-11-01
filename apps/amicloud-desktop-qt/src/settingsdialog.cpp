// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
#include "settingsdialog.h"
#include "foldertree.h"
#include "platform.h"
#include "synccontroller.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <vector>

extern "C" {
#include "sync.h"
}

SettingsDialog::SettingsDialog(SyncController *ctl, QWidget *parent)
    : QDialog(parent), m_ctl(ctl), m_orig(ctl->settings())
{
    setWindowTitle(tr("AmiCloud Desktop – Einstellungen"));
    auto *v = new QVBoxLayout(this);
    m_tabs = new QTabWidget;
    m_tabs->addTab(buildAccountTab(), tr("Konto"));
    m_tabs->addTab(buildFolderTab(), tr("Ordner"));
    m_tabs->addTab(buildSyncTab(), tr("Abgleich"));
    v->addWidget(m_tabs);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(tr("Übernehmen"));
    bb->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    connect(bb, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    v->addWidget(bb);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int i) {
        if (i == 1 && !m_treeLoaded) loadTree();
    });
    resize(640, 520);
}

void SettingsDialog::showAccountTab()
{
    m_tabs->setCurrentIndex(0);
}

QWidget *SettingsDialog::buildAccountTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_server = new QLineEdit(m_orig.server);
    m_user = new QLineEdit(m_orig.user);
    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(tr("unverändert"));
    m_insecure = new QCheckBox(tr("Zertifikat nicht prüfen (nur zum Testen)"));
    m_insecure->setChecked(!m_orig.verifySsl);
    form->addRow(tr("Server:"), m_server);
    form->addRow(tr("Benutzer:"), m_user);
    form->addRow(tr("Neues App-Passwort:"), m_password);
    form->addRow(QString(), m_insecure);
    v->addLayout(form);

    auto *row = new QHBoxLayout;
    auto *create = new QPushButton(tr("App-Passwort im Browser anlegen …"));
    auto *check = new QPushButton(tr("Anmeldung prüfen"));
    // Enter in a field = "Übernehmen", never one of these
    create->setAutoDefault(false);
    check->setAutoDefault(false);
    row->addWidget(create);
    row->addWidget(check);
    row->addStretch();
    v->addLayout(row);
    m_accountStatus = new QLabel;
    m_accountStatus->setWordWrap(true);
    v->addWidget(m_accountStatus);
    auto *note = new QLabel(tr("Das App-Passwort liegt in %1 (nur für dich lesbar).")
                                .arg(QDir::toNativeSeparators(CredentialStore::filePath())));
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: palette(mid);"));
    v->addStretch();
    v->addWidget(note);

    if (m_ctl->state() == SyncController::AuthError)
        m_accountStatus->setText(tr("<b>Der Server hat die Anmeldung abgelehnt.</b> Bitte ein "
                                    "neues App-Passwort eintragen."));

    connect(create, &QPushButton::clicked, this, [this] {
        AppSettings s;
        s.server = m_server->text();
        QDesktopServices::openUrl(QUrl(s.appPasswordUrl()));
    });
    connect(check, &QPushButton::clicked, this, [this] {
        m_accountStatus->setText(tr("Prüfe …"));
        m_checkReq = m_ctl->checkAccount(formAccount());
    });
    connect(m_ctl, &SyncController::accountChecked, this, [this](int id, const AccountInfo &info) {
        if (id != m_checkReq) return;
        m_checkReq = 0;
        if (!info.ok) {
            m_accountStatus->setText(QStringLiteral("<b>%1</b>").arg(info.error.toHtmlEscaped()));
            return;
        }
        QString who = info.displayName.isEmpty() ? info.user : info.displayName;
        QString text = tr("Angemeldet als %1, Server %2.").arg(who, info.version);
        if (info.hasQuota)
            text += QLatin1Char(' ') + tr("Belegt: %1 von %2.")
                                         .arg(QLocale().formattedDataSize(info.usedKb * 1024),
                                              QLocale().formattedDataSize(info.totalKb * 1024));
        m_accountStatus->setText(text);
    });
    return w;
}

QWidget *SettingsDialog::buildFolderTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *lrow = new QHBoxLayout;
    m_local = new QLineEdit(m_orig.localFolder);
    auto *pick = new QPushButton(tr("Wählen …"));
    pick->setAutoDefault(false);
    lrow->addWidget(m_local, 1);
    lrow->addWidget(pick);
    form->addRow(tr("Lokaler Ordner:"), lrow);
    auto *rrow = new QHBoxLayout;
    m_remote = new QLineEdit(m_orig.remoteFolder);
    auto *show = new QPushButton(tr("Anzeigen"));
    show->setAutoDefault(false);
    rrow->addWidget(m_remote, 1);
    rrow->addWidget(show);
    form->addRow(tr("Startordner auf dem Server:"), rrow);
    v->addLayout(form);
    v->addWidget(new QLabel(tr("Abgeglichene Server-Ordner (neue sind automatisch dabei):")));
    m_tree = new FolderTree(m_ctl);
    v->addWidget(m_tree, 1);
    m_treeError = new QLabel;
    m_treeError->setWordWrap(true);
    v->addWidget(m_treeError);

    connect(pick, &QPushButton::clicked, this, [this] {
        QString d = QFileDialog::getExistingDirectory(this, tr("Lokaler Ordner"), m_local->text());
        if (!d.isEmpty()) m_local->setText(d);
    });
    connect(show, &QPushButton::clicked, this, &SettingsDialog::loadTree);
    connect(m_remote, &QLineEdit::returnPressed, this, &SettingsDialog::loadTree);
    connect(m_tree, &FolderTree::loadFailed, this, [this](const QString &m, int) {
        m_treeError->setText(QStringLiteral("<b>%1</b>").arg(m.toHtmlEscaped()));
    });
    return w;
}

QWidget *SettingsDialog::buildSyncTab()
{
    auto *w = new QWidget;
    auto *form = new QFormLayout(w);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_interval = new QSpinBox;
    m_interval->setRange(1, 1440);
    m_interval->setSuffix(tr(" min"));
    m_interval->setValue(m_orig.intervalMin);
    auto *irow = new QHBoxLayout;   // not stretched over the whole width
    irow->addWidget(m_interval);
    irow->addWidget(new QLabel(tr("und sofort bei Änderungen im Ordner")));
    irow->addStretch();
    form->addRow(tr("Abgleich alle:"), irow);
    m_delMin = new QSpinBox;
    m_delMin->setRange(1, 100000);
    m_delMin->setSuffix(tr(" Dateien"));
    m_delMin->setValue(m_orig.delMin);
    m_delPct = new QSpinBox;
    m_delPct->setRange(1, 100);
    m_delPct->setSuffix(QStringLiteral(" %"));
    m_delPct->setValue(m_orig.delPct);
    auto *row = new QHBoxLayout;
    row->addWidget(m_delMin);
    row->addWidget(new QLabel(tr("und")));
    row->addWidget(m_delPct);
    row->addStretch();
    form->addRow(tr("Rückfrage beim Löschen ab:"), row);
    auto *hint = new QLabel(tr("Sollen auf einer Seite mehr Dateien gelöscht werden, fragt "
                               "AmiCloud Desktop vorher nach. „Nicht löschen“ holt sie beim "
                               "nächsten Abgleich zurück."));
    hint->setWordWrap(true);
    form->addRow(QString(), hint);
    if (platformAutostartSupported()) {
        // lives in the system (login items), not in our settings file
        m_autostart = new QCheckBox(tr("Bei der Anmeldung starten"));
        m_autostart->setChecked(platformAutostartEnabled());
        form->addRow(QString(), m_autostart);
    }
    return w;
}

Account SettingsDialog::formAccount() const
{
    Account a;
    a.server = AppSettings::normalizeServer(m_server->text());
    a.user = m_user->text().trimmed();
    a.password = m_password->text().isEmpty() ? m_ctl->account().password : m_password->text();
    a.verifySsl = !m_insecure->isChecked();
    return a;
}

void SettingsDialog::loadTree()
{
    m_treeLoaded = true;
    m_treeError->clear();
    QString root = AppSettings::normalizeRemote(m_remote->text());
    m_remote->setText(root);
    // another start folder: the old exclusions mean nothing there
    QStringList ex = root == m_orig.remoteFolder ? m_orig.excludes : QStringList();
    m_tree->load(formAccount(), root, ex);
}

void SettingsDialog::accept()
{
    AppSettings s = m_orig;
    s.server = AppSettings::normalizeServer(m_server->text());
    s.user = m_user->text().trimmed();
    s.verifySsl = !m_insecure->isChecked();
    s.localFolder = QDir::cleanPath(m_local->text().trimmed());
    s.remoteFolder = AppSettings::normalizeRemote(m_remote->text());
    if (m_treeLoaded) s.excludes = m_tree->excludes();
    else if (s.remoteFolder != m_orig.remoteFolder) s.excludes.clear();
    s.intervalMin = m_interval->value();
    s.delMin = m_delMin->value();
    s.delPct = m_delPct->value();

    if (s.server.isEmpty() || s.user.isEmpty() || s.localFolder.isEmpty() ||
        QDir::isRelativePath(s.localFolder)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Server, Benutzer und ein vollständiger lokaler Pfad sind nötig."));
        return;
    }

    // folders taken out of the selection that are here locally
    QStringList trash;
    if (s.localFolder == m_orig.localFolder && s.remoteFolder == m_orig.remoteFolder) {
        std::vector<QByteArray> oldBytes;
        std::vector<const char *> oldPtrs;
        for (const QString &e : m_orig.excludes) oldBytes.push_back(e.toUtf8());
        for (const QByteArray &b : oldBytes) oldPtrs.push_back(b.constData());
        QStringList gone;
        for (const QString &e : std::as_const(s.excludes)) {
            QByteArray eb = e.toUtf8();
            if (sync_path_excluded(oldPtrs.data(), int(oldPtrs.size()), eb.constData())) continue;
            QString local = s.localFolder + QLatin1Char('/') + e;
            if (QFileInfo(local).isDir()) gone << local;
        }
        if (!gone.isEmpty()) {
            QMessageBox box(this);
            box.setIcon(QMessageBox::Question);
            box.setWindowTitle(tr("Ordner abgewählt"));
            box.setText(tr("Diese Ordner werden nicht mehr abgeglichen. Was soll mit den "
                           "Kopien auf diesem Rechner geschehen? Was auf dem Server liegt, "
                           "bleibt dort erhalten. Dateien, die seit dem letzten Abgleich hier "
                           "neu oder geändert sind, gibt es dann nur noch im Papierkorb."));
            QStringList shown;
            for (const QString &g : std::as_const(gone)) shown << QDir::toNativeSeparators(g);
            box.setInformativeText(shown.join(QLatin1Char('\n')));
            QPushButton *toTrash = box.addButton(tr("In den Papierkorb"), QMessageBox::DestructiveRole);
            QPushButton *keep = box.addButton(tr("Behalten"), QMessageBox::AcceptRole);
            QPushButton *cancel = box.addButton(tr("Abbrechen"), QMessageBox::RejectRole);
            box.setDefaultButton(keep);
            box.setEscapeButton(cancel);
            box.exec();
            if (box.clickedButton() == toTrash) trash = gone;
            else if (box.clickedButton() != keep) return;
        }
    }

    if (m_autostart && m_autostart->isChecked() != platformAutostartEnabled()) {
        QString err;
        if (!platformSetAutostart(m_autostart->isChecked(), &err) || !err.isEmpty())
            QMessageBox::information(this, windowTitle(),
                                     tr("Start bei der Anmeldung: %1").arg(err));
    }
    m_ctl->applySettings(s, m_password->text(), trash);
    QDialog::accept();
}
