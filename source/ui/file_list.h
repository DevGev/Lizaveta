/* file_list.h - file/directory list widget. */

#ifndef LIZ_FILE_LIST_H
#define LIZ_FILE_LIST_H

#include "app/app.h"

/* Draws the file list, its scrollbar and hover/selection highlights. */
void liz_list_draw(liz_app* app);

/* Maps a window y coordinate to an absolute row index, or -1. */
int liz_list_row_at(liz_app* app, int y);

/* Left edge of the file list in window pixels: the sidebar's width when it
 * is visible, otherwise 0. */
int liz_list_area_left(liz_app* app);

/* Number of rows that fit in the visible list area. */
int liz_list_visible_count(liz_app* app);

/* Scrolls the list by `delta` rows (clamped). */
void liz_list_scroll(liz_app* app, int delta);

/* Adjusts scroll so the selected row is inside the visible area. */
void liz_list_keep_selection_visible(liz_app* app);

#endif /* LIZ_FILE_LIST_H */
