/* filemanager1.h - org.freedesktop.FileManager1 support. */

#ifndef LIZ_FILEMANAGER1_H
#define LIZ_FILEMANAGER1_H

#include <dbus/dbus.h>
#include <stdbool.h>

/* Claims org.freedesktop.FileManager1 on `conn` and registers
 * /org/freedesktop/FileManager1, answering ShowFolders/ShowItems/
 * ShowItemProperties by spawning a detached `lizaveta_exe PATH` window
 * (reusing the running lizaveta binary, just like a normal launch -- no
 * special GUI mode needed for this one).
 *
 * Returns false without registering anything if the name is already owned
 * by something else -- typically a *running* Thunar/Nautilus/etc, not just
 * one that's D-Bus-activatable, which a plain "request the name" call
 * cannot preempt. The caller should log this and keep going: it's not a
 * fatal error for whatever else is being served on the same connection. */
bool liz_filemanager1_register(DBusConnection* conn, const char* lizaveta_exe);

#endif /* LIZ_FILEMANAGER1_H */
