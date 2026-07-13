#include "MainWindow.h"

#include "TagDialog.h"
#include "Version.h"

#include <algorithm>
#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBrush>
#include <QByteArray>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QIODevice>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStatusBar>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
QStringList cellLines(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text.replace(QLatin1Char('\t'), QStringLiteral("    "));

    for (qsizetype index = 0; index < text.size(); ++index) {
        const ushort codePoint = text.at(index).unicode();
        if ((codePoint < 0x20U && codePoint != 0x0AU) || codePoint == 0x7FU) {
            text[index] = QLatin1Char(' ');
        }
    }

    return text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    createUi();
    createMenus();
    updateActions();

    setWindowTitle(QStringLiteral("metadata"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("document-properties")));
    resize(980, 620);

    if (!m_backend.exifToolAvailable()) {
        statusBar()->showMessage(
            QStringLiteral("ExifTool is missing; install perl-image-exiftool."));
    } else {
        statusBar()->showMessage(QStringLiteral("Open a file to inspect its metadata."));
    }
}

void MainWindow::openPath(const QString &path)
{
    QString localPath = path;
    const QUrl possibleUrl(path);
    if (possibleUrl.isLocalFile()) {
        localPath = possibleUrl.toLocalFile();
    }

    const QFileInfo info(localPath);
    if (!info.exists() || !info.isFile()) {
        QMessageBox::warning(this,
                             QStringLiteral("Open file"),
                             QStringLiteral("The selected path is not a regular file:\n%1")
                                 .arg(localPath));
        return;
    }

    m_currentFile = info.absoluteFilePath();
    m_filePath->setText(m_currentFile);
    loadMetadata();
}

void MainWindow::createUi()
{
    auto *central = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(central);

    auto *pathLayout = new QHBoxLayout;
    auto *pathLabel = new QLabel(QStringLiteral("File:"), central);
    m_filePath = new QLineEdit(central);
    m_filePath->setReadOnly(true);
    m_filePath->setPlaceholderText(QStringLiteral("No file selected"));
    auto *openButton = new QPushButton(QStringLiteral("Open…"), central);

    pathLayout->addWidget(pathLabel);
    pathLayout->addWidget(m_filePath, 1);
    pathLayout->addWidget(openButton);

    m_tree = new QTreeWidget(central);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels(
        {QStringLiteral("Group"), QStringLiteral("Tag"), QStringLiteral("Value")});
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(0, Qt::AscendingOrder);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);

    auto *buttonLayout = new QHBoxLayout;
    m_addButton = new QPushButton(QStringLiteral("Add"), central);
    m_editButton = new QPushButton(QStringLiteral("Edit"), central);
    m_removeButton = new QPushButton(QStringLiteral("Remove"), central);
    m_removeAllButton = new QPushButton(QStringLiteral("Remove All"), central);
    m_copyButton = new QPushButton(QStringLiteral("Copy to Clipboard"), central);
    m_exportButton = new QPushButton(QStringLiteral("Export"), central);
    m_refreshButton = new QPushButton(QStringLiteral("Refresh"), central);

    buttonLayout->addWidget(m_addButton);
    buttonLayout->addWidget(m_editButton);
    buttonLayout->addWidget(m_removeButton);
    buttonLayout->addWidget(m_removeAllButton);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(m_copyButton);
    buttonLayout->addWidget(m_exportButton);
    buttonLayout->addWidget(m_refreshButton);

    mainLayout->addLayout(pathLayout);
    mainLayout->addWidget(m_tree, 1);
    mainLayout->addLayout(buttonLayout);
    setCentralWidget(central);

    connect(openButton, &QPushButton::clicked, this, [this] { chooseFile(); });
    connect(m_addButton, &QPushButton::clicked, this, [this] { addMetadata(); });
    connect(m_editButton, &QPushButton::clicked, this, [this] { editMetadata(); });
    connect(m_removeButton, &QPushButton::clicked, this, [this] { removeMetadata(); });
    connect(m_removeAllButton,
            &QPushButton::clicked,
            this,
            [this] { removeAllMetadata(); });
    connect(m_copyButton,
            &QPushButton::clicked,
            this,
            [this] { copyMetadataToClipboard(); });
    connect(m_exportButton,
            &QPushButton::clicked,
            this,
            [this] { exportMetadata(); });
    connect(m_refreshButton, &QPushButton::clicked, this, [this] { loadMetadata(); });
    connect(m_tree,
            &QTreeWidget::itemSelectionChanged,
            this,
            [this] { updateActions(); });
    connect(m_tree,
            &QTreeWidget::itemDoubleClicked,
            this,
            [this](QTreeWidgetItem *, int) {
                if (selectedItemEditable()) {
                    editMetadata();
                }
            });
}

