/* apps.c - which installed application opens a file, and running it. */

#include "apps/apps.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fs/fs.h"
#include "fs/ini.h"
#include "icons/mime.h"
#include "icons/xdg.h"

#define LIZ_APPS_ASSOC_MAX 4096 /* longest desktop-id list read from one key */
#define LIZ_APPS_TYPES_MAX 32   /* candidate types, aliases included */

static char* liz_apps_slurp(const char* path)
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

/* Locates the desktop entry named `id` and parses the fields we use.
 *
 * A desktop file id may encode a subdirectory as a dash, so
 * "kde4-konsole.desktop" can live at "kde4/konsole.desktop"; both spellings
 * are tried. Entries marked Hidden are treated as absent, which is how the
 * spec says to retract an association. */
static bool liz_apps_load(const char* id, liz_desktop_app* out)
{
    if (!id || !id[0] || strchr(id, '/') != NULL)
        return false;

    char dirs[LIZ_XDG_DIRS_MAX][LIZ_XDG_PATH_MAX];
    int ndirs = liz_xdg_data_dirs(dirs, LIZ_XDG_DIRS_MAX);

    char* text = NULL;
    for (int i = 0; i < ndirs && !text; i++) {
        char apps[LIZ_XDG_PATH_MAX + 32];
        char file[PATH_MAX];
        if (liz_fs_join(apps, sizeof(apps), dirs[i], "applications") != 0)
            continue;
        if (liz_fs_join(file, sizeof(file), apps, id) == 0)
            text = liz_apps_slurp(file);

        for (const char* dash = strchr(id, '-'); dash && !text; dash = strchr(dash + 1, '-')) {
            char nested[LIZ_APP_ID_MAX];
            snprintf(nested, sizeof(nested), "%s", id);
            nested[dash - id] = '/';
            if (liz_fs_join(file, sizeof(file), apps, nested) == 0)
                text = liz_apps_slurp(file);
        }
    }
    if (!text)
        return false;

    char exec[LIZ_APP_EXEC_MAX];
    bool ok = liz_ini_get(text, "Desktop Entry", "Exec", exec, sizeof(exec));
    if (ok) {
        char flag[LIZ_APP_NAME_MAX];

        memset(out, 0, sizeof(*out));
        snprintf(out->id, sizeof(out->id), "%s", id);
        snprintf(out->exec, sizeof(out->exec), "%s", exec);

        if (!liz_ini_get(text, "Desktop Entry", "Name", out->name, sizeof(out->name)))
            snprintf(out->name, sizeof(out->name), "%s", id);

        if (liz_ini_get(text, "Desktop Entry", "Terminal", flag, sizeof(flag)))
            out->terminal = strcmp(flag, "true") == 0;

        if (liz_ini_get(text, "Desktop Entry", "Hidden", flag, sizeof(flag))
            && strcmp(flag, "true") == 0)
            ok = false;
    }

    free(text);
    return ok;
}

/* Calls `visit` for each id in a "a.desktop;b.desktop;" list until one
 * returns true. */
static bool liz_apps_each_id(char* list, bool (*visit)(const char* id, void* data),
                            void* data)
{
    char* cursor = list;
    while (cursor && *cursor) {
        char* sep = strchr(cursor, ';');
        if (sep)
            *sep = '\0';
        while (*cursor == ' ')
            cursor++;
        if (*cursor && visit(cursor, data))
            return true;
        if (!sep)
            break;
        cursor = sep + 1;
    }
    return false;
}

/* The mimeapps.list files, in the precedence the association spec gives
 * them: the user's configuration first, then system configuration, then the
 * per-applications-directory lists. Desktop-specific lists shadow the plain
 * ones. Returns how many paths were written. */
