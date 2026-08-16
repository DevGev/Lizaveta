#!/bin/sh
# Registers lizaveta itself as two D-Bus services:
#   - org.freedesktop.impl.portal.FileChooser (Open/Save/"Select File"
#     dialogs from Firefox and other portal-aware apps)
#   - org.freedesktop.FileManager1 ("Show in folder" from Firefox's
#     downloads panel and similar -- a different, older mechanism, unrelated
#     to xdg-desktop-portal)
# both served by the same `lizaveta --portal-service` process.
#
# Unlike install-termfilechooser.sh, there is no third-party backend to
# build and no wrapper script to glue it to lizaveta: lizaveta answers the
# D-Bus calls directly. If you're migrating from the termfilechooser setup,
# run uninstall-termfilechooser.sh first.
#
# Usage:  ./install/install-portal.sh [LIZAVETA_BIN]

set -e

HERE="$(cd "$(dirname "$0")/.." && pwd)"
LIZAVETA="${1:-/bin/lizaveta}"
if [ ! -x "$LIZAVETA" ]; then
    echo "error: lizaveta binary not found at $LIZAVETA (build it with xmake)" >&2
    exit 1
fi
case "$LIZAVETA" in
    /*) LIZAVETA="$(readlink -f "$LIZAVETA")" ;;
    *)  LIZAVETA="$(readlink -f "$(pwd)/$LIZAVETA")" ;;
esac

DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
NAME=lizaveta

mkdir -p "$DATA_HOME/xdg-desktop-portal/portals" "$DATA_HOME/dbus-1/services" \
         "$CONFIG_HOME/xdg-desktop-portal"

# 1. D-Bus activation service: starts `lizaveta --portal-service` on demand
#    the first time something calls org.freedesktop.impl.portal.desktop.lizaveta.
cat > "$DATA_HOME/dbus-1/services/org.freedesktop.impl.portal.desktop.$NAME.service" <<EOF
[D-BUS Service]
Name=org.freedesktop.impl.portal.desktop.$NAME
Exec=$LIZAVETA --portal-service
EOF
echo ">> installed $DATA_HOME/dbus-1/services/org.freedesktop.impl.portal.desktop.$NAME.service"

# 2. Portal registration (.portal filename is the portal name used below in
#    portals.conf).
cat > "$DATA_HOME/xdg-desktop-portal/portals/$NAME.portal" <<EOF
[portal]
DBusName=org.freedesktop.impl.portal.desktop.$NAME
Interfaces=org.freedesktop.impl.portal.FileChooser
UseIn=*
EOF
echo ">> installed $DATA_HOME/xdg-desktop-portal/portals/$NAME.portal"

# 3. Prefer lizaveta over gtk for the FileChooser portal specifically,
#    leaving everything else (screenshot, etc.) on the desktop default.
CONF="$CONFIG_HOME/xdg-desktop-portal/portals.conf"
if [ -e "$CONF" ] && [ ! -e "$CONF.bak" ]; then
    cp "$CONF" "$CONF.bak"
    echo ">> backed up $CONF to $CONF.bak"
fi
cat > "$CONF" <<EOF
[preferred]
default=gtk
org.freedesktop.impl.portal.FileChooser=$NAME
EOF
echo ">> installed $CONF"

# 4. FileManager1 activation service ("Show in folder"). Same Exec line as
#    step 1 -- starting the process claims both bus names -- but D-Bus
#    activation is per-name, so it needs its own .service file to be found.
#
#    Unlike the portal name above, this name is commonly held by a *running*
#    file manager (Thunar, Nautilus, ...) rather than just being available
#    for on-demand activation, and a plain "request the name" can't preempt
#    that. If one is already running, this step still installs the service
#    file (so lizaveta takes over once that file manager next exits and
#    releases the name), but "Show in folder" keeps opening the old one
#    until then. Close it once for this to take effect immediately.
cat > "$DATA_HOME/dbus-1/services/org.freedesktop.FileManager1.service" <<EOF
[D-BUS Service]
Name=org.freedesktop.FileManager1
Exec=$LIZAVETA --portal-service
EOF
echo ">> installed $DATA_HOME/dbus-1/services/org.freedesktop.FileManager1.service"

# 5. Force Firefox through the portal file chooser, from the user-level
#    desktop entry (overrides the system one without touching /usr). If
#    install-termfilechooser.sh already did this, it's a no-op.
FF_SRC=/usr/share/applications/firefox.desktop
if [ -f "$FF_SRC" ] && ! grep -q "GTK_USE_PORTAL=1" "$DATA_HOME/applications/firefox.desktop" 2>/dev/null; then
    mkdir -p "$DATA_HOME/applications"
    sed 's/^Exec=\(.*\)/Exec=env GTK_USE_PORTAL=1 \1/' "$FF_SRC" \
        > "$DATA_HOME/applications/firefox.desktop"
    echo ">> Firefox desktop entry set to GTK_USE_PORTAL=1 ($DATA_HOME/applications/firefox.desktop)"
    echo "   note: launching firefox from a terminal needs 'export GTK_USE_PORTAL=1' instead"
fi

# 6. Restart the portal daemon so it picks up the new .service/.portal
#    files and portals.conf. All D-Bus activated, so they come back on
#    demand -- lizaveta's own service only starts the first time a file
#    picker (or "Show in folder") is actually requested, it isn't left
#    running otherwise.
pkill -f '/usr/lib/xdg-desktop-[p]ortal' 2>/dev/null || true
pkill -f "xdg-desktop-portal-[g]tk" 2>/dev/null || true

# Is anything currently holding org.freedesktop.FileManager1? If so, "Show
# in folder" won't reach lizaveta until it exits -- tell the person now
# instead of leaving them to wonder why the D-Bus service alone didn't work.
if command -v dbus-send >/dev/null 2>&1; then
    OWNER="$(dbus-send --session --print-reply --dest=org.freedesktop.DBus \
        /org/freedesktop/DBus org.freedesktop.DBus.GetNameOwner \
        string:org.freedesktop.FileManager1 2>/dev/null | grep -o '":[0-9.]*"' || true)"
    if [ -n "$OWNER" ]; then
        echo ">> note: org.freedesktop.FileManager1 is currently held by a running file"
        echo "   manager (owner $OWNER) -- \"Show in folder\" will keep opening that one"
        echo "   until it's closed. Close it once and lizaveta will take over from then on."
    fi
fi

echo
echo "done. Firefox's 'Select File' / 'Save As' should now open lizaveta directly,"
echo "and 'Show in folder' in its downloads panel should too (see the note above"
echo "if another file manager is currently running)."
echo "sanity check without relaunching Firefox:"
echo "  dbus-send --session --print-reply \\"
echo "    --dest=org.freedesktop.impl.portal.desktop.$NAME \\"
echo "    /org/freedesktop/portal/desktop \\"
echo "    org.freedesktop.DBus.Introspectable.Introspect"

