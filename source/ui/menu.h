/* menu.h - right-click context menus for the file list and the sidebar. */

#ifndef LIZ_MENU_H
#define LIZ_MENU_H

#include "app/app.h"

/* Opens the menu at (x, y) for a right-click that hit `row` (-1 for empty
 * space). Builds the item list and clamps the menu to stay on screen. */
void liz_menu_open(liz_app* app, int x, int y, int row);

/* Opens the sidebar menu at (x, y) for the sidebar entry `index`
 * (pinned[] when is_device is false, devices[] when true). */
void liz_menu_open_sidebar(liz_app* app, int x, int y, int index, bool is_device);

/* Closes the menu without running an action. */
void liz_menu_close(liz_app* app);

/* Handles any button press while the menu is active: a click on an item
 * runs it, a click anywhere else just dismisses the menu. Always consumes
 * the event -- call this instead of the normal button dispatch. */
void liz_menu_handle_button(liz_app* app, xc_event ev);

/* Updates which item is hovered, for the highlight. */
void liz_menu_handle_motion(liz_app* app, xc_event ev);

/* Any key while the menu is active dismisses it (a simple menu; no keyboard
 * navigation of items). Returns true (always consumes) when the menu was
 * active. */
bool liz_menu_handle_key(liz_app* app, xc_event ev);

/* Draws the menu on top of everything else. No-op when inactive. */
void liz_menu_draw(liz_app* app);

#endif /* LIZ_MENU_H */