void MainWindow::createMenus()
{
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    auto *openAction = fileMenu->addAction(QStringLiteral("&Open…"));
    openAction->setShortcut(QKeySequence::Open);
    auto *exitAction = fileMenu->addAction(QStringLiteral("E&xit"));
    exitAction->setShortcut(QKeySequence::Quit);

    connect(openAction, &QAction::triggered, this, [this] { chooseFile(); });
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto *settingsMenu = menuBar()->addMenu(QStringLiteral("&Settings"));

    auto *copyScopeMenu = settingsMenu->addMenu(QStringLiteral("Copy to Clipboard"));
    auto *copyScopeGroup = new QActionGroup(copyScopeMenu);
    copyScopeGroup->setExclusive(true);
    auto *copyEditableAction =
        copyScopeMenu->addAction(QStringLiteral("Editable key/values"));
    auto *copyAllAction = copyScopeMenu->addAction(QStringLiteral("All key/values"));
    copyEditableAction->setCheckable(true);
    copyAllAction->setCheckable(true);
    copyScopeGroup->addAction(copyEditableAction);
    copyScopeGroup->addAction(copyAllAction);

    const MetadataScope copyScope =
        configuredScope(QStringLiteral("copy/scope"), MetadataScope::Editable);
    copyEditableAction->setChecked(copyScope == MetadataScope::Editable);
    copyAllAction->setChecked(copyScope == MetadataScope::All);

    connect(copyEditableAction, &QAction::triggered, this, [this] {
        QSettings().setValue(QStringLiteral("copy/scope"), QStringLiteral("editable"));
        updateActions();
    });
    connect(copyAllAction, &QAction::triggered, this, [this] {
        QSettings().setValue(QStringLiteral("copy/scope"), QStringLiteral("all"));
        updateActions();
    });

    auto *exportScopeMenu = settingsMenu->addMenu(QStringLiteral("Export"));
    auto *exportScopeGroup = new QActionGroup(exportScopeMenu);
    exportScopeGroup->setExclusive(true);
    auto *exportAllAction = exportScopeMenu->addAction(QStringLiteral("All key/values"));
    auto *exportEditableAction =
        exportScopeMenu->addAction(QStringLiteral("Editable key/values"));
    exportAllAction->setCheckable(true);
    exportEditableAction->setCheckable(true);
    exportScopeGroup->addAction(exportAllAction);
    exportScopeGroup->addAction(exportEditableAction);

    const MetadataScope exportScope =
        configuredScope(QStringLiteral("export/scope"), MetadataScope::All);
    exportAllAction->setChecked(exportScope == MetadataScope::All);
    exportEditableAction->setChecked(exportScope == MetadataScope::Editable);

    connect(exportAllAction, &QAction::triggered, this, [this] {
        QSettings().setValue(QStringLiteral("export/scope"), QStringLiteral("all"));
        updateActions();
    });
    connect(exportEditableAction, &QAction::triggered, this, [this] {
        QSettings().setValue(QStringLiteral("export/scope"),
                             QStringLiteral("editable"));
        updateActions();
    });

    auto *aboutMenu = menuBar()->addMenu(QStringLiteral("&About"));
    auto *aboutMetadata = aboutMenu->addAction(QStringLiteral("About metadata"));
    auto *aboutQt = aboutMenu->addAction(QStringLiteral("About Qt"));

    connect(aboutMetadata,
            &QAction::triggered,
            this,
            [this] { showAboutMetadata(); });
    connect(aboutQt,
            &QAction::triggered,
            this,
            [this] { QMessageBox::aboutQt(this); });
}