static int liz_apps_assoc_files(char out[][PATH_MAX], int max)
{
    const char* desktop = getenv("XDG_CURRENT_DESKTOP");
    char lower[64] = "";
    if (desktop && desktop[0]) {
        size_t n = 0;
        for (; desktop[n] && n < sizeof(lower) - 1; n++) {
            char c = desktop[n];
            if (c == ':') /* only the first name of a colon-separated list */
                break;
            lower[n] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        }
        lower[n] = '\0';
    }

    int count = 0;
    char dirs[LIZ_XDG_DIRS_MAX][LIZ_XDG_PATH_MAX];

    int nconf = liz_xdg_config_dirs(dirs, LIZ_XDG_DIRS_MAX);
    for (int i = 0; i < nconf && count < max; i++) {
        char leaf[80];
        if (lower[0]) {
            snprintf(leaf, sizeof(leaf), "%s-mimeapps.list", lower);
            if (count < max && liz_fs_join(out[count], PATH_MAX, dirs[i], leaf) == 0)
                count++;
        }
        if (count < max && liz_fs_join(out[count], PATH_MAX, dirs[i], "mimeapps.list") == 0)
            count++;
    }

    int ndata = liz_xdg_data_dirs(dirs, LIZ_XDG_DIRS_MAX);
    for (int i = 0; i < ndata && count < max; i++) {
        char apps[LIZ_XDG_PATH_MAX + 32];
        if (liz_fs_join(apps, sizeof(apps), dirs[i], "applications") != 0)
            continue;
        if (lower[0]) {
            char leaf[80];
            snprintf(leaf, sizeof(leaf), "%s-mimeapps.list", lower);
            if (count < max && liz_fs_join(out[count], PATH_MAX, apps, leaf) == 0)
                count++;
        }
        if (count < max && liz_fs_join(out[count], PATH_MAX, apps, "mimeapps.list") == 0)
            count++;
    }
    return count;
}

static bool liz_apps_load_into(const char* id, void* data)
{
    return liz_apps_load(id, (liz_desktop_app*)data);
}

/* Every MIME type that could describe `name`, each followed by its
 * aliases, most specific first. Both halves matter: an extension can be
 * claimed by several types, and an application may have registered any of
 * the spellings a type is known by. */
static int liz_apps_types(const char* name, char out[][LIZ_MIME_MAX], int max)
{
    char types[LIZ_MIME_TYPES_MAX][LIZ_MIME_MAX];
    int ntypes = liz_mime_types(name, types, LIZ_MIME_TYPES_MAX);

    int n = 0;
    for (int i = 0; i < ntypes && n < max; i++) {
        char aliases[LIZ_MIME_ALIASES_MAX][LIZ_MIME_MAX];
        int naliases = liz_mime_alias_names(types[i], aliases, LIZ_MIME_ALIASES_MAX);
        for (int a = 0; a < naliases && n < max; a++) {
            bool seen = false;
            for (int j = 0; j < n && !seen; j++)
                seen = strcmp(out[j], aliases[a]) == 0;
            if (!seen) {
                size_t len = strnlen(aliases[a], LIZ_MIME_MAX - 1);
                memcpy(out[n], aliases[a], len);
                out[n][len] = '\0';
                n++;
            }
        }
    }
    return n;
}

bool liz_apps_default(const char* path, liz_desktop_app* out)
{
    const char* name = strrchr(path, '/');
    name = name ? name + 1 : path;

    char types[LIZ_APPS_TYPES_MAX][LIZ_MIME_MAX];
    int ntypes = liz_apps_types(name, types, LIZ_APPS_TYPES_MAX);

    char files[LIZ_XDG_DIRS_MAX * 4][PATH_MAX];
    int count = liz_apps_assoc_files(files, LIZ_XDG_DIRS_MAX * 4);

    for (int i = 0; i < count; i++) {
        char* text = liz_apps_slurp(files[i]);
        if (!text)
            continue;
        bool found = false;
        for (int t = 0; t < ntypes && !found; t++) {
            char list[LIZ_APPS_ASSOC_MAX];
            found = liz_ini_get(text, "Default Applications", types[t], list, sizeof(list))
                    && liz_apps_each_id(list, liz_apps_load_into, out);
        }
        free(text);
        if (found)
            return true;
    }
    return false;
}

