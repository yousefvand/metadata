#!/usr/bin/env bash
set -Eeuo pipefail

APP_NAME="metadata"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/build}"
PREFIX="${PREFIX:-/usr}"
STATE_DIR="${XDG_STATE_HOME:-${HOME}/.local/state}/${APP_NAME}"
STATE_FILE="${STATE_DIR}/install-state"
CONTEXT_MENU_PATH=""
DETECTED_FILE_MANAGER=""

log() { printf '[metadata] %s\n' "$*"; }
fail() { printf '[metadata] ERROR: %s\n' "$*" >&2; exit 1; }

if [[ "$(uname -s)" != "Linux" ]] || ! command -v pacman >/dev/null 2>&1; then
    fail "Install.sh is intended for Arch Linux and pacman-based systems."
fi

if (( EUID == 0 )); then
    SUDO=()
else
    command -v sudo >/dev/null 2>&1 || fail "sudo is required for installation to ${PREFIX}."
    SUDO=(sudo)
fi

missing_packages=()
for package in cmake ninja qt6-base perl-image-exiftool qpdf hicolor-icon-theme; do
    pacman -Q "$package" >/dev/null 2>&1 || missing_packages+=("$package")
done

if ! command -v c++ >/dev/null 2>&1; then
    missing_packages+=(base-devel)
fi

if ((${#missing_packages[@]})); then
    printf 'Required packages are missing: %s\n' "${missing_packages[*]}"
    read -r -p "Install them with pacman? [Y/n] " answer
    answer="${answer:-Y}"
    if [[ "$answer" =~ ^[Yy]$ ]]; then
        "${SUDO[@]}" pacman -S --needed "${missing_packages[@]}"
    else
        fail "Required build/runtime packages are missing."
    fi
fi

log "Configuring the Qt 6 application."
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DINSTALL_DOLPHIN_SERVICE_MENU=OFF

log "Building."
cmake --build "$BUILD_DIR" --parallel

log "Installing to ${PREFIX}."
"${SUDO[@]}" cmake --install "$BUILD_DIR"

command -v update-desktop-database >/dev/null 2>&1 \
    && "${SUDO[@]}" update-desktop-database "${PREFIX}/share/applications" >/dev/null 2>&1 \
    || true
command -v gtk-update-icon-cache >/dev/null 2>&1 \
    && "${SUDO[@]}" gtk-update-icon-cache -f -t "${PREFIX}/share/icons/hicolor" >/dev/null 2>&1 \
    || true

detect_file_manager() {
    local desktop=""
    if command -v xdg-mime >/dev/null 2>&1; then
        desktop="$(xdg-mime query default inode/directory 2>/dev/null || true)"
    fi
    desktop="${desktop,,}"

    case "$desktop" in
        *dolphin*|*konqueror*) printf 'dolphin'; return 0 ;;
    esac

    if [[ "${XDG_CURRENT_DESKTOP:-}" == *KDE* ]] && command -v dolphin >/dev/null 2>&1; then
        printf 'dolphin'
        return 0
    fi

    if pgrep -x dolphin >/dev/null 2>&1; then
        printf 'dolphin'
        return 0
    fi

    printf 'unsupported'
}

printf '\n'
read -r -p 'Do you want to add "Show metadata" to your file manager? [Y/n] ' context_answer
context_answer="${context_answer:-Y}"

if [[ "$context_answer" =~ ^[Yy]$ ]]; then
    DETECTED_FILE_MANAGER="$(detect_file_manager)"
    if [[ "$DETECTED_FILE_MANAGER" == "dolphin" ]]; then
        CONTEXT_MENU_PATH="${XDG_DATA_HOME:-${HOME}/.local/share}/kio/servicemenus/metadata-show.desktop"
        mkdir -p "$(dirname -- "$CONTEXT_MENU_PATH")"
        cat > "$CONTEXT_MENU_PATH" <<MENU
[Desktop Entry]
Type=Service
MimeType=application/octet-stream;
Actions=showMetadata;

[Desktop Action showMetadata]
Name=Show metadata
Icon=document-properties
Exec=${PREFIX}/bin/metadata %f
MENU
        chmod +x "$CONTEXT_MENU_PATH"
        command -v kbuildsycoca6 >/dev/null 2>&1 && kbuildsycoca6 >/dev/null 2>&1 || true
        log "Added the Dolphin/Konqueror context-menu action."
    else
        log "The default file manager is not Dolphin/Konqueror. No context-menu file was installed."
        log "This project targets Arch Linux KDE; its native integration is a KDE service menu."
    fi
fi

mkdir -p "$STATE_DIR"
cat > "$STATE_FILE" <<STATE
PREFIX=${PREFIX}
BUILD_DIR=${BUILD_DIR}
FILE_MANAGER=${DETECTED_FILE_MANAGER}
CONTEXT_MENU_PATH=${CONTEXT_MENU_PATH}
STATE

log "Installation complete. Run: metadata"
