#pragma once

#include "MetadataBackend.h"

#include <QString>

namespace DocumentMetadata
{
[[nodiscard]] bool isOoxml(const QString &filePath);
[[nodiscard]] bool isOdfPackage(const QString &filePath);
[[nodiscard]] bool isFlatOdf(const QString &filePath);
[[nodiscard]] bool isSupportedDocument(const QString &filePath);

[[nodiscard]] MetadataResult read(const QString &filePath);
[[nodiscard]] MetadataResult apply(const QString &filePath,
                                   bool removeAll,
                                   const QList<MetadataChange> &changes);
[[nodiscard]] QString validationError(const QString &filePath,
                                      const QString &tag);
[[nodiscard]] QString tagHint(const QString &filePath);
} // namespace DocumentMetadata
