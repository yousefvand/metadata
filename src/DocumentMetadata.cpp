#include "DocumentMetadata.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringList>

#include <zip.h>

namespace
{
constexpr auto CorePart = "docProps/core.xml";
constexpr auto AppPart = "docProps/app.xml";
constexpr auto CustomPart = "docProps/custom.xml";
constexpr auto RootRelationshipsPart = "_rels/.rels";
constexpr auto ContentTypesPart = "[Content_Types].xml";
constexpr auto OdfMetaPart = "meta.xml";
constexpr auto OdfManifestPart = "META-INF/manifest.xml";

const QString NsCore = QStringLiteral(
    "http://schemas.openxmlformats.org/package/2006/metadata/core-properties");
const QString NsDc = QStringLiteral("http://purl.org/dc/elements/1.1/");
const QString NsDcterms = QStringLiteral("http://purl.org/dc/terms/");
const QString NsXsi = QStringLiteral("http://www.w3.org/2001/XMLSchema-instance");
const QString NsExtended = QStringLiteral(
    "http://schemas.openxmlformats.org/officeDocument/2006/extended-properties");
const QString NsVt = QStringLiteral(
    "http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes");
const QString NsCustom = QStringLiteral(
    "http://schemas.openxmlformats.org/officeDocument/2006/custom-properties");
const QString NsRelationships = QStringLiteral(
    "http://schemas.openxmlformats.org/package/2006/relationships");
const QString NsContentTypes = QStringLiteral(
    "http://schemas.openxmlformats.org/package/2006/content-types");

const QString NsOffice = QStringLiteral(
    "urn:oasis:names:tc:opendocument:xmlns:office:1.0");
const QString NsMeta = QStringLiteral(
    "urn:oasis:names:tc:opendocument:xmlns:meta:1.0");
const QString NsManifest = QStringLiteral(
    "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0");

struct XmlProperty
{
    QString publicTag;
    QString displayTag;
    QString namespaceUri;
    QString qualifiedName;
    QString part;
    bool dateValue = false;
};

const QList<XmlProperty> &officeProperties()
{
    static const QList<XmlProperty> properties = {
        {QStringLiteral("Office:Title"), QStringLiteral("Title"), NsDc,
         QStringLiteral("dc:title"), QString::fromLatin1(CorePart), false},
        {QStringLiteral("Office:Subject"), QStringLiteral("Subject"), NsDc,
         QStringLiteral("dc:subject"), QString::fromLatin1(CorePart), false},
        {QStringLiteral("Office:Author"), QStringLiteral("Author"), NsDc,
         QStringLiteral("dc:creator"), QString::fromLatin1(CorePart), false},
        {QStringLiteral("Office:Keywords"), QStringLiteral("Keywords"), NsCore,
         QStringLiteral("cp:keywords"), QString::fromLatin1(CorePart), false},
        {QStringLiteral("Office:Description"), QStringLiteral("Description"), NsDc,
         QStringLiteral("dc:description"), QString::fromLatin1(CorePart), false},
        {QStringLiteral("Office:Identifier"), QStringLiteral("Identifier"), NsDc,
         QStringLiteral("dc:identifier"), QString::fromLatin1(CorePart), false},
        {QStringLiteral("Office:Language"), QStringLiteral("Language"), NsDc,
         QStringLiteral("dc:language"), QString::fromLatin1(CorePart), false},
        {QStringLiteral("Office:LastModifiedBy"), QStringLiteral("LastModifiedBy"),
         NsCore, QStringLiteral("cp:lastModifiedBy"), QString::fromLatin1(CorePart),
         false},
        {QStringLiteral("Office:Revision"), QStringLiteral("Revision"), NsCore,
         QStringLiteral("cp:revision"), QString::fromLatin1(CorePart), false},
        {QStringLiteral("Office:Category"), QStringLiteral("Category"), NsCore,
         QStringLiteral("cp:category"), QString::fromLatin1(CorePart), false},
        {QStringLiteral("Office:ContentStatus"), QStringLiteral("ContentStatus"),
         NsCore, QStringLiteral("cp:contentStatus"), QString::fromLatin1(CorePart),
         false},
        {QStringLiteral("Office:ContentType"), QStringLiteral("ContentType"), NsCore,
         QStringLiteral("cp:contentType"), QString::fromLatin1(CorePart), false},
        {QStringLiteral("Office:Version"), QStringLiteral("Version"), NsCore,
         QStringLiteral("cp:version"), QString::fromLatin1(CorePart), false},
        {QStringLiteral("Office:Created"), QStringLiteral("Created"), NsDcterms,
         QStringLiteral("dcterms:created"), QString::fromLatin1(CorePart), true},
        {QStringLiteral("Office:Modified"), QStringLiteral("Modified"), NsDcterms,
         QStringLiteral("dcterms:modified"), QString::fromLatin1(CorePart), true},
        {QStringLiteral("Office:Application"), QStringLiteral("Application"),
         NsExtended, QStringLiteral("Application"), QString::fromLatin1(AppPart),
         false},
        {QStringLiteral("Office:AppVersion"), QStringLiteral("AppVersion"),
         NsExtended, QStringLiteral("AppVersion"), QString::fromLatin1(AppPart),
         false},
        {QStringLiteral("Office:Company"), QStringLiteral("Company"), NsExtended,
         QStringLiteral("Company"), QString::fromLatin1(AppPart), false},
        {QStringLiteral("Office:Manager"), QStringLiteral("Manager"), NsExtended,
         QStringLiteral("Manager"), QString::fromLatin1(AppPart), false},
        {QStringLiteral("Office:Template"), QStringLiteral("Template"), NsExtended,
         QStringLiteral("Template"), QString::fromLatin1(AppPart), false},
    };
    return properties;
}

struct OdfProperty
{
    QString publicTag;
    QString displayTag;
    QString namespaceUri;
    QString qualifiedName;
    bool multiple = false;
};

const QList<OdfProperty> &odfProperties()
{
    static const QList<OdfProperty> properties = {
        {QStringLiteral("ODF:Title"), QStringLiteral("Title"), NsDc,
         QStringLiteral("dc:title"), false},
        {QStringLiteral("ODF:Subject"), QStringLiteral("Subject"), NsDc,
         QStringLiteral("dc:subject"), false},
        {QStringLiteral("ODF:Description"), QStringLiteral("Description"), NsDc,
         QStringLiteral("dc:description"), false},
        {QStringLiteral("ODF:Keywords"), QStringLiteral("Keywords"), NsMeta,
         QStringLiteral("meta:keyword"), true},
        {QStringLiteral("ODF:InitialCreator"), QStringLiteral("InitialCreator"),
         NsMeta, QStringLiteral("meta:initial-creator"), false},
        {QStringLiteral("ODF:Creator"), QStringLiteral("Creator"), NsDc,
         QStringLiteral("dc:creator"), false},
        {QStringLiteral("ODF:CreationDate"), QStringLiteral("CreationDate"), NsMeta,
         QStringLiteral("meta:creation-date"), false},
        {QStringLiteral("ODF:Modified"), QStringLiteral("Modified"), NsDc,
         QStringLiteral("dc:date"), false},
        {QStringLiteral("ODF:EditingDuration"), QStringLiteral("EditingDuration"),
         NsMeta, QStringLiteral("meta:editing-duration"), false},
        {QStringLiteral("ODF:EditingCycles"), QStringLiteral("EditingCycles"),
         NsMeta, QStringLiteral("meta:editing-cycles"), false},
        {QStringLiteral("ODF:Generator"), QStringLiteral("Generator"), NsMeta,
         QStringLiteral("meta:generator"), false},
        {QStringLiteral("ODF:PrintedBy"), QStringLiteral("PrintedBy"), NsMeta,
         QStringLiteral("meta:printed-by"), false},
        {QStringLiteral("ODF:PrintDate"), QStringLiteral("PrintDate"), NsMeta,
         QStringLiteral("meta:print-date"), false},
        {QStringLiteral("ODF:Language"), QStringLiteral("Language"), NsDc,
         QStringLiteral("dc:language"), false},
    };
    return properties;
}

QString suffix(const QString &filePath)
{
    return QFileInfo(filePath).suffix().toLower();
}

QString customNameForTag(const QString &tag)
{
    const qsizetype separator = tag.indexOf(QLatin1Char(':'));
    return separator < 0 ? QString() : tag.mid(separator + 1).trimmed();
}

bool parseXml(const QByteArray &data, QDomDocument *document, QString *error)
{
    const QDomDocument::ParseResult result = document->setContent(
        data, QDomDocument::ParseOption::UseNamespaceProcessing);
    if (!result) {
        if (error != nullptr) {
            *error = QStringLiteral("Invalid XML at line %1, column %2: %3")
                         .arg(result.errorLine)
                         .arg(result.errorColumn)
                         .arg(result.errorMessage);
        }
        return false;
    }
    return true;
}

QDomElement directChild(const QDomElement &parent,
                        const QString &namespaceUri,
                        const QString &localName)
{
    for (QDomNode node = parent.firstChild(); !node.isNull(); node = node.nextSibling()) {
        const QDomElement element = node.toElement();
        if (!element.isNull() && element.namespaceURI() == namespaceUri
            && element.localName() == localName) {
            return element;
        }
    }
    return {};
}

QList<QDomElement> directChildren(const QDomElement &parent,
                                  const QString &namespaceUri,
                                  const QString &localName)
{
    QList<QDomElement> result;
    for (QDomNode node = parent.firstChild(); !node.isNull(); node = node.nextSibling()) {
        const QDomElement element = node.toElement();
        if (!element.isNull() && element.namespaceURI() == namespaceUri
            && element.localName() == localName) {
            result.append(element);
        }
    }
    return result;
}

void removeDirectChildren(QDomElement parent,
                          const QString &namespaceUri,
                          const QString &localName)
{
    const QList<QDomElement> matches = directChildren(parent, namespaceUri, localName);
    for (const QDomElement &element : matches) {
        parent.removeChild(element);
    }
}

void removeAllElementChildren(QDomElement parent)
{
    QDomNode node = parent.firstChild();
    while (!node.isNull()) {
        const QDomNode next = node.nextSibling();
        if (node.isElement()) {
            parent.removeChild(node);
        }
        node = next;
    }
}

bool readZipEntry(zip_t *archive,
                  const QString &name,
                  QByteArray *data,
                  QString *error,
                  const bool optional = false)
{
    zip_stat_t stat;
    zip_stat_init(&stat);
    const QByteArray encodedName = name.toUtf8();
    if (zip_stat(archive, encodedName.constData(), ZIP_FL_ENC_UTF_8, &stat) != 0) {
        if (optional) {
            data->clear();
            return true;
        }
        if (error != nullptr) {
            *error = QStringLiteral("The document package is missing %1.").arg(name);
        }
        return false;
    }
    if (stat.size > static_cast<zip_uint64_t>(std::numeric_limits<int>::max())) {
        if (error != nullptr) {
            *error = QStringLiteral("%1 is too large to process safely.").arg(name);
        }
        return false;
    }

    zip_file_t *file = zip_fopen(archive, encodedName.constData(), ZIP_FL_ENC_UTF_8);
    if (file == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("Could not open %1 inside the document package.")
                         .arg(name);
        }
        return false;
    }

