/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#include "searchdialog.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QRadioButton>
#include <QCheckBox>
#include <QDialogButtonBox>

SearchDialog::SearchDialog(bool cloudSource, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Suchen"));

    m_pattern = new QLineEdit(this);
    m_pattern->setPlaceholderText(tr("*.txt, #?.info, foo?, ..."));

    m_currentFolder = new QRadioButton(tr("Aktueller Ordner"), this);
    m_wholeScope    = new QRadioButton(
        cloudSource ? tr("Ganze Cloud (ab /)") : tr("Gesamtes Laufwerk"), this);
    m_currentFolder->setChecked(true);

    m_dirsToo = new QCheckBox(tr("Auch Ordner"), this);
    m_dirsToo->setChecked(true);

    auto *form = new QFormLayout;
    form->addRow(tr("Suchmuster:"), m_pattern);
    form->addRow(tr("Bereich:"), m_currentFolder);
    form->addRow(QString(), m_wholeScope);
    form->addRow(QString(), m_dirsToo);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    m_pattern->setFocus();
}

QString SearchDialog::pattern() const
{
    return m_pattern->text().trimmed();
}

bool SearchDialog::wholeScope() const
{
    return m_wholeScope->isChecked();
}

bool SearchDialog::dirsToo() const
{
    return m_dirsToo->isChecked();
}
