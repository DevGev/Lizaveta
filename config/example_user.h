#ifndef LIZAVETA_CONFIG_USER_H
#define LIZAVETA_CONFIG_USER_H

#include <X11/keysym.h>
#include "ui/keybind.h"

/* Copy to config/user.h and uncomment what you want to change. */


/* ---- Theme --------------------------------------------------------------- */

/* Catppuccin Mocha */
// #undef LIZ_THEME_DEFAULT
// #define LIZ_THEME_CATPPUCCIN_MOCHA

/* Thunar */
// #undef LIZ_THEME_DEFAULT
// #define LIZ_THEME_THUNAR


/* ---- Keybindings --------------------------------------------------------- *
 *
 * Modifiers: LIZ_MOD_CTRL | LIZ_MOD_ALT | LIZ_MOD_SHIFT | LIZ_MOD_SUPER
 *
 * Examples:
 *
 * Ctrl+Shift+C:
 *   #undef LIZ_BIND_COPY
 *   #define LIZ_BIND_COPY LIZ_BIND(COPY, XK_c, LIZ_MOD_CTRL | LIZ_MOD_SHIFT)
 *
 * Unmodified key:
 *   #undef LIZ_BIND_GO_HOME
 *   #define LIZ_BIND_GO_HOME LIZ_BIND(GO_HOME, XK_H, 0)
 */

/* Quit              Ctrl+Alt+D */
// #undef LIZ_BIND_QUIT
// #define LIZ_BIND_QUIT LIZ_BIND(QUIT, XK_d, LIZ_MOD_CTRL | LIZ_MOD_ALT)

/* Edit navigation   Ctrl+L */
// #undef LIZ_BIND_NAV_EDIT
// #define LIZ_BIND_NAV_EDIT LIZ_BIND(NAV_EDIT, XK_l, LIZ_MOD_CTRL)

/* Hidden files      Ctrl+H */
// #undef LIZ_BIND_TOGGLE_HIDDEN
// #define LIZ_BIND_TOGGLE_HIDDEN LIZ_BIND(TOGGLE_HIDDEN, XK_h, LIZ_MOD_CTRL)

/* Sidebar           Ctrl+B */
// #undef LIZ_BIND_TOGGLE_SIDEBAR
// #define LIZ_BIND_TOGGLE_SIDEBAR LIZ_BIND(TOGGLE_SIDEBAR, XK_b, LIZ_MOD_CTRL)

/* Half down         Ctrl+D */
// #undef LIZ_BIND_HALF_DOWN
// #define LIZ_BIND_HALF_DOWN LIZ_BIND(HALF_DOWN, XK_d, LIZ_MOD_CTRL)

/* Half up           Ctrl+U */
// #undef LIZ_BIND_HALF_UP
// #define LIZ_BIND_HALF_UP LIZ_BIND(HALF_UP, XK_u, LIZ_MOD_CTRL)

/* History back      Ctrl+O */
// #undef LIZ_BIND_HISTORY_BACK
// #define LIZ_BIND_HISTORY_BACK LIZ_BIND(HISTORY_BACK, XK_o, LIZ_MOD_CTRL)

/* History forward   Ctrl+I */
// #undef LIZ_BIND_HISTORY_FORWARD
// #define LIZ_BIND_HISTORY_FORWARD LIZ_BIND(HISTORY_FORWARD, XK_i, LIZ_MOD_CTRL)

/* Copy              Ctrl+C */
// #undef LIZ_BIND_COPY
// #define LIZ_BIND_COPY LIZ_BIND(COPY, XK_c, LIZ_MOD_CTRL)

/* Cut               Ctrl+X */
// #undef LIZ_BIND_CUT
// #define LIZ_BIND_CUT LIZ_BIND(CUT, XK_x, LIZ_MOD_CTRL)

/* Paste             Ctrl+V */
// #undef LIZ_BIND_PASTE
// #define LIZ_BIND_PASTE LIZ_BIND(PASTE, XK_v, LIZ_MOD_CTRL)

/* Preview           P */
// #undef LIZ_BIND_PREVIEW
// #define LIZ_BIND_PREVIEW LIZ_BIND(PREVIEW, XK_p, 0)

/* New folder        O */
// #undef LIZ_BIND_NEW_FOLDER
// #define LIZ_BIND_NEW_FOLDER LIZ_BIND(NEW_FOLDER, XK_o, 0)

/* Open terminal     T */
// #undef LIZ_BIND_OPEN_TERMINAL
// #define LIZ_BIND_OPEN_TERMINAL LIZ_BIND(OPEN_TERMINAL, XK_t, 0)

/* Terminal here     Shift+T */
// #undef LIZ_BIND_OPEN_TERMINAL_DIR
// #define LIZ_BIND_OPEN_TERMINAL_DIR LIZ_BIND(OPEN_TERMINAL_DIR, XK_T, 0)

/* Rename            R */
// #undef LIZ_BIND_RENAME
// #define LIZ_BIND_RENAME LIZ_BIND(RENAME, XK_r, 0)

/* Home              H */
// #undef LIZ_BIND_GO_HOME
// #define LIZ_BIND_GO_HOME LIZ_BIND(GO_HOME, XK_H, 0)

/* Close preview     Escape */
// #undef LIZ_BIND_CLOSE_PREVIEW
// #define LIZ_BIND_CLOSE_PREVIEW LIZ_BIND(CLOSE_PREVIEW, XK_Escape, 0)

#endif
