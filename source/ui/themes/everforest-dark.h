/* everforest-dark.h - Everforest Dark palette.
 * https://github.com/sainnhe/everforest */

#ifndef LIZ_THEME_EVERFOREST_DARK_H
#define LIZ_THEME_EVERFOREST_DARK_H

#include "rendering/x11/xc.h"

#define LIZ_COL_RGB(r, g, b) ((xc_color) { (r), (g), (b), 255 })

static const xc_color liz_theme_bg         = LIZ_COL_RGB(0x2d, 0x35, 0x3b); /* bg0 */
static const xc_color liz_theme_panel      = LIZ_COL_RGB(0x34, 0x3f, 0x44); /* bg1 */
static const xc_color liz_theme_panel_edge = LIZ_COL_RGB(0x3d, 0x48, 0x4d); /* bg2 */
static const xc_color liz_theme_text       = LIZ_COL_RGB(0xd3, 0xc6, 0xaa); /* fg */
static const xc_color liz_theme_text_dim   = LIZ_COL_RGB(0x85, 0x92, 0x89); /* grey1 */
static const xc_color liz_theme_accent     = LIZ_COL_RGB(0xa7, 0xc0, 0x80); /* green */
static const xc_color liz_theme_accent_dim = LIZ_COL_RGB(0x7a, 0x84, 0x78); /* grey0 */
static const xc_color liz_theme_dir        = LIZ_COL_RGB(0x7f, 0xbb, 0xb3); /* blue */
static const xc_color liz_theme_sel_bg     = LIZ_COL_RGB(0x4f, 0x58, 0x5e); /* bg4 */
static const xc_color liz_theme_sel_dim    = LIZ_COL_RGB(0x47, 0x52, 0x58); /* bg3 */
static const xc_color liz_theme_hover_bg   = LIZ_COL_RGB(0x47, 0x52, 0x58); /* bg3 */
/* Row highlights carry opaque text on top, so the yellow accent is mixed
 * down over the background rather than used at full strength:
 * #dbBc7f at 35% here. */
static const xc_color liz_theme_search_bg  = LIZ_COL_RGB(0x6a, 0x64, 0x53);
static const xc_color liz_theme_error      = LIZ_COL_RGB(0xe6, 0x7e, 0x80); /* red */

#endif /* LIZ_THEME_EVERFOREST_DARK_H */