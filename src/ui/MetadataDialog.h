#pragma once

#include "core/SoundClip.h"

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QTextEdit;

class MetadataDialog : public QDialog {
    Q_OBJECT

public:
    explicit MetadataDialog(const SoundClip& clip, QWidget* parent = nullptr);

    SoundMetadata metadata() const;

private:
    QLineEdit* m_titleEdit = nullptr;
    QLineEdit* m_categoryEdit = nullptr;
    QCheckBox* m_favoriteCheck = nullptr;
    QLineEdit* m_iconEdit = nullptr;
    QLineEdit* m_aliasesEdit = nullptr;
    QTextEdit* m_notesEdit = nullptr;
};
