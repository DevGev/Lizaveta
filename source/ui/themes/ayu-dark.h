/* ayu-dark.h - Ayu Dark palette (from vscode-ayu's dark variant).
 * https://github.com/ayu-theme/vscode-ayu (ayu-dark.json) */

#ifndef LIZ_THEME_AYU_DARK_H
#define LIZ_THEME_AYU_DARK_H

#include "rendering/x11/xc.h"

#define LIZ_COL_RGB(r, g, b) ((xc_color) { (r), (g), (b), 255 })

static const xc_color liz_theme_bg         = LIZ_COL_RGB(0x10, 0x14, 0x1c); /* editor.background */
static const xc_color liz_theme_panel      = LIZ_COL_RGB(0x0d, 0x10, 0x17); /* sideBar/panel/statusBar */
static const xc_color liz_theme_panel_edge = LIZ_COL_RGB(0x1b, 0x1f, 0x29); /* widget.border */
static const xc_color liz_theme_text       = LIZ_COL_RGB(0xbf, 0xbd, 0xb6); /* editor.foreground */
static const xc_color liz_theme_text_dim   = LIZ_COL_RGB(0x5a, 0x63, 0x78); /* foreground */
static const xc_color liz_theme_accent     = LIZ_COL_RGB(0xe6, 0xb4, 0x50); /* focusBorder */
static const xc_color liz_theme_accent_dim = LIZ_COL_RGB(0x5a, 0x66, 0x73); /* comment */
static const xc_color liz_theme_dir        = LIZ_COL_RGB(0x59, 0xc2, 0xff); /* entity.name */
/* The theme's list selection backgrounds are translucent; these are them
 * composited over editor.background:
 *   #475266 40% -> #1e242f (active),  #475266 20% -> #1b202b (inactive). */
static const xc_color liz_theme_sel_bg     = LIZ_COL_RGB(0x1e, 0x24, 0x2f);
static const xc_color liz_theme_sel_dim    = LIZ_COL_RGB(0x1b, 0x20, 0x2b);
static const xc_color liz_theme_hover_bg   = LIZ_COL_RGB(0x16, 0x1a, 0x24); /* editor.lineHighlightBackground */
static const xc_color liz_theme_search_bg  = LIZ_COL_RGB(0x4c, 0x41, 0x26); /* editor.findMatchBackground */
static const xc_color liz_theme_error      = LIZ_COL_RGB(0xd9, 0x57, 0x57); /* errorForeground */

#endif /* LIZ_THEME_AYU_DARK_H */