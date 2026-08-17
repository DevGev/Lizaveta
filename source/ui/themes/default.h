/* default.h - the palette lizaveta ships with. */

#ifndef LIZ_THEME_DEFAULT_H
#define LIZ_THEME_DEFAULT_H

#include "rendering/x11/xc.h"

#define LIZ_COL_RGB(r, g, b) ((xc_color) { (r), (g), (b), 255 })

static const xc_color liz_theme_bg         = LIZ_COL_RGB(0x0d, 0x0d, 0x0d);
static const xc_color liz_theme_panel      = LIZ_COL_RGB(0x08, 0x08, 0x08);
static const xc_color liz_theme_panel_edge = LIZ_COL_RGB(0x3c, 0x3c, 0x46);
static const xc_color liz_theme_text       = LIZ_COL_RGB(0xd9, 0xd9, 0xe0);
static const xc_color liz_theme_text_dim   = LIZ_COL_RGB(0x84, 0x84, 0x90);
static const xc_color liz_theme_accent     = LIZ_COL_RGB(0x6c, 0x9e, 0xe8);
static const xc_color liz_theme_accent_dim = LIZ_COL_RGB(0x37, 0x46, 0x5f);
static const xc_color liz_theme_dir        = LIZ_COL_RGB(0x7e, 0xb9, 0xff);
static const xc_color liz_theme_sel_bg     = LIZ_COL_RGB(0x34, 0x3f, 0x55);
static const xc_color liz_theme_sel_dim    = LIZ_COL_RGB(0x2d, 0x36, 0x48);
static const xc_color liz_theme_hover_bg   = LIZ_COL_RGB(0x2b, 0x2f, 0x3a);
static const xc_color liz_theme_search_bg  = LIZ_COL_RGB(0xc9, 0x9a, 0x1e);
static const xc_color liz_theme_error      = LIZ_COL_RGB(0xe5, 0x5c, 0x5c);

#endif /* LIZ_THEME_DEFAULT_H */
