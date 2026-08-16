/* delete.h - delete confirmation prompt. */

#ifndef LIZ_DELETE_H
#define LIZ_DELETE_H

#include "app/app.h"

/* Starts a confirmation for the inclusive row range [a, b]. */
void liz_delete_start_range(liz_app* app, int a, int b);

/* Starts a confirmation for the selected rows. Does nothing when nothing is
 * selected -- `d` only deletes a real selection. */
void liz_delete_start_selection(liz_app* app);

/* Cancels the in-progress confirmation. */
void liz_delete_cancel(liz_app* app);

/* Handles a key while the confirmation is active. Returns true when
 * consumed. */
bool liz_delete_handle_key(liz_app* app, xc_event ev);

/* Draws the confirmation prompt in the status bar. */
void liz_delete_draw(liz_app* app);

#endif /* LIZ_DELETE_H */