void MainWindow::updateActions()
{
    const bool fileLoaded = !m_currentFile.isEmpty();
    const bool editable = selectedItemEditable();
    const bool metadataAvailable = !m_metadataEntries.isEmpty();

    m_addButton->setEnabled(fileLoaded);
    m_editButton->setEnabled(fileLoaded && editable);
    m_removeButton->setEnabled(fileLoaded && editable);
    m_removeAllButton->setEnabled(
        fileLoaded && !MetadataBackend::isProprietaryRaw(m_currentFile));
    m_copyButton->setEnabled(metadataAvailable);
    m_exportButton->setEnabled(metadataAvailable);
    m_refreshButton->setEnabled(fileLoaded);
}

void MainWindow::loadMetadata()
{
    if (m_currentFile.isEmpty()) {
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const MetadataResult result = m_backend.read(m_currentFile);
    QApplication::restoreOverrideCursor();

    m_tree->clear();
    m_metadataEntries.clear();
    if (!result.ok) {
        QMessageBox::critical(this, QStringLiteral("Metadata error"), result.message);
        updateActions();
        return;
    }

    m_metadataEntries = result.entries;
    m_tree->setSortingEnabled(false);
    int embeddedCount = 0;
    for (const MetadataEntry &entry : result.entries) {
        auto *item = new QTreeWidgetItem({entry.group, entry.tag, entry.value});
        item->setData(0, FullTagRole, entry.fullTag);
        item->setData(0, EditableRole, entry.editable);

        if (!entry.editable) {
            const QBrush disabledBrush(
                palette().color(QPalette::Disabled, QPalette::Text));
            for (int column = 0; column < 3; ++column) {
                item->setForeground(column, disabledBrush);
                item->setToolTip(column,
                                 QStringLiteral("Read-only file or derived property"));
            }
        }
        if (entry.embedded) {
            ++embeddedCount;
        }
        m_tree->addTopLevelItem(item);
    }
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(0, Qt::AscendingOrder);

    QString status = QStringLiteral("%1 fields shown; %2 embedded metadata fields.")
                         .arg(result.entries.size())
                         .arg(embeddedCount);
    if (embeddedCount == 0) {
        status += QStringLiteral(" No editable embedded metadata was detected.");
    }
    if (!result.message.isEmpty()) {
        status += QStringLiteral(" ") + result.message;
    }
    statusBar()->showMessage(status);
    updateActions();
}

void MainWindow::showOperationResult(const MetadataResult &result,
                                     const QString &successFallback)
{
    if (!result.ok) {
        QMessageBox::critical(this, QStringLiteral("Metadata error"), result.message);
        return;
    }

    loadMetadata();
    statusBar()->showMessage(
        result.message.isEmpty() ? successFallback : result.message,
        10000);
}

void MainWindow::chooseFile()
{
    const QString initialDirectory = m_currentFile.isEmpty()
        ? QString()
        : QFileInfo(m_currentFile).absolutePath();
    const QString selected = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open file"),
        initialDirectory,
        QStringLiteral("All files (*)"));

    if (!selected.isEmpty()) {
        openPath(selected);
    }
}

void MainWindow::addMetadata()
{
    TagDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Add metadata"));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (dialog.tag().isEmpty() || dialog.value().isEmpty()) {
        QMessageBox::warning(this,
                             QStringLiteral("Add metadata"),
                             QStringLiteral("Both tag and value are required. Use "
                                            "Remove to delete a field."));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const MetadataResult result =
        m_backend.addOrEdit(m_currentFile, dialog.tag(), dialog.value());
    QApplication::restoreOverrideCursor();
    showOperationResult(result, QStringLiteral("Metadata added."));
}

