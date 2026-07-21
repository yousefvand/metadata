#include "MetadataBackend.h"

#include "DocumentMetadata.h"

#include <algorithm>
#include <QDateTime>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace
{
QString absoluteFilePath(const QString &filePath)
{
    return QFileInfo(filePath).absoluteFilePath();
}

QString temporaryTemplateFor(const QString &filePath)
{
    const QFileInfo info(filePath);
    const QString suffixPart = info.suffix().isEmpty()
        ? QString()
        : QLatin1Char('.') + info.suffix();
    return info.absolutePath() + QStringLiteral("/.") + info.completeBaseName()
        + QStringLiteral(".metadata-XXXXXX") + suffixPart;
}

bool copyFileToDevice(const QString &sourcePath,
                      QIODevice *destination,
                      QString *error)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("Could not read %1: %2")
                         .arg(sourcePath, source.errorString());
        }
        return false;
    }

    QByteArray buffer;
    buffer.resize(1024 * 1024);
    while (!source.atEnd()) {
        const qint64 amount = source.read(buffer.data(), buffer.size());
        if (amount < 0) {
            if (error != nullptr) {
                *error = QStringLiteral("Could not read %1: %2")
                             .arg(sourcePath, source.errorString());
            }
            return false;
        }
        if (amount == 0) {
            break;
        }
        if (destination->write(buffer.constData(), amount) != amount) {
            if (error != nullptr) {
                *error = QStringLiteral("Could not write a temporary file.");
            }
            return false;
        }
    }
    return true;
}

} // namespace

bool MetadataBackend::exifToolAvailable() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("exiftool")).isEmpty();
}

bool MetadataBackend::qpdfAvailable() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("qpdf")).isEmpty();
}

MetadataResult MetadataBackend::read(const QString &filePath) const
{
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        return {false, QStringLiteral("The selected path is not a regular file."), {}};
    }

    if (DocumentMetadata::isSupportedDocument(filePath)) {
        MetadataResult documentResult = DocumentMetadata::read(filePath);
        if (!documentResult.ok) {
            return documentResult;
        }

        if (exifToolAvailable()) {
            const MetadataResult exifResult = readExif(filePath);
            if (exifResult.ok) {
                for (MetadataEntry entry : exifResult.entries) {
                    if (entry.group == QStringLiteral("ExifTool")
                        || entry.group == QStringLiteral("File")
                        || entry.group == QStringLiteral("System")
                        || entry.group == QStringLiteral("Composite")
                        || entry.group == QStringLiteral("ZIP")) {
                        entry.editable = false;
                        entry.embedded = false;
                        documentResult.entries.append(entry);
                    }
                }
            }
        }

        std::sort(documentResult.entries.begin(), documentResult.entries.end(),
                  [](const MetadataEntry &left, const MetadataEntry &right) {
                      const int groupComparison =
                          QString::localeAwareCompare(left.group, right.group);
                      if (groupComparison != 0) {
                          return groupComparison < 0;
                      }
                      return QString::localeAwareCompare(left.tag, right.tag) < 0;
                  });
        return documentResult;
    }

    return readExif(filePath);
}

MetadataResult MetadataBackend::readExif(const QString &filePath) const
{
    if (!exifToolAvailable()) {
        return {false,
                QStringLiteral("ExifTool was not found. Install the Arch package "
                               "perl-image-exiftool."),
                {}};
    }

    const QStringList arguments = {
        QStringLiteral("-j"),
        QStringLiteral("-G1"),
        QStringLiteral("-a"),
        QStringLiteral("-s"),
        QStringLiteral("-charset"),
        QStringLiteral("filename=UTF8"),
        QStringLiteral("-api"),
        QStringLiteral("LargeFileSupport=1"),
        absoluteFilePath(filePath),
    };

    const ProcessResult process = runProcess(QStringLiteral("exiftool"), arguments);
    if (!process.launched || process.timedOut || process.exitCode != 0) {
        return {false, processErrorText(QStringLiteral("ExifTool"), process), {}};
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(process.standardOutput.toUtf8(), &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isArray()
        || document.array().isEmpty()) {
        return {false,
                QStringLiteral("ExifTool returned invalid JSON: %1")
                    .arg(parseError.errorString()),
                {}};
    }

    const QJsonObject object = document.array().first().toObject();
    QList<MetadataEntry> entries;
    entries.reserve(object.size());

    static const QRegularExpression bracketKey(
        QStringLiteral(R"(^\[([^\]]+)\](.+)$)"));

    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
        const QString originalKey = iterator.key();
        if (originalKey == QStringLiteral("SourceFile")) {
            continue;
        }

        QString group = QStringLiteral("General");
        QString tag = originalKey;

        const QRegularExpressionMatch bracketMatch = bracketKey.match(originalKey);
        if (bracketMatch.hasMatch()) {
            group = bracketMatch.captured(1);
            tag = bracketMatch.captured(2);
        } else {
            const qsizetype colon = originalKey.indexOf(QLatin1Char(':'));
            if (colon > 0) {
                group = originalKey.left(colon);
                tag = originalKey.mid(colon + 1);
            }
        }

        const QString fullTag =
            group == QStringLiteral("General") ? tag : group + QLatin1Char(':') + tag;

        entries.append({group,
                        tag,
                        jsonValueToString(iterator.value()),
                        fullTag,
                        !isReadOnlyGroup(group),
                        isEmbeddedGroup(group)});
    }

    std::sort(entries.begin(), entries.end(), [](const MetadataEntry &left,
                                                  const MetadataEntry &right) {
        const int groupComparison =
            QString::localeAwareCompare(left.group, right.group);
        if (groupComparison != 0) {
            return groupComparison < 0;
        }
        return QString::localeAwareCompare(left.tag, right.tag) < 0;
    });

    QString message;
    if (!process.standardError.trimmed().isEmpty()) {
        message = process.standardError.trimmed();
    }

    return {true, message, entries};
}

