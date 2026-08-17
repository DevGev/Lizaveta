/* ui.h - layout constants and small shared drawing helpers. */

#ifndef LIZ_UI_H
#define LIZ_UI_H

#include <sys/types.h>

#include "rendering/x11/xc.h"

#define LIZ_UI_NAV_H     30
#define LIZ_UI_STATUS_H  22
#define LIZ_UI_ROW_H     20
#define LIZ_UI_PAD       8
#define LIZ_UI_SIDEBAR_W 180

/* The UI font, named once so it can be changed in one place. The size is
 * in pixels rather than points, so it tracks LIZ_UI_ROW_H instead of the
 * DPI the display reports. */
#define LIZ_UI_FONT      "monospace"
#define LIZ_UI_FONT_PX   13

/* Format `size` bytes as a compact human readable string. */
void liz_ui_format_size(char* buf, size_t bufsz, off_t size);

/* Draws `text` (len bytes) at baseline (x, y) using `f`, clipping it to
 * `max_width` pixels with a trailing "…" if needed. Returns the advance. */
int liz_ui_text_clip(xwindow* w, int x, int y, const char* text, int len,
                    xc_font* f, int max_width);

#endif /* LIZ_UI_H */