void MainWindow::editMetadata()
{
    QTreeWidgetItem *item = selectedItem();
    if (item == nullptr || !selectedItemEditable()) {
        return;
    }

    TagDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Edit metadata"));
    dialog.setTag(selectedFullTag(), true);
    dialog.setValue(item->text(2));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (dialog.value().isEmpty()) {
        QMessageBox::warning(this,
                             QStringLiteral("Edit metadata"),
                             QStringLiteral("A value is required. Use Remove to "
                                            "delete this field."));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const MetadataResult result =
        m_backend.addOrEdit(m_currentFile, dialog.tag(), dialog.value());
    QApplication::restoreOverrideCursor();
    showOperationResult(result, QStringLiteral("Metadata updated."));
}

void MainWindow::removeMetadata()
{
    const QString tag = selectedFullTag();
    if (tag.isEmpty() || !selectedItemEditable()) {
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        QStringLiteral("Remove metadata"),
        QStringLiteral("Remove the metadata field “%1”?\n\nThis operation modifies "
                       "the file directly.")
            .arg(tag),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const MetadataResult result = m_backend.remove(m_currentFile, tag);
    QApplication::restoreOverrideCursor();
    showOperationResult(result, QStringLiteral("Metadata removed."));
}

void MainWindow::removeAllMetadata()
{
    if (MetadataBackend::isProprietaryRaw(m_currentFile)) {
        QMessageBox::warning(
            this,
            QStringLiteral("Remove All blocked"),
            QStringLiteral("Removing all metadata from proprietary RAW files can "
                           "remove rendering-critical information."));
        return;
    }

    QString warning =
        QStringLiteral("Remove all removable metadata from this file?\n\n"
                       "This is irreversible. Some formats contain structural or "
                       "colour-management data that may affect rendering.");
    if (MetadataBackend::isPdf(m_currentFile)) {
        warning += QStringLiteral("\n\nThe PDF will be rewritten with qpdf so that "
                                  "old incremental metadata objects are discarded.");
    }

    const QMessageBox::StandardButton answer = QMessageBox::warning(
        this,
        QStringLiteral("Remove all metadata"),
        warning,
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) {
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const MetadataResult result = m_backend.removeAll(m_currentFile);
    QApplication::restoreOverrideCursor();
    showOperationResult(result, QStringLiteral("All removable metadata removed."));
}

void MainWindow::copyMetadataToClipboard()
{
    const MetadataScope scope =
        configuredScope(QStringLiteral("copy/scope"), MetadataScope::Editable);
    const QList<MetadataEntry> entries = entriesForScope(scope);
    if (entries.isEmpty()) {
        QMessageBox::information(
            this,
            QStringLiteral("Copy metadata"),
            QStringLiteral("No metadata fields match the selected copy setting."));
        return;
    }

    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) {
        QMessageBox::critical(this,
                              QStringLiteral("Copy metadata"),
                              QStringLiteral("The system clipboard is unavailable."));
        return;
    }

    clipboard->setText(makeAsciiTable(entries));
    statusBar()->showMessage(
        QStringLiteral("Copied %1 metadata fields to the clipboard.").arg(entries.size()),
        10000);
}

void MainWindow::exportMetadata()
{
    const MetadataScope scope =
        configuredScope(QStringLiteral("export/scope"), MetadataScope::All);
    const QList<MetadataEntry> entries = entriesForScope(scope);
    if (entries.isEmpty()) {
        QMessageBox::information(
            this,
            QStringLiteral("Export metadata"),
            QStringLiteral("No metadata fields match the selected export setting."));
        return;
    }

    const QFileInfo sourceInfo(m_currentFile);
    const QString defaultPath =
        QDir(sourceInfo.absolutePath()).filePath(sourceInfo.fileName()
                                                + QStringLiteral(".txt"));
    const QString exportPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export metadata"),
        defaultPath,
        QStringLiteral("Text files (*.txt);;All files (*)"));
    if (exportPath.isEmpty()) {
        return;
    }

    QSaveFile output(exportPath);
    if (!output.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this,
                              QStringLiteral("Export metadata"),
                              QStringLiteral("Could not open the export file:\n%1")
                                  .arg(output.errorString()));
        return;
    }

    const QByteArray payload = makeAsciiTable(entries).toUtf8();
    const qint64 written = output.write(payload);
    if (written != static_cast<qint64>(payload.size()) || !output.commit()) {
        QMessageBox::critical(this,
                              QStringLiteral("Export metadata"),
                              QStringLiteral("Could not write the export file:\n%1")
                                  .arg(output.errorString()));
        return;
    }

    statusBar()->showMessage(
        QStringLiteral("Exported %1 metadata fields to %2.")
            .arg(entries.size())
            .arg(exportPath),
        10000);
}

