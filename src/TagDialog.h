#pragma once

#include <QDialog>

class QLineEdit;
class QPlainTextEdit;

class TagDialog final : public QDialog
{
public:
    explicit TagDialog(QWidget *parent = nullptr);

    void setTag(const QString &tag, bool readOnly);
    void setValue(const QString &value);

    [[nodiscard]] QString tag() const;
    [[nodiscard]] QString value() const;

private:
    QLineEdit *m_tagEdit = nullptr;
    QPlainTextEdit *m_valueEdit = nullptr;
};
