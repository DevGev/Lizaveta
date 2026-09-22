#ifndef LIZAVETA_CONFIG_USER_H
#define LIZAVETA_CONFIG_USER_H

#include <X11/keysym.h>
#include "ui/keybind.h"

/* Copy to config/user.h and uncomment what you want to change. */


/* ---- Theme --------------------------------------------------------------- */

/* Default */
// #undef LIZ_THEME_DEFAULT
// #define LIZ_THEME_DEFAULT

/* Catppuccin Mocha */
// #undef LIZ_THEME_DEFAULT
// #define LIZ_THEME_CATPPUCCIN_MOCHA

/* Thunar */
// #undef LIZ_THEME_DEFAULT
// #define LIZ_THEME_THUNAR

/* Ayu Dark */
// #undef LIZ_THEME_DEFAULT
// #define LIZ_THEME_AYU_DARK

/* Nord */
// #undef LIZ_THEME_DEFAULT
// #define LIZ_THEME_NORD

/* Tokyo Dark */
// #undef LIZ_THEME_DEFAULT
// #define LIZ_THEME_TOKYO_DARK

/* Everforest Dark */
// #undef LIZ_THEME_DEFAULT
// #define LIZ_THEME_EVERFOREST_DARK


/* ---- Font ---------------------------------------------------------------- *
 *
 * Any fontconfig family name. The size is in pixels.
 */

// #undef LIZ_FONT
// #define LIZ_FONT "monospace"

// #undef LIZ_FONT_SIZE
// #define LIZ_FONT_SIZE 13


/* ---- Pinned sidebar entries ---------------------------------------------- *
 *
 * 2D array of {"Label", "Path"} pairs. Paths beginning with : are expanded
 * at runtime: :home: -> $HOME, :trash: -> XDG trash directory.
 * Maximum LIZ_SIDEBAR_PINNED_MAX (16) entries.
 */

// #undef LIZ_PINNED
// #define LIZ_PINNED { \
//     {"Home",      ":home:"              }, \
//     {"Desktop",   ":home:/Desktop"      }, \
//     {"Documents", ":home:/Documents"    }, \
//     {"Downloads", ":home:/Downloads"    }, \
//     {"Music",     ":home:/Music"        }, \
//     {"Pictures",  ":home:/Pictures"     }, \
//     {"Videos",    ":home:/Videos"       }, \
//     {"Trash",     ":trash:"             }, \
// }


/* ---- Background ---------------------------------------------------------- *
 *
 * Translucent, gaussian-blurred background (frosted glass). LIZ_BG_OPACITY
 * is 0.0..1.0; below 1.0 the file list area becomes translucent so the
 * desktop shows through. LIZ_BG_BLUR set to 1 makes the compositor blur
 * what's behind the window. Both need an ARGB-capable server, a running
 * compositor, and the compositor honoring _NET_WM_BLUR_BEHIND_REGION
 * (picom, compton, ...).
 */

// #undef LIZ_BG_OPACITY
// #define LIZ_BG_OPACITY 0.8

// #undef LIZ_BG_BLUR
// #define LIZ_BG_BLUR 1

/* ---- Preview ------------------------------------------------------------- *
 *
 * The embedded preview pane spawns separate programs for images and text.
 * Each template is a comma-separated argv list (like LIZ_PINNED); placeholders
 * are substituted at spawn time:
 *
 *   {geo}     pane geometry relative to lizaveta's window: WxH+X+Y
 *   {screen}  pane geometry in screen coordinates: WxH+X+Y
 *   {win}     lizaveta's X window id (0x...) for -w/--embed embedding
 *   {title}   the marker title (LIZ_PREVIEW_TITLE) used to find/embed the window
 *   {path}    the file being previewed
 *
 * The spawned program must let the manager find its window: either honor a
 * fixed title/class containing {title}, or title the window with the file
 * basename (the fallback the manager matches). Max 15 arguments per template.
 */

/* Marker title/class the manager matches on to find the preview window. */
// #undef LIZ_PREVIEW_TITLE
// #define LIZ_PREVIEW_TITLE "lzaveta-preview"

/* Images: swap feh for another viewer. Example: sxiv */
// #undef LIZ_PREVIEW_IMAGE_ARGV
// #define LIZ_PREVIEW_IMAGE_ARGV { \
//     "sxiv", "-z", "-g", "{screen}", "-t", "{title}", "{path}", \
// }

/* Text: swap st+vim for another terminal+editor. Example: kitty with vim */
// #undef LIZ_PREVIEW_TEXT_ARGV
// #define LIZ_PREVIEW_TEXT_ARGV { \
//     "kitty", "--class", "{title}", "--title", "{title}", \
//     "--embed", "{win}", "--hold", "vim", "{path}", \
// }

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

/* ---- Input mode --------------------------------------------------------- *
 *
 * 1 = vim mode (the default): / search, : command line, plain h/j/k/l
 *     motions and plain-letter bindings (p/o/t/r/H).
 * 0 = non-vim mode: typing a plain character starts an incremental
 *     type-to-search (Enter opens the match, Esc/Backspace clears it) and
 *     the LIZ_NOVIM_BIND_* table below is used instead.
 */
// #undef LIZ_VIM_MODE_DEFAULT
// #define LIZ_VIM_MODE_DEFAULT 1

/* Delete          Delete */
// #undef LIZ_BIND_DELETE
// #define LIZ_BIND_DELETE LIZ_BIND(DELETE, XK_Delete, 0)

