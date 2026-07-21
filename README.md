# metadata

A native Qt 6/C++ metadata editor for Arch Linux KDE Wayland. It reads metadata,
stages additions/edits/removals in the interface, and changes the file only after
the user clicks **Apply**.

General file formats are handled through ExifTool. Microsoft Office Open XML and
LibreOffice/OpenDocument files are handled directly as document packages, so
their editable document properties do not depend on ExifTool write support.

## Features

- View metadata grouped by source group and tag.
- Stage metadata additions, edits, individual removals, or Remove All.
- Apply all pending changes together with the **Apply** button beside
  **Remove All**.
- See pending Add/Edit/Remove states before writing the file.
- Preserve the original file when any part of an apply operation fails by using
  a temporary copy and atomic replacement.
- Copy metadata to the clipboard as a minimal ASCII key/value table.
- Export metadata to a UTF-8 text file as a minimal ASCII key/value table.
- Configure copy and export independently to include all fields or only editable
  fields.
- Open a file directly or pass its path on the command line for Dolphin service
  menu integration.
- Rewrite PDFs with qpdf after metadata changes to discard old incremental
  objects.
- Block Remove All for proprietary camera RAW formats whose metadata may be
  rendering-critical.

Copy defaults to editable key/values. Export defaults to all key/values. Exported
files use the complete source filename followed by `.txt`; for example,
`report.pdf` becomes `report.pdf.txt`.

## Microsoft Office support

Version 0.3.0 can read, add, edit, remove, and remove-all metadata in Office Open
XML packages:

- Word: DOCX, DOCM, DOTX, DOTM
- Excel: XLSX, XLSM, XLTX, XLTM, XLSB, XLAM
- PowerPoint: PPTX, PPTM, POTX, POTM, PPSX, PPSM, PPAM, SLDX, SLDM
- Visio: VSDX, VSDM, VSTX, VSTM

Supported standard property names include:

```text
Office:Title
Office:Subject
Office:Author
Office:Keywords
Office:Description
Office:Identifier
Office:Language
Office:LastModifiedBy
Office:Revision
Office:Category
Office:ContentStatus
Office:ContentType
Office:Version
Office:Created
Office:Modified
Office:Application
Office:AppVersion
Office:Company
Office:Manager
Office:Template
```

Custom Office properties use:

```text
OfficeCustom:ProjectName
```

Legacy binary DOC, XLS, and PPT files are not rewritten. ExifTool may display
metadata from them as read-only information. Password-protected or encrypted
Office packages cannot be edited.

## LibreOffice and OpenDocument support

Package formats supported include ODT, ODS, ODP, ODG, ODF, ODB, ODC, ODI,
ODM and their OTT, OTS, OTP, OTG, and OTM templates. Flat XML formats FODT,
FODS, FODP, and FODG are also supported.

Supported standard property names include:

```text
ODF:Title
ODF:Subject
ODF:Description
ODF:Keywords
ODF:InitialCreator
ODF:Creator
ODF:CreationDate
ODF:Modified
ODF:EditingDuration
ODF:EditingCycles
ODF:Generator
ODF:PrintedBy
ODF:PrintDate
ODF:Language
```

Custom OpenDocument properties use:

```text
ODFCustom:ProjectName
```

Separate multiple `ODF:Keywords` values with semicolons or new lines.

## Apply workflow

1. Open a file.
2. Use Add, Edit, Remove, or Remove All.
3. Review the Pending column. The file has not changed yet.
4. Click Apply and confirm the operation.

Refresh, opening another file, or quitting prompts before discarding unapplied
changes. Changing metadata in a digitally signed Office/OpenDocument package can
invalidate its signature.

## Runtime architecture

The GUI is Qt 6 Widgets/C++. The application uses:

- ExifTool (`perl-image-exiftool`) for general metadata formats.
- qpdf for secure PDF rewriting.
- libzip and Qt XML for Office Open XML and OpenDocument packages.

ExifTool does not guarantee complete metadata removal for every format. JPEG
removal is generally the most complete; TIFF, PNG, PDF, PostScript, MOV/MP4, and
RAW formats have format-specific limitations. No general-purpose tool can promise
that every proprietary or structural field in every format is safely removable.

## Installation

### Arch Linux (AUR)

```bash
yay -S metadata
```

### Build from source

```bash
sudo pacman -S --needed base-devel cmake ninja qt6-base libzip \
  perl-image-exiftool qpdf hicolor-icon-theme
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
./build/metadata
```

### Install and uninstall scripts

```bash
chmod +x install.sh uninstall.sh aur.sh
./install.sh
./uninstall.sh
```

`install.sh` installs to `/usr` by default and asks whether to add **Show
metadata** to Dolphin/Konqueror. The default is yes. The user service-menu file is
created at:

```text
~/.local/share/kio/servicemenus/metadata-show.desktop
```

`uninstall.sh` removes files installed by the script, the optional user service
menu, the local build directory, and installer state. It does not remove shared
packages such as Qt, libzip, ExifTool, or qpdf.

Use a different prefix with:

```bash
PREFIX="$HOME/.local" ./install.sh
```

## General ExifTool tag examples

For non-Office/non-OpenDocument files:

```text
XMP-dc:Title
XMP-dc:Description
EXIF:Artist
IPTC:Keywords
PDF:Author
```

Whether a tag is writable depends on the destination format and ExifTool.
Read-only groups such as File, System, ExifTool, Composite, and ZIP are displayed
but cannot be edited.

## License

MIT. See `LICENSE`.