    data->resize(static_cast<int>(stat.size));
    zip_int64_t total = 0;
    while (total < static_cast<zip_int64_t>(stat.size)) {
        const zip_int64_t amount = zip_fread(
            file, data->data() + total,
            static_cast<zip_uint64_t>(static_cast<zip_int64_t>(stat.size) - total));
        if (amount < 0) {
            zip_fclose(file);
            if (error != nullptr) {
                *error = QStringLiteral("Could not read %1 from the document package.")
                             .arg(name);
            }
            return false;
        }
        if (amount == 0) {
            break;
        }
        total += amount;
    }
    zip_fclose(file);
    if (total != static_cast<zip_int64_t>(stat.size)) {
        data->clear();
        if (error != nullptr) {
            *error = QStringLiteral("Could not read all of %1 from the document package.")
                         .arg(name);
        }
        return false;
    }
    data->resize(static_cast<int>(total));
    return true;
}

bool writeZipEntry(zip_t *archive,
                   const QString &name,
                   const QByteArray &data,
                   QString *error)
{
    const size_t allocationSize = std::max<size_t>(
        1U, static_cast<size_t>(data.size()));
    void *buffer = malloc(allocationSize);
    if (buffer == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("Out of memory while updating %1.").arg(name);
        }
        return false;
    }
    if (!data.isEmpty()) {
        memcpy(buffer, data.constData(), static_cast<size_t>(data.size()));
    }

    zip_source_t *source = zip_source_buffer(archive, buffer,
                                             static_cast<zip_uint64_t>(data.size()), 1);
    if (source == nullptr) {
        free(buffer);
        if (error != nullptr) {
            *error = QStringLiteral("Could not create ZIP data for %1.").arg(name);
        }
        return false;
    }

    const QByteArray encodedName = name.toUtf8();
    const zip_int64_t index = zip_name_locate(
        archive, encodedName.constData(), ZIP_FL_ENC_UTF_8);
    zip_int64_t result = -1;
    if (index >= 0) {
        result = zip_file_replace(archive, static_cast<zip_uint64_t>(index), source, 0);
    } else {
        result = zip_file_add(archive, encodedName.constData(), source, ZIP_FL_ENC_UTF_8);
    }
    if (result < 0) {
        zip_source_free(source);
        if (error != nullptr) {
            *error = QStringLiteral("Could not update %1: %2")
                         .arg(name, QString::fromUtf8(zip_strerror(archive)));
        }
        return false;
    }
    return true;
}

