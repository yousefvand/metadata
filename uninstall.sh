#!/usr/bin/env bash
set -Eeuo pipefail

APP_NAME="metadata"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
STATE_DIR="${XDG_STATE_HOME:-${HOME}/.local/state}/${APP_NAME}"
STATE_FILE="${STATE_DIR}/install-state"
PREFIX="${PREFIX:-/usr}"
BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/build}"
CONTEXT_MENU_PATH="${XDG_DATA_HOME:-${HOME}/.local/share}/kio/servicemenus/metadata-show.desktop"

log() { printf '[metadata] %s\n' "$*"; }
fail() { printf '[metadata] ERROR: %s\n' "$*" >&2; exit 1; }

if [[ -f "$STATE_FILE" ]]; then
    # The state file is created by install.sh, owned by the current user, and mode 0600.
    # shellcheck disable=SC1090
    source "$STATE_FILE"
fi

if (( EUID == 0 )); then
    SUDO=()
elif [[ -w "$PREFIX" ]]; then
    SUDO=()
else
    command -v sudo >/dev/null 2>&1 || fail "sudo is required for ${PREFIX}."
    SUDO=(sudo)
fi

binary="${PREFIX}/bin/metadata"
if [[ -e "$binary" ]] && command -v pacman >/dev/null 2>&1 \
   && pacman -Qo "$binary" >/dev/null 2>&1; then
    owner="$(pacman -Qoq "$binary" 2>/dev/null || true)"
    fail "The executable is owned by package '${owner}'. Remove it with pacman."
fi

log "Removing application-specific installed files."
"${SUDO[@]}" rm -f \
    "${PREFIX}/bin/metadata" \
    "${PREFIX}/share/applications/io.github.yousefvand.metadata.desktop" \
    "${PREFIX}/share/icons/hicolor/scalable/apps/io.github.yousefvand.metadata.svg" \
    "${PREFIX}/share/licenses/metadata/LICENSE" \
    "${PREFIX}/share/doc/metadata/README.md" \
    "${PREFIX}/share/doc/metadata/CHANGELOG.md" \
    "${PREFIX}/share/kio/servicemenus/metadata-show.desktop"
"${SUDO[@]}" rmdir \
    "${PREFIX}/share/licenses/metadata" \
    "${PREFIX}/share/doc/metadata" 2>/dev/null || true

if [[ -n "${CONTEXT_MENU_PATH:-}" ]]; then
    rm -f -- "$CONTEXT_MENU_PATH"
fi

command -v update-desktop-database >/dev/null 2>&1 \
    && "${SUDO[@]}" update-desktop-database "${PREFIX}/share/applications" >/dev/null 2>&1 \
    || true
command -v gtk-update-icon-cache >/dev/null 2>&1 \
    && "${SUDO[@]}" gtk-update-icon-cache -f -t "${PREFIX}/share/icons/hicolor" >/dev/null 2>&1 \
    || true
command -v kbuildsycoca6 >/dev/null 2>&1 \
    && kbuildsycoca6 >/dev/null 2>&1 || true

rm -rf -- "$BUILD_DIR" "$STATE_DIR"
log "Uninstallation complete. Shared packages were left installed intentionally."