typedef struct {
    liz_desktop_app* out;
    int count;
    int max;
} liz_apps_collect;

/* Appends `id` unless it is already in the list. Returns true only when
 * the output is full, to stop liz_apps_each_id early. */
static bool liz_apps_collect_id(const char* id, void* data)
{
    liz_apps_collect* c = (liz_apps_collect*)data;
    if (c->count >= c->max)
        return true;
    for (int i = 0; i < c->count; i++) {
        if (strcmp(c->out[i].id, id) == 0)
            return false;
    }
    if (!liz_apps_load(id, &c->out[c->count]))
        return false;

    /* Several ids can carry one display name, as a browser's versioned and
     * unversioned entries do. Listing it twice helps nobody. */
    for (int i = 0; i < c->count; i++) {
        if (strcmp(c->out[i].name, c->out[c->count].name) == 0)
            return false;
    }
    c->count++;
    return false;
}

int liz_apps_candidates(const char* path, liz_desktop_app* out, int max)
{
    if (max <= 0)
        return 0;

    const char* name = strrchr(path, '/');
    name = name ? name + 1 : path;

    char types[LIZ_APPS_TYPES_MAX][LIZ_MIME_MAX];
    int ntypes = liz_apps_types(name, types, LIZ_APPS_TYPES_MAX);

    /* Both tables are read once and walked per type, because the type has
     * to be the outer loop: an application that registered for the exact
     * type belongs above one that claimed a more general ancestor. Firefox
     * lists itself under audio/ogg, while the audio player registers the
     * audio/x-vorbis+ogg an .ogg file usually is. */
    char* assoc[LIZ_XDG_DIRS_MAX * 4];
    char files[LIZ_XDG_DIRS_MAX * 4][PATH_MAX];
    int nassoc = liz_apps_assoc_files(files, LIZ_XDG_DIRS_MAX * 4);
    for (int i = 0; i < nassoc; i++)
        assoc[i] = liz_apps_slurp(files[i]);

    char* caches[LIZ_XDG_DIRS_MAX];
    char dirs[LIZ_XDG_DIRS_MAX][LIZ_XDG_PATH_MAX];
    int ndata = liz_xdg_data_dirs(dirs, LIZ_XDG_DIRS_MAX);
    for (int i = 0; i < ndata; i++) {
        char apps[LIZ_XDG_PATH_MAX + 32];
        char cache[PATH_MAX];
        caches[i] = NULL;
        if (liz_fs_join(apps, sizeof(apps), dirs[i], "applications") == 0
            && liz_fs_join(cache, sizeof(cache), apps, "mimeinfo.cache") == 0)
            caches[i] = liz_apps_slurp(cache);
    }

    liz_apps_collect collect = { out, 0, max };
    if (liz_apps_default(path, &out[0]))
        collect.count = 1;

    for (int t = 0; t < ntypes; t++) {
        char list[LIZ_APPS_ASSOC_MAX];

        /* what the user added by hand comes before what applications
         * registered for themselves */
        for (int i = 0; i < nassoc; i++) {
            if (assoc[i] && liz_ini_get(assoc[i], "Added Associations", types[t],
                                        list, sizeof(list)))
                liz_apps_each_id(list, liz_apps_collect_id, &collect);
        }
        /* mimeinfo.cache is what update-desktop-database builds from every
         * installed entry's own MimeType line */
        for (int i = 0; i < ndata; i++) {
            if (caches[i] && liz_ini_get(caches[i], "MIME Cache", types[t],
                                         list, sizeof(list)))
                liz_apps_each_id(list, liz_apps_collect_id, &collect);
        }
    }

    for (int i = 0; i < nassoc; i++)
        free(assoc[i]);
    for (int i = 0; i < ndata; i++)
        free(caches[i]);

    return collect.count;
}