bool deleteZipEntry(zip_t *archive, const QString &name, QString *error)
{
    const QByteArray encodedName = name.toUtf8();
    const zip_int64_t index = zip_name_locate(
        archive, encodedName.constData(), ZIP_FL_ENC_UTF_8);
    if (index < 0) {
        return true;
    }
    if (zip_delete(archive, static_cast<zip_uint64_t>(index)) != 0) {
        if (error != nullptr) {
            *error = QStringLiteral("Could not remove %1: %2")
                         .arg(name, QString::fromUtf8(zip_strerror(archive)));
        }
        return false;
    }
    return true;
}

QDomDocument newCoreDocument()
{
    QDomDocument document;
    QDomElement root = document.createElementNS(NsCore, QStringLiteral("cp:coreProperties"));
    root.setAttribute(QStringLiteral("xmlns:cp"), NsCore);
    root.setAttribute(QStringLiteral("xmlns:dc"), NsDc);
    root.setAttribute(QStringLiteral("xmlns:dcterms"), NsDcterms);
    root.setAttribute(QStringLiteral("xmlns:dcmitype"),
                      QStringLiteral("http://purl.org/dc/dcmitype/"));
    root.setAttribute(QStringLiteral("xmlns:xsi"), NsXsi);
    document.appendChild(root);
    return document;
}

QDomDocument newAppDocument()
{
    QDomDocument document;
    QDomElement root = document.createElementNS(NsExtended, QStringLiteral("Properties"));
    root.setAttribute(QStringLiteral("xmlns"), NsExtended);
    root.setAttribute(QStringLiteral("xmlns:vt"), NsVt);
    document.appendChild(root);
    return document;
}

QDomDocument newCustomDocument()
{
    QDomDocument document;
    QDomElement root = document.createElementNS(NsCustom, QStringLiteral("Properties"));
    root.setAttribute(QStringLiteral("xmlns"), NsCustom);
    root.setAttribute(QStringLiteral("xmlns:vt"), NsVt);
    document.appendChild(root);
    return document;
}

QDomDocument newRelationshipsDocument()
{
    QDomDocument document;
    QDomElement root = document.createElementNS(NsRelationships,
                                                 QStringLiteral("Relationships"));
    root.setAttribute(QStringLiteral("xmlns"), NsRelationships);
    document.appendChild(root);
    return document;
}

QDomDocument newContentTypesDocument()
{
    QDomDocument document;
    QDomElement root = document.createElementNS(NsContentTypes,
                                                 QStringLiteral("Types"));
    root.setAttribute(QStringLiteral("xmlns"), NsContentTypes);
    QDomElement defaultXml = document.createElementNS(NsContentTypes,
                                                       QStringLiteral("Default"));
    defaultXml.setAttribute(QStringLiteral("Extension"), QStringLiteral("xml"));
    defaultXml.setAttribute(QStringLiteral("ContentType"),
                            QStringLiteral("application/xml"));
    root.appendChild(defaultXml);
    document.appendChild(root);
    return document;
}

bool loadXmlPart(zip_t *archive,
                 const QString &name,
                 QDomDocument *document,
                 QString *error,
                 const std::function<QDomDocument()> &factory)
{
    QByteArray data;
    if (!readZipEntry(archive, name, &data, error, true)) {
        return false;
    }
    if (data.isEmpty()) {
        *document = factory();
        return true;
    }
    return parseXml(data, document, error);
}

bool saveXmlPart(zip_t *archive,
                 const QString &name,
                 const QDomDocument &document,
                 QString *error)
{
    return writeZipEntry(archive, name,
                         document.toByteArray(2), error);
}

QString nextRelationshipId(const QDomElement &root)
{
    int maxId = 0;
    static const QRegularExpression idExpression(QStringLiteral(R"(^rId(\d+)$)"));
    for (QDomNode node = root.firstChild(); !node.isNull(); node = node.nextSibling()) {
        const QDomElement element = node.toElement();
        if (element.isNull() || element.localName() != QStringLiteral("Relationship")) {
            continue;
        }
        const QRegularExpressionMatch match =
            idExpression.match(element.attribute(QStringLiteral("Id")));
        if (match.hasMatch()) {
            maxId = std::max(maxId, match.captured(1).toInt());
        }
    }
    return QStringLiteral("rId%1").arg(maxId + 1);
}

