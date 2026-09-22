/* apps.h - which installed application opens a file, and running it.
 *
 * This is what xdg-open would do, done directly. xdg-open only consults the
 * association database when it recognises the desktop; under anything it
 * does not know, Hyprland included, it falls through to launching the web
 * browser, which then hands the file back to the real default application. */

#ifndef LIZ_APPS_H
#define LIZ_APPS_H

#include <stdbool.h>

#define LIZ_APP_ID_MAX   96
#define LIZ_APP_NAME_MAX 64
#define LIZ_APP_EXEC_MAX 256
#define LIZ_APPS_MAX     12

/* One installed application, read from its desktop entry. */
typedef struct {
    char id[LIZ_APP_ID_MAX];     /* desktop file name, e.g. org.gnome.Loupe.desktop */
    char name[LIZ_APP_NAME_MAX]; /* Name=, or the id when it has none */
    char exec[LIZ_APP_EXEC_MAX]; /* Exec=, still holding its % field codes */
    bool terminal;               /* Terminal=true: needs a terminal to run in */
} liz_desktop_app;

/* The desktop's default application for `path`. Returns false when the
 * association database names none, or names one that is not installed. */
bool liz_apps_default(const char* path, liz_desktop_app* out);

/* The application to open `path` with: the desktop default when one is
 * set, otherwise the first application registered for the type. Returns
 * false when nothing claims the file. */
bool liz_apps_for(const char* path, liz_desktop_app* out);

/* Every application registered for `path`'s MIME type, the default first.
 * Returns how many were written (at most `max`). */
int liz_apps_candidates(const char* path, liz_desktop_app* out, int max);

/* Replaces the current process with `app` opening `path`. Only returns if
 * the program could not be executed. Call it from a detached child. */
void liz_apps_exec(const liz_desktop_app* app, const char* path);

#endif /* LIZ_APPS_H */
