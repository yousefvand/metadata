#include "MetadataBackend.h"

#include <algorithm>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace
{
QString absoluteFilePath(const QString &filePath)
{
    return QFileInfo(filePath).absoluteFilePath();
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

MetadataResult MetadataBackend::addOrEdit(const QString &filePath,
                                          const QString &tag,
                                          const QString &value) const
{
    const QString cleanTag = tag.trimmed();
    if (!validTagName(cleanTag)) {
        return {false,
                QStringLiteral("Invalid tag name. Use letters, numbers, '.', '_', "
                               "'-', or ':'; for example XMP-dc:Title."),
                {}};
    }

    return runExifWrite(filePath,
                        QStringLiteral("-") + cleanTag + QLatin1Char('=') + value);
}

MetadataResult MetadataBackend::remove(const QString &filePath,
                                       const QString &tag) const
{
    const QString cleanTag = tag.trimmed();
    if (!validTagName(cleanTag)) {
        return {false, QStringLiteral("Invalid metadata tag name."), {}};
    }

    return runExifWrite(filePath,
                        QStringLiteral("-") + cleanTag + QLatin1Char('='));
}

MetadataResult MetadataBackend::removeAll(const QString &filePath) const
{
    if (isProprietaryRaw(filePath)) {
        return {false,
                QStringLiteral("Remove All is blocked for proprietary RAW files. "
                               "Their metadata may contain information required to "
                               "render the image."),
                {}};
    }

    if (isPdf(filePath)) {
        return rewritePdf(filePath, true);
    }

    return runExifWrite(filePath, QStringLiteral("-all="));
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
        QStringLiteral("General"),
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

    // Prevent an ungrouped user-supplied tag from being interpreted as an
    // ExifTool command-line option. Group-qualified tags such as XMP:Title are
    // unambiguous tag assignments.
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

MetadataResult MetadataBackend::runExifWrite(const QString &filePath,
                                             const QString &assignment) const
{
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        return {false, QStringLiteral("The selected file no longer exists."), {}};
    }
    if (!info.isWritable()) {
        return {false, QStringLiteral("The selected file is not writable."), {}};
    }
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

    const QStringList arguments = {
        QStringLiteral("-overwrite_original"),
        QStringLiteral("-P"),
        QStringLiteral("-charset"),
        QStringLiteral("filename=UTF8"),
        assignment,
        absoluteFilePath(filePath),
    };

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
            output.isEmpty() ? QStringLiteral("Metadata updated.") : output,
            {}};
}

MetadataResult MetadataBackend::rewritePdf(const QString &filePath,
                                           const bool removeAllMetadata) const
{
    if (!qpdfAvailable()) {
        return {false, QStringLiteral("qpdf was not found. Install the qpdf package."), {}};
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

    // qpdf may preserve the input under this name when it emitted warnings.
    // Removing it prevents an old metadata-bearing copy from being left behind.
    QFile::remove(absoluteFilePath(filePath) + QStringLiteral(".~qpdf-orig"));

    QString message = combinedProcessText(process);
    if (message.isEmpty()) {
        message = removeAllMetadata
            ? QStringLiteral("PDF information dictionaries and metadata streams removed.")
            : QStringLiteral("PDF rewritten to discard prior incremental revisions.");
    }

    return {true, message, {}};
}