MetadataResult MetadataBackend::applyChanges(
    const QString &filePath,
    const bool removeAll,
    const QList<MetadataChange> &changes) const
{
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        return {false, QStringLiteral("The selected file no longer exists."), {}};
    }
    if (!info.isWritable()) {
        return {false, QStringLiteral("The selected file is not writable."), {}};
    }
    if (isProprietaryRaw(filePath) && removeAll) {
        return {false,
                QStringLiteral("Remove All is blocked for proprietary RAW files. "
                               "Their metadata may contain information required to "
                               "render the image."),
                {}};
    }
    if (!removeAll && changes.isEmpty()) {
        return {true, QStringLiteral("There are no pending metadata changes."), {}};
    }

    for (const MetadataChange &change : changes) {
        const QString validation = tagValidationError(filePath, change.tag);
        if (!validation.isEmpty()) {
            return {false, validation, {}};
        }
    }

    QTemporaryFile temporary(temporaryTemplateFor(filePath));
    temporary.setAutoRemove(true);
    if (!temporary.open()) {
        return {false,
                QStringLiteral("Could not create a temporary file beside the source: %1")
                    .arg(temporary.errorString()),
                {}};
    }

    QString copyError;
    if (!copyFileToDevice(filePath, &temporary, &copyError) || !temporary.flush()) {
        return {false, copyError.isEmpty() ? temporary.errorString() : copyError, {}};
    }
    const QString temporaryPath = temporary.fileName();
    temporary.close();

    MetadataResult operationResult;
    if (DocumentMetadata::isSupportedDocument(filePath)) {
        operationResult = DocumentMetadata::apply(temporaryPath, removeAll, changes);
    } else {
        operationResult = runExifWrites(temporaryPath, removeAll, changes);
    }
    if (!operationResult.ok) {
        return operationResult;
    }

    return commitTemporaryFile(temporaryPath, filePath, operationResult.message);
}

MetadataResult MetadataBackend::addOrEdit(const QString &filePath,
                                          const QString &tag,
                                          const QString &value) const
{
    return applyChanges(filePath, false, {{tag, value, false}});
}

MetadataResult MetadataBackend::remove(const QString &filePath,
                                       const QString &tag) const
{
    return applyChanges(filePath, false, {{tag, {}, true}});
}

MetadataResult MetadataBackend::removeAll(const QString &filePath) const
{
    return applyChanges(filePath, true, {});
}

QString MetadataBackend::tagValidationError(const QString &filePath,
                                            const QString &tag) const
{
    if (DocumentMetadata::isSupportedDocument(filePath)) {
        return DocumentMetadata::validationError(filePath, tag);
    }

    const QString cleanTag = tag.trimmed();
    if (!validTagName(cleanTag)) {
        return QStringLiteral("Invalid tag name. Use letters, numbers, '.', '_', "
                              "'-', or ':'; for example XMP-dc:Title.");
    }
    return {};
}

QString MetadataBackend::tagHint(const QString &filePath) const
{
    const QString documentHint = DocumentMetadata::tagHint(filePath);
    if (!documentHint.isEmpty()) {
        return documentHint;
    }
    return QStringLiteral(
        "Use an ExifTool tag name. A group prefix is recommended, for example "
        "XMP-dc:Title, EXIF:Artist, IPTC:Keywords, or PDF:Author.");
}

