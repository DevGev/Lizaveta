/* nav.h - breadcrumb navigation bar widget. */

#ifndef LIZ_NAV_H
#define LIZ_NAV_H

#include "app/app.h"

/* Draws the breadcrumb navigation bar. Rebuilds app->nav_sg (segment labels
 * and pixel bounds) for hit-testing. */
void liz_nav_draw(liz_app* app);

/* Returns the index of the breadcrumb segment covering pixel x, or -1. */
int liz_nav_hit(liz_app* app, int x, int y);

/* Location-bar editing: toggles between the breadcrumb view and a raw-path
 * editor. While editing, liz_nav_draw shows the path with a text cursor and
 * a gray tab-completion suffix. */
void liz_nav_toggle_edit(liz_app* app);

/* Handles a key while the location bar is being edited. Returns true when
 * the key was consumed. */
bool liz_nav_handle_key(liz_app* app, xc_event ev);

/* Places the text cursor at the byte offset under pixel x and begins a
 * selection drag there. Returns the new offset, or -1 when not editing. */
int liz_nav_edit_click(liz_app* app, int x);

/* Extends the selection drag to the byte offset under pixel x. Called on
 * pointer motion while the left button is held. */
void liz_nav_edit_drag(liz_app* app, int x);

/* Recomputes the gray completion suffix after the editor text changed
 * outside the normal key path. */
void liz_nav_refresh_complete(liz_app* app);

#endif /* LIZ_NAV_H */
