#include "ui/file_list.h"

#include <ctype.h>
#include <string.h>

#include "icons/icons.h"
#include "ui/preview.h"
#include "ui/theme.h"

#define LIZ_LIST_MARKER_W 20 /* left gutter for the type marker */
#define LIZ_LIST_SCROLLBAR_W 6

int liz_list_area_left(liz_app* app)
{
    return app->sidebar_visible ? LIZ_UI_SIDEBAR_W : 0;
}

static int liz_list_area_top(void)
{
    return LIZ_UI_NAV_H;
}

/* The list stops above the preview pane when one is active. */
static int liz_list_area_bottom(liz_app* app)
{
    int top = liz_preview_pane_top(app);
    if (top >= 0)
        return top;
    return app->win->height - LIZ_UI_STATUS_H;
}

int liz_list_visible_count(liz_app* app)
{
    int area = liz_list_area_bottom(app) - liz_list_area_top();
    if (area < 0)
        return 0;
    return area / LIZ_UI_ROW_H;
}

int liz_list_row_at(liz_app* app, int y)
{
    int top = liz_list_area_top();
    int bottom = liz_list_area_bottom(app);
    if (y < top || y >= bottom)
        return -1;
    int idx = (y - top) / LIZ_UI_ROW_H + app->scroll;
    if (idx < 0 || idx >= (int)app->entry_count)
        return -1;
    return idx;
}

void liz_list_scroll(liz_app* app, int delta)
{
    int visible = liz_list_visible_count(app);
    int max = (int)app->entry_count - visible;
    if (max < 0)
        max = 0;
    app->scroll += delta;
    if (app->scroll < 0)
        app->scroll = 0;
    if (app->scroll > max)
        app->scroll = max;
}

void liz_list_keep_selection_visible(liz_app* app)
{
    int visible = liz_list_visible_count(app);
    if (visible <= 0 || app->selected < 0)
        return;
    if (app->selected < app->scroll)
        app->scroll = app->selected;
    else if (app->selected >= app->scroll + visible)
        app->scroll = app->selected - visible + 1;
}

/* Falls back to a colored square when the icon theme has nothing for an
 * entry, so the type is still readable. */
static void liz_list_draw_marker(xwindow* w, liz_fs_type type, int x, int cy)
{
    switch (type) {
    case LIZ_FS_DIR:
        xc_rect(w, x, cy - 4, 8, 8, liz_theme_dir);
        break;
    case LIZ_FS_VIRTUAL:
        xc_rect(w, x, cy - 4, 8, 8, liz_theme_dir);
        break;
    case LIZ_FS_LINK:
        xc_rect(w, x + 1, cy - 3, 6, 6, liz_theme_accent);
        break;
    case LIZ_FS_FILE:
        xc_rect(w, x + 2, cy - 2, 4, 4, liz_theme_text_dim);
        break;
    default:
        xc_rect(w, x + 1, cy - 1, 6, 1, liz_theme_text_dim);
        xc_rect(w, x + 1, cy - 1, 1, 2, liz_theme_text_dim);
        break;
    }
}

/* Draws a highlight rect behind every case-insensitive occurrence of
 * `query` in `name`, like vim's hlsearch. Positions are derived by
 * re-measuring text slices with the row's actual font, so they line up
 * with whatever liz_ui_text_clip ends up drawing; occurrences past
 * `max_width` are simply not drawn instead of overflowing into the size
 * column. */
static void liz_list_draw_search_hl(xwindow* w, xc_font* f, const char* name, int namelen,
                                    const char* query, int qlen, int x, int y, int max_width)
{
    if (qlen <= 0 || max_width <= 0)
        return;

    int i = 0;
    while (i + qlen <= namelen) {
        int j = 0;
        for (; j < qlen; j++) {
            if (tolower((unsigned char)name[i + j]) != tolower((unsigned char)query[j]))
                break;
        }
        if (j == qlen) {
            int pre_w = 0;
            if (i > 0)
                xc_text_measure(w, name, i, f, &pre_w, NULL);
            if (pre_w < max_width) {
                int match_w = 0;
                xc_text_measure(w, name + i, qlen, f, &match_w, NULL);
                if (pre_w + match_w > max_width)
                    match_w = max_width - pre_w;
                if (match_w > 0)
                    xc_rect(w, x + pre_w, y, match_w, LIZ_UI_ROW_H, liz_theme_search_bg);
            }
            i += qlen;
        } else {
            i++;
        }
    }
}

