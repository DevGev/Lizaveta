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

// Pinned sidebar entries: array of {label, path} pairs.
// Paths beginning with : are expanded at runtime:
//   :home:   -> $HOME (or /etc/passwd fallback)
//   :trash:  -> $XDG_DATA_HOME/Trash or ~/.local/share/Trash

#include <limits.h>

#ifndef LIZ_PINNED
#define LIZ_PINNED { \
    {"Home",      ":home:"             }, \
    {"Desktop",   ":home:/Desktop"     }, \
    {"Downloads", ":home:/Downloads"   }, \
    {"Pictures",  ":home:/Pictures"    }, \
    {"Trash",     ":trash:"            }, \
}
#endif

// Preview

/* Window title (and WM_CLASS instance/class) the preview slaves are told to
 * use; the manager matches this, plus the previewed file's basename, to find
 * and embed the slave's window. Keep it unique-ish so two Lizaveta instances
 * do not grab each other's preview windows. */
#ifndef LIZ_PREVIEW_TITLE
#define LIZ_PREVIEW_TITLE "lzaveta-preview"
#endif

/* argv templates for the embedded preview slaves. Each expands to a
 * comma-separated list of quoted argument strings (like LIZ_PINNED). The
 * manager substitutes these placeholders when spawning:
 *
 *   {geo}     pane geometry relative to lizaveta's window: WxH+X+Y
 *   {screen}  pane geometry in screen coordinates: WxH+X+Y
 *   {win}     lizaveta's X window id (0x...), for -w/--embed style embedding
 *   {title}   LIZ_PREVIEW_TITLE (the marker the manager matches on)
 *   {path}    absolute path of the file being previewed
 *
 * The window title/class must contain {title} or otherwise be findable by the
 * manager, or the preview window will never be embedded. A template may use at
 * most 15 arguments (LIZ_PREVIEW_ARGV_MAX - 1). */
#ifndef LIZ_PREVIEW_IMAGE_ARGV
#define LIZ_PREVIEW_IMAGE_ARGV { \
    "feh", "-x", "-g", "{screen}", "-Z", "--no-fehbg", \
    "--image-bg", "#212128", "--title", "{title}", "{path}", \
}
#endif

/* Utilizes the anygeometry patch for suckless.
 * Remove -G {geo} if you don't have the patch applied. */
#ifndef LIZ_PREVIEW_TEXT_ARGV
#define LIZ_PREVIEW_TEXT_ARGV { \
    "st", "-G", "{geo}", "-n", "{title}", "-c", "{title}", \
    "-T", "{title}", "-w", "{win}", "-e", "vim", "{path}", \
}
#endif

// Background

/* Window background opacity: 0.0 fully transparent, 1.0 fully opaque.
 * Requires a server with an ARGB visual and a running compositor (picom,
 * compton, ...). Below 1.0 the desktop -- and, with LIZ_BG_BLUR, a
 * gaussian-blurred copy of it -- shows through the file list area. The
 * chrome (nav/sidebar/status bars) keeps its opaque theme colors. */
#ifndef LIZ_BG_OPACITY
#define LIZ_BG_OPACITY 1.0
#endif

/* Gaussian blur behind the window, done by the compositor via the
 * _NET_WM_BLUR_BEHIND_REGION hint. 0 disables it; any nonzero value blurs
 * whatever is behind the window with the compositor's own blur radius (the
 * hint carries no radius). Only visible combined with LIZ_BG_OPACITY below
 * 1.0, and only when the compositor honors the hint. */
#ifndef LIZ_BG_BLUR
#define LIZ_BG_BLUR 0
#endif

// Keybindings

/* The default input mode. 1 = vim mode (/ search, `:` command line, plain
 * h/j/k/l motions, plain-letter bindings). 0 = non-vim mode: plain
 * keystrokes type into an incremental search, and the LIZ_NOVIM_BIND_*
 * defaults below use modified keys only (Ctrl+T opens a terminal, F2
 * renames, Delete deletes, ...). */
#ifndef LIZ_VIM_MODE_DEFAULT
#define LIZ_VIM_MODE_DEFAULT 1
#endif

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
#ifndef LIZ_BIND_DELETE
#define LIZ_BIND_DELETE        LIZ_BIND(DELETE,        XK_Delete, 0)
#endif