void MainWindow::showAboutMetadata()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("About metadata"));
    dialog.setWindowIcon(windowIcon());
    dialog.setModal(true);

    auto *iconLabel = new QLabel(&dialog);
    iconLabel->setPixmap(windowIcon().pixmap(64, 64));
    iconLabel->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    auto *textLabel = new QLabel(&dialog);
    textLabel->setTextFormat(Qt::RichText);
    textLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    textLabel->setOpenExternalLinks(true);
    textLabel->setText(
        QStringLiteral("<h3>metadata</h3>"
                       "<p>metadata version %1</p>"
                       "<p><a href=\"%2\">%2</a></p>")
            .arg(QApplication::applicationVersion(),
                 QString::fromLatin1(MetadataBuildInfo::RepositoryUrl)));

    auto *contentLayout = new QHBoxLayout;
    contentLayout->addWidget(iconLabel);
    contentLayout->addWidget(textLabel, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);

    auto *layout = new QVBoxLayout(&dialog);
    layout->addLayout(contentLayout);
    layout->addWidget(buttons);
    dialog.exec();
}

QTreeWidgetItem *MainWindow::selectedItem() const
{
    const QList<QTreeWidgetItem *> selected = m_tree->selectedItems();
    return selected.isEmpty() ? nullptr : selected.first();
}

QString MainWindow::selectedFullTag() const
{
    const QTreeWidgetItem *item = selectedItem();
    return item == nullptr ? QString() : item->data(0, FullTagRole).toString();
}

bool MainWindow::selectedItemEditable() const
{
    const QTreeWidgetItem *item = selectedItem();
    return item != nullptr && item->data(0, EditableRole).toBool();
}

MainWindow::MetadataScope MainWindow::configuredScope(
    const QString &settingsKey,
    const MetadataScope defaultScope) const
{
    const QString defaultValue = defaultScope == MetadataScope::All
        ? QStringLiteral("all")
        : QStringLiteral("editable");
    const QString value = QSettings().value(settingsKey, defaultValue).toString();
    if (value.compare(QStringLiteral("all"), Qt::CaseInsensitive) == 0) {
        return MetadataScope::All;
    }
    if (value.compare(QStringLiteral("editable"), Qt::CaseInsensitive) == 0) {
        return MetadataScope::Editable;
    }
    return defaultScope;
}

QList<MetadataEntry> MainWindow::entriesForScope(const MetadataScope scope) const
{
    if (scope == MetadataScope::All) {
        return m_metadataEntries;
    }

    QList<MetadataEntry> editableEntries;
    editableEntries.reserve(m_metadataEntries.size());
    for (const MetadataEntry &entry : m_metadataEntries) {
        if (entry.editable) {
            editableEntries.append(entry);
        }
    }
    return editableEntries;
}

QString MainWindow::makeAsciiTable(const QList<MetadataEntry> &entries)
{
    struct TableRow
    {
        QString key;
        QString value;
    };

    QList<TableRow> rows;
    qsizetype keyWidth = QStringLiteral("Key").size();
    qsizetype valueWidth = QStringLiteral("Value").size();

    for (const MetadataEntry &entry : entries) {
        const QStringList keyLines = cellLines(entry.fullTag);
        const QStringList valueLines = cellLines(entry.value);
        const qsizetype lineCount = std::max(keyLines.size(), valueLines.size());

        for (qsizetype line = 0; line < lineCount; ++line) {
            const QString key = line < keyLines.size() ? keyLines.at(line) : QString();
            const QString value =
                line < valueLines.size() ? valueLines.at(line) : QString();
            keyWidth = std::max(keyWidth, key.size());
            valueWidth = std::max(valueWidth, value.size());
            rows.append({key, value});
        }
    }

    const QString separator =
        QStringLiteral("+%1+%2+")
            .arg(QString(keyWidth + 2, QLatin1Char('-')),
                 QString(valueWidth + 2, QLatin1Char('-')));

    QStringList output;
    output.reserve(rows.size() + 4);
    output.append(separator);
    output.append(QStringLiteral("| %1 | %2 |")
                      .arg(QStringLiteral("Key").leftJustified(keyWidth),
                           QStringLiteral("Value").leftJustified(valueWidth)));
    output.append(separator);
    for (const TableRow &row : rows) {
        output.append(QStringLiteral("| %1 | %2 |")
                          .arg(row.key.leftJustified(keyWidth),
                               row.value.leftJustified(valueWidth)));
    }
    output.append(separator);

    return output.join(QLatin1Char('\n')) + QLatin1Char('\n');
}