bool ensureRelationship(zip_t *archive,
                        const QString &type,
                        const QString &target,
                        QString *error)
{
    QDomDocument document;
    if (!loadXmlPart(archive, QString::fromLatin1(RootRelationshipsPart), &document,
                     error, newRelationshipsDocument)) {
        return false;
    }
    QDomElement root = document.documentElement();
    for (QDomNode node = root.firstChild(); !node.isNull(); node = node.nextSibling()) {
        QDomElement element = node.toElement();
        if (!element.isNull()
            && element.attribute(QStringLiteral("Type")) == type) {
            element.setAttribute(QStringLiteral("Target"), target);
            return saveXmlPart(archive, QString::fromLatin1(RootRelationshipsPart),
                               document, error);
        }
    }

    QDomElement relationship = document.createElementNS(
        NsRelationships, QStringLiteral("Relationship"));
    relationship.setAttribute(QStringLiteral("Id"), nextRelationshipId(root));
    relationship.setAttribute(QStringLiteral("Type"), type);
    relationship.setAttribute(QStringLiteral("Target"), target);
    root.appendChild(relationship);
    return saveXmlPart(archive, QString::fromLatin1(RootRelationshipsPart),
                       document, error);
}

bool removeRelationship(zip_t *archive, const QString &type, QString *error)
{
    QByteArray data;
    if (!readZipEntry(archive, QString::fromLatin1(RootRelationshipsPart), &data,
                      error, true)) {
        return false;
    }
    if (data.isEmpty()) {
        return true;
    }
    QDomDocument document;
    if (!parseXml(data, &document, error)) {
        return false;
    }
    QDomElement root = document.documentElement();
    for (QDomNode node = root.firstChild(); !node.isNull();) {
        const QDomNode next = node.nextSibling();
        const QDomElement element = node.toElement();
        if (!element.isNull()
            && element.attribute(QStringLiteral("Type")) == type) {
            root.removeChild(node);
        }
        node = next;
    }
    return saveXmlPart(archive, QString::fromLatin1(RootRelationshipsPart),
                       document, error);
}

bool ensureContentType(zip_t *archive,
                       const QString &partName,
                       const QString &contentType,
                       QString *error)
{
    QDomDocument document;
    if (!loadXmlPart(archive, QString::fromLatin1(ContentTypesPart), &document,
                     error, newContentTypesDocument)) {
        return false;
    }
    QDomElement root = document.documentElement();
    for (QDomNode node = root.firstChild(); !node.isNull(); node = node.nextSibling()) {
        QDomElement element = node.toElement();
        if (!element.isNull() && element.localName() == QStringLiteral("Override")
            && element.attribute(QStringLiteral("PartName")) == partName) {
            element.setAttribute(QStringLiteral("ContentType"), contentType);
            return saveXmlPart(archive, QString::fromLatin1(ContentTypesPart),
                               document, error);
        }
    }
    QDomElement overrideElement = document.createElementNS(
        NsContentTypes, QStringLiteral("Override"));
    overrideElement.setAttribute(QStringLiteral("PartName"), partName);
    overrideElement.setAttribute(QStringLiteral("ContentType"), contentType);
    root.appendChild(overrideElement);
    return saveXmlPart(archive, QString::fromLatin1(ContentTypesPart), document, error);
}

bool removeContentType(zip_t *archive, const QString &partName, QString *error)
{
    QByteArray data;
    if (!readZipEntry(archive, QString::fromLatin1(ContentTypesPart), &data,
                      error, true)) {
        return false;
    }
    if (data.isEmpty()) {
        return true;
    }
    QDomDocument document;
    if (!parseXml(data, &document, error)) {
        return false;
    }
    QDomElement root = document.documentElement();
    for (QDomNode node = root.firstChild(); !node.isNull();) {
        const QDomNode next = node.nextSibling();
        const QDomElement element = node.toElement();
        if (!element.isNull() && element.localName() == QStringLiteral("Override")
            && element.attribute(QStringLiteral("PartName")) == partName) {
            root.removeChild(node);
        }
        node = next;
    }
    return saveXmlPart(archive, QString::fromLatin1(ContentTypesPart), document, error);
}

bool removeOfficePartRegistration(zip_t *archive,
                                  const QString &part,
                                  const QString &relationshipType,
                                  const QString &partName,
                                  QString *error)
{
    return deleteZipEntry(archive, part, error)
        && removeRelationship(archive, relationshipType, error)
        && removeContentType(archive, partName, error);
}

bool ensureOfficePartRegistration(zip_t *archive,
                                  const QString &part,
                                  QString *error)
{
    if (part == QString::fromLatin1(CorePart)) {
        return ensureRelationship(
                   archive,
                   QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties"),
                   QStringLiteral("docProps/core.xml"), error)
            && ensureContentType(
                   archive, QStringLiteral("/docProps/core.xml"),
                   QStringLiteral("application/vnd.openxmlformats-package.core-properties+xml"),
                   error);
    }
    if (part == QString::fromLatin1(AppPart)) {
        return ensureRelationship(
                   archive,
                   QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties"),
                   QStringLiteral("docProps/app.xml"), error)
            && ensureContentType(
                   archive, QStringLiteral("/docProps/app.xml"),
                   QStringLiteral("application/vnd.openxmlformats-officedocument.extended-properties+xml"),
                   error);
    }
    return ensureRelationship(
               archive,
               QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/custom-properties"),
               QStringLiteral("docProps/custom.xml"), error)
        && ensureContentType(
               archive, QStringLiteral("/docProps/custom.xml"),
               QStringLiteral("application/vnd.openxmlformats-officedocument.custom-properties+xml"),
               error);
}

const XmlProperty *officePropertyForTag(const QString &tag)
{
    for (const XmlProperty &property : officeProperties()) {
        if (property.publicTag.compare(tag, Qt::CaseInsensitive) == 0) {
            return &property;
        }
    }
    return nullptr;
}

const OdfProperty *odfPropertyForTag(const QString &tag)
{
    for (const OdfProperty &property : odfProperties()) {
        if (property.publicTag.compare(tag, Qt::CaseInsensitive) == 0) {
            return &property;
        }
    }
    return nullptr;
}

