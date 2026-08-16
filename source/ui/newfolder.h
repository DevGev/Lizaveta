/* newfolder.h - "create folder" prompt widget. */

#ifndef LIZ_NEWFOLDER_H
#define LIZ_NEWFOLDER_H

#include "app/app.h"

/* Starts the "New folder: " prompt in the current directory. */
void liz_newfolder_start(liz_app* app);

/* Cancels the in-progress prompt. */
void liz_newfolder_cancel(liz_app* app);

/* Handles a key while the prompt is active. Returns true when consumed. */
bool liz_newfolder_handle_key(liz_app* app, xc_event ev);

/* Places the cursor at the byte offset under pixel x and begins a selection
 * drag. Returns the offset, or -1 when the prompt is inactive. */
int liz_newfolder_click(liz_app* app, int x);

/* Extends the selection drag to the byte offset under pixel x. */
void liz_newfolder_drag(liz_app* app, int x);

/* Draws the prompt in the status bar. */
void liz_newfolder_draw(liz_app* app);

#endif /* LIZ_NEWFOLDER_H */
