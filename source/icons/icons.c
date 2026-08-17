/* icons.c - freedesktop icon theme lookup, rendered through nanosvg. */

#include "icons/icons.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fs/ini.h"
#include "icons/mime.h"
#include "icons/xdg.h"
#include "third_party/nanosvg/nanosvg.h"
#include "third_party/nanosvg/nanosvgrast.h"

#define LIZ_ICON_SEARCH_MAX 1024 /* resolved directories to scan per lookup */
#define LIZ_ICON_THEME_MAX  8    /* depth of the Inherits chain */
#define LIZ_ICON_THEME_NAME_MAX 128
#define LIZ_ICON_BASE_MAX   1024 /* an icon base directory, e.g. /usr/share/icons */
#define LIZ_ICON_CACHE_SLOTS 512
#define LIZ_ICON_CANDIDATES 6

typedef struct {
    char path[PATH_MAX]; /* <icon base dir>/<theme>/<subdir> */
} liz_icon_dir;

typedef struct {
    char name[LIZ_MIME_MAX]; /* empty slot when name[0] == '\0' */
    xc_color tint;           /* symbolic icons differ per tint, so it is part of the key */
    xc_image* image;         /* NULL means "looked up, theme has nothing" */
} liz_icon_slot;

static liz_icon_dir* g_dirs;
static int g_dir_count;
static liz_icon_slot* g_cache;
static NSVGrasterizer* g_rast;
static bool g_ready;

/* ---- index.theme parsing ---- */

