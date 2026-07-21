#pragma once

#include <QDialog>

class QLineEdit;
class QPlainTextEdit;
class QLabel;

class TagDialog final : public QDialog
{
public:
    explicit TagDialog(QWidget *parent = nullptr);

    void setTag(const QString &tag, bool readOnly);
    void setValue(const QString &value);
    void setHint(const QString &hint);

    [[nodiscard]] QString tag() const;
    [[nodiscard]] QString value() const;

private:
    QLineEdit *m_tagEdit = nullptr;
    QPlainTextEdit *m_valueEdit = nullptr;
    QLabel *m_hint = nullptr;
};