MetadataResult readOoxml(const QString &filePath)
{
    int errorCode = 0;
    const QByteArray encodedPath = QFile::encodeName(filePath);
    zip_t *archive = zip_open(encodedPath.constData(), ZIP_RDONLY, &errorCode);
    if (archive == nullptr) {
        return {false,
                QStringLiteral("This Office file is not a readable OOXML package. "
                               "Encrypted or legacy binary Office files cannot be edited."),
                {}};
    }

    QList<MetadataEntry> entries;
    QString error;
    QHash<QString, QDomDocument> documents;
    for (const QString &part : {QString::fromLatin1(CorePart),
                                QString::fromLatin1(AppPart)}) {
        QByteArray data;
        if (!readZipEntry(archive, part, &data, &error, true)) {
            zip_discard(archive);
            return {false, error, {}};
        }
        if (!data.isEmpty()) {
            QDomDocument document;
            if (!parseXml(data, &document, &error)) {
                zip_discard(archive);
                return {false, QStringLiteral("%1: %2").arg(part, error), {}};
            }
            documents.insert(part, document);
        }
    }

    for (const XmlProperty &property : officeProperties()) {
        if (!documents.contains(property.part)) {
            continue;
        }
        const QDomElement root = documents.value(property.part).documentElement();
        const QString localName = property.qualifiedName.section(QLatin1Char(':'), -1);
        const QDomElement element = directChild(root, property.namespaceUri, localName);
        if (!element.isNull() && !element.text().isEmpty()) {
            entries.append({QStringLiteral("Office"), property.displayTag,
                            element.text(), property.publicTag, true, true});
        }
    }

    QByteArray customData;
    if (!readZipEntry(archive, QString::fromLatin1(CustomPart), &customData, &error,
                      true)) {
        zip_discard(archive);
        return {false, error, {}};
    }
    if (!customData.isEmpty()) {
        QDomDocument customDocument;
        if (!parseXml(customData, &customDocument, &error)) {
            zip_discard(archive);
            return {false, QStringLiteral("docProps/custom.xml: %1").arg(error), {}};
        }
        const QDomElement root = customDocument.documentElement();
        for (QDomNode node = root.firstChild(); !node.isNull(); node = node.nextSibling()) {
            const QDomElement property = node.toElement();
            if (property.isNull() || property.localName() != QStringLiteral("property")) {
                continue;
            }
            const QString name = property.attribute(QStringLiteral("name"));
            const QDomElement valueElement = property.firstChildElement();
            if (!name.isEmpty()) {
                entries.append({QStringLiteral("OfficeCustom"), name,
                                valueElement.text(),
                                QStringLiteral("OfficeCustom:") + name, true, true});
            }
        }
    }

    zip_discard(archive);
    std::sort(entries.begin(), entries.end(), [](const MetadataEntry &left,
                                                  const MetadataEntry &right) {
        const int groupCompare = QString::localeAwareCompare(left.group, right.group);
        return groupCompare == 0
            ? QString::localeAwareCompare(left.tag, right.tag) < 0
            : groupCompare < 0;
    });
    return {true, QStringLiteral("Microsoft Office package metadata."), entries};
}

bool applyOfficeStandardProperty(zip_t *archive,
                                 const XmlProperty &property,
                                 const MetadataChange &change,
                                 QString *error)
{
    QDomDocument document;
    const auto factory = property.part == QString::fromLatin1(CorePart)
        ? std::function<QDomDocument()>(newCoreDocument)
        : std::function<QDomDocument()>(newAppDocument);
    if (!loadXmlPart(archive, property.part, &document, error, factory)) {
        return false;
    }
    QDomElement root = document.documentElement();
    const QString localName = property.qualifiedName.section(QLatin1Char(':'), -1);
    removeDirectChildren(root, property.namespaceUri, localName);
    if (!change.remove) {
        QDomElement element = document.createElementNS(property.namespaceUri,
                                                        property.qualifiedName);
        if (property.dateValue) {
            element.setAttributeNS(NsXsi, QStringLiteral("xsi:type"),
                                   QStringLiteral("dcterms:W3CDTF"));
        }
        element.appendChild(document.createTextNode(change.value));
        root.appendChild(element);
    }
    return ensureOfficePartRegistration(archive, property.part, error)
        && saveXmlPart(archive, property.part, document, error);
}

bool applyOfficeCustomProperty(zip_t *archive,
                               const QString &name,
                               const MetadataChange &change,
                               QString *error)
{
    QDomDocument document;
    if (!loadXmlPart(archive, QString::fromLatin1(CustomPart), &document, error,
                     newCustomDocument)) {
        return false;
    }
    QDomElement root = document.documentElement();
    QDomElement existing;
    int maxPid = 1;
    for (QDomNode node = root.firstChild(); !node.isNull(); node = node.nextSibling()) {
        const QDomElement property = node.toElement();
        if (property.isNull() || property.localName() != QStringLiteral("property")) {
            continue;
        }
        maxPid = std::max(maxPid, property.attribute(QStringLiteral("pid")).toInt());
        if (property.attribute(QStringLiteral("name")) == name) {
            existing = property;
        }
    }

    if (change.remove) {
        if (!existing.isNull()) {
            root.removeChild(existing);
        }
    } else {
        if (existing.isNull()) {
            existing = document.createElementNS(NsCustom,
                                                 QStringLiteral("property"));
            existing.setAttribute(QStringLiteral("fmtid"),
                                  QStringLiteral("{D5CDD505-2E9C-101B-9397-08002B2CF9AE}"));
            existing.setAttribute(QStringLiteral("pid"), maxPid + 1);
            existing.setAttribute(QStringLiteral("name"), name);
            root.appendChild(existing);
        }
        while (!existing.firstChild().isNull()) {
            existing.removeChild(existing.firstChild());
        }
        QDomElement valueElement = document.createElementNS(NsVt,
                                                             QStringLiteral("vt:lpwstr"));
        valueElement.appendChild(document.createTextNode(change.value));
        existing.appendChild(valueElement);
    }

    bool hasProperties = false;
    for (QDomNode node = root.firstChild(); !node.isNull(); node = node.nextSibling()) {
        if (!node.toElement().isNull()) {
            hasProperties = true;
            break;
        }
    }
    if (!hasProperties) {
        return removeOfficePartRegistration(
            archive, QString::fromLatin1(CustomPart),
            QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/custom-properties"),
            QStringLiteral("/docProps/custom.xml"), error);
    }

    return ensureOfficePartRegistration(archive, QString::fromLatin1(CustomPart),
                                        error)
        && saveXmlPart(archive, QString::fromLatin1(CustomPart), document, error);
}

