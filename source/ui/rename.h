/* rename.h - rename prompt widget. */

#ifndef LIZ_RENAME_H
#define LIZ_RENAME_H

#include "app/app.h"

/* Starts renaming the selected entry. Ignored when there is no selection or
 * the entry is "..". The prompt is pre-edited with the current name. */
void liz_rename_start(liz_app* app);

/* Cancels the in-progress rename. */
void liz_rename_cancel(liz_app* app);

/* Handles a key while the rename prompt is active. Returns true when
 * consumed. */
bool liz_rename_handle_key(liz_app* app, xc_event ev);

/* Places the cursor at the byte offset under pixel x and begins a selection
 * drag. Returns the offset, or -1 when the prompt is inactive. */
int liz_rename_click(liz_app* app, int x);

/* Extends the selection drag to the byte offset under pixel x. */
void liz_rename_drag(liz_app* app, int x);

/* Draws the rename prompt in the status bar. */
void liz_rename_draw(liz_app* app);

#endif /* LIZ_RENAME_H */
