#pragma once

#include <QList>
#include <QString>
#include <QStringList>

struct MetadataEntry
{
    QString group;
    QString tag;
    QString value;
    QString fullTag;
    bool editable = false;
    bool embedded = false;
};

struct MetadataResult
{
    bool ok = false;
    QString message;
    QList<MetadataEntry> entries;
};

class MetadataBackend
{
public:
    [[nodiscard]] bool exifToolAvailable() const;
    [[nodiscard]] bool qpdfAvailable() const;

    [[nodiscard]] MetadataResult read(const QString &filePath) const;
    [[nodiscard]] MetadataResult addOrEdit(const QString &filePath,
                                           const QString &tag,
                                           const QString &value) const;
    [[nodiscard]] MetadataResult remove(const QString &filePath,
                                        const QString &tag) const;
    [[nodiscard]] MetadataResult removeAll(const QString &filePath) const;

    [[nodiscard]] static bool isProprietaryRaw(const QString &filePath);
    [[nodiscard]] static bool isPdf(const QString &filePath);

private:
    struct ProcessResult
    {
        bool launched = false;
        bool timedOut = false;
        int exitCode = -1;
        QString standardOutput;
        QString standardError;
    };

    [[nodiscard]] static ProcessResult runProcess(const QString &program,
                                                  const QStringList &arguments,
                                                  int timeoutMs = 120000);
    [[nodiscard]] static QString processErrorText(const QString &program,
                                                  const ProcessResult &result);
    [[nodiscard]] static QString combinedProcessText(const ProcessResult &result);
    [[nodiscard]] static QString jsonValueToString(const class QJsonValue &value);
    [[nodiscard]] static bool isReadOnlyGroup(const QString &group);
    [[nodiscard]] static bool isEmbeddedGroup(const QString &group);
    [[nodiscard]] static bool validTagName(const QString &tag);

    [[nodiscard]] MetadataResult runExifWrite(const QString &filePath,
                                              const QString &assignment) const;
    [[nodiscard]] MetadataResult rewritePdf(const QString &filePath,
                                            bool removeAllMetadata) const;
};
