# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project follows [Semantic Versioning](https://semver.org/).

## [0.3.0] - 2026-07-21

### Added

- Native metadata reading, adding, editing, and removal for Microsoft Office Open
  XML packages, including DOCX, XLSX, PPTX, templates, macro-enabled variants,
  and Visio Open XML files.
- Native metadata support for LibreOffice/OpenDocument packages and flat XML
  documents, including ODT, ODS, ODP, ODG, ODM, templates, FODT, FODS, FODP, and
  FODG.
- Support for standard Office and OpenDocument properties plus custom document
  properties through `OfficeCustom:Name` and `ODFCustom:Name` tags.
- An **Apply** button beside **Remove All**. Add, Edit, Remove, and Remove All now
  stage changes without modifying the source file.
- A Pending column and visual indicators for staged additions, edits, and
  removals.
- Atomic writes: changes are applied to a temporary copy and replace the source
  only after the complete operation succeeds.
- A tracked Dolphin/Konqueror service-menu file for AUR/system-wide installs.
- This changelog.

### Changed

- Version increased from 0.2.0 to 0.3.0.
- `Install.sh` and `Uninstall.sh` were renamed to `install.sh` and
  `uninstall.sh`.
- Installation scripts now include the `libzip` dependency, preserve safer
  installer state, handle writable custom prefixes without unnecessary `sudo`,
  and remove installed documentation during uninstall.
- `aur.sh` now packages only `PKGBUILD` and `.SRCINFO`; application source is
  downloaded from the tagged GitHub archive by `makepkg`.
- README installation, format-support, workflow, and AUR instructions were
  updated.

### Fixed

- Fixed Qt 6 XML const-correctness build errors in Office relationship,
  content-type, and OpenDocument metadata handling.
- Fixed the AUR build path that enabled a Dolphin service-menu CMake option while
  the referenced service-menu file was absent.
- Fixed `aur.sh` and its generated `PKGBUILD` to use the repository's actual
  `0.3.0` tag instead of requesting the nonexistent `v0.3.0` tag.
- Fixed the Dolphin context-menu action to use the metadata application icon
  instead of a generic icon, installed it in both application and action icon
  contexts, and forced a non-incremental KDE service-cache refresh.
- Set the AUR packaging revision to `0.3.0-2`; its `prepare()` step corrects the
  icon without moving or rewriting the published `0.3.0` tag.
- Prevented unapplied changes from being silently lost when opening another file,
  refreshing, or quitting.

### Notes

- Editing metadata invalidates existing digital signatures on signed document
  packages.
- Password-protected/encrypted Office files and legacy binary DOC/XLS/PPT files
  are not rewritten. Legacy files may still be readable through ExifTool.

## [0.2.0]

### Added

- Qt 6 metadata viewer/editor backed by ExifTool.
- Add, edit, remove, remove-all, copy, export, PDF rewriting through qpdf, and
  KDE file-manager integration.