// Non-vim mode keybindings (used when LIZ_VIM_MODE_DEFAULT is 0).
// Plain unmodified character keys are reserved for type-to-search, so every
// bound key here is either modified (Ctrl/Alt/Super) or a non-text key such
// as F2/Delete/Escape.

#ifndef LIZ_NOVIM_BIND_QUIT
#define LIZ_NOVIM_BIND_QUIT          LIZ_BIND(QUIT,          XK_d, LIZ_MOD_CTRL | LIZ_MOD_ALT)
#endif
#ifndef LIZ_NOVIM_BIND_NAV_EDIT
#define LIZ_NOVIM_BIND_NAV_EDIT      LIZ_BIND(NAV_EDIT,      XK_l, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_NOVIM_BIND_TOGGLE_HIDDEN
#define LIZ_NOVIM_BIND_TOGGLE_HIDDEN LIZ_BIND(TOGGLE_HIDDEN,  XK_h, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_NOVIM_BIND_TOGGLE_SIDEBAR
#define LIZ_NOVIM_BIND_TOGGLE_SIDEBAR LIZ_BIND(TOGGLE_SIDEBAR, XK_b, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_NOVIM_BIND_HALF_DOWN
#define LIZ_NOVIM_BIND_HALF_DOWN     LIZ_BIND(HALF_DOWN,     XK_d, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_NOVIM_BIND_HALF_UP
#define LIZ_NOVIM_BIND_HALF_UP       LIZ_BIND(HALF_UP,       XK_u, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_NOVIM_BIND_HISTORY_BACK
#define LIZ_NOVIM_BIND_HISTORY_BACK  LIZ_BIND(HISTORY_BACK,  XK_o, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_NOVIM_BIND_HISTORY_FORWARD
#define LIZ_NOVIM_BIND_HISTORY_FORWARD LIZ_BIND(HISTORY_FORWARD, XK_i, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_NOVIM_BIND_COPY
#define LIZ_NOVIM_BIND_COPY          LIZ_BIND(COPY,          XK_c, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_NOVIM_BIND_CUT
#define LIZ_NOVIM_BIND_CUT           LIZ_BIND(CUT,           XK_x, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_NOVIM_BIND_PASTE
#define LIZ_NOVIM_BIND_PASTE         LIZ_BIND(PASTE,         XK_v, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_NOVIM_BIND_PREVIEW
#define LIZ_NOVIM_BIND_PREVIEW       LIZ_BIND(PREVIEW,       XK_p, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_NOVIM_BIND_NEW_FOLDER
#define LIZ_NOVIM_BIND_NEW_FOLDER    LIZ_BIND(NEW_FOLDER,    XK_n, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_NOVIM_BIND_OPEN_TERMINAL
#define LIZ_NOVIM_BIND_OPEN_TERMINAL LIZ_BIND(OPEN_TERMINAL, XK_t, LIZ_MOD_CTRL)
#endif
#ifndef LIZ_NOVIM_BIND_OPEN_TERMINAL_DIR
#define LIZ_NOVIM_BIND_OPEN_TERMINAL_DIR LIZ_BIND(OPEN_TERMINAL_DIR, XK_t, LIZ_MOD_CTRL | LIZ_MOD_SHIFT)
#endif
#ifndef LIZ_NOVIM_BIND_RENAME
#define LIZ_NOVIM_BIND_RENAME        LIZ_BIND(RENAME,        XK_F2, 0)
#endif
#ifndef LIZ_NOVIM_BIND_GO_HOME
#define LIZ_NOVIM_BIND_GO_HOME       LIZ_BIND(GO_HOME,       XK_Home, LIZ_MOD_ALT)
#endif
#ifndef LIZ_NOVIM_BIND_CLOSE_PREVIEW
#define LIZ_NOVIM_BIND_CLOSE_PREVIEW LIZ_BIND(CLOSE_PREVIEW, XK_Escape, 0)
#endif
#ifndef LIZ_NOVIM_BIND_DELETE
#define LIZ_NOVIM_BIND_DELETE        LIZ_BIND(DELETE,        XK_Delete, 0)
#endif

#endif
