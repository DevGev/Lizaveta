/* catppuccin-mocha.h - Catppuccin Mocha palette.
 * https://github.com/catppuccin/catppuccin */

#ifndef LIZ_THEME_CATPPUCCIN_MOCHA_H
#define LIZ_THEME_CATPPUCCIN_MOCHA_H

#include "rendering/x11/xc.h"

#define LIZ_COL_RGB(r, g, b) ((xc_color) { (r), (g), (b), 255 })

static const xc_color liz_theme_bg         = LIZ_COL_RGB(0x1e, 0x1e, 0x2e); /* base */
static const xc_color liz_theme_panel      = LIZ_COL_RGB(0x18, 0x18, 0x25); /* mantle */
static const xc_color liz_theme_panel_edge = LIZ_COL_RGB(0x31, 0x32, 0x44); /* surface0 */
static const xc_color liz_theme_text       = LIZ_COL_RGB(0xcd, 0xd6, 0xf4); /* text */
static const xc_color liz_theme_text_dim   = LIZ_COL_RGB(0x7f, 0x84, 0x9c); /* overlay1 */
static const xc_color liz_theme_accent     = LIZ_COL_RGB(0xcb, 0xa6, 0xf7); /* mauve */
static const xc_color liz_theme_accent_dim = LIZ_COL_RGB(0x45, 0x47, 0x5a); /* surface1 */
static const xc_color liz_theme_dir        = LIZ_COL_RGB(0x89, 0xb4, 0xfa); /* blue */
static const xc_color liz_theme_sel_bg     = LIZ_COL_RGB(0x58, 0x5b, 0x70); /* surface2 */
static const xc_color liz_theme_sel_dim    = LIZ_COL_RGB(0x45, 0x47, 0x5a); /* surface1 */
static const xc_color liz_theme_hover_bg   = LIZ_COL_RGB(0x31, 0x32, 0x44); /* surface0 */
/* Row highlights carry opaque text on top, so the warm accents are mixed
 * down over base rather than used at full strength: yellow at 35% here. */
static const xc_color liz_theme_search_bg  = LIZ_COL_RGB(0x6b, 0x63, 0x5b);
static const xc_color liz_theme_error      = LIZ_COL_RGB(0xf3, 0x8b, 0xa8); /* red */

#endif /* LIZ_THEME_CATPPUCCIN_MOCHA_H */
