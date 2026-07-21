#pragma once

#include "MetadataBackend.h"

#include <QHash>
#include <QMainWindow>

class QCloseEvent;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void openPath(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    enum ItemDataRole
    {
        FullTagRole = Qt::UserRole + 1,
        EditableRole,
        PendingRemovalRole,
        OriginalEntryRole,
    };

    enum class MetadataScope
    {
        All,
        Editable,
    };

    void createUi();
    void createMenus();
    void updateActions();
    void loadMetadata();
    void rebuildMetadataView();
    void showOperationResult(const MetadataResult &result,
                             const QString &successFallback);

    void chooseFile();
    void addMetadata();
    void editMetadata();
    void removeMetadata();
    void removeAllMetadata();
    void applyMetadata();
    void refreshMetadata();
    void copyMetadataToClipboard();
    void exportMetadata();
    void showAboutMetadata();

    [[nodiscard]] bool maybeDiscardPendingChanges(const QString &action);
    [[nodiscard]] bool hasPendingChanges() const;
    [[nodiscard]] qsizetype pendingChangeCount() const;
    [[nodiscard]] bool baseContainsTag(const QString &tag) const;
    [[nodiscard]] QString baseValueForTag(const QString &tag) const;
    void stageValue(const QString &tag, const QString &value);
    void stageRemoval(const QString &tag);
    void clearPendingChanges();

    [[nodiscard]] QTreeWidgetItem *selectedItem() const;
    [[nodiscard]] QString selectedFullTag() const;
    [[nodiscard]] bool selectedItemEditable() const;
    [[nodiscard]] MetadataScope configuredScope(const QString &settingsKey,
                                                 MetadataScope defaultScope) const;
    [[nodiscard]] QList<MetadataEntry> entriesForScope(MetadataScope scope) const;
    [[nodiscard]] static QString makeAsciiTable(const QList<MetadataEntry> &entries);
    [[nodiscard]] static QString changeKey(const QString &tag);

    MetadataBackend m_backend;
    QString m_currentFile;
    QList<MetadataEntry> m_baseMetadataEntries;
    QList<MetadataEntry> m_metadataEntries;
    QHash<QString, MetadataChange> m_pendingChanges;
    bool m_removeAllPending = false;
    QString m_lastReadMessage;

    QLineEdit *m_filePath = nullptr;
    QTreeWidget *m_tree = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QPushButton *m_removeAllButton = nullptr;
    QPushButton *m_applyButton = nullptr;
    QPushButton *m_copyButton = nullptr;
    QPushButton *m_exportButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
};