MetadataResult applyOoxml(const QString &filePath,
                          const bool removeAll,
                          const QList<MetadataChange> &changes)
{
    int errorCode = 0;
    const QByteArray encodedPath = QFile::encodeName(filePath);
    zip_t *archive = zip_open(encodedPath.constData(), 0, &errorCode);
    if (archive == nullptr) {
        return {false,
                QStringLiteral("This Office file is not a writable OOXML package. "
                               "Encrypted or legacy binary Office files cannot be edited."),
                {}};
    }

    QString error;
    if (removeAll) {
        const bool removed =
            removeOfficePartRegistration(
                archive, QString::fromLatin1(CorePart),
                QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties"),
                QStringLiteral("/docProps/core.xml"), &error)
            && removeOfficePartRegistration(
                archive, QString::fromLatin1(AppPart),
                QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties"),
                QStringLiteral("/docProps/app.xml"), &error)
            && removeOfficePartRegistration(
                archive, QString::fromLatin1(CustomPart),
                QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/custom-properties"),
                QStringLiteral("/docProps/custom.xml"), &error);
        if (!removed) {
            zip_discard(archive);
            return {false, error, {}};
        }
    }

    for (const MetadataChange &change : changes) {
        const XmlProperty *property = officePropertyForTag(change.tag);
        if (property != nullptr) {
            if (!applyOfficeStandardProperty(archive, *property, change, &error)) {
                zip_discard(archive);
                return {false, error, {}};
            }
            continue;
        }
        if (change.tag.startsWith(QStringLiteral("OfficeCustom:"),
                                  Qt::CaseInsensitive)) {
            const QString name = customNameForTag(change.tag);
            if (!applyOfficeCustomProperty(archive, name, change, &error)) {
                zip_discard(archive);
                return {false, error, {}};
            }
        }
    }

    if (zip_close(archive) != 0) {
        error = QString::fromUtf8(zip_strerror(archive));
        zip_discard(archive);
        return {false, QStringLiteral("Could not save the Office package: %1").arg(error),
                {}};
    }
    return {true, QStringLiteral("Microsoft Office metadata changes applied."), {}};
}

QDomDocument newOdfMetaDocument()
{
    QDomDocument document;
    QDomElement root = document.createElementNS(NsOffice,
                                                 QStringLiteral("office:document-meta"));
    root.setAttribute(QStringLiteral("xmlns:office"), NsOffice);
    root.setAttribute(QStringLiteral("xmlns:meta"), NsMeta);
    root.setAttribute(QStringLiteral("xmlns:dc"), NsDc);
    root.setAttributeNS(NsOffice, QStringLiteral("office:version"),
                        QStringLiteral("1.2"));
    root.appendChild(document.createElementNS(NsOffice,
                                               QStringLiteral("office:meta")));
    document.appendChild(root);
    return document;
}

QDomElement odfMetaElement(QDomDocument &document)
{
    QDomElement root = document.documentElement();
    QDomElement meta = directChild(root, NsOffice, QStringLiteral("meta"));
    if (meta.isNull()) {
        meta = document.createElementNS(NsOffice, QStringLiteral("office:meta"));
        root.appendChild(meta);
    }
    return meta;
}

MetadataResult readOdfDocument(QDomDocument document)
{
    QList<MetadataEntry> entries;
    QDomElement root = document.documentElement();
    QDomElement meta = directChild(root, NsOffice, QStringLiteral("meta"));
    if (meta.isNull() && root.namespaceURI() == NsOffice
        && root.localName() == QStringLiteral("document-meta")) {
        meta = directChild(root, NsOffice, QStringLiteral("meta"));
    }
    if (meta.isNull()) {
        const QDomNodeList nodes = document.elementsByTagNameNS(
            NsOffice, QStringLiteral("meta"));
        if (!nodes.isEmpty()) {
            meta = nodes.at(0).toElement();
        }
    }
    if (meta.isNull()) {
        return {true, QStringLiteral("No OpenDocument metadata block was found."), {}};
    }

    for (const OdfProperty &property : odfProperties()) {
        const QString localName = property.qualifiedName.section(QLatin1Char(':'), -1);
        const QList<QDomElement> elements = directChildren(meta, property.namespaceUri,
                                                            localName);
        QStringList values;
        for (const QDomElement &element : elements) {
            if (!element.text().isEmpty()) {
                values.append(element.text());
            }
        }
        if (!values.isEmpty()) {
            entries.append({QStringLiteral("ODF"), property.displayTag,
                            values.join(QStringLiteral("; ")), property.publicTag,
                            true, true});
        }
    }

    const QList<QDomElement> customElements = directChildren(
        meta, NsMeta, QStringLiteral("user-defined"));
    for (const QDomElement &element : customElements) {
        const QString name = element.attributeNS(NsMeta, QStringLiteral("name"));
        if (!name.isEmpty()) {
            entries.append({QStringLiteral("ODFCustom"), name, element.text(),
                            QStringLiteral("ODFCustom:") + name, true, true});
        }
    }

    std::sort(entries.begin(), entries.end(), [](const MetadataEntry &left,
                                                  const MetadataEntry &right) {
        const int groupCompare = QString::localeAwareCompare(left.group, right.group);
        return groupCompare == 0
            ? QString::localeAwareCompare(left.tag, right.tag) < 0
            : groupCompare < 0;
    });
    return {true, QStringLiteral("LibreOffice/OpenDocument metadata."), entries};
}

MetadataResult readOdfPackage(const QString &filePath)
{
    int errorCode = 0;
    const QByteArray encodedPath = QFile::encodeName(filePath);
    zip_t *archive = zip_open(encodedPath.constData(), ZIP_RDONLY, &errorCode);
    if (archive == nullptr) {
        return {false,
                QStringLiteral("This file is not a readable OpenDocument package."),
                {}};
    }
    QByteArray data;
    QString error;
    if (!readZipEntry(archive, QString::fromLatin1(OdfMetaPart), &data, &error,
                      true)) {
        zip_discard(archive);
        return {false, error, {}};
    }
    zip_discard(archive);
    if (data.isEmpty()) {
        return {true, QStringLiteral("No OpenDocument metadata block was found."), {}};
    }
    QDomDocument document;
    if (!parseXml(data, &document, &error)) {
        return {false, QStringLiteral("meta.xml: %1").arg(error), {}};
    }
    return readOdfDocument(document);
}

