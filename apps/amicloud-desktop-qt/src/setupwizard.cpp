// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
#include "setupwizard.h"
#include "appsettings.h"
#include "foldertree.h"
#include "synccontroller.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWizardPage>

// --------------------------------------------------------------------------

class ServerPage : public QWizardPage
{
public:
    explicit ServerPage(SetupWizard *w)
    {
        setTitle(tr("Server"));
        setSubTitle(tr("Die Adresse des AmiCloud-Servers, so wie im Browser."));
        auto *form = new QFormLayout(this);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        w->serverEdit = new QLineEdit;
        w->serverEdit->setPlaceholderText(QStringLiteral("https://amicloud.example.org"));
        form->addRow(tr("Adresse:"), w->serverEdit);
        w->insecureBox = new QCheckBox(tr("Zertifikat nicht prüfen (nur zum Testen)"));
        form->addRow(QString(), w->insecureBox);
        registerField(QStringLiteral("server*"), w->serverEdit);
    }
};

// --------------------------------------------------------------------------

class LoginPage : public QWizardPage
{
public:
    LoginPage(SetupWizard *w, SyncController *ctl) : m_w(w), m_ctl(ctl)
    {
        setTitle(tr("Anmeldung"));
        setSubTitle(tr("Bitte ein App-Passwort verwenden, nicht das Kontopasswort: "
                       "Es lässt sich jederzeit auf dem Server widerrufen."));
        auto *v = new QVBoxLayout(this);
        auto *form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        w->userEdit = new QLineEdit;
        w->passwordEdit = new QLineEdit;
        w->passwordEdit->setEchoMode(QLineEdit::Password);
        form->addRow(tr("Benutzer:"), w->userEdit);
        form->addRow(tr("App-Passwort:"), w->passwordEdit);
        v->addLayout(form);
        auto *open = new QPushButton(tr("App-Passwort im Browser anlegen …"));
        open->setToolTip(tr("Öffnet die Einstellungen der Web-Oberfläche. Dort ein "
                            "App-Passwort anlegen und hier einfügen."));
        open->setAutoDefault(false);   // Enter in the password field = Weiter
        auto *row = new QHBoxLayout;
        row->addWidget(open);
        row->addStretch();
        v->addLayout(row);
        m_status = new QLabel;
        m_status->setWordWrap(true);
        v->addWidget(m_status);
        v->addStretch();
        registerField(QStringLiteral("user*"), w->userEdit);
        registerField(QStringLiteral("password*"), w->passwordEdit);

        connect(open, &QPushButton::clicked, this, [this] {
            AppSettings s;
            s.server = m_w->serverEdit->text();
            QDesktopServices::openUrl(QUrl(s.appPasswordUrl()));
        });
        connect(ctl, &SyncController::accountChecked, this, [this](int id, const AccountInfo &info) {
            if (id != m_req) return;
            m_req = 0;
            setEnabled(true);
            if (!info.ok) {
                m_status->setText(QStringLiteral("<b>%1</b>").arg(info.error.toHtmlEscaped()));
                return;
            }
            m_okKey = key();
            QString who = info.displayName.isEmpty() ? info.user : info.displayName;
            m_status->setText(tr("Angemeldet als %1 (Server %2).").arg(who, info.version));
            m_w->next();
        });
    }

    bool validatePage() override
    {
        if (m_okKey == key()) return true;
        if (m_req) return false;
        m_status->setText(tr("Prüfe die Anmeldung …"));
        setEnabled(false);
        m_req = m_ctl->checkAccount(m_w->account());
        return false;   // continues in the accountChecked handler
    }

private:
    QString key() const
    {
        return m_w->serverEdit->text() + QChar(1) + m_w->userEdit->text() + QChar(1) +
               m_w->passwordEdit->text() + QChar(1) + QString::number(m_w->verifySsl());
    }
    SetupWizard *m_w;
    SyncController *m_ctl;
    QLabel *m_status;
    int m_req = 0;
    QString m_okKey;
};

// --------------------------------------------------------------------------

class LocalPage : public QWizardPage
{
public:
    explicit LocalPage(SetupWizard *w) : m_w(w)
    {
        setTitle(tr("Lokaler Ordner"));
        setSubTitle(tr("Dieser Ordner wird mit dem Server abgeglichen."));
        auto *v = new QVBoxLayout(this);
        auto *row = new QHBoxLayout;
        w->localEdit = new QLineEdit(QDir::homePath() + QStringLiteral("/AmiCloud"));
        auto *pick = new QPushButton(tr("Wählen …"));
        pick->setAutoDefault(false);
        row->addWidget(w->localEdit, 1);
        row->addWidget(pick);
        v->addLayout(row);
        m_hint = new QLabel;
        m_hint->setWordWrap(true);
        v->addWidget(m_hint);
        v->addStretch();
        // not "local*": a mandatory field only counts as filled once it
        // differs from its value at registration -- the proposed path
        // could not be taken as it is (isComplete() below instead)
        registerField(QStringLiteral("local"), w->localEdit);
        connect(pick, &QPushButton::clicked, this, [this] {
            QString d = QFileDialog::getExistingDirectory(this, tr("Lokaler Ordner"),
                                                          m_w->localEdit->text());
            if (!d.isEmpty()) m_w->localEdit->setText(d);
        });
        connect(w->localEdit, &QLineEdit::textChanged, this, [this] {
            updateHint();
            emit completeChanged();
        });
        updateHint();
    }

