#include "ui/MetadataDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QVBoxLayout>

MetadataDialog::MetadataDialog(const SoundClip& clip, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Edit Sound"));
    setModal(true);
    resize(460, 420);

    m_titleEdit = new QLineEdit(clip.metadata.title, this);
    m_categoryEdit = new QLineEdit(clip.metadata.category, this);
    m_favoriteCheck = new QCheckBox(tr("Favorite"), this);
    m_favoriteCheck->setChecked(clip.metadata.favorite);
    m_iconEdit = new QLineEdit(clip.metadata.icon, this);
    m_aliasesEdit = new QLineEdit(clip.metadata.aliases.join(", "), this);
    m_notesEdit = new QTextEdit(clip.metadata.notes, this);
    m_notesEdit->setAcceptRichText(false);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->addRow(tr("Title"), m_titleEdit);
    form->addRow(tr("Category"), m_categoryEdit);
    form->addRow(tr("Icon or emoji"), m_iconEdit);
    form->addRow(tr("Aliases"), m_aliasesEdit);
    form->addRow(QString(), m_favoriteCheck);
    form->addRow(tr("Notes"), m_notesEdit);

    auto* pathLabel = new QLabel(clip.relativePath, this);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathLabel->setWordWrap(true);
    pathLabel->setObjectName("PathLabel");

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(pathLabel);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

SoundMetadata MetadataDialog::metadata() const
{
    SoundMetadata value;
    value.title = m_titleEdit->text().trimmed();
    value.category = m_categoryEdit->text().trimmed();
    value.favorite = m_favoriteCheck->isChecked();
    value.icon = m_iconEdit->text().trimmed();
    value.notes = m_notesEdit->toPlainText().trimmed();

    const QStringList aliases = m_aliasesEdit->text().split(',', Qt::SkipEmptyParts);
    for (const QString& alias : aliases) {
        const QString trimmed = alias.trimmed();
        if (!trimmed.isEmpty()) {
            value.aliases.append(trimmed);
        }
    }

    return value;
}