MetadataResult readFlatOdf(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {false, QStringLiteral("Could not open the flat OpenDocument file: %1")
                           .arg(file.errorString()), {}};
    }
    QDomDocument document;
    QString error;
    if (!parseXml(file.readAll(), &document, &error)) {
        return {false, error, {}};
    }
    return readOdfDocument(document);
}

bool ensureOdfManifestEntry(zip_t *archive, QString *error)
{
    QByteArray data;
    if (!readZipEntry(archive, QString::fromLatin1(OdfManifestPart), &data, error,
                      true)) {
        return false;
    }
    QDomDocument document;
    if (data.isEmpty()) {
        QDomElement root = document.createElementNS(
            NsManifest, QStringLiteral("manifest:manifest"));
        root.setAttribute(QStringLiteral("xmlns:manifest"), NsManifest);
        root.setAttributeNS(NsManifest, QStringLiteral("manifest:version"),
                            QStringLiteral("1.2"));
        document.appendChild(root);
    } else if (!parseXml(data, &document, error)) {
        return false;
    }
    QDomElement root = document.documentElement();
    for (QDomNode node = root.firstChild(); !node.isNull(); node = node.nextSibling()) {
        const QDomElement element = node.toElement();
        if (!element.isNull() && element.localName() == QStringLiteral("file-entry")
            && element.attributeNS(NsManifest, QStringLiteral("full-path"))
                == QStringLiteral("meta.xml")) {
            return true;
        }
    }
    QDomElement entry = document.createElementNS(NsManifest,
                                                  QStringLiteral("manifest:file-entry"));
    entry.setAttributeNS(NsManifest, QStringLiteral("manifest:full-path"),
                         QStringLiteral("meta.xml"));
    entry.setAttributeNS(NsManifest, QStringLiteral("manifest:media-type"),
                         QStringLiteral("text/xml"));
    root.appendChild(entry);
    return saveXmlPart(archive, QString::fromLatin1(OdfManifestPart), document, error);
}

bool applyOdfChangesToDocument(QDomDocument *document,
                               const bool removeAll,
                               const QList<MetadataChange> &changes,
                               QString *error)
{
    Q_UNUSED(error)
    QDomElement meta = odfMetaElement(*document);
    if (removeAll) {
        removeAllElementChildren(meta);
    }

    for (const MetadataChange &change : changes) {
        const OdfProperty *property = odfPropertyForTag(change.tag);
        if (property != nullptr) {
            const QString localName = property->qualifiedName.section(QLatin1Char(':'),
                                                                      -1);
            removeDirectChildren(meta, property->namespaceUri, localName);
            if (!change.remove) {
                QStringList values;
                if (property->multiple) {
                    values = change.value.split(
                        QRegularExpression(QStringLiteral(R"([;\n]+)")),
                        Qt::SkipEmptyParts);
                } else {
                    values.append(change.value);
                }
                for (QString value : values) {
                    value = value.trimmed();
                    if (value.isEmpty()) {
                        continue;
                    }
                    QDomElement element = document->createElementNS(
                        property->namespaceUri, property->qualifiedName);
                    element.appendChild(document->createTextNode(value));
                    meta.appendChild(element);
                }
            }
            continue;
        }

        if (change.tag.startsWith(QStringLiteral("ODFCustom:"),
                                  Qt::CaseInsensitive)) {
            const QString name = customNameForTag(change.tag);
            for (QDomNode node = meta.firstChild(); !node.isNull();) {
                const QDomNode next = node.nextSibling();
                const QDomElement element = node.toElement();
                if (!element.isNull()
                    && element.namespaceURI() == NsMeta
                    && element.localName() == QStringLiteral("user-defined")
                    && element.attributeNS(NsMeta, QStringLiteral("name")) == name) {
                    meta.removeChild(node);
                }
                node = next;
            }
            if (!change.remove) {
                QDomElement element = document->createElementNS(
                    NsMeta, QStringLiteral("meta:user-defined"));
                element.setAttributeNS(NsMeta, QStringLiteral("meta:name"), name);
                element.setAttributeNS(NsMeta, QStringLiteral("meta:value-type"),
                                       QStringLiteral("string"));
                element.appendChild(document->createTextNode(change.value));
                meta.appendChild(element);
            }
        }
    }
    return true;
}

MetadataResult applyOdfPackage(const QString &filePath,
                               const bool removeAll,
                               const QList<MetadataChange> &changes)
{
    int errorCode = 0;
    const QByteArray encodedPath = QFile::encodeName(filePath);
    zip_t *archive = zip_open(encodedPath.constData(), 0, &errorCode);
    if (archive == nullptr) {
        return {false, QStringLiteral("This file is not a writable OpenDocument package."),
                {}};
    }
    QString error;
    QDomDocument document;
    if (!loadXmlPart(archive, QString::fromLatin1(OdfMetaPart), &document, &error,
                     newOdfMetaDocument)) {
        zip_discard(archive);
        return {false, error, {}};
    }
    if (!applyOdfChangesToDocument(&document, removeAll, changes, &error)
        || !writeZipEntry(archive, QString::fromLatin1(OdfMetaPart),
                          document.toByteArray(2), &error)
        || !ensureOdfManifestEntry(archive, &error)) {
        zip_discard(archive);
        return {false, error, {}};
    }
    if (zip_close(archive) != 0) {
        error = QString::fromUtf8(zip_strerror(archive));
        zip_discard(archive);
        return {false, QStringLiteral("Could not save the OpenDocument package: %1")
                           .arg(error), {}};
    }
    return {true, QStringLiteral("LibreOffice/OpenDocument metadata changes applied."),
            {}};
}

MetadataResult applyFlatOdf(const QString &filePath,
                            const bool removeAll,
                            const QList<MetadataChange> &changes)
{
    QFile input(filePath);
    if (!input.open(QIODevice::ReadOnly)) {
        return {false, QStringLiteral("Could not open the flat OpenDocument file: %1")
                           .arg(input.errorString()), {}};
    }
    QDomDocument document;
    QString error;
    if (!parseXml(input.readAll(), &document, &error)) {
        return {false, error, {}};
    }
    input.close();
    if (!applyOdfChangesToDocument(&document, removeAll, changes, &error)) {
        return {false, error, {}};
    }
    QSaveFile output(filePath);
    if (!output.open(QIODevice::WriteOnly)) {
        return {false, QStringLiteral("Could not write the flat OpenDocument file: %1")
                           .arg(output.errorString()), {}};
    }
    const QByteArray payload = document.toByteArray(2);
    if (output.write(payload) != payload.size() || !output.commit()) {
        return {false, QStringLiteral("Could not save the flat OpenDocument file: %1")
                           .arg(output.errorString()), {}};
    }
    return {true, QStringLiteral("Flat OpenDocument metadata changes applied."), {}};
}

