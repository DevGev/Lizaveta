/* nord.h - Nord palette.
 * https://www.nordtheme.com/ */

#ifndef LIZ_THEME_NORD_H
#define LIZ_THEME_NORD_H

#include "rendering/x11/xc.h"

#define LIZ_COL_RGB(r, g, b) ((xc_color) { (r), (g), (b), 255 })

static const xc_color liz_theme_bg         = LIZ_COL_RGB(0x2e, 0x34, 0x40); /* nord0 */
static const xc_color liz_theme_panel      = LIZ_COL_RGB(0x3b, 0x42, 0x52); /* nord1 */
static const xc_color liz_theme_panel_edge = LIZ_COL_RGB(0x43, 0x4c, 0x5e); /* nord2 */
static const xc_color liz_theme_text       = LIZ_COL_RGB(0xd8, 0xde, 0xe9); /* nord4 */
static const xc_color liz_theme_text_dim   = LIZ_COL_RGB(0x4c, 0x56, 0x6a); /* nord3 */
static const xc_color liz_theme_accent     = LIZ_COL_RGB(0x88, 0xc0, 0xd0); /* nord8 (frost) */
static const xc_color liz_theme_accent_dim = LIZ_COL_RGB(0x4c, 0x56, 0x6a); /* nord3 */
static const xc_color liz_theme_dir        = LIZ_COL_RGB(0x81, 0xa1, 0xc1); /* nord9 (frost) */
static const xc_color liz_theme_sel_bg     = LIZ_COL_RGB(0x4c, 0x56, 0x6a); /* nord3 */
static const xc_color liz_theme_sel_dim    = LIZ_COL_RGB(0x43, 0x4c, 0x5e); /* nord2 */
static const xc_color liz_theme_hover_bg   = LIZ_COL_RGB(0x43, 0x4c, 0x5e); /* nord2 */
/* Row highlights carry opaque text on top, so the yellow aurora is mixed
 * down over the background rather than used at full strength:
 * nord13 at 35% here. */
static const xc_color liz_theme_search_bg  = LIZ_COL_RGB(0x70, 0x69, 0x5a);
static const xc_color liz_theme_error      = LIZ_COL_RGB(0xbf, 0x61, 0x6a); /* nord11 */

#endif /* LIZ_THEME_NORD_H */