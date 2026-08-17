/* sidebar.c - left sidebar with pinned folders and connected devices. */

#include "ui/sidebar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include <libudev.h>

#include "icons/icons.h"
#include "ui/theme.h"

#define LIZ_SIDEBAR_PAD_X 10
#define LIZ_SIDEBAR_ICON_GAP 6 /* between an item's icon and its label */

static int liz_sidebar_top(void)
{
    return LIZ_UI_NAV_H;
}

static int liz_sidebar_bottom(xwindow* w)
{
    return w->height - LIZ_UI_STATUS_H;
}

static void liz_sidebar_add_pinned(liz_app* app, const char* label, const char* path,
                                  const char* icon)
{
    if (app->sidebar.pinned_count >= LIZ_SIDEBAR_PINNED_MAX)
        return;
    liz_sidebar_entry* e = &app->sidebar.pinned[app->sidebar.pinned_count++];
    snprintf(e->label, sizeof(e->label), "%.*s", (int)sizeof(e->label) - 1, label);
    snprintf(e->path, sizeof(e->path), "%.*s", (int)sizeof(e->path) - 1, path);
    snprintf(e->icon, sizeof(e->icon), "%.*s", (int)sizeof(e->icon) - 1, icon);
}

static void liz_sidebar_add_device(liz_app* app, const char* label, const char* path,
                                  const char* dev, const char* icon)
{
    if (app->sidebar.devices_count >= LIZ_SIDEBAR_DEVICES_MAX)
        return;
    liz_sidebar_entry* e = &app->sidebar.devices[app->sidebar.devices_count++];
    snprintf(e->label, sizeof(e->label), "%.*s", (int)sizeof(e->label) - 1, label);
    snprintf(e->path, sizeof(e->path), "%.*s", (int)sizeof(e->path) - 1, path);
    snprintf(e->dev, sizeof(e->dev), "%.*s", (int)sizeof(e->dev) - 1,
             dev ? dev : "");
    snprintf(e->icon, sizeof(e->icon), "%.*s", (int)sizeof(e->icon) - 1, icon);
}

/* Writes home + "/" + leaf into out, clamping home so the result fits. */
static void liz_sidebar_sub(char* out, size_t outsz, const char* home, const char* leaf)
{
    int leafsz = (int)strlen(leaf);
    int max_home = (int)outsz - leafsz - 1;
    if (max_home < 0)
        max_home = 0;
    snprintf(out, outsz, "%.*s%s", max_home, home, leaf);
}

/* Media mounts are the ones real file managers surface as removable or
 * manually mounted devices: anything under /media, /mnt or /run/media. */
static bool liz_sidebar_is_media_mount(const char* mnt)
{
    return strncmp(mnt, "/media/", 7) == 0
        || strncmp(mnt, "/mnt/", 5) == 0
        || strncmp(mnt, "/run/media/", 11) == 0;
}

/* Formats `bytes` the way UDisks does for unnamed volumes, so the sidebar
 * matches Thunar/Nautilus: power-of-ten units, one decimal below 10 and
 * none at or above it. */
static void liz_sidebar_format_size(unsigned long long bytes, char* out, size_t outsz)
{
    const double kb = 1000.0;
    const double mb = 1000.0 * 1000.0;
    const double gb = 1000.0 * 1000.0 * 1000.0;
    const double tb = 1000.0 * 1000.0 * 1000.0 * 1000.0;

    double val;
    const char* unit;
    if (bytes < mb) {
        val = (double)bytes / kb;
        unit = "KB";
    } else if (bytes < gb) {
        val = (double)bytes / mb;
        unit = "MB";
    } else if (bytes < tb) {
        val = (double)bytes / gb;
        unit = "GB";
    } else {
        val = (double)bytes / tb;
        unit = "TB";
    }
    snprintf(out, outsz, "%.*f %s", val < 10.0 ? 1 : 0, val, unit);
}

/* Builds the display name for a mounted device the way GIO/UDisks do: the
 * filesystem label when the volume has one, otherwise "<size> Volume".
 * Returns true on success, false when udev has no record for `dev` and the
 * caller should keep its mountpoint fallback. */
static bool liz_sidebar_device_name(struct udev* udev_ctx, const char* dev,
                                   char* out, size_t outsz)
{
    const char* sysname = strrchr(dev, '/');
    sysname = (sysname && sysname[1]) ? sysname + 1 : dev;

    struct udev_device* d =
        udev_device_new_from_subsystem_sysname(udev_ctx, "block", sysname);
    if (!d)
        return false;

    bool ok = false;
    const char* label = udev_device_get_property_value(d, "ID_FS_LABEL");
    if (label && label[0]) {
        snprintf(out, outsz, "%.*s", (int)outsz - 1, label);
        ok = true;
    } else {
        const char* size = udev_device_get_sysattr_value(d, "size");
        unsigned long long bytes = size ? strtoull(size, NULL, 10) * 512ULL : 0;
        if (bytes > 0) {
            liz_sidebar_format_size(bytes, out, outsz);
            strncat(out, " Volume", outsz - strlen(out) - 1);
            ok = true;
        }
    }
    udev_device_unref(d);
    return ok;
}