static char* liz_icons_slurp(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    char* buf = (char*)malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

/* How far a subdirectory's size range is from LIZ_ICON_SIZE, following the
 * spec's three directory types. 0 means it holds exactly our size. */
static int liz_icons_size_distance(const char* index, const char* subdir)
{
    char buf[64];
    int size = liz_ini_get(index, subdir, "Size", buf, sizeof(buf)) ? atoi(buf) : 0;
    if (size <= 0)
        return INT_MAX;

    char type[32] = "Threshold";
    liz_ini_get(index, subdir, "Type", type, sizeof(type));

    int min = size, max = size;
    if (strcmp(type, "Scalable") == 0) {
        min = liz_ini_get(index, subdir, "MinSize", buf, sizeof(buf)) ? atoi(buf) : size;
        max = liz_ini_get(index, subdir, "MaxSize", buf, sizeof(buf)) ? atoi(buf) : size;
    } else if (strcmp(type, "Threshold") == 0) {
        int th = liz_ini_get(index, subdir, "Threshold", buf, sizeof(buf))
                     ? atoi(buf) : 2;
        min = size - th;
        max = size + th;
    }

    if (LIZ_ICON_SIZE < min)
        return min - LIZ_ICON_SIZE;
    if (LIZ_ICON_SIZE > max)
        return LIZ_ICON_SIZE - max;
    return 0;
}

static void liz_icons_add_dir(const char* base, const char* theme, const char* subdir)
{
    if (g_dir_count >= LIZ_ICON_SEARCH_MAX)
        return;
    liz_icon_dir* d = &g_dirs[g_dir_count];
    if ((size_t)snprintf(d->path, sizeof(d->path), "%s/%s/%s", base, theme, subdir)
        >= sizeof(d->path))
        return;
    if (access(d->path, F_OK) != 0)
        return;
    g_dir_count++;
}

/* Appends every directory of `theme` to the search list, then recurses into
 * the themes it inherits. `seen` guards against inheritance cycles. */
static void liz_icons_scan_theme(const char* theme, char seen[][LIZ_ICON_THEME_NAME_MAX],
                                 int* seen_count, char bases[][LIZ_ICON_BASE_MAX],
                                 int base_count, int depth)
{
    if (depth >= LIZ_ICON_THEME_MAX || *seen_count >= LIZ_ICON_THEME_MAX)
        return;
    for (int i = 0; i < *seen_count; i++) {
        if (strcmp(seen[i], theme) == 0)
            return;
    }
    snprintf(seen[(*seen_count)++], LIZ_ICON_THEME_NAME_MAX, "%s", theme);

    char themedir[PATH_MAX];
    char path[PATH_MAX];
    char* index = NULL;
    for (int i = 0; i < base_count && !index; i++) {
        if (liz_fs_join(themedir, sizeof(themedir), bases[i], theme) != 0
            || liz_fs_join(path, sizeof(path), themedir, "index.theme") != 0)
            continue;
        index = liz_icons_slurp(path);
    }
    if (!index)
        return;

    /* Only the unscaled Directories key is read: the @2x variants in
     * ScaledDirectories hold icons drawn for twice the nominal size. */
    char* dirs = (char*)malloc(1 << 16);
    if (dirs && liz_ini_get(index, NULL, "Directories", dirs, 1 << 16)) {
        /* two passes so exact-size directories are searched before the
         * approximate ones, whatever order the theme lists them in */
        for (int pass = 0; pass < 2; pass++) {
            char* cursor = dirs;
            while (*cursor) {
                char* comma = strchr(cursor, ',');
                if (comma)
                    *comma = '\0';

                int distance = liz_icons_size_distance(index, cursor);
                if (distance != INT_MAX && ((pass == 0) == (distance == 0))) {
                    for (int i = 0; i < base_count; i++)
                        liz_icons_add_dir(bases[i], theme, cursor);
                }

                if (!comma)
                    break;
                *comma = ',';
                cursor = comma + 1;
            }
        }
    }
    free(dirs);

    char inherits[512];
    if (liz_ini_get(index, NULL, "Inherits", inherits, sizeof(inherits))) {
        char* cursor = inherits;
        while (*cursor) {
            char* comma = strchr(cursor, ',');
            if (comma)
                *comma = '\0';
            if (*cursor)
                liz_icons_scan_theme(cursor, seen, seen_count, bases, base_count, depth + 1);
            if (!comma)
                break;
            cursor = comma + 1;
        }
    }
    free(index);
}

/* The icon theme the rest of the desktop uses, per the GTK settings files.
 * Falls back to Adwaita, which almost every desktop ships. */
static void liz_icons_theme_name(char* out, size_t outsz)
{
    snprintf(out, outsz, "Adwaita");

    const char* home = getenv("HOME");
    const char* config = getenv("XDG_CONFIG_HOME");
    char candidates[3][PATH_MAX];
    int n = 0;

    if (config && config[0])
        snprintf(candidates[n++], PATH_MAX, "%s/gtk-3.0/settings.ini", config);
    else if (home && home[0])
        snprintf(candidates[n++], PATH_MAX, "%s/.config/gtk-3.0/settings.ini", home);
    if (home && home[0]) {
        snprintf(candidates[n++], PATH_MAX, "%s/.config/gtk-4.0/settings.ini", home);
        snprintf(candidates[n++], PATH_MAX, "%s/.gtkrc-2.0", home);
    }

    for (int i = 0; i < n; i++) {
        char* text = liz_icons_slurp(candidates[i]);
        if (!text)
            continue;
        char value[128];
        /* gtkrc-2.0 has no sections, so the key is searched from the top */
        if (liz_ini_get(text, NULL, "gtk-icon-theme-name", value, sizeof(value))) {
            char* v = value;
            size_t len = strlen(v);
            if (len >= 2 && v[0] == '"' && v[len - 1] == '"') {
                v[len - 1] = '\0';
                v++;
            }
            if (v[0])
                snprintf(out, outsz, "%s", v);
            free(text);
            return;
        }
        free(text);
    }
}

/* ---- rasterizing and caching ---- */

/* nanosvg understands neither the currentColor keyword nor the CSS classes
 * symbolic icons carry, so the keyword is rewritten to a literal color
 * before parsing. The replacement is shorter than the keyword, so it is
 * done in place. */
static void liz_icons_resolve_current_color(char* svg, xc_color tint)
{
    static const char key[] = "currentColor";
    const size_t keylen = sizeof(key) - 1;

    char hex[8];
    snprintf(hex, sizeof(hex), "#%02x%02x%02x", tint.r, tint.g, tint.b);
    const size_t hexlen = strlen(hex);

    char* read = svg;
    char* write = svg;
    for (;;) {
        char* hit = strstr(read, key);
        if (!hit) {
            memmove(write, read, strlen(read) + 1);
            return;
        }
        size_t lead = (size_t)(hit - read);
        memmove(write, read, lead);
        write += lead;
        memcpy(write, hex, hexlen);
        write += hexlen;
        read = hit + keylen;
    }
}

static xc_image* liz_icons_render(xwindow* w, const char* path, xc_color tint)
{
    char* text = liz_icons_slurp(path);
    if (!text)
        return NULL;
    liz_icons_resolve_current_color(text, tint);

    /* nsvgParse consumes the buffer it is handed */
    NSVGimage* svg = nsvgParse(text, "px", 96.0f);
    free(text);
    if (!svg)
        return NULL;
    if (svg->width <= 0.0f || svg->height <= 0.0f) {
        nsvgDelete(svg);
        return NULL;
    }

    /* fit the drawing into the box and centre it, so icons authored with
     * padding or a non-square canvas still line up with each other */
    float scale = (float)LIZ_ICON_SIZE / (svg->width > svg->height ? svg->width : svg->height);
    float tx = ((float)LIZ_ICON_SIZE - svg->width * scale) * 0.5f;
    float ty = ((float)LIZ_ICON_SIZE - svg->height * scale) * 0.5f;

    unsigned char* rgba = (unsigned char*)calloc(LIZ_ICON_SIZE * LIZ_ICON_SIZE * 4, 1);
    if (!rgba) {
        nsvgDelete(svg);
        return NULL;
    }
    nsvgRasterize(g_rast, svg, tx, ty, scale, rgba, LIZ_ICON_SIZE, LIZ_ICON_SIZE,
                  LIZ_ICON_SIZE * 4);
    nsvgDelete(svg);

    xc_image* img = xc_image_from_rgba(w, rgba, LIZ_ICON_SIZE, LIZ_ICON_SIZE);
    free(rgba);
    return img;
}

static unsigned liz_icons_hash(const char* s)
{
    unsigned h = 2166136261u;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 16777619u;
    }
    return h;
}