/* ---- Non-vim mode keybindings ------------------------------------------ *
 *
 * Only in effect when LIZ_VIM_MODE_DEFAULT is 0. Plain unmodified character
 * keys are reserved for type-to-search, so keep these on modifiers or
 * non-text keys (F2/Delete/Escape).
 */

/* Quit              Ctrl+Alt+D */
// #undef LIZ_NOVIM_BIND_QUIT
// #define LIZ_NOVIM_BIND_QUIT LIZ_BIND(QUIT, XK_d, LIZ_MOD_CTRL | LIZ_MOD_ALT)

/* Edit navigation   Ctrl+L */
// #undef LIZ_NOVIM_BIND_NAV_EDIT
// #define LIZ_NOVIM_BIND_NAV_EDIT LIZ_BIND(NAV_EDIT, XK_l, LIZ_MOD_CTRL)

/* Hidden files      Ctrl+H */
// #undef LIZ_NOVIM_BIND_TOGGLE_HIDDEN
// #define LIZ_NOVIM_BIND_TOGGLE_HIDDEN LIZ_BIND(TOGGLE_HIDDEN, XK_h, LIZ_MOD_CTRL)

/* Sidebar           Ctrl+B */
// #undef LIZ_NOVIM_BIND_TOGGLE_SIDEBAR
// #define LIZ_NOVIM_BIND_TOGGLE_SIDEBAR LIZ_BIND(TOGGLE_SIDEBAR, XK_b, LIZ_MOD_CTRL)

/* Half down         Ctrl+D */
// #undef LIZ_NOVIM_BIND_HALF_DOWN
// #define LIZ_NOVIM_BIND_HALF_DOWN LIZ_BIND(HALF_DOWN, XK_d, LIZ_MOD_CTRL)

/* Half up           Ctrl+U */
// #undef LIZ_NOVIM_BIND_HALF_UP
// #define LIZ_NOVIM_BIND_HALF_UP LIZ_BIND(HALF_UP, XK_u, LIZ_MOD_CTRL)

/* History back      Ctrl+O */
// #undef LIZ_NOVIM_BIND_HISTORY_BACK
// #define LIZ_NOVIM_BIND_HISTORY_BACK LIZ_BIND(HISTORY_BACK, XK_o, LIZ_MOD_CTRL)

/* History forward   Ctrl+I */
// #undef LIZ_NOVIM_BIND_HISTORY_FORWARD
// #define LIZ_NOVIM_BIND_HISTORY_FORWARD LIZ_BIND(HISTORY_FORWARD, XK_i, LIZ_MOD_CTRL)

/* Copy              Ctrl+C */
// #undef LIZ_NOVIM_BIND_COPY
// #define LIZ_NOVIM_BIND_COPY LIZ_BIND(COPY, XK_c, LIZ_MOD_CTRL)

/* Cut               Ctrl+X */
// #undef LIZ_NOVIM_BIND_CUT
// #define LIZ_NOVIM_BIND_CUT LIZ_BIND(CUT, XK_x, LIZ_MOD_CTRL)

/* Paste             Ctrl+V */
// #undef LIZ_NOVIM_BIND_PASTE
// #define LIZ_NOVIM_BIND_PASTE LIZ_BIND(PASTE, XK_v, LIZ_MOD_CTRL)

/* Preview           Ctrl+P */
// #undef LIZ_NOVIM_BIND_PREVIEW
// #define LIZ_NOVIM_BIND_PREVIEW LIZ_BIND(PREVIEW, XK_p, LIZ_MOD_CTRL)

/* New folder        Ctrl+N */
// #undef LIZ_NOVIM_BIND_NEW_FOLDER
// #define LIZ_NOVIM_BIND_NEW_FOLDER LIZ_BIND(NEW_FOLDER, XK_n, LIZ_MOD_CTRL)

/* Open terminal     Ctrl+T */
// #undef LIZ_NOVIM_BIND_OPEN_TERMINAL
// #define LIZ_NOVIM_BIND_OPEN_TERMINAL LIZ_BIND(OPEN_TERMINAL, XK_t, LIZ_MOD_CTRL)

/* Terminal here     Ctrl+Shift+T */
// #undef LIZ_NOVIM_BIND_OPEN_TERMINAL_DIR
// #define LIZ_NOVIM_BIND_OPEN_TERMINAL_DIR LIZ_BIND(OPEN_TERMINAL_DIR, XK_t, LIZ_MOD_CTRL | LIZ_MOD_SHIFT)

/* Rename            F2 */
// #undef LIZ_NOVIM_BIND_RENAME
// #define LIZ_NOVIM_BIND_RENAME LIZ_BIND(RENAME, XK_F2, 0)

/* Home              Alt+Home */
// #undef LIZ_NOVIM_BIND_GO_HOME
// #define LIZ_NOVIM_BIND_GO_HOME LIZ_BIND(GO_HOME, XK_Home, LIZ_MOD_ALT)

/* Close preview     Escape */
// #undef LIZ_NOVIM_BIND_CLOSE_PREVIEW
// #define LIZ_NOVIM_BIND_CLOSE_PREVIEW LIZ_BIND(CLOSE_PREVIEW, XK_Escape, 0)

/* Delete            Delete */
// #undef LIZ_NOVIM_BIND_DELETE
// #define LIZ_NOVIM_BIND_DELETE LIZ_BIND(DELETE, XK_Delete, 0)

#endif
