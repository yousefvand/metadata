#!/usr/bin/env bash
set -Eeuo pipefail

APP_NAME="metadata"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
STATE_DIR="${XDG_STATE_HOME:-${HOME}/.local/state}/${APP_NAME}"
STATE_FILE="${STATE_DIR}/install-state"
PREFIX="/usr"
BUILD_DIR="${SCRIPT_DIR}/build"
CONTEXT_MENU_PATH="${XDG_DATA_HOME:-${HOME}/.local/share}/kio/servicemenus/metadata-show.desktop"

log() { printf '[metadata] %s\n' "$*"; }

if [[ -f "$STATE_FILE" ]]; then
    while IFS='=' read -r key value; do
        case "$key" in
            PREFIX) PREFIX="$value" ;;
            BUILD_DIR) BUILD_DIR="$value" ;;
            CONTEXT_MENU_PATH) CONTEXT_MENU_PATH="$value" ;;
        esac
    done < "$STATE_FILE"
fi

if (( EUID == 0 )); then
    SUDO=()
else
    command -v sudo >/dev/null 2>&1 || { printf 'sudo is required.\n' >&2; exit 1; }
    SUDO=(sudo)
fi

binary="${PREFIX}/bin/metadata"
if [[ -e "$binary" ]] && command -v pacman >/dev/null 2>&1 \
   && pacman -Qo "$binary" >/dev/null 2>&1; then
    owner="$(pacman -Qoq "$binary" 2>/dev/null || true)"
    printf 'The executable is owned by package "%s".\n' "$owner" >&2
    printf 'Remove it with pacman rather than Uninstall.sh.\n' >&2
    exit 1
fi

log "Removing application-specific installed files."
"${SUDO[@]}" rm -f \
    "${PREFIX}/bin/metadata" \
    "${PREFIX}/share/applications/io.github.yousefvand.metadata.desktop" \
    "${PREFIX}/share/icons/hicolor/scalable/apps/io.github.yousefvand.metadata.svg" \
    "${PREFIX}/share/licenses/metadata/LICENSE" \
    "${PREFIX}/share/kio/servicemenus/metadata-show.desktop"
"${SUDO[@]}" rmdir "${PREFIX}/share/licenses/metadata" 2>/dev/null || true

if [[ -n "$CONTEXT_MENU_PATH" ]]; then
    rm -f -- "$CONTEXT_MENU_PATH"
fi

command -v update-desktop-database >/dev/null 2>&1 \
    && "${SUDO[@]}" update-desktop-database "${PREFIX}/share/applications" >/dev/null 2>&1 \
    || true
command -v gtk-update-icon-cache >/dev/null 2>&1 \
    && "${SUDO[@]}" gtk-update-icon-cache -f -t "${PREFIX}/share/icons/hicolor" >/dev/null 2>&1 \
    || true
command -v kbuildsycoca6 >/dev/null 2>&1 && kbuildsycoca6 >/dev/null 2>&1 || true

rm -rf -- "$BUILD_DIR" "$STATE_DIR"

log "Uninstallation complete. Shared build/runtime packages were left installed intentionally."