bool MetadataBackend::isProprietaryRaw(const QString &filePath)
{
    static const QSet<QString> rawExtensions = {
        QStringLiteral("3fr"), QStringLiteral("arq"), QStringLiteral("arw"),
        QStringLiteral("cr2"), QStringLiteral("cr3"), QStringLiteral("crw"),
        QStringLiteral("dcr"), QStringLiteral("erf"), QStringLiteral("fff"),
        QStringLiteral("iiq"), QStringLiteral("k25"), QStringLiteral("kdc"),
        QStringLiteral("mef"), QStringLiteral("mos"), QStringLiteral("mrw"),
        QStringLiteral("nef"), QStringLiteral("nrw"), QStringLiteral("orf"),
        QStringLiteral("pef"), QStringLiteral("raf"), QStringLiteral("raw"),
        QStringLiteral("rw2"), QStringLiteral("rwl"), QStringLiteral("sr2"),
        QStringLiteral("srf"), QStringLiteral("srw"), QStringLiteral("x3f"),
    };

    return rawExtensions.contains(QFileInfo(filePath).suffix().toLower());
}

bool MetadataBackend::isPdf(const QString &filePath)
{
    return QFileInfo(filePath).suffix().compare(QStringLiteral("pdf"),
                                                Qt::CaseInsensitive)
        == 0;
}

bool MetadataBackend::isDocumentPackage(const QString &filePath)
{
    return DocumentMetadata::isSupportedDocument(filePath);
}

MetadataBackend::ProcessResult MetadataBackend::runProcess(
    const QString &program,
    const QStringList &arguments,
    const int timeoutMs)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(program, arguments, QIODevice::ReadOnly);

    ProcessResult result;
    result.launched = process.waitForStarted(5000);
    if (!result.launched) {
        result.standardError = process.errorString();
        return result;
    }

    if (!process.waitForFinished(timeoutMs)) {
        result.timedOut = true;
        process.kill();
        process.waitForFinished(3000);
    }

    result.exitCode = process.exitCode();
    result.standardOutput = QString::fromUtf8(process.readAllStandardOutput());
    result.standardError = QString::fromUtf8(process.readAllStandardError());
    return result;
}

QString MetadataBackend::combinedProcessText(const ProcessResult &result)
{
    QStringList parts;
    if (!result.standardError.trimmed().isEmpty()) {
        parts.append(result.standardError.trimmed());
    }
    if (!result.standardOutput.trimmed().isEmpty()) {
        parts.append(result.standardOutput.trimmed());
    }
    return parts.join(QStringLiteral("\n"));
}

QString MetadataBackend::processErrorText(const QString &program,
                                          const ProcessResult &result)
{
    if (!result.launched) {
        return QStringLiteral("%1 could not be started: %2")
            .arg(program, result.standardError);
    }
    if (result.timedOut) {
        return QStringLiteral("%1 timed out.").arg(program);
    }

    const QString details = combinedProcessText(result);
    if (details.isEmpty()) {
        return QStringLiteral("%1 failed with exit code %2.")
            .arg(program)
            .arg(result.exitCode);
    }

    return QStringLiteral("%1 failed with exit code %2:\n%3")
        .arg(program)
        .arg(result.exitCode)
        .arg(details);
}

