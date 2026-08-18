#include "ui/menu.h"

#include <string.h>

#include "apps/apps.h"
#include "fs/fs.h"
#include "ui/newfolder.h"
#include "ui/rename.h"
#include "ui/theme.h"

#ifdef ARCHIVE_SUPPORT
#include "archive/archive.h"
#endif

#define LIZ_MENU_W       180 /* the narrowest a menu gets */
#define LIZ_MENU_W_MAX   360
#define LIZ_MENU_PAD_X   10
#define LIZ_MENU_PAD_Y   4

static void liz_menu_add_data(liz_context_menu* m, liz_menu_action action,
                             const char* label, int data)
{
    if (m->item_count >= LIZ_MENU_ITEMS_MAX)
        return;
    m->items[m->item_count].action = action;
    m->items[m->item_count].label = label;
    m->items[m->item_count].data = data;
    m->item_count++;
}

static void liz_menu_add(liz_context_menu* m, liz_menu_action action, const char* label)
{
    liz_menu_add_data(m, action, label, 0);
}

static void liz_menu_place(liz_app* app, int x, int y);

void liz_menu_open(liz_app* app, int x, int y, int row)
{
    liz_context_menu* m = &app->menu;
    m->item_count = 0;
    m->app_count = 0;
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

            /* The application list is gathered now rather than when the
             * item is clicked, so that "Open with" is only offered when
             * something would actually be in it. */
            if (app->entries[row].type != LIZ_FS_DIR) {
                char path[PATH_MAX];
                if (liz_fs_join(path, sizeof(path), app->cwd, app->entries[row].name) == 0)
                    m->app_count = liz_apps_candidates(path, m->apps, LIZ_APPS_MAX);
                if (m->app_count > 0)
                    liz_menu_add(m, LIZ_MENU_OPEN_WITH, "Open with...");
            }

            if (!is_dotdot)
                liz_menu_add(m, LIZ_MENU_RENAME, "Rename");
            liz_menu_add(m, LIZ_MENU_COPY, "Copy");
            liz_menu_add(m, LIZ_MENU_CUT, "Cut");

#ifdef ARCHIVE_SUPPORT
            if (!is_dotdot
                && (app->entries[row].type == LIZ_FS_FILE
                    || app->entries[row].type == LIZ_FS_LINK)
                && liz_archive_is(app->entries[row].name)) {
                liz_menu_add(m, LIZ_MENU_EXTRACT_HERE, "Extract here");
                liz_menu_add(m, LIZ_MENU_EXTRACT_TO, "Extract to...");
            }
#endif
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
    m->app_count = 0;
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

    /* application names are whatever their desktop entry says, so the menu
     * grows to fit rather than truncating them at a fixed width */
    int mw = LIZ_MENU_W;
    for (int i = 0; i < m->item_count; i++) {
        int tw = 0;
        xc_text_measure(app->win, m->items[i].label, (int)strlen(m->items[i].label),
                        app->font, &tw, NULL);
        if (tw + 2 * LIZ_MENU_PAD_X > mw)
            mw = tw + 2 * LIZ_MENU_PAD_X;
    }
    if (mw > LIZ_MENU_W_MAX)
        mw = LIZ_MENU_W_MAX;
    m->width = mw;

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
    int mw = m->width;
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

/* Replaces the menu's items with the applications gathered when it was
 * opened, in place, so the list appears where the pointer already is. */
static void liz_menu_show_apps(liz_app* app)
{
    liz_context_menu* m = &app->menu;
    int x = m->x;
    int y = m->y;

    m->item_count = 0;
    m->hover = -1;
    for (int i = 0; i < m->app_count; i++)
        liz_menu_add_data(m, LIZ_MENU_OPEN_WITH_APP, m->apps[i].name, i);

    liz_menu_place(app, x, y);
}

static void liz_menu_run(liz_app* app, const liz_menu_item* item)
{
    liz_menu_action action = item->action;
    switch (action) {
    case LIZ_MENU_OPEN:
        liz_app_open_row(app, app->menu.row);
        break;
    case LIZ_MENU_OPEN_WITH:
        break; /* handled before the menu closes */
    case LIZ_MENU_OPEN_WITH_APP:
        if (item->data >= 0 && item->data < app->menu.app_count)
            liz_app_open_row_with(app, app->menu.row, &app->menu.apps[item->data]);
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
#ifdef ARCHIVE_SUPPORT
    case LIZ_MENU_EXTRACT_HERE: {
        int row = app->menu.row;
        if (row >= 0 && (size_t)row < app->entry_count) {
            char path[PATH_MAX];
            if (liz_fs_join(path, sizeof(path), app->cwd,
                           app->entries[row].name) == 0)
                liz_archive_extract_all(path, app->cwd);
            liz_app_navigate(app, app->cwd);
        }
        break;
    }
    case LIZ_MENU_EXTRACT_TO: {
        int row = app->menu.row;
        if (row >= 0 && (size_t)row < app->entry_count) {
            char path[PATH_MAX];
            if (liz_fs_join(path, sizeof(path), app->cwd,
                           app->entries[row].name) == 0) {
                snprintf(g_extract_archive, sizeof(g_extract_archive), "%s", path);
                liz_app_open_new_chooser(app, app->cwd);
            }
        }
        break;
    }
#endif
    default:
        break;
    }
}

void liz_menu_handle_button(liz_app* app, xc_event ev)
{
    int idx = liz_menu_hit(app, ev.x, ev.y);
    /* only a left click activates an item; a right click (or any other
     * button) just dismisses the menu, so a right-click double click
     * never runs the item that happens to sit under the pointer */
    if (idx < 0 || ev.button != 1) {
        liz_menu_close(app);
        return;
    }

    /* "Open with" is the one item that leads somewhere instead of doing
     * something, so the menu stays up and swaps its contents */
    liz_menu_item item = app->menu.items[idx];
    if (item.action == LIZ_MENU_OPEN_WITH) {
        liz_menu_show_apps(app);
        return;
    }

    liz_menu_close(app);
    liz_menu_run(app, &item);
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
    int mw = m->width;
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
        liz_ui_text_clip(w, m->x + LIZ_MENU_PAD_X, text_y, label, (int)strlen(label),
                        app->font, mw - 2 * LIZ_MENU_PAD_X);
    }
}
