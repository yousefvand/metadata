#pragma once

#include "MetadataBackend.h"

#include <QMainWindow>

class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void openPath(const QString &path);

private:
    enum ItemDataRole
    {
        FullTagRole = Qt::UserRole + 1,
        EditableRole,
    };

    void createUi();
    void createMenus();
    void updateActions();
    void loadMetadata();
    void showOperationResult(const MetadataResult &result,
                             const QString &successFallback);

    void chooseFile();
    void addMetadata();
    void editMetadata();
    void removeMetadata();
    void removeAllMetadata();
    void showAboutMetadata();

    [[nodiscard]] QTreeWidgetItem *selectedItem() const;
    [[nodiscard]] QString selectedFullTag() const;
    [[nodiscard]] bool selectedItemEditable() const;

    MetadataBackend m_backend;
    QString m_currentFile;

    QLineEdit *m_filePath = nullptr;
    QTreeWidget *m_tree = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QPushButton *m_removeAllButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
};