bool validCustomName(const QString &name)
{
    if (name.trimmed().isEmpty() || name.size() > 255) {
        return false;
    }
    for (const QChar character : name) {
        if (character.unicode() < 0x20U || character == QLatin1Char(':')) {
            return false;
        }
    }
    return true;
}

} // namespace

namespace DocumentMetadata
{
bool isOoxml(const QString &filePath)
{
    static const QSet<QString> extensions = {
        QStringLiteral("docx"), QStringLiteral("docm"), QStringLiteral("dotx"),
        QStringLiteral("dotm"), QStringLiteral("xlsx"), QStringLiteral("xlsm"),
        QStringLiteral("xltx"), QStringLiteral("xltm"), QStringLiteral("xlsb"),
        QStringLiteral("xlam"), QStringLiteral("pptx"), QStringLiteral("pptm"),
        QStringLiteral("potx"), QStringLiteral("potm"), QStringLiteral("ppsx"),
        QStringLiteral("ppsm"), QStringLiteral("ppam"),
        QStringLiteral("sldx"), QStringLiteral("sldm"), QStringLiteral("vsdx"),
        QStringLiteral("vsdm"), QStringLiteral("vstx"), QStringLiteral("vstm"),
    };
    return extensions.contains(suffix(filePath));
}

bool isOdfPackage(const QString &filePath)
{
    static const QSet<QString> extensions = {
        QStringLiteral("odt"), QStringLiteral("ods"), QStringLiteral("odp"),
        QStringLiteral("odg"), QStringLiteral("odf"), QStringLiteral("odb"),
        QStringLiteral("odc"), QStringLiteral("odi"), QStringLiteral("odm"),
        QStringLiteral("ott"), QStringLiteral("ots"), QStringLiteral("otp"),
        QStringLiteral("otg"), QStringLiteral("otm"),
    };
    return extensions.contains(suffix(filePath));
}

bool isFlatOdf(const QString &filePath)
{
    static const QSet<QString> extensions = {
        QStringLiteral("fodt"), QStringLiteral("fods"), QStringLiteral("fodp"),
        QStringLiteral("fodg"),
    };
    return extensions.contains(suffix(filePath));
}

bool isSupportedDocument(const QString &filePath)
{
    return isOoxml(filePath) || isOdfPackage(filePath) || isFlatOdf(filePath);
}

MetadataResult read(const QString &filePath)
{
    if (isOoxml(filePath)) {
        return readOoxml(filePath);
    }
    if (isOdfPackage(filePath)) {
        return readOdfPackage(filePath);
    }
    if (isFlatOdf(filePath)) {
        return readFlatOdf(filePath);
    }
    return {false, QStringLiteral("Unsupported document metadata format."), {}};
}

MetadataResult apply(const QString &filePath,
                     const bool removeAll,
                     const QList<MetadataChange> &changes)
{
    if (isOoxml(filePath)) {
        return applyOoxml(filePath, removeAll, changes);
    }
    if (isOdfPackage(filePath)) {
        return applyOdfPackage(filePath, removeAll, changes);
    }
    if (isFlatOdf(filePath)) {
        return applyFlatOdf(filePath, removeAll, changes);
    }
    return {false, QStringLiteral("Unsupported document metadata format."), {}};
}

QString validationError(const QString &filePath, const QString &tag)
{
    const QString cleanTag = tag.trimmed();
    if (isOoxml(filePath)) {
        if (officePropertyForTag(cleanTag) != nullptr) {
            return {};
        }
        if (cleanTag.startsWith(QStringLiteral("OfficeCustom:"),
                                Qt::CaseInsensitive)
            && validCustomName(customNameForTag(cleanTag))) {
            return {};
        }
        return QStringLiteral(
            "Unsupported Office property. Use Office:Title, Office:Subject, "
            "Office:Author, Office:Keywords, Office:Description, "
            "Office:Identifier, Office:Language, Office:LastModifiedBy, "
            "Office:Revision, Office:Category, Office:ContentStatus, "
            "Office:ContentType, Office:Version, Office:Created, Office:Modified, "
            "Office:Application, Office:AppVersion, Office:Company, "
            "Office:Manager, Office:Template, or OfficeCustom:Name.");
    }
    if (isOdfPackage(filePath) || isFlatOdf(filePath)) {
        if (odfPropertyForTag(cleanTag) != nullptr) {
            return {};
        }
        if (cleanTag.startsWith(QStringLiteral("ODFCustom:"), Qt::CaseInsensitive)
            && validCustomName(customNameForTag(cleanTag))) {
            return {};
        }
        return QStringLiteral(
            "Unsupported OpenDocument property. Use ODF:Title, ODF:Subject, "
            "ODF:Description, ODF:Keywords, ODF:InitialCreator, ODF:Creator, "
            "ODF:CreationDate, ODF:Modified, ODF:EditingDuration, "
            "ODF:EditingCycles, ODF:Generator, ODF:PrintedBy, ODF:PrintDate, "
            "ODF:Language, or ODFCustom:Name.");
    }
    return QStringLiteral("Unsupported document metadata format.");
}

QString tagHint(const QString &filePath)
{
    if (isOoxml(filePath)) {
        return QStringLiteral(
            "Office examples: Office:Title, Office:Author, Office:Company, "
            "Office:Keywords, or OfficeCustom:ProjectName.");
    }
    if (isOdfPackage(filePath) || isFlatOdf(filePath)) {
        return QStringLiteral(
            "LibreOffice examples: ODF:Title, ODF:Creator, ODF:Keywords, "
            "ODF:Description, or ODFCustom:ProjectName.");
    }
    return {};
}
} // namespace DocumentMetadata