void liz_list_draw(liz_app* app)
{
    xwindow* w = app->win;
    int top = liz_list_area_top();
    int bottom = liz_list_area_bottom(app);
    int left = liz_list_area_left(app);
    if (bottom <= top)
        return;

    xc_rect(w, left, top, w->width - left, bottom - top, liz_theme_bg);

    int visible = liz_list_visible_count(app);
    if (visible <= 0 || app->entry_count == 0)
        return;

    /* clamp scroll position */
    int max_scroll = (int)app->entry_count - visible;
    if (max_scroll < 0)
        max_scroll = 0;
    if (app->scroll > max_scroll)
        app->scroll = max_scroll;
    if (app->scroll < 0)
        app->scroll = 0;
    if (app->selected < 0)
        app->selected = 0;
    if (app->selected >= (int)app->entry_count)
        app->selected = (int)app->entry_count - 1;

    /* the visible area shrank or grew (preview toggled, window resized):
     * reframe the selection so the cursor stays on screen */
    if (app->last_list_visible != visible) {
        liz_list_keep_selection_visible(app);
        app->last_list_visible = visible;
    }

    int ascent = 0, descent = 0;
    xc_font_metrics(app->font, &ascent, &descent);
    int line_h = ascent + descent;

    /* width of the right-aligned size column; skip it entirely if there is
     * not enough room (oversized font / narrow window) rather than letting
     * it eat the space reserved for entry names */
    int sizew = 0;
    xc_text_measure(w, "000.0M", 6, app->font_dim, &sizew, NULL);
    int text_x = left + LIZ_UI_PAD + LIZ_LIST_MARKER_W;
    int size_x = w->width - LIZ_UI_PAD - LIZ_LIST_SCROLLBAR_W - sizew;
    bool draw_size = size_x >= text_x + LIZ_UI_PAD;
    if (!draw_size)
        size_x = w->width;
    int max_name_w = size_x - text_x - LIZ_UI_PAD;

    char sizebuf[16];

    for (int row = app->scroll; row < app->scroll + visible && row < (int)app->entry_count; row++) {
        int y = top + (row - app->scroll) * LIZ_UI_ROW_H;
        liz_fs_entry* e = &app->entries[row];
        int namelen = (int)strlen(e->name);

        if (app->dnd_active && row == app->dnd_row) {
            /* an in-flight drop targets this directory row */
            xc_rect(w, left, y, w->width - left, LIZ_UI_ROW_H, liz_theme_search_bg);
        } else if (row == app->selected) {
            xc_rect(w, left, y, w->width - left, LIZ_UI_ROW_H, liz_theme_sel_bg);
            /* focus marker on the cursor row */
            xc_rect(w, left, y, 3, LIZ_UI_ROW_H, liz_theme_accent);
        } else if (liz_app_row_selected(app, row)) {
            xc_rect(w, left, y, w->width - left, LIZ_UI_ROW_H, liz_theme_sel_dim);
        } else if (row == app->hover_row) {
            xc_rect(w, left, y, w->width - left, LIZ_UI_ROW_H, liz_theme_hover_bg);
        }

        xc_font* f = app->font;
        if (e->type == LIZ_FS_DIR || e->type == LIZ_FS_LINK
            || e->type == LIZ_FS_VIRTUAL)
            f = app->font_accent;
        else if (e->hidden)
            f = app->font_dim;
        if (row == app->selected)
            f = app->font_bold;

        if (app->vim.search_active && app->vim.search_query[0] != '\0') {
            int qlen = (int)strlen(app->vim.search_query);
            liz_list_draw_search_hl(w, f, e->name, namelen, app->vim.search_query, qlen,
                                    text_x, y, max_name_w);
        }

#ifdef ICON_SUPPORT
        /* symbolic icons are drawn in the inherited text color, so they
         * follow whatever color this row's label uses */
        xc_color tint = liz_theme_text;
        if (e->type == LIZ_FS_DIR || e->type == LIZ_FS_LINK
            || e->type == LIZ_FS_VIRTUAL)
            tint = liz_theme_dir;
        else if (e->hidden)
            tint = liz_theme_text_dim;

        const xc_image* icon = liz_icons_for_entry(w, app->cwd, e, tint);
        if (icon)
            xc_image_draw(w, icon, left + LIZ_UI_PAD,
                          y + (LIZ_UI_ROW_H - LIZ_ICON_SIZE) / 2);
        else
#endif
            liz_list_draw_marker(w, e->type, left + LIZ_UI_PAD + 2, y + LIZ_UI_ROW_H / 2);

        int text_y = y + (LIZ_UI_ROW_H - line_h) / 2 + ascent;

        liz_ui_text_clip(w, text_x, text_y, e->name, namelen, f, max_name_w);

        if (draw_size && (e->type == LIZ_FS_FILE || e->type == LIZ_FS_SPECIAL)) {
            liz_ui_format_size(sizebuf, sizeof(sizebuf), e->size);
            int slen = (int)strlen(sizebuf);
            int sw = 0;
            xc_text_measure(w, sizebuf, slen, app->font_dim, &sw, NULL);
            xc_text(w, size_x + (sizew - sw), text_y, sizebuf, slen, app->font_dim);
        }
    }

    /* drop into the current directory: outline the list area so the user
     * knows where the drop will land */
    if (app->dnd_active && app->dnd_row < 0) {
        xc_rect(w, left, top, w->width - left, 2, liz_theme_accent);
        xc_rect(w, left, bottom - 2, w->width - left, 2, liz_theme_accent);
        xc_rect(w, left, top, 2, bottom - top, liz_theme_accent);
        xc_rect(w, w->width - 2, top, 2, bottom - top, liz_theme_accent);
    }

    /* scrollbar (decorative for now, thumb reflects scroll position) */
    if ((int)app->entry_count > visible) {
        int track = bottom - top;
        int thumb_h = track * visible / (int)app->entry_count;
        if (thumb_h < LIZ_UI_ROW_H)
            thumb_h = LIZ_UI_ROW_H;
        int thumb_y = top + (track - thumb_h) * app->scroll / max_scroll;
        int bx = w->width - LIZ_UI_PAD / 2 - LIZ_LIST_SCROLLBAR_W;
        xc_rect(w, bx, top, LIZ_LIST_SCROLLBAR_W, track, liz_theme_panel_edge);
        xc_rect(w, bx, thumb_y, LIZ_LIST_SCROLLBAR_W, thumb_h, liz_theme_accent_dim);
    }
}