static void liz_icons_cache_clear(xwindow* w)
{
    if (!g_cache)
        return;
    for (int i = 0; i < LIZ_ICON_CACHE_SLOTS; i++) {
        if (g_cache[i].name[0]) {
            xc_image_free(w, g_cache[i].image);
            g_cache[i].name[0] = '\0';
            g_cache[i].image = NULL;
        }
    }
}

static bool liz_icons_same_tint(xc_color a, xc_color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

const xc_image* liz_icons_by_name(xwindow* w, const char* name, xc_color tint)
{
    if (!g_ready)
        liz_icons_init();
    if (!g_cache || !g_rast || !name || !name[0] || strlen(name) >= LIZ_MIME_MAX)
        return NULL;

    unsigned start = liz_icons_hash(name) % LIZ_ICON_CACHE_SLOTS;
    for (unsigned probe = 0; probe < LIZ_ICON_CACHE_SLOTS; probe++) {
        liz_icon_slot* slot = &g_cache[(start + probe) % LIZ_ICON_CACHE_SLOTS];
        if (slot->name[0]) {
            if (strcmp(slot->name, name) == 0 && liz_icons_same_tint(slot->tint, tint))
                return slot->image; /* NULL here is a cached miss */
            continue;
        }

        /* first miss: resolve and rasterize into this free slot */
        xc_image* img = NULL;
        char path[PATH_MAX];
        for (int i = 0; i < g_dir_count && !img; i++) {
            if ((size_t)snprintf(path, sizeof(path), "%s/%s.svg", g_dirs[i].path, name)
                >= sizeof(path))
                continue;
            if (access(path, R_OK) == 0)
                img = liz_icons_render(w, path, tint);
        }
        snprintf(slot->name, sizeof(slot->name), "%s", name);
        slot->tint = tint;
        slot->image = img;
        return img;
    }

    /* a full cache means the session has touched more icon names than we
     * planned for; start over rather than growing without bound */
    liz_icons_cache_clear(w);
    return NULL;
}

/* ---- entry to icon name ---- */

/* The user's home folders get their own icons, matched the same way the
 * sidebar builds its list: by name directly below $HOME. */
static const char* liz_icons_home_folder(const char* dir, const char* name)
{
    const char* home = getenv("HOME");
    if (!home || !home[0] || strcmp(dir, home) != 0)
        return NULL;

    static const struct {
        const char* name;
        const char* icon;
    } known[] = {
        { "Desktop", "folder-desktop" }, { "Documents", "folder-documents" },
        { "Downloads", "folder-download" }, { "Music", "folder-music" },
        { "Pictures", "folder-pictures" }, { "Videos", "folder-videos" },
        { "Public", "folder-publicshare" }, { "Templates", "folder-templates" },
    };
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        if (strcmp(name, known[i].name) == 0)
            return known[i].icon;
    }
    return NULL;
}

