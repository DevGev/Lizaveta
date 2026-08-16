/* preview.h - embedded preview pane for the selected entry. */

#ifndef LIZ_PREVIEW_H
#define LIZ_PREVIEW_H

#include "app/app.h"
#include "fs/fs.h"
#include "rendering/x11/xc.h"

/* The preview pane occupies the bottom half of the list view. When that
 * half would be smaller than this many pixels, the pane is hidden even if
 * the selected entry is previewable. */
#define LIZ_PREVIEW_MIN_H 200

/* Maximum number of argv slots a slave may use. */
#define LIZ_PREVIEW_ARGV_MAX 16

/* Pane geometry handed to the command builder. x/y/w/h describe the pane
 * within the app window; sx/sy is the pane's position on screen, which a
 * slave can use as its initial window placement so it launches in the right
 * spot instead of flashing in at 0,0. */
typedef struct liz_preview_geom {
    int x, y, w, h;
    int sx, sy;
} liz_preview_geom;

/* A preview slave renders one kind of entry inside an embedded X11 window.
 * The spawned program must set its window title so the manager can locate
 * its window for reparenting. */
typedef struct liz_preview_slave {
    const char* name;   /* slave name */
    /* True when this slave can preview `e`. */
    bool (*matches)(const liz_fs_entry* e);
    /* Writes the command line for `path` into `argv` (capacity `cap`) and
     * returns the number of arguments written, or 0 on failure. The slots
     * may point at string literals, at `path`, or at buffers owned by the
     * manager; no allocation happens here and nothing is ever freed. */
    int (*command)(const char* path, const liz_preview_geom* geom,
                   char** argv, int cap);
    /* Window title the preview window is given (also matched against the
     * image basename as a fallback). */
    const char* window_title;
} liz_preview_slave;

/* Toggles the preview pane. Arming records the current selection; the
 * preview stays up only while that same entry is selected. Called from the
 * key handler. */
void liz_preview_toggle(liz_app* app);

/* Closes the preview pane (disarms and stops any running slave). */
void liz_preview_close(liz_app* app);

/* Returns the top edge of the active preview pane, i.e. the y at which the
 * file list should stop drawing, or -1 when no preview is shown. */
int liz_preview_pane_top(liz_app* app);

/* Re-evaluates the preview pane for the current selection: while armed it
 * spawns or kills the slave, embeds/positions its window, and reaps dead
 * slaves. It also closes the preview when the selection moves. Cheap when
 * nothing changed. Called from liz_app_render. */
void liz_preview_sync(liz_app* app);

/* Stops any running slave and releases the module. Called from
 * liz_app_quit before the X connection is torn down. */
void liz_preview_shutdown(liz_app* app);

#endif /* LIZ_PREVIEW_H */