/* Splits an Exec value into argv, substituting the field codes.
 *
 * %f/%u pass the file, %F/%U pass it as a one-element list, and the rest
 * (%i, %c, %k and the deprecated codes) carry no meaning here and are
 * dropped rather than passed through as literal text. Quoting follows the
 * desktop entry spec: double quotes group, backslash escapes inside them. */
static int liz_apps_build_argv(const char* exec, const char* path,
                              char* store, size_t storesz,
                              char** argv, int argv_max)
{
    int argc = 0;
    size_t used = 0;
    const char* p = exec;
    bool got_path = false;

    while (*p && argc < argv_max - 1) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;

        char* arg = store + used;
        size_t arglen = 0;
        bool quoted = false;
        bool drop = false;

        while (*p && (quoted || (*p != ' ' && *p != '\t'))) {
            if (*p == '"') {
                quoted = !quoted;
                p++;
                continue;
            }
            if (*p == '\\' && quoted && p[1]) {
                p++;
            } else if (*p == '%' && p[1]) {
                char code = p[1];
                p += 2;
                if (code == 'f' || code == 'u' || code == 'F' || code == 'U') {
                    size_t plen = strlen(path);
                    if (used + arglen + plen + 1 >= storesz)
                        return 0;
                    memcpy(arg + arglen, path, plen);
                    arglen += plen;
                    got_path = true;
                } else if (code == '%') {
                    if (used + arglen + 2 >= storesz)
                        return 0;
                    arg[arglen++] = '%';
                } else if (arglen == 0) {
                    /* a standalone code such as %i expands to nothing, so
                     * the whole argument disappears rather than becoming
                     * an empty one */
                    drop = true;
                }
                continue;
            }
            if (used + arglen + 2 >= storesz)
                return 0;
            arg[arglen++] = *p++;
        }

        arg[arglen] = '\0';
        used += arglen + 1;
        if (!drop && arglen > 0)
            argv[argc++] = arg;
    }

    /* An entry with no field code still opens the file it was chosen for. */
    if (!got_path && argc > 0 && argc < argv_max - 1) {
        size_t plen = strlen(path);
        if (used + plen + 1 >= storesz)
            return 0;
        char* arg = store + used;
        memcpy(arg, path, plen + 1);
        used += plen + 1;
        argv[argc++] = arg;
    }

    argv[argc] = NULL;
    return argc;
}

void liz_apps_exec(const liz_desktop_app* app, const char* path)
{
    char store[LIZ_APP_EXEC_MAX + PATH_MAX + 64];
    char* argv[32];

    if (liz_apps_build_argv(app->exec, path, store, sizeof(store), argv,
                            (int)(sizeof(argv) / sizeof(argv[0]))) <= 0)
        return;

    if (!app->terminal) {
        execvp(argv[0], argv);
        return;
    }

    /* A terminal application is handed to $TERMINAL, which is also how the
     * "Open terminal" action finds one. */
    const char* term = getenv("TERMINAL");
    if (!term || !term[0])
        term = "st";

    char* targv[35];
    targv[0] = (char*)term;
    targv[1] = (char*)"-e";
    int n = 0;
    for (; argv[n] && n < 32; n++)
        targv[n + 2] = argv[n];
    targv[n + 2] = NULL;
    execvp(term, targv);
}

bool liz_apps_for(const char* path, liz_desktop_app* out)
{
    if (liz_apps_default(path, out))
        return true;

    /* Nothing is set as the default for this type. The first application
     * that registered for it still beats handing the file to xdg-open,
     * which would route it through the web browser. */
    liz_desktop_app all[LIZ_APPS_MAX];
    if (liz_apps_candidates(path, all, LIZ_APPS_MAX) <= 0)
        return false;
    *out = all[0];
    return true;
}
