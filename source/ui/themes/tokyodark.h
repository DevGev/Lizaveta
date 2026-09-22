/* tokyodark.h - Tokyo Dark palette.
 * https://github.com/smnatale/tokyodark-vscode-theme */

#ifndef LIZ_THEME_TOKYO_DARK_H
#define LIZ_THEME_TOKYO_DARK_H

#include "rendering/x11/xc.h"

#define LIZ_COL_RGB(r, g, b) ((xc_color) { (r), (g), (b), 255 })

static const xc_color liz_theme_bg         = LIZ_COL_RGB(0x09, 0x0b, 0x10); /* editor.background */
static const xc_color liz_theme_panel      = LIZ_COL_RGB(0x06, 0x07, 0x0a); /* sideBar.background */
static const xc_color liz_theme_panel_edge = LIZ_COL_RGB(0x10, 0x10, 0x14); /* (e.g.) tab.border */
static const xc_color liz_theme_text       = LIZ_COL_RGB(0xa9, 0xb1, 0xd6); /* editor.foreground */
static const xc_color liz_theme_text_dim   = LIZ_COL_RGB(0x78, 0x7c, 0x99); /* sideBar.foreground */
static const xc_color liz_theme_accent     = LIZ_COL_RGB(0x7a, 0xa2, 0xf7); /* blue */
static const xc_color liz_theme_accent_dim = LIZ_COL_RGB(0x3d, 0x59, 0xa1); /* button.background */
static const xc_color liz_theme_dir        = LIZ_COL_RGB(0x7d, 0xcf, 0xff); /* cyan */
static const xc_color liz_theme_sel_bg     = LIZ_COL_RGB(0x20, 0x23, 0x30); /* list.activeSelectionBackground */
static const xc_color liz_theme_sel_dim    = LIZ_COL_RGB(0x1c, 0x1d, 0x29); /* list.inactiveSelectionBackground */
static const xc_color liz_theme_hover_bg   = LIZ_COL_RGB(0x13, 0x13, 0x1a); /* list.hoverBackground */
/* Row highlights carry opaque text on top, so the yellow is mixed down
 * over the background rather than used at full strength: #e0af68 at 35%. */
static const xc_color liz_theme_search_bg  = LIZ_COL_RGB(0x54, 0x44, 0x2f);
static const xc_color liz_theme_error      = LIZ_COL_RGB(0xf7, 0x76, 0x8e); /* red */

#endif /* LIZ_THEME_TOKYO_DARK_H */
