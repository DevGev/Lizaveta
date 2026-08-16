#include "ui/menu.h"

#include <string.h>

#include "ui/newfolder.h"
#include "ui/rename.h"
#include "ui/theme.h"

#define LIZ_MENU_W       180
#define LIZ_MENU_PAD_X   10
#define LIZ_MENU_PAD_Y   4

static void liz_menu_add(liz_context_menu* m, liz_menu_action action, const char* label)
{
    if (m->item_count >= LIZ_MENU_ITEMS_MAX)
        return;
    m->items[m->item_count].action = action;
    m->items[m->item_count].label = label;
    m->item_count++;
}

static void liz_menu_place(liz_app* app, int x, int y);

void liz_menu_open(liz_app* app, int x, int y, int row)
{
    liz_context_menu* m = &app->menu;
    m->item_count = 0;
    m->hover = -1;
    m->row = row;
    m->source = LIZ_MENU_SRC_LIST;
    m->sidebar_index = -1;
    m->sidebar_is_device = false;

    bool has_row = row >= 0 && (size_t)row < app->entry_count;

    if (has_row) {
        bool row_in_multi = liz_app_row_selected(app, row) && liz_app_selection_count(app) > 1;
        if (row_in_multi) {
            liz_menu_add(m, LIZ_MENU_COPY, "Copy");
            liz_menu_add(m, LIZ_MENU_CUT, "Cut");
        } else {
            /* right-clicking a row outside the current multi-selection
             * collapses the selection to just that row, like a plain
             * left-click would -- so Rename/Copy/Cut act on what the menu
             * shows, not on a stale multi-selection */
            liz_app_clear_selection(app);
            if (app->sel)
                app->sel[row] = true;
            app->anchor_row = row;
            liz_app_set_selected(app, row);
            app->vim.visual_active = false;
            app->vim.pending_g = false;

            bool is_dotdot = strcmp(app->entries[row].name, "..") == 0;
            liz_menu_add(m, LIZ_MENU_OPEN, "Open");
            if (!is_dotdot)
                liz_menu_add(m, LIZ_MENU_RENAME, "Rename");
            liz_menu_add(m, LIZ_MENU_COPY, "Copy");
            liz_menu_add(m, LIZ_MENU_CUT, "Cut");
        }
    }

    liz_menu_add(m, LIZ_MENU_NEW_FOLDER, "Create folder");
    liz_menu_add(m, LIZ_MENU_TOGGLE_HIDDEN,
                app->show_hidden ? "Hide hidden files" : "Show hidden files");

    liz_menu_place(app, x, y);
}

/* Opens the sidebar's context menu for the entry `index` (pinned[] when
 * is_device is false, devices[] when true). */
void liz_menu_open_sidebar(liz_app* app, int x, int y, int index, bool is_device)
{
    liz_context_menu* m = &app->menu;
    m->item_count = 0;
    m->hover = -1;
    m->row = -1;
    m->source = LIZ_MENU_SRC_SIDEBAR;
    m->sidebar_index = index;
    m->sidebar_is_device = is_device;

    /* only real block devices can be unmounted; the "File system" root
     * entry is just a navigation shortcut */
    if (is_device && index >= 0 && index < app->sidebar.devices_count
        && app->sidebar.devices[index].dev[0] != '\0')
        liz_menu_add(m, LIZ_MENU_UNMOUNT, "Safely remove");
    liz_menu_add(m, LIZ_MENU_OPEN_NEW_WINDOW, "Open in new window");
    liz_menu_add(m, LIZ_MENU_TOGGLE_SIDEBAR,
                app->sidebar_visible ? "Hide panel" : "Show panel");

    liz_menu_place(app, x, y);
}

/* Clamps the menu to stay on screen and marks it active. */
static void liz_menu_place(liz_app* app, int x, int y)
{
    liz_context_menu* m = &app->menu;
    int mw = LIZ_MENU_W;
    int mh = m->item_count * LIZ_UI_ROW_H + 2 * LIZ_MENU_PAD_Y;
    if (x + mw > app->win->width)
        x = app->win->width - mw;
    if (y + mh > app->win->height)
        y = app->win->height - mh;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    m->x = x;
    m->y = y;
    m->active = true;
}

void liz_menu_close(liz_app* app)
{
    app->menu.active = false;
    app->menu.item_count = 0;
    app->menu.hover = -1;
}

