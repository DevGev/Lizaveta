/* sidebar.h - left sidebar with pinned folders and connected devices. */

#ifndef LIZ_SIDEBAR_H
#define LIZ_SIDEBAR_H

#include "app/app.h"

/* Builds the pinned folder list from the user's home and enumerates the
 * mounted devices. Safe to call once at startup. */
void liz_sidebar_init(liz_app* app);

/* Draws the sidebar, its section headers and hover highlights. Re-reads
 * /proc/mounts so newly connected devices show up without a restart. */
void liz_sidebar_draw(liz_app* app);

/* Maps a window y coordinate to a flattened sidebar item index (pinned
 * entries first, then devices), or -1 when the pointer is not over an item. */
int liz_sidebar_item_at(liz_app* app, int y);

/* Navigates to the quick link under (x, y), if any. Returns true when the
 * click was consumed by the sidebar. */
bool liz_sidebar_click(liz_app* app, int x, int y);

#endif /* LIZ_SIDEBAR_H */
