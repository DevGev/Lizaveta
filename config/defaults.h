/* This file contains the default project configuration.
 * For your own configurations create config/user.h with overrides, see config/example_user.h.
 *
 * cp config/example_user.h config/user.h
 * */

#ifndef LIZAVETA_CONFIG_DEFAULTS_H
#define LIZAVETA_CONFIG_DEFAULTS_H

#include <X11/keysym.h>
#include "ui/keybind.h"

// Theme

#define LIZ_THEME_DEFAULT

// Font

/* The size is in pixels rather than points, so it tracks LIZ_UI_ROW_H
 * instead of the DPI the display reports. */

#ifndef LIZ_FONT
#define LIZ_FONT      "monospace"
#endif
#ifndef LIZ_FONT_SIZE
#define LIZ_FONT_SIZE 13
#endif

// Keybindings

#ifndef LIZ_BIND_QUIT
#define LIZ_BIND_QUIT          LIZ_BIND(QUIT,          XK_d, LIZ_MOD_CTRL | LIZ_MOD_ALT)
#endif
#ifndef LIZ_BIND_NAV_EDIT
#define LIZ_BIND_NAV_EDIT      LIZ_BIND(NAV_EDIT,      XK_l, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_BIND_TOGGLE_HIDDEN
#define LIZ_BIND_TOGGLE_HIDDEN LIZ_BIND(TOGGLE_HIDDEN,  XK_h, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_BIND_TOGGLE_SIDEBAR
#define LIZ_BIND_TOGGLE_SIDEBAR LIZ_BIND(TOGGLE_SIDEBAR, XK_b, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_BIND_HALF_DOWN
#define LIZ_BIND_HALF_DOWN     LIZ_BIND(HALF_DOWN,     XK_d, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_BIND_HALF_UP
#define LIZ_BIND_HALF_UP       LIZ_BIND(HALF_UP,       XK_u, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_BIND_HISTORY_BACK
#define LIZ_BIND_HISTORY_BACK  LIZ_BIND(HISTORY_BACK,  XK_o, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_BIND_HISTORY_FORWARD
#define LIZ_BIND_HISTORY_FORWARD LIZ_BIND(HISTORY_FORWARD, XK_i, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_BIND_COPY
#define LIZ_BIND_COPY          LIZ_BIND(COPY,          XK_c, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_BIND_CUT
#define LIZ_BIND_CUT           LIZ_BIND(CUT,           XK_x, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_BIND_PASTE
#define LIZ_BIND_PASTE         LIZ_BIND(PASTE,         XK_v, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_BIND_PREVIEW
#define LIZ_BIND_PREVIEW       LIZ_BIND(PREVIEW,       XK_p, 0)
#endif
#ifndef LIZ_BIND_NEW_FOLDER
#define LIZ_BIND_NEW_FOLDER    LIZ_BIND(NEW_FOLDER,    XK_o, 0)
#endif
#ifndef LIZ_BIND_OPEN_TERMINAL
#define LIZ_BIND_OPEN_TERMINAL LIZ_BIND(OPEN_TERMINAL, XK_t, 0)
#endif
#ifndef LIZ_BIND_OPEN_TERMINAL_DIR
#define LIZ_BIND_OPEN_TERMINAL_DIR LIZ_BIND(OPEN_TERMINAL_DIR, XK_T, 0)
#endif
#ifndef LIZ_BIND_RENAME
#define LIZ_BIND_RENAME        LIZ_BIND(RENAME,        XK_r, 0)
#endif
#ifndef LIZ_BIND_GO_HOME
#define LIZ_BIND_GO_HOME       LIZ_BIND(GO_HOME,       XK_H, 0)
#endif
#ifndef LIZ_BIND_CLOSE_PREVIEW
#define LIZ_BIND_CLOSE_PREVIEW LIZ_BIND(CLOSE_PREVIEW, XK_Escape, 0)
#endif

#endif