static int liz_menu_hit(liz_app* app, int x, int y)
{
    liz_context_menu* m = &app->menu;
    if (!m->active)
        return -1;
    int mw = LIZ_MENU_W;
    int mh = m->item_count * LIZ_UI_ROW_H + 2 * LIZ_MENU_PAD_Y;
    if (x < m->x || x >= m->x + mw || y < m->y || y >= m->y + mh)
        return -1;
    int idx = (y - m->y - LIZ_MENU_PAD_Y) / LIZ_UI_ROW_H;
    if (idx < 0 || idx >= m->item_count)
        return -1;
    return idx;
}

/* The sidebar entry the menu was opened on, or NULL for list menus. */
static const liz_sidebar_entry* liz_menu_sidebar_entry(const liz_app* app)
{
    if (app->menu.source != LIZ_MENU_SRC_SIDEBAR
        || app->menu.sidebar_index < 0)
        return NULL;
    if (app->menu.sidebar_is_device) {
        if (app->menu.sidebar_index < app->sidebar.devices_count)
            return &app->sidebar.devices[app->menu.sidebar_index];
    } else if (app->menu.sidebar_index < app->sidebar.pinned_count) {
        return &app->sidebar.pinned[app->menu.sidebar_index];
    }
    return NULL;
}

static void liz_menu_run(liz_app* app, liz_menu_action action)
{
    switch (action) {
    case LIZ_MENU_OPEN:
        liz_app_open_row(app, app->menu.row);
        break;
    case LIZ_MENU_RENAME:
        liz_rename_start(app);
        break;
    case LIZ_MENU_COPY:
        liz_app_copy_selection(app);
        break;
    case LIZ_MENU_CUT:
        liz_app_cut_selection(app);
        break;
    case LIZ_MENU_NEW_FOLDER:
        liz_newfolder_start(app);
        break;
    case LIZ_MENU_TOGGLE_HIDDEN:
        liz_app_toggle_hidden(app);
        break;
    case LIZ_MENU_UNMOUNT: {
        const liz_sidebar_entry* e = liz_menu_sidebar_entry(app);
        if (e)
            liz_app_unmount_device(app, e->dev, e->path);
        break;
    }
    case LIZ_MENU_OPEN_NEW_WINDOW: {
        const liz_sidebar_entry* e = liz_menu_sidebar_entry(app);
        if (e)
            liz_app_open_new_window(app, e->path);
        break;
    }
    case LIZ_MENU_TOGGLE_SIDEBAR:
        liz_app_toggle_sidebar(app);
        break;
    }
}

void liz_menu_handle_button(liz_app* app, xc_event ev)
{
    int idx = liz_menu_hit(app, ev.x, ev.y);
    liz_menu_action action = LIZ_MENU_OPEN;
    /* only a left click activates an item; a right click (or any other
     * button) just dismisses the menu, so a right-click double click
     * never runs the item that happens to sit under the pointer */
    bool run = idx >= 0 && ev.button == 1;
    if (run)
        action = app->menu.items[idx].action;

    liz_menu_close(app);

    if (run)
        liz_menu_run(app, action);
}

void liz_menu_handle_motion(liz_app* app, xc_event ev)
{
    app->menu.hover = liz_menu_hit(app, ev.x, ev.y);
}

bool liz_menu_handle_key(liz_app* app, xc_event ev)
{
    (void)ev;
    if (!app->menu.active)
        return false;
    liz_menu_close(app);
    return true;
}

void liz_menu_draw(liz_app* app)
{
    liz_context_menu* m = &app->menu;
    if (!m->active)
        return;

    xwindow* w = app->win;
    int mw = LIZ_MENU_W;
    int mh = m->item_count * LIZ_UI_ROW_H + 2 * LIZ_MENU_PAD_Y;

    xc_rect(w, m->x, m->y, mw, mh, liz_theme_panel);
    xc_rect(w, m->x, m->y, mw, 1, liz_theme_panel_edge);
    xc_rect(w, m->x, m->y + mh - 1, mw, 1, liz_theme_panel_edge);
    xc_rect(w, m->x, m->y, 1, mh, liz_theme_panel_edge);
    xc_rect(w, m->x + mw - 1, m->y, 1, mh, liz_theme_panel_edge);

    int ascent = 0, descent = 0;
    xc_font_metrics(app->font, &ascent, &descent);
    int line_h = ascent + descent;

    for (int i = 0; i < m->item_count; i++) {
        int iy = m->y + LIZ_MENU_PAD_Y + i * LIZ_UI_ROW_H;
        if (i == m->hover)
            xc_rect(w, m->x + 1, iy, mw - 2, LIZ_UI_ROW_H, liz_theme_hover_bg);
        int text_y = iy + (LIZ_UI_ROW_H - line_h) / 2 + ascent;
        const char* label = m->items[i].label;
        xc_text(w, m->x + LIZ_MENU_PAD_X, text_y, label, (int)strlen(label), app->font);
    }
}
