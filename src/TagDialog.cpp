#include "TagDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

TagDialog::TagDialog(QWidget *parent)
    : QDialog(parent)
    , m_tagEdit(new QLineEdit(this))
    , m_valueEdit(new QPlainTextEdit(this))
    , m_hint(new QLabel(this))
{
    setWindowTitle(QStringLiteral("Metadata field"));
    setModal(true);
    resize(520, 280);

    m_tagEdit->setPlaceholderText(QStringLiteral("Example: XMP-dc:Title"));
    m_tagEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral(R"(^[A-Za-z0-9][A-Za-z0-9_.: -]*$)")),
        m_tagEdit));

    m_valueEdit->setPlaceholderText(QStringLiteral("Metadata value"));

    m_hint->setText(
        QStringLiteral("Use an ExifTool tag name. A group prefix is recommended "
                       "when adding a field, for example XMP-dc:Title or EXIF:Artist."));
    m_hint->setWordWrap(true);

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Tag:"), m_tagEdit);
    form->addRow(QStringLiteral("Value:"), m_valueEdit);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_hint);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void TagDialog::setTag(const QString &tag, const bool readOnly)
{
    m_tagEdit->setText(tag);
    m_tagEdit->setReadOnly(readOnly);
}

void TagDialog::setValue(const QString &value)
{
    m_valueEdit->setPlainText(value);
}

QString TagDialog::tag() const
{
    return m_tagEdit->text().trimmed();
}

QString TagDialog::value() const
{
    return m_valueEdit->toPlainText();
}

void TagDialog::setHint(const QString &hint)
{
    m_hint->setText(hint);
}