/* Re-reads /proc/mounts and rebuilds the device list. The filesystem root
 * is always present as the first entry. */
static void liz_sidebar_refresh_devices(liz_app* app)
{
    app->sidebar.devices_count = 0;
    liz_sidebar_add_device(app, "File system", "/", "", "drive-harddisk-root");

    FILE* f = fopen("/proc/mounts", "r");
    if (!f)
        return;

    struct udev* udev_ctx = udev_new();

    /* Read the file line by line and split off just the fields we use (the
     * device and the mount point). The mount options that follow can run to
     * thousands of characters (overlay/network mounts), so field-width
     * scanning is unreliable: a single too-long options field used to make
     * fscanf bail out mid-file, silently dropping every later media mount. */
    char* line = NULL;
    size_t linecap = 0;
    while (getline(&line, &linecap, f) >= 0) {
        char* save = NULL;
        char* dev = strtok_r(line, " \t\n", &save);
        char* mnt = dev ? strtok_r(NULL, " \t\n", &save) : NULL;
        if (!mnt)
            continue;

        if (strcmp(mnt, "/") == 0)
            continue;
        if (!liz_sidebar_is_media_mount(mnt))
            continue;
        if (strncmp(dev, "/dev/", 5) != 0)
            continue;

        char name[LIZ_FS_NAME_MAX];
        if (!udev_ctx || !liz_sidebar_device_name(udev_ctx, dev, name, sizeof(name))) {
            const char* leaf = strrchr(mnt, '/');
            leaf = (leaf && leaf[1]) ? leaf + 1 : mnt;
            snprintf(name, sizeof(name), "%.*s", (int)sizeof(name) - 1, leaf);
        }
        liz_sidebar_add_device(app, name, mnt, dev, "drive-removable-media");
    }
    free(line);

    if (udev_ctx)
        udev_unref(udev_ctx);
    fclose(f);
}

void liz_sidebar_init(liz_app* app)
{
    app->sidebar.pinned_count = 0;
    app->sidebar.devices_count = 0;
    app->sidebar.hover_item = -1;

    char home[PATH_MAX];
    const char* h = getenv("HOME");
    if (!h || !h[0]) {
        struct passwd* pw = getpwuid(getuid());
        h = (pw && pw->pw_dir) ? pw->pw_dir : "/";
    }
    snprintf(home, sizeof(home), "%s", h);

    liz_sidebar_add_pinned(app, "Home", home, "user-home");

    char sub[PATH_MAX];
    liz_sidebar_sub(sub, sizeof(sub), home, "/Desktop");
    liz_sidebar_add_pinned(app, "Desktop", sub, "user-desktop");
    liz_sidebar_sub(sub, sizeof(sub), home, "/Downloads");
    liz_sidebar_add_pinned(app, "Downloads", sub, "folder-download");
    liz_sidebar_sub(sub, sizeof(sub), home, "/Pictures");
    liz_sidebar_add_pinned(app, "Pictures", sub, "folder-pictures");

    const char* xdg_data = getenv("XDG_DATA_HOME");
    if (xdg_data && xdg_data[0]) {
        liz_sidebar_sub(sub, sizeof(sub), xdg_data, "/Trash");
    } else {
        liz_sidebar_sub(sub, sizeof(sub), home, "/.local/share/Trash");
    }
    liz_sidebar_add_pinned(app, "Trash", sub, "user-trash");

    liz_sidebar_refresh_devices(app);
}

/* Maps a window y coordinate to a flattened item index: pinned entries come
 * first, then device entries. Returns -1 when the pointer is not over any
 * item (including the section headers). */
int liz_sidebar_item_at(liz_app* app, int y)
{
    int top = liz_sidebar_top();
    int bottom = liz_sidebar_bottom(app->win);
    if (y < top || y >= bottom)
        return -1;

    int idx = 0;
    int py = top + LIZ_UI_ROW_H; /* skip the "Pinned" header */
    for (int i = 0; i < app->sidebar.pinned_count; i++, idx++) {
        if (y >= py && y < py + LIZ_UI_ROW_H)
            return idx;
        py += LIZ_UI_ROW_H;
    }
    py += LIZ_UI_ROW_H; /* skip the "Devices" header */
    for (int i = 0; i < app->sidebar.devices_count; i++, idx++) {
        if (y >= py && y < py + LIZ_UI_ROW_H)
            return idx;
        py += LIZ_UI_ROW_H;
    }
    return -1;
}

