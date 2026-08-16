#!/bin/sh
# Registers lizaveta as the default handler for the "file-selector" xdg
# scheme, so that `xdg-open file-selector:///path/...` and any app that
# uses that scheme launches lizaveta in file-picker mode.
#
# Usage:
#   ./install/install-filechooser.sh [BIN]
#
# BIN defaults to the xmake release build next to this script; pass a path
# to install a different lizaveta binary.

set -e

HERE="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${1:-/bin/lizaveta}"

if [ ! -x "$BIN" ]; then
    echo "error: lizaveta binary not found at $BIN" >&2
    echo "       build it first (xmake) or pass the binary path as an argument" >&2
    exit 1
fi

# Canonical, absolute path; the .desktop Exec line must not change when the
# user later moves the binary or the repo.
case "$BIN" in
    /*) ABS="$BIN" ;;
    *)  ABS="$(pwd)/$BIN" ;;
esac
ABS="$(readlink -f "$ABS")"

APPS_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$APPS_DIR"

# The Exec first word must be the bare executable path with no quotes:
# xdg-open runs `command -v <first word>` to validate the entry, so quoted
# paths are rejected.
DESKTOP="$APPS_DIR/lizaveta-filechooser.desktop"
cat > "$DESKTOP" <<EOF
[Desktop Entry]
Type=Application
Name=Lizaveta (file picker)
GenericName=Select a file
Comment=Choose a file with the lizaveta file manager
Exec=$ABS --filechooser %f
Terminal=false
Categories=Utility;FileManager;
MimeType=x-scheme-handler/file-selector;
EOF

# Let the desktop database know about the new entry (best effort).
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$APPS_DIR" >/dev/null 2>&1 || true
fi

# Make it the default handler for the file-selector scheme. xdg-mime needs
# the .desktop file to live in an applications dir under XDG_DATA_HOME/DIRS
# and writes the association into ~/.config/mimeapps.list.
xdg-mime default lizaveta-filechooser.desktop x-scheme-handler/file-selector

echo "Installed $DESKTOP"
echo "lizaveta is now the default handler for x-scheme-handler/file-selector."
echo
echo "Test it with:  xdg-open 'file-selector://$HOME'"