const xc_image* liz_icons_for_entry(xwindow* w, const char* dir, const liz_fs_entry* e,
                                    xc_color tint)
{
    if (!g_ready)
        liz_icons_init();

    char candidates[LIZ_ICON_CANDIDATES][LIZ_MIME_MAX];
    int n = 0;

    switch (e->type) {
    case LIZ_FS_DIR:
        if (strcmp(e->name, "..") == 0) {
            snprintf(candidates[n++], LIZ_MIME_MAX, "go-up");
        } else {
            const char* special = liz_icons_home_folder(dir, e->name);
            if (special)
                snprintf(candidates[n++], LIZ_MIME_MAX, "%s", special);
        }
        snprintf(candidates[n++], LIZ_MIME_MAX, "folder");
        break;
    case LIZ_FS_LINK:
        /* the target is not stat'd, so a symlink is named by its own
         * filename and falls back to the generic link icon */
        n = liz_mime_icon_names(e->name, candidates, LIZ_ICON_CANDIDATES - 1);
        snprintf(candidates[n++], LIZ_MIME_MAX, "inode-symlink");
        break;
    case LIZ_FS_SPECIAL:
        snprintf(candidates[n++], LIZ_MIME_MAX, "application-x-generic");
        break;
    case LIZ_FS_FILE:
    default:
        n = liz_mime_icon_names(e->name, candidates, LIZ_ICON_CANDIDATES);
        break;
    }

    for (int i = 0; i < n; i++) {
        const xc_image* img = liz_icons_by_name(w, candidates[i], tint);
        if (img)
            return img;
    }
    return NULL;
}

/* ---- lifetime ---- */

void liz_icons_init(void)
{
    if (g_ready)
        return;
    g_ready = true;

    g_dirs = (liz_icon_dir*)calloc(LIZ_ICON_SEARCH_MAX, sizeof(*g_dirs));
    g_cache = (liz_icon_slot*)calloc(LIZ_ICON_CACHE_SLOTS, sizeof(*g_cache));
    g_rast = nsvgCreateRasterizer();
    if (!g_dirs || !g_cache || !g_rast)
        return;

    liz_mime_init();

    char data[LIZ_XDG_DIRS_MAX][LIZ_XDG_PATH_MAX];
    int data_count = liz_xdg_data_dirs(data, LIZ_XDG_DIRS_MAX);

    /* ~/.icons predates the XDG spec but themes still land there */
    char bases[LIZ_XDG_DIRS_MAX + 1][LIZ_ICON_BASE_MAX];
    int base_count = 0;
    const char* home = getenv("HOME");
    if (home && home[0] && liz_fs_join(bases[base_count], LIZ_ICON_BASE_MAX, home, ".icons") == 0)
        base_count++;
    for (int i = 0; i < data_count; i++) {
        if (liz_fs_join(bases[base_count], LIZ_ICON_BASE_MAX, data[i], "icons") == 0)
            base_count++;
    }

    char theme[LIZ_ICON_THEME_NAME_MAX];
    liz_icons_theme_name(theme, sizeof(theme));

    char seen[LIZ_ICON_THEME_MAX][LIZ_ICON_THEME_NAME_MAX];
    int seen_count = 0;
    liz_icons_scan_theme(theme, seen, &seen_count, bases, base_count, 0);
    liz_icons_scan_theme("hicolor", seen, &seen_count, bases, base_count, 0);
}

void liz_icons_shutdown(xwindow* w)
{
    liz_icons_cache_clear(w);
    free(g_cache);
    free(g_dirs);
    if (g_rast)
        nsvgDeleteRasterizer(g_rast);
    liz_mime_shutdown();

    g_cache = NULL;
    g_dirs = NULL;
    g_rast = NULL;
    g_dir_count = 0;
    g_ready = false;
}