static void liz_sidebar_draw_item(liz_app* app, xwindow* w, const liz_sidebar_entry* e,
                                 int idx, int y, int icon_x, int text_x, int max_w,
                                 int ascent, int descent)
{
    if (idx == app->sidebar.hover_item)
        xc_rect(w, 0, y, LIZ_UI_SIDEBAR_W, LIZ_UI_ROW_H, liz_theme_hover_bg);

#ifdef ICON_SUPPORT
    const xc_image* icon = liz_icons_by_name(w, e->icon, liz_theme_text);
    if (icon)
        xc_image_draw(w, icon, icon_x, y + (LIZ_UI_ROW_H - LIZ_ICON_SIZE) / 2);
#else
    (void)icon_x;
#endif

    int ty = y + (LIZ_UI_ROW_H - (ascent + descent)) / 2 + ascent;
    liz_ui_text_clip(w, text_x, ty, e->label, (int)strlen(e->label), app->font, max_w);
}

static void liz_sidebar_draw_header(xwindow* w, const char* title, int len, int y,
                                   int text_x, xc_font* f, int ascent, int descent)
{
    int ty = y + (LIZ_UI_ROW_H - (ascent + descent)) / 2 + ascent;
    xc_text(w, text_x, ty, title, len, f);
}

void liz_sidebar_draw(liz_app* app)
{
    if (!app->sidebar_visible)
        return;

    xwindow* w = app->win;
    int top = liz_sidebar_top();
    int bottom = liz_sidebar_bottom(w);
    if (bottom <= top)
        return;

    liz_sidebar_refresh_devices(app);

    xc_rect(w, 0, top, LIZ_UI_SIDEBAR_W, bottom - top, liz_theme_panel);
    xc_rect(w, LIZ_UI_SIDEBAR_W - 1, top, 1, bottom - top, liz_theme_panel_edge);

    app->sidebar.hover_item = -1;
    if (app->mouse_x >= 0 && app->mouse_x < LIZ_UI_SIDEBAR_W)
        app->sidebar.hover_item = liz_sidebar_item_at(app, app->mouse_y);

    int ascent = 0, descent = 0;
    xc_font_metrics(app->font, &ascent, &descent);
    /* section headers keep the outer padding; items are indented past
     * their icon so the labels line up in one column */
    int icon_x = LIZ_SIDEBAR_PAD_X;
#ifdef ICON_SUPPORT
    int text_x = icon_x + LIZ_ICON_SIZE + LIZ_SIDEBAR_ICON_GAP;
#else
    int text_x = LIZ_SIDEBAR_PAD_X;
#endif
    int max_w = LIZ_UI_SIDEBAR_W - text_x - LIZ_UI_PAD;

    int y = top;
    liz_sidebar_draw_header(w, "Pinned", 6, y, LIZ_SIDEBAR_PAD_X, app->font_dim, ascent, descent);
    y += LIZ_UI_ROW_H;

    int idx = 0;
    for (int i = 0; i < app->sidebar.pinned_count; i++, idx++) {
        if (y + LIZ_UI_ROW_H > bottom)
            return;
        liz_sidebar_draw_item(app, w, &app->sidebar.pinned[i], idx, y, icon_x, text_x,
                              max_w, ascent, descent);
        y += LIZ_UI_ROW_H;
    }

    if (y + LIZ_UI_ROW_H > bottom)
        return;
    liz_sidebar_draw_header(w, "Devices", 7, y, LIZ_SIDEBAR_PAD_X, app->font_dim, ascent, descent);
    y += LIZ_UI_ROW_H;

    for (int i = 0; i < app->sidebar.devices_count; i++, idx++) {
        if (y + LIZ_UI_ROW_H > bottom)
            break;
        liz_sidebar_draw_item(app, w, &app->sidebar.devices[i], idx, y, icon_x, text_x,
                              max_w, ascent, descent);
        y += LIZ_UI_ROW_H;
    }
}

bool liz_sidebar_click(liz_app* app, int x, int y)
{
    if (!app->sidebar_visible || x < 0 || x >= LIZ_UI_SIDEBAR_W)
        return false;

    /* keep the layout consistent with what liz_sidebar_draw just painted */
    liz_sidebar_refresh_devices(app);

    int idx = liz_sidebar_item_at(app, y);
    if (idx < 0)
        return false;

    if (idx < app->sidebar.pinned_count) {
        liz_app_navigate(app, app->sidebar.pinned[idx].path);
        return true;
    }

    int di = idx - app->sidebar.pinned_count;
    if (di < 0 || di >= app->sidebar.devices_count)
        return false;
    liz_app_navigate(app, app->sidebar.devices[di].path);
    return true;
}
