#!/usr/bin/env bash
set -Eeuo pipefail

# AUR publishing helper only. It never copies application source into the AUR
# repository: PKGBUILD downloads the tagged release directly from GitHub.
MAINTAINER_NAME="${MAINTAINER_NAME:-Remisa Phillips}"
MAINTAINER_EMAIL="${MAINTAINER_EMAIL:-remisa.yousefvand@gmail.com}"
AUR_PACKAGE_NAME="${AUR_PACKAGE_NAME:-metadata}"
GITHUB_OWNER="${GITHUB_OWNER:-yousefvand}"
GITHUB_REPOSITORY="${GITHUB_REPOSITORY:-metadata}"
VERSION="${VERSION:-0.3.0}"
PKGREL="${PKGREL:-2}"
AUR_WORKDIR="${AUR_WORKDIR:-${TMPDIR:-/tmp}/${AUR_PACKAGE_NAME}-aur}"
AUR_SSH_URL="${AUR_SSH_URL:-ssh://aur@aur.archlinux.org/${AUR_PACKAGE_NAME}.git}"
UPSTREAM_URL="https://github.com/${GITHUB_OWNER}/${GITHUB_REPOSITORY}"
GITHUB_TAG="${GITHUB_TAG:-${VERSION}}"
SOURCE_URL="${UPSTREAM_URL}/archive/refs/tags/${GITHUB_TAG}.tar.gz"

fail() { printf '[aur] ERROR: %s\n' "$*" >&2; exit 1; }
log() { printf '[aur] %s\n' "$*"; }

(( EUID != 0 )) || fail "Do not run aur.sh or makepkg as root."
for command_name in git curl sha256sum makepkg; do
    command -v "$command_name" >/dev/null 2>&1 \
        || fail "Required command not found: ${command_name}"
done

log "Downloading GitHub tag ${GITHUB_TAG} to calculate the source checksum."
tarball="$(mktemp --suffix=.tar.gz)"
trap 'rm -f -- "$tarball"' EXIT
curl --fail --location --silent --show-error "$SOURCE_URL" -o "$tarball" \
    || fail "Could not download ${SOURCE_URL}. Confirm that GitHub tag ${GITHUB_TAG} exists and is public."
checksum="$(sha256sum "$tarball" | awk '{print $1}')"

if [[ -d "${AUR_WORKDIR}/.git" ]]; then
    log "Updating the existing AUR checkout."
    git -C "$AUR_WORKDIR" fetch origin
else
    rm -rf -- "$AUR_WORKDIR"
    log "Cloning ${AUR_SSH_URL}."
    git clone "$AUR_SSH_URL" "$AUR_WORKDIR"
fi

if git -C "$AUR_WORKDIR" show-ref --verify --quiet refs/remotes/origin/master; then
    git -C "$AUR_WORKDIR" checkout -B master origin/master
else
    git -C "$AUR_WORKDIR" checkout -B master
fi

git -C "$AUR_WORKDIR" config user.name "$MAINTAINER_NAME"
git -C "$AUR_WORKDIR" config user.email "$MAINTAINER_EMAIL"

cat > "${AUR_WORKDIR}/PKGBUILD" <<PKGBUILD
# Maintainer: ${MAINTAINER_NAME} <${MAINTAINER_EMAIL}>
pkgname=${AUR_PACKAGE_NAME}
pkgver=${VERSION}
pkgrel=${PKGREL}
_tag='${GITHUB_TAG}'
pkgdesc='Qt 6 metadata editor with Office and OpenDocument support'
arch=('x86_64')
url='${UPSTREAM_URL}'
license=('MIT')
depends=('qt6-base' 'libzip' 'perl-image-exiftool' 'qpdf' 'hicolor-icon-theme')
optdepends=('dolphin: Show metadata context-menu integration')
makedepends=('cmake' 'ninja')
source=("\${pkgname}-\${pkgver}.tar.gz::\${url}/archive/refs/tags/\${_tag}.tar.gz")
sha256sums=('${checksum}')

prepare() {
    cd "${GITHUB_REPOSITORY}-\${_tag}"
    # Keep the published 0.3.0 tag immutable and correct the original generic
    # Dolphin action icon at package-build time.
    sed -i 's/^Icon=document-properties$/Icon=io.github.yousefvand.metadata/' \\
        integration/dolphin/metadata-show.desktop
}

build() {
    cmake -S "${GITHUB_REPOSITORY}-\${_tag}" -B build -G Ninja \\
        -DCMAKE_BUILD_TYPE=Release \\
        -DCMAKE_INSTALL_PREFIX=/usr \\
        -DINSTALL_DOLPHIN_SERVICE_MENU=ON
    cmake --build build
}

package() {
    DESTDIR="\${pkgdir}" cmake --install build

    # Also install the icon in the Actions context so Dolphin can resolve it
    # regardless of the active icon theme's inheritance behavior.
    install -Dm644 \\
        "${GITHUB_REPOSITORY}-\${_tag}/resources/io.github.yousefvand.metadata.svg" \\
        "\${pkgdir}/usr/share/icons/hicolor/scalable/actions/io.github.yousefvand.metadata.svg"
}
PKGBUILD

(
    cd "$AUR_WORKDIR"
    makepkg --printsrcinfo > .SRCINFO

    # Keep exactly the two AUR packaging files. The project source is always
    # fetched from GitHub by makepkg through the source= URL above.
    while IFS= read -r -d '' path; do
        case "$path" in
            PKGBUILD|.SRCINFO) ;;
            *) git rm -rf --ignore-unmatch -- "$path" ;;
        esac
    done < <(git ls-files -z)
    find . -mindepth 1 -maxdepth 1 \
        ! -name .git ! -name PKGBUILD ! -name .SRCINFO \
        -exec rm -rf -- {} +

    if [[ "${AUR_SKIP_BUILD:-0}" != "1" ]]; then
        log "Building the GitHub-sourced package locally before publishing."
        makepkg --cleanbuild --syncdeps --noconfirm
        rm -rf -- pkg src "${AUR_PACKAGE_NAME}-${VERSION}-${PKGREL}-"*.pkg.tar.*
    fi

    find . -mindepth 1 -maxdepth 1 \
        ! -name .git ! -name PKGBUILD ! -name .SRCINFO \
        -exec rm -rf -- {} +
    git add PKGBUILD .SRCINFO

    unexpected="$(git ls-files | grep -Ev '^(PKGBUILD|\.SRCINFO)$' || true)"
    [[ -z "$unexpected" ]] \
        || fail "AUR checkout contains unexpected tracked files: ${unexpected}"

    if git diff --cached --quiet; then
        log "No AUR changes to push."
        exit 0
    fi

    git commit -m "Update to ${VERSION}-${PKGREL}"
    git push origin master
)

log "AUR package metadata published."
