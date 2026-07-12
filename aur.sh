#!/usr/bin/env bash
set -Eeuo pipefail

MAINTAINER_NAME="${MAINTAINER_NAME:-Remisa Phillips}"
MAINTAINER_EMAIL="${MAINTAINER_EMAIL:-remisa.yousefvand@gmail.com}"

AUR_PACKAGE_NAME="${AUR_PACKAGE_NAME:-metadata}"
GITHUB_OWNER="${GITHUB_OWNER:-yousefvand}"
GITHUB_REPOSITORY="${GITHUB_REPOSITORY:-metadata}"
VERSION="${VERSION:-0.1.0}"
PKGREL="${PKGREL:-1}"
AUR_WORKDIR="${AUR_WORKDIR:-${TMPDIR:-/tmp}/${AUR_PACKAGE_NAME}-aur}"
AUR_SSH_URL="ssh://aur@aur.archlinux.org/${AUR_PACKAGE_NAME}.git"
UPSTREAM_URL="https://github.com/${GITHUB_OWNER}/${GITHUB_REPOSITORY}"
SOURCE_URL="${UPSTREAM_URL}/archive/refs/tags/v${VERSION}.tar.gz"

fail() { printf '[aur] ERROR: %s\n' "$*" >&2; exit 1; }
log() { printf '[aur] %s\n' "$*"; }

(( EUID != 0 )) || fail "Do not run makepkg/aur.sh as root."

[[ "$MAINTAINER_NAME" != REPLACE_* ]] \
    || fail "Set MAINTAINER_NAME in aur.sh or the environment."
[[ "$MAINTAINER_EMAIL" != REPLACE_* ]] \
    || fail "Set MAINTAINER_EMAIL in aur.sh or the environment."

for command_name in git curl sha256sum makepkg; do
    command -v "$command_name" >/dev/null 2>&1 \
        || fail "Required command not found: ${command_name}"
done

log "Checking that the GitHub tag v${VERSION} exists."
tarball="$(mktemp --suffix=.tar.gz)"
trap 'rm -f -- "$tarball"' EXIT
curl --fail --location --silent --show-error "$SOURCE_URL" -o "$tarball" \
    || fail "Could not download ${SOURCE_URL}. Push tag v${VERSION} to GitHub first."
checksum="$(sha256sum "$tarball" | awk '{print $1}')"

if [[ -d "${AUR_WORKDIR}/.git" ]]; then
    log "Updating existing AUR clone."
    git -C "$AUR_WORKDIR" fetch origin
    if git -C "$AUR_WORKDIR" show-ref --verify --quiet refs/remotes/origin/master; then
        git -C "$AUR_WORKDIR" checkout -B master origin/master
    else
        git -C "$AUR_WORKDIR" checkout -B master
    fi
else
    rm -rf -- "$AUR_WORKDIR"
    log "Cloning ${AUR_SSH_URL}."
    git clone "$AUR_SSH_URL" "$AUR_WORKDIR"
    if git -C "$AUR_WORKDIR" show-ref --verify --quiet refs/remotes/origin/master; then
        git -C "$AUR_WORKDIR" checkout -B master origin/master
    else
        git -C "$AUR_WORKDIR" checkout -B master
    fi
fi

git -C "$AUR_WORKDIR" config user.name "$MAINTAINER_NAME"
git -C "$AUR_WORKDIR" config user.email "$MAINTAINER_EMAIL"

cat > "${AUR_WORKDIR}/PKGBUILD" <<PKGBUILD
# Maintainer: ${MAINTAINER_NAME} <${MAINTAINER_EMAIL}>
pkgname=${AUR_PACKAGE_NAME}
pkgver=${VERSION}
pkgrel=${PKGREL}
pkgdesc='Qt 6 application for viewing, adding, editing, and removing file metadata'
arch=('x86_64')
url='${UPSTREAM_URL}'
license=('MIT')
depends=('qt6-base' 'perl-image-exiftool' 'qpdf' 'hicolor-icon-theme')
optdepends=('dolphin: Show Metadata file-manager context-menu integration')
makedepends=('cmake' 'ninja')
source=("\${pkgname}-\${pkgver}.tar.gz::\${url}/archive/refs/tags/v\${pkgver}.tar.gz")
sha256sums=('${checksum}')

build() {
    cmake -S "${GITHUB_REPOSITORY}-\${pkgver}" -B build -G Ninja \\
        -DCMAKE_BUILD_TYPE=Release \\
        -DCMAKE_INSTALL_PREFIX=/usr \\
        -DINSTALL_DOLPHIN_SERVICE_MENU=ON
    cmake --build build
}

package() {
    DESTDIR="\${pkgdir}" cmake --install build
}
PKGBUILD

(
    cd "$AUR_WORKDIR"
    makepkg --printsrcinfo > .SRCINFO

    # The AUR Git repository must contain packaging metadata only. Source is
    # downloaded from GitHub by makepkg.
    find . -maxdepth 1 -type f \
        ! -name PKGBUILD \
        ! -name .SRCINFO \
        -delete

    if [[ "${AUR_SKIP_BUILD:-0}" != "1" ]]; then
        log "Building the package locally before push."
        makepkg --cleanbuild --syncdeps --noconfirm
        rm -rf -- pkg src "${AUR_PACKAGE_NAME}-${VERSION}-${PKGREL}-"*.pkg.tar.*
    fi

    find . -maxdepth 1 -type f \
        ! -name PKGBUILD \
        ! -name .SRCINFO \
        -delete

    while IFS= read -r -d '' tracked_file; do
        case "$tracked_file" in
            PKGBUILD|.SRCINFO) ;;
            *) git rm -rf --ignore-unmatch -- "$tracked_file" ;;
        esac
    done < <(git ls-files -z)

    git add PKGBUILD .SRCINFO

    unexpected="$(git ls-files | grep -Ev '^(PKGBUILD|\.SRCINFO)$' || true)"
    [[ -z "$unexpected" ]] \
        || fail "AUR repository contains unexpected files: ${unexpected}"

    if git diff --cached --quiet; then
        log "No AUR changes to push."
        exit 0
    fi

    git commit -m "Update to ${VERSION}-${PKGREL}"
    git push origin master
)

log "AUR push completed."
