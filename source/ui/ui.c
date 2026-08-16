#include "ui/ui.h"

#include <stdio.h>
#include <string.h>

void liz_ui_format_size(char* buf, size_t bufsz, off_t size)
{
    static const char* units[] = { "B", "K", "M", "G", "T" };
    double value = (double)size;
    int unit = 0;

    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        unit++;
    }

    if (unit == 0)
        snprintf(buf, bufsz, "%lldB", (long long)size);
    else if (value < 10.0)
        snprintf(buf, bufsz, "%.1f%s", value, units[unit]);
    else
        snprintf(buf, bufsz, "%.0f%s", value, units[unit]);
}

int liz_ui_text_clip(xwindow* w, int x, int y, const char* text, int len,
                    xc_font* f, int max_width)
{
    if (max_width <= 0)
        return 0;

    static const char ellipsis[] = "…";
    int ellen = 3; /* UTF-8 bytes of "…" */

    int width = 0;
    xc_text_measure(w, text, len, f, &width, NULL);
    if (width <= max_width)
        return xc_text(w, x, y, text, len, f);

    int avail = max_width;
    int ewidth = 0;
    xc_text_measure(w, ellipsis, ellen, f, &ewidth, NULL);
    avail -= ewidth;

    /* shrink until the visible text plus "…" fits */
    int fit = len;
    while (fit > 0) {
        int w2 = 0;
        xc_text_measure(w, text, fit, f, &w2, NULL);
        if (w2 <= avail)
            break;
        /* step back one UTF-8 code point */
        do {
            fit--;
        } while (fit > 0 && ((text[fit] & 0xC0) == 0x80));
    }

    if (fit > 0)
        xc_text(w, x, y, text, fit, f);
    return xc_text(w, x + avail, y, ellipsis, ellen, f);
}