QString MetadataBackend::jsonValueToString(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 15);
    }
    if (value.isNull() || value.isUndefined()) {
        return {};
    }

    if (value.isArray()) {
        return QString::fromUtf8(
            QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }

    return QString::fromUtf8(
        QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
}

bool MetadataBackend::isReadOnlyGroup(const QString &group)
{
    static const QSet<QString> readOnlyGroups = {
        QStringLiteral("ExifTool"), QStringLiteral("File"),
        QStringLiteral("System"), QStringLiteral("Composite"),
        QStringLiteral("General"), QStringLiteral("ZIP"),
        QStringLiteral("ZIP64"), QStringLiteral("OOXML"),
        QStringLiteral("OpenDocument"),
    };
    return readOnlyGroups.contains(group);
}

bool MetadataBackend::isEmbeddedGroup(const QString &group)
{
    return !isReadOnlyGroup(group);
}

bool MetadataBackend::validTagName(const QString &tag)
{
    static const QRegularExpression valid(
        QStringLiteral(R"(^[A-Za-z0-9][A-Za-z0-9_.:-]*$)"));
    if (!valid.match(tag).hasMatch()) {
        return false;
    }

    if (!tag.contains(QLatin1Char(':'))) {
        static const QSet<QString> reservedOptionNames = {
            QStringLiteral("api"), QStringLiteral("argfile"),
            QStringLiteral("charset"), QStringLiteral("common_args"),
            QStringLiteral("config"), QStringLiteral("execute"),
            QStringLiteral("ext"), QStringLiteral("fileorder"),
            QStringLiteral("geotag"), QStringLiteral("if"),
            QStringLiteral("overwrite_original"), QStringLiteral("p"),
            QStringLiteral("stay_open"), QStringLiteral("tagsfromfile"),
        };
        if (reservedOptionNames.contains(tag.toLower())) {
            return false;
        }
    }

    return true;
}

MetadataResult MetadataBackend::runExifWrites(
    const QString &filePath,
    const bool removeAll,
    const QList<MetadataChange> &changes) const
{
    if (!exifToolAvailable()) {
        return {false,
                QStringLiteral("ExifTool was not found. Install perl-image-exiftool."),
                {}};
    }
    if (isPdf(filePath) && !qpdfAvailable()) {
        return {false,
                QStringLiteral("qpdf is required for PDF writes so that previous "
                               "incremental metadata revisions can be discarded."),
                {}};
    }

    if (isPdf(filePath) && removeAll) {
        const MetadataResult rewrite = rewritePdf(filePath, true);
        if (!rewrite.ok) {
            return rewrite;
        }
        if (changes.isEmpty()) {
            return rewrite;
        }
    }

    QStringList arguments = {
        QStringLiteral("-overwrite_original"),
        QStringLiteral("-P"),
        QStringLiteral("-charset"),
        QStringLiteral("filename=UTF8"),
    };
    if (removeAll && !isPdf(filePath)) {
        arguments.append(QStringLiteral("-all="));
    }
    for (const MetadataChange &change : changes) {
        QString assignment = QStringLiteral("-") + change.tag.trimmed()
            + QLatin1Char('=');
        if (!change.remove) {
            assignment += change.value;
        }
        arguments.append(assignment);
    }
    arguments.append(absoluteFilePath(filePath));

    const ProcessResult process = runProcess(QStringLiteral("exiftool"), arguments);
    if (!process.launched || process.timedOut || process.exitCode != 0) {
        return {false, processErrorText(QStringLiteral("ExifTool"), process), {}};
    }

    if (isPdf(filePath)) {
        const MetadataResult rewrite = rewritePdf(filePath, false);
        if (!rewrite.ok) {
            return {false,
                    QStringLiteral("ExifTool changed the PDF, but the secure qpdf "
                                   "rewrite failed:\n%1")
                        .arg(rewrite.message),
                    {}};
        }
    }

    const QString output = combinedProcessText(process);
    return {true,
            output.isEmpty() ? QStringLiteral("Metadata changes applied.") : output,
            {}};
}

MetadataResult MetadataBackend::rewritePdf(const QString &filePath,
                                           const bool removeAllMetadata) const
{
    if (!qpdfAvailable()) {
        return {false, QStringLiteral("qpdf was not found. Install the qpdf package."),
                {}};
    }

    QStringList arguments;
    arguments.append(absoluteFilePath(filePath));
    if (removeAllMetadata) {
        arguments.append(QStringLiteral("--remove-info"));
        arguments.append(QStringLiteral("--remove-metadata"));
    }
    arguments.append(QStringLiteral("--replace-input"));

    const ProcessResult process = runProcess(QStringLiteral("qpdf"), arguments);
    const bool qpdfSucceeded = process.launched && !process.timedOut
        && (process.exitCode == 0 || process.exitCode == 3);

    if (!qpdfSucceeded) {
        return {false, processErrorText(QStringLiteral("qpdf"), process), {}};
    }

    QFile::remove(absoluteFilePath(filePath) + QStringLiteral(".~qpdf-orig"));

    QString message = combinedProcessText(process);
    if (message.isEmpty()) {
        message = removeAllMetadata
            ? QStringLiteral("PDF information dictionaries and metadata streams removed.")
            : QStringLiteral("PDF rewritten to discard prior incremental revisions.");
    }

    return {true, message, {}};
}

MetadataResult MetadataBackend::commitTemporaryFile(
    const QString &temporaryPath,
    const QString &destinationPath,
    const QString &successMessage)
{
    const QFileInfo originalInfo(destinationPath);
    const QFileDevice::Permissions permissions = originalInfo.permissions();
    const QDateTime modified = originalInfo.lastModified();

    QSaveFile destination(destinationPath);
    destination.setDirectWriteFallback(false);
    if (!destination.open(QIODevice::WriteOnly)) {
        return {false,
                QStringLiteral("Could not prepare the final file: %1")
                    .arg(destination.errorString()),
                {}};
    }

    QString copyError;
    if (!copyFileToDevice(temporaryPath, &destination, &copyError)) {
        destination.cancelWriting();
        return {false, copyError, {}};
    }
    destination.setPermissions(permissions);
    if (!destination.commit()) {
        return {false,
                QStringLiteral("Could not replace the original file safely: %1")
                    .arg(destination.errorString()),
                {}};
    }

    QFile finalFile(destinationPath);
    if (finalFile.open(QIODevice::ReadWrite)) {
        finalFile.setFileTime(modified, QFileDevice::FileModificationTime);
        finalFile.close();
    }

    return {true,
            successMessage.isEmpty() ? QStringLiteral("Metadata changes applied.")
                                     : successMessage,
            {}};
}
