/* portal.h - native D-Bus backends: org.freedesktop.impl.portal.FileChooser and org.freedesktop.FileManager1. */

#ifndef LIZ_PORTAL_H
#define LIZ_PORTAL_H

/* Runs the service loop forever (until the bus connection drops or setup
 * fails). `lizaveta_exe` is the absolute path to this same binary, used to
 * spawn the picker windows. Returns a process exit code; only returns at
 * all on a fatal setup or connection error. */
int liz_portal_service_run(const char* lizaveta_exe);

#endif /* LIZ_PORTAL_H */