    bool isComplete() const override
    {
        return !m_w->localEdit->text().trimmed().isEmpty();
    }

    bool validatePage() override
    {
        QString p = QDir::cleanPath(m_w->localEdit->text().trimmed());
        if (QDir::isRelativePath(p)) {
            QMessageBox::warning(this, tr("Lokaler Ordner"), tr("Bitte einen vollständigen Pfad angeben."));
            return false;
        }
        if (p == QDir::homePath() || p == QStringLiteral("/")) {
            QMessageBox::warning(this, tr("Lokaler Ordner"),
                                 tr("Bitte einen eigenen Ordner wählen, nicht den Benutzerordner "
                                    "oder das ganze Laufwerk."));
            return false;
        }
        m_w->localEdit->setText(p);
        return true;
    }

private:
    void updateHint()
    {
        QDir d(m_w->localEdit->text().trimmed());
        if (d.exists() && !d.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden))
            m_hint->setText(tr("Der Ordner ist nicht leer. Sein Inhalt wird mit dem Server "
                               "zusammengeführt; beim ersten Abgleich wird nichts gelöscht, "
                               "verschiedene Fassungen bleiben beide erhalten."));
        else
            m_hint->setText(d.exists() ? tr("Der Ordner ist leer.")
                                       : tr("Der Ordner wird angelegt."));
    }
    SetupWizard *m_w;
    QLabel *m_hint;
};

// --------------------------------------------------------------------------

class SelectionPage : public QWizardPage
{
public:
    SelectionPage(SetupWizard *w, SyncController *ctl) : m_w(w)
    {
        setTitle(tr("Auswahl"));
        setSubTitle(tr("Welche Server-Ordner abgeglichen werden. Neue Ordner auf dem "
                       "Server sind automatisch dabei."));
        auto *v = new QVBoxLayout(this);
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("Startordner auf dem Server:")));
        w->remoteEdit = new QLineEdit(QStringLiteral("/"));
        auto *reload = new QPushButton(tr("Anzeigen"));
        reload->setAutoDefault(false);
        row->addWidget(w->remoteEdit, 1);
        row->addWidget(reload);
        v->addLayout(row);
        w->tree = new FolderTree(ctl);
        v->addWidget(w->tree, 1);
        m_err = new QLabel;
        m_err->setWordWrap(true);
        v->addWidget(m_err);
        connect(reload, &QPushButton::clicked, this, [this] { initializePage(); });
        connect(w->remoteEdit, &QLineEdit::returnPressed, this, [this] { initializePage(); });
        connect(w->tree, &FolderTree::loadFailed, this, [this](const QString &m, int) {
            m_err->setText(QStringLiteral("<b>%1</b>").arg(m.toHtmlEscaped()));
        });
    }

    void initializePage() override
    {
        m_err->clear();
        QString root = AppSettings::normalizeRemote(m_w->remoteEdit->text());
        m_w->remoteEdit->setText(root);
        m_w->tree->load(m_w->account(), root, QStringList());
    }

private:
    SetupWizard *m_w;
    QLabel *m_err;
};

// --------------------------------------------------------------------------

SetupWizard::SetupWizard(SyncController *ctl, QWidget *parent) : QWizard(parent), m_ctl(ctl)
{
    setWindowTitle(tr("AmiCloud Desktop einrichten"));
    setWizardStyle(QWizard::ModernStyle);   // MacStyle paints a bow tie behind the pages
    setOption(QWizard::NoBackButtonOnStartPage);
    // no Qt translations are shipped: German button texts set here
    setButtonText(QWizard::BackButton, tr("< Zurück"));
    setButtonText(QWizard::NextButton, tr("Weiter >"));
    setButtonText(QWizard::CommitButton, tr("Weiter"));
    setButtonText(QWizard::FinishButton, tr("Fertig"));
    setButtonText(QWizard::CancelButton, tr("Abbrechen"));
    addPage(new ServerPage(this));
    addPage(new LoginPage(this, ctl));
    addPage(new LocalPage(this));
    addPage(new SelectionPage(this, ctl));
    resize(620, 480);

    const AppSettings &cur = ctl->settings();
    if (!cur.server.isEmpty()) serverEdit->setText(cur.server);
    if (!cur.user.isEmpty()) userEdit->setText(cur.user);
    if (!cur.localFolder.isEmpty()) localEdit->setText(cur.localFolder);
    insecureBox->setChecked(!cur.verifySsl);
}

Account SetupWizard::account() const
{
    Account a;
    a.server = AppSettings::normalizeServer(serverEdit->text());
    a.user = userEdit->text().trimmed();
    a.password = passwordEdit->text();
    a.verifySsl = verifySsl();
    return a;
}

bool SetupWizard::verifySsl() const
{
    return !insecureBox->isChecked();
}

void SetupWizard::accept()
{
    AppSettings s = m_ctl->settings();
    Account a = account();
    s.server = a.server;
    s.user = a.user;
    s.verifySsl = a.verifySsl;
    s.localFolder = localEdit->text();
    s.remoteFolder = AppSettings::normalizeRemote(remoteEdit->text());
    s.excludes = tree->excludes();
    m_ctl->applySettings(s, a.password, QStringList());
    QWizard::accept();
}
