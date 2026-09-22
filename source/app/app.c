#include "app/app.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <pwd.h>
#include <sys/types.h>

#include <fontconfig/fontconfig.h>

#include "defaults/defaults.h"
#include "icons/icons.h"
#include "ui/chooser.h"
#include "ui/delete.h"
#include "ui/file_list.h"
#include "ui/keybind.h"
#include "ui/menu.h"
#include "ui/nav.h"
#include "ui/newfolder.h"
#include "ui/preview.h"
#include "ui/rename.h"
#include "ui/sidebar.h"
#include "ui/status.h"
#include "ui/theme.h"

#ifdef ARCHIVE_SUPPORT
#include "archive/archive.h"
#endif

#define LIZ_KEYBIND_COUNT                                                     \
    (sizeof(liz_keybinds) / sizeof(liz_keybinds[0]))

static const struct liz_keybind liz_keybinds[] = {
    LIZ_BIND_QUIT,
    LIZ_BIND_NAV_EDIT,
    LIZ_BIND_TOGGLE_HIDDEN,
    LIZ_BIND_TOGGLE_SIDEBAR,
    LIZ_BIND_HALF_DOWN,
    LIZ_BIND_HALF_UP,
    LIZ_BIND_HISTORY_BACK,
    LIZ_BIND_HISTORY_FORWARD,
    LIZ_BIND_COPY,
    LIZ_BIND_CUT,
    LIZ_BIND_PASTE,
    LIZ_BIND_PREVIEW,
    LIZ_BIND_NEW_FOLDER,
    LIZ_BIND_OPEN_TERMINAL,
    LIZ_BIND_OPEN_TERMINAL_DIR,
    LIZ_BIND_RENAME,
    LIZ_BIND_GO_HOME,
    LIZ_BIND_CLOSE_PREVIEW,
    LIZ_BIND_DELETE,
};

/* Non-vim mode keybindings (LIZ_VIM_MODE_DEFAULT 0): plain unmodified
 * characters are reserved for type-to-search, so every default lives on a
 * modifier or a text-free key. */
#define LIZ_NOVIM_KEYBIND_COUNT                                               \
    (sizeof(liz_novim_keybinds) / sizeof(liz_novim_keybinds[0]))

static const struct liz_keybind liz_novim_keybinds[] = {
    LIZ_NOVIM_BIND_QUIT,
    LIZ_NOVIM_BIND_NAV_EDIT,
    LIZ_NOVIM_BIND_TOGGLE_HIDDEN,
    LIZ_NOVIM_BIND_TOGGLE_SIDEBAR,
    LIZ_NOVIM_BIND_HALF_DOWN,
    LIZ_NOVIM_BIND_HALF_UP,
    LIZ_NOVIM_BIND_HISTORY_BACK,
    LIZ_NOVIM_BIND_HISTORY_FORWARD,
    LIZ_NOVIM_BIND_COPY,
    LIZ_NOVIM_BIND_CUT,
    LIZ_NOVIM_BIND_PASTE,
    LIZ_NOVIM_BIND_PREVIEW,
    LIZ_NOVIM_BIND_NEW_FOLDER,
    LIZ_NOVIM_BIND_OPEN_TERMINAL,
    LIZ_NOVIM_BIND_OPEN_TERMINAL_DIR,
    LIZ_NOVIM_BIND_RENAME,
    LIZ_NOVIM_BIND_GO_HOME,
    LIZ_NOVIM_BIND_CLOSE_PREVIEW,
    LIZ_NOVIM_BIND_DELETE,
};

double liz_app_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* Pushes the current location onto a jumplist stack, dropping the oldest
 * entry when full. */
static void liz_app_jump_push(liz_app* app, liz_jump_entry* stack, int* count)
{
    if (*count >= LIZ_JUMPLIST_MAX) {
        memmove(stack, stack + 1, (size_t)(LIZ_JUMPLIST_MAX - 1) * sizeof(liz_jump_entry));
        (*count)--;
    }
    liz_jump_entry* e = &stack[*count];
    snprintf(e->path, sizeof(e->path), "%s", app->cwd);
    e->row = app->selected;
    (*count)++;
}

/* The caller's location is stashed and the jumplist stack is popped, then
 * the stored location is restored (its directory and focused row). */
static void liz_app_jump_to(liz_app* app, liz_jump_entry* from, int* from_count,
                           liz_jump_entry* to, int* to_count)
{
    if (*from_count == 0)
        return;
    liz_app_jump_push(app, to, to_count);
    liz_jump_entry e = from[--(*from_count)];
    app->jump_suppress = true;
    liz_app_navigate(app, e.path);
    app->jump_suppress = false;
    liz_app_set_selected(app, e.row);
}

void liz_app_jump_back(liz_app* app)
{
    liz_app_jump_to(app, app->jump_back, &app->jump_back_count,
                   app->jump_fwd, &app->jump_fwd_count);
}

void liz_app_jump_fwd(liz_app* app)
{
    liz_app_jump_to(app, app->jump_fwd, &app->jump_fwd_count,
                   app->jump_back, &app->jump_back_count);
}

void liz_app_go_home(liz_app* app)
{
    char home[PATH_MAX];
    const char* h = getenv("HOME");
    if (!h || !h[0]) {
        struct passwd* pw = getpwuid(getuid());
        h = (pw && pw->pw_dir) ? pw->pw_dir : "/";
    }
    snprintf(home, sizeof(home), "%s", h);
    liz_app_navigate(app, home);
}

void liz_app_toggle_hidden(liz_app* app)
{
    app->show_hidden = !app->show_hidden;
    liz_app_navigate(app, app->cwd);
}

void liz_app_toggle_sidebar(liz_app* app)
{
    app->sidebar_visible = !app->sidebar_visible;
}

/* In chooser mode with an active filter, drops files that don't match it
 * from the listing (directories, including symlinks that resolve to one,
 * always stay so navigation is never blocked by a filter meant for files).
 * Compacts `entries` in place and shrinks `*count`. */
static void liz_app_apply_chooser_filter(liz_app* app, const char* dir,
                                        liz_fs_entry* entries, size_t* count)
{
    if (!app->chooser.active || app->chooser.filter_count == 0)
        return;

    size_t kept = 0;
    for (size_t i = 0; i < *count; i++) {
        liz_fs_entry* e = &entries[i];
        bool is_dir = e->type == LIZ_FS_DIR;
        if (!is_dir && e->type == LIZ_FS_LINK) {
            char full[PATH_MAX], canon[PATH_MAX];
            struct stat st;
            if (liz_fs_join(full, sizeof(full), dir, e->name) == 0
                && liz_fs_canonical(canon, sizeof(canon), full) == 0
                && stat(canon, &st) == 0 && S_ISDIR(st.st_mode))
                is_dir = true;
        }
        if (is_dir || liz_chooser_name_matches_filter(app, e->name))
            entries[kept++] = *e;
    }
    *count = kept;
}

void liz_app_navigate(liz_app* app, const char* path)
{
    char canon[PATH_MAX];
    if (liz_fs_canonical(canon, sizeof(canon), path) != 0) {
        /* might be a virtual path inside an archive -- keep it as-is */
        snprintf(canon, sizeof(canon), "%s", path);
    }

#ifdef ARCHIVE_SUPPORT
    /* check if canonical path starts with an archive marker */
    const char* marker = strstr(canon, "archive://");
    if (marker == canon) {
        /* virtual path inside an archive: parse archive_path:virtual_path */
        const char* inner = canon + 10; /* skip "archive://" */
        const char* colon = strchr(inner, ':');
        if (colon) {
            size_t alen = (size_t)(colon - inner);
            char apath[PATH_MAX];
            if (alen >= sizeof(apath))
                alen = sizeof(apath) - 1;
            memcpy(apath, inner, alen);
            apath[alen] = '\0';
            const char* vpath = colon + 1;

            liz_fs_entry* entries = NULL;
            size_t count = 0;
            if (liz_archive_read(apath, vpath, &entries, &count) != 0)
                return;

            liz_fs_entries_free(app->entries, app->entry_count);
            app->entries = entries;
            app->entry_count = count;

            free(app->sel);
            app->sel = (bool*)calloc(count > 0 ? count : 1, sizeof(bool));

            snprintf(app->cwd, sizeof(app->cwd), "%s", canon);
            app->archive.inside = true;
            snprintf(app->archive.archive_path, sizeof(app->archive.archive_path),
                     "%s", apath);
            snprintf(app->archive.virtual_path, sizeof(app->archive.virtual_path),
                     "%s", vpath);

            app->selected = 0;
            app->scroll = 0;
            app->hover_row = -1;
            app->nav_segments = 0;
            app->nav_input.editing = false;
            app->rename.active = false;
            app->del.active = false;
            app->anchor_row = 0;
            app->vim.search_active = false;
            app->vim.pending_g = false;
            app->vim.pending_d = false;
            app->vim.visual_active = false;
            app->novim_search.active = false;
            app->novim_search.query[0] = '\0';

            if (app->win)
                XStoreName(app->win->display, app->win->window, canon);
            return;
        }
    }
#endif

#ifdef ARCHIVE_SUPPORT
    /* navigating away from an archive clears the archive state, but only
     * after the new directory is successfully read so we don't break the
     * display if liz_fs_read fails (e.g. path is a file, not a dir) */
    bool was_archive = app->archive.inside;
#endif

    liz_fs_entry* entries = NULL;
    size_t count = 0;
    if (liz_fs_read(canon, app->show_hidden, &entries, &count) != 0)
        return;

#ifdef ARCHIVE_SUPPORT
    if (was_archive) {
        app->archive.inside = false;
        app->archive.archive_path[0] = '\0';
        app->archive.virtual_path[0] = '\0';
    }
#endif

    liz_app_apply_chooser_filter(app, canon, entries, &count);

    /* a real location change (not a reload of the same directory, and not a
     * Ctrl+O/I jump) is recorded so Ctrl+O can walk back through it */
    if (app->cwd[0] != '\0' && !app->jump_suppress
        && strcmp(canon, app->cwd) != 0) {
        liz_app_jump_push(app, app->jump_back, &app->jump_back_count);
        app->jump_fwd_count = 0;
    }

    liz_fs_entries_free(app->entries, app->entry_count);
    app->entries = entries;
    app->entry_count = count;

    free(app->sel);
    app->sel = (bool*)calloc(count > 0 ? count : 1, sizeof(bool));

    snprintf(app->cwd, sizeof(app->cwd), "%s", canon);
    app->selected = 0;
    app->scroll = 0;
    app->hover_row = -1;
    app->nav_segments = 0;
    app->nav_input.editing = false;
    app->rename.active = false;
    app->del.active = false;
    app->anchor_row = 0;

    /* the listing just changed under us: an in-progress search or selection
     * would be pointing at rows that no longer mean what they did */
    app->vim.search_active = false;
    app->vim.pending_g = false;
    app->vim.pending_d = false;
    app->vim.visual_active = false;
    app->novim_search.active = false;
    app->novim_search.query[0] = '\0';

    if (app->win)
        XStoreName(app->win->display, app->win->window, canon);
}

void liz_app_navigate_and_select(liz_app* app, const char* path)
{
    char canon[PATH_MAX];
    if (liz_fs_canonical(canon, sizeof(canon), path) != 0) {
        liz_app_navigate(app, path); /* let navigate's own error handling deal with it */
        return;
    }

    struct stat st;
    if (stat(canon, &st) == 0 && S_ISDIR(st.st_mode)) {
        liz_app_navigate(app, canon);
        return;
    }

    /* not a directory (a file, or doesn't exist yet) -- open its parent
     * and select it by name if present there */
    char parent[PATH_MAX];
    if (liz_fs_parent(parent, sizeof(parent), canon) != 0) {
        liz_app_navigate(app, canon);
        return;
    }

    const char* base = strrchr(canon, '/');
    base = base ? base + 1 : canon;
    char* saved = strdup(base);

    liz_app_navigate(app, parent);

    if (saved) {
        for (size_t i = 0; i < app->entry_count; i++) {
            if (strcmp(app->entries[i].name, saved) == 0) {
                liz_app_set_selected(app, (int)i);
                break;
            }
        }
        free(saved);
    }
}

void liz_app_open_row(liz_app* app, int row)
{
    if (row < 0 || (size_t)row >= app->entry_count)
        return;

    /* in file-picker mode, opening a row means choosing it (see chooser.c) */
    if (app->chooser.active) {
        liz_chooser_open_row(app, row);
        return;
    }

    liz_fs_entry* e = &app->entries[row];

#ifdef ARCHIVE_SUPPORT
    /* inside an archive: virtual entries */
    if (app->archive.inside) {
        if (e->type == LIZ_FS_VIRTUAL || e->type == LIZ_FS_DIR) {
            if (strcmp(e->name, "..") == 0) {
                if (app->archive.virtual_path[0] == '\0') {
                    /* at archive root: exit to filesystem parent */
                    char parent[PATH_MAX];
                    if (liz_fs_parent(parent, sizeof(parent),
                                      app->archive.archive_path) == 0)
                        liz_app_navigate(app, parent);
                    return;
                }
                /* go up: navigate to the parent virtual path */
                char vpath[LIZ_ARCHIVE_PATH_MAX];
                snprintf(vpath, sizeof(vpath), "%s", app->archive.virtual_path);
                /* strip the last component */
                char* sl = strrchr(vpath, '/');
                if (sl && sl != vpath) {
                    *sl = '\0';
                } else {
                    vpath[0] = '\0';
                }
                char nav[LIZ_ARCHIVE_PATH_MAX + PATH_MAX + 32];
                snprintf(nav, sizeof(nav), "archive://%s:%s",
                         app->archive.archive_path, vpath);
                liz_app_navigate(app, nav);
                return;
            }
            /* navigate into this virtual directory */
            char new_vpath[LIZ_ARCHIVE_PATH_MAX + LIZ_FS_NAME_MAX + 2];
            if (app->archive.virtual_path[0] != '\0')
                snprintf(new_vpath, sizeof(new_vpath), "%s/%s",
                         app->archive.virtual_path, e->name);
            else
                snprintf(new_vpath, sizeof(new_vpath), "%s", e->name);
            char nav[LIZ_ARCHIVE_PATH_MAX + PATH_MAX + 32];
            snprintf(nav, sizeof(nav), "archive://%s:%s",
                     app->archive.archive_path, new_vpath);
            liz_app_navigate(app, nav);
            return;
        }
        if (e->type == LIZ_FS_FILE || e->type == LIZ_FS_LINK) {
            /* extract the file to /tmp and open it */
            char tmppath[PATH_MAX];
            const char* basename = strrchr(e->name, '/');
            basename = basename ? basename + 1 : e->name;
            snprintf(tmppath, sizeof(tmppath), "/tmp/lizaveta-XXXXXX-%s", basename);
            int fd = mkstemp(tmppath);
            if (fd >= 0) {
                close(fd);
                unlink(tmppath);
            }
            char entry_full[LIZ_ARCHIVE_PATH_MAX + LIZ_FS_NAME_MAX + 2];
            if (app->archive.virtual_path[0] != '\0')
                snprintf(entry_full, sizeof(entry_full), "%s/%s",
                         app->archive.virtual_path, e->name);
            else
                snprintf(entry_full, sizeof(entry_full), "%s", e->name);
            if (liz_archive_extract(app->archive.archive_path, entry_full,
                                    "/tmp") == 0) {
                /* mkstemp created the file, but extract might have created
                 * a different name; find the extracted file */
                char extracted[PATH_MAX];
                snprintf(extracted, sizeof(extracted), "/tmp/%s", basename);
                liz_app_open_file(app, extracted);
            }
            return;
        }
    }

    /* not inside an archive: check if this is an archive file to open */
    if (!app->archive.inside
        && (e->type == LIZ_FS_FILE || e->type == LIZ_FS_LINK)
        && liz_archive_is(e->name)) {
        char path[PATH_MAX];
        if (liz_fs_join(path, sizeof(path), app->cwd, e->name) != 0)
            return;
        /* open the archive as a virtual directory */
        char nav[LIZ_ARCHIVE_PATH_MAX + PATH_MAX + 32];
        snprintf(nav, sizeof(nav), "archive://%s:", path);
        liz_app_navigate(app, nav);
        return;
    }
#endif

    char path[PATH_MAX];
    if (liz_fs_join(path, sizeof(path), app->cwd, e->name) != 0)
        return;

    if (e->type == LIZ_FS_DIR) {
        liz_app_navigate(app, path);
        return;
    }

    if (e->type == LIZ_FS_LINK) {
        /* resolve the link: dirs navigate, anything else opens */
        char canon[PATH_MAX];
        if (liz_fs_canonical(canon, sizeof(canon), path) == 0) {
            struct stat st;
            if (stat(canon, &st) == 0 && S_ISDIR(st.st_mode)) {
                liz_app_navigate(app, path);
                return;
            }
        }
        liz_app_open_file(app, path);
        return;
    }

    if (e->type == LIZ_FS_FILE)
        liz_app_open_file(app, path);
}

/* Double-forks a task fully detached from the file manager: the caller
 * (in the parent) returns immediately, and only the detached grandchild
 * reaches the code after the call -- setsid drops any controlling
 * terminal and stdio is pointed at /dev/null so the child's chatter never
 * reaches the app and it never becomes a zombie. Returns true only in the
 * grandchild. */
static bool liz_app_detach(void)
{
    pid_t pid = fork();
    if (pid < 0)
        return false;
    if (pid > 0)
        return false;

    setsid();
    pid_t pid2 = fork();
    if (pid2 > 0)
        _exit(0);

    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > 2)
            close(devnull);
    }
    return true;
}

/* Opens `path` with the desktop default application.
 *
 * The association database is read directly rather than deferring to
 * xdg-open, which only consults it for the handful of desktops it knows by
 * name. Under anything else it falls through to launching the web browser,
 * so opening a PDF would flash up Firefox on the way to the PDF viewer.
 * xdg-open remains the fallback for a file no application claims. */
void liz_app_open_file(liz_app* app, const char* path)
{
    (void)app;
    liz_desktop_app handler;
    bool resolved = liz_defaults_for(path, &handler);

    if (!liz_app_detach())
        return;
    if (resolved)
        liz_defaults_exec(&handler, path); /* only returns if it could not run */
    execlp("xdg-open", "xdg-open", path, (char*)NULL);
    _exit(127);
}

void liz_app_open_row_with(liz_app* app, int row, const liz_desktop_app* with)
{
    if (row < 0 || (size_t)row >= app->entry_count || !with)
        return;

    char path[PATH_MAX];
    if (liz_fs_join(path, sizeof(path), app->cwd, app->entries[row].name) != 0)
        return;

    if (!liz_app_detach())
        return;
    liz_defaults_exec(with, path);
    _exit(127);
}

/* Opens a terminal in `path`. The directory is set with chdir() in the
 * child before exec, so the terminal (and the shell it spawns) inherits it
 * directly; no `-e` command string is used, since st and friends re-join and
 * re-parse those and would mangle any quoting. */
void liz_app_open_terminal(liz_app* app, const char* path)
{
    (void)app;
    const char* term = getenv("TERMINAL");
    if (term == NULL || term[0] == '\0')
        term = "st";
    if (path == NULL || path[0] == '\0')
        return;

    if (!liz_app_detach())
        return;

    if (chdir(path) != 0)
        _exit(127);
    execlp(term, term, (char*)NULL);
    _exit(127);
}

/* Opens a second lizaveta window at `path`, using the current executable so
 * the new instance shares the running build. */
void liz_app_open_new_window(liz_app* app, const char* path)
{
    (void)app;
    if (path == NULL || path[0] == '\0')
        return;

    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0)
        return;
    exe[n] = '\0';

    if (!liz_app_detach())
        return;
    execl(exe, exe, path, (char*)NULL);
    _exit(127);
}

#ifdef ARCHIVE_SUPPORT
/* Stores the path of a pending "Extract to" chooser temp file.
 * liz_app_poll_extract checks if the chooser has finished. */
static char g_extract_temp[PATH_MAX];
char g_extract_archive[PATH_MAX];

void liz_app_poll_extract(liz_app* app)
{
    if (g_extract_temp[0] == '\0')
        return;
    struct stat st;
    if (stat(g_extract_temp, &st) != 0 || st.st_size == 0)
        return;

    FILE* f = fopen(g_extract_temp, "r");
    char dest[PATH_MAX];
    dest[0] = '\0';
    if (f) {
        if (fgets(dest, sizeof(dest), f))
            fclose(f);
        else
            fclose(f);
    }
    dest[strcspn(dest, "\r\n")] = '\0';

    unlink(g_extract_temp);
    g_extract_temp[0] = '\0';

    if (dest[0] != '\0' && g_extract_archive[0] != '\0')
        liz_archive_extract_all(g_extract_archive, dest);
    g_extract_archive[0] = '\0';

    liz_app_navigate(app, app->cwd);
}

void liz_app_open_new_chooser(liz_app* app, const char* start_dir)
{
    (void)app;
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0)
        return;
    exe[n] = '\0';

    snprintf(g_extract_temp, sizeof(g_extract_temp),
             "/tmp/lizaveta-extract-XXXXXX");
    int fd = mkstemp(g_extract_temp);
    if (fd < 0) {
        g_extract_temp[0] = '\0';
        return;
    }
    close(fd);

    if (!liz_app_detach()) {
        return;
    }
    /* in the detached child, run the filechooser */
    execl(exe, exe, "--filechooser", "--directory", "--out", g_extract_temp,
          start_dir ? start_dir : "/", (char*)NULL);
    _exit(127);
}
#endif

/* Safely unmounts a device: udisksctl when the block device is known
 * (this is what makes it "safe" -- it syncs and tells the device it may be
 * removed), with a plain umount of the mount point as a fallback. */
void liz_app_unmount_device(liz_app* app, const char* dev, const char* mountpoint)
{
    (void)app;
    if ((!dev || !dev[0]) && (!mountpoint || !mountpoint[0]))
        return;

    if (!liz_app_detach())
        return;

    if (dev && dev[0]) {
        pid_t c = fork();
        if (c == 0) {
            execlp("udisksctl", "udisksctl", "unmount", "-b", dev, (char*)NULL);
            _exit(127);
        }
        int status = 0;
        while (waitpid(c, &status, 0) < 0) { }
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
            _exit(0);
    }
    if (mountpoint && mountpoint[0]) {
        pid_t c = fork();
        if (c == 0) {
            execlp("umount", "umount", mountpoint, (char*)NULL);
            _exit(127);
        }
        while (waitpid(c, NULL, 0) < 0) { }
    }
    _exit(0);
}

/* Unmounts a GVFS-backed device (MTP phone, PTP camera). The gvfs fuse
 * tree cannot be unmounted with umount(8); gio tells the owning backend,
 * which powers the device down safely. Detached from the app. */
void liz_app_unmount_uri(liz_app* app, const char* uri)
{
    (void)app;
    if (!uri || !uri[0])
        return;
    if (!liz_app_detach())
        return;
    execlp("gio", "gio", "mount", "-u", uri, (char*)NULL);
    _exit(127);
}

/* Where liz_app_mount_device stashes its result for liz_app_poll_mount to
 * pick up once the detached mount process has finished. */
static char g_mount_temp[PATH_MAX];
static double g_mount_started_at; /* when a mount was last launched */

/* How long a mount may take before the poll loop gives up on it. MTP
 * negotiation with a phone is not instant, so this has to be generous. */
#define LIZ_MOUNT_TIMEOUT_SECS 15.0

/* The detached child appends this line after its mount tool exits, so the
 * render loop can tell "streaming output" from "tool finished". */
#define LIZ_MOUNT_DONE_MARKER "___LIZAVETA_MOUNT_DONE___"

/* Records `msg` as the current mount status to show in the status bar. A
 * trailing newline is trimmed; only the first line is kept. */
static void liz_app_set_mount_error(liz_app* app, const char* msg)
{
    app->mount_error[0] = '\0';
    if (msg && msg[0]) {
        const char* nl = strchr(msg, '\n');
        size_t len = nl ? (size_t)(nl - msg) : strlen(msg);
        while (len > 0 && (msg[len - 1] == '\n' || msg[len - 1] == '\r'))
            len--;
        if (len >= sizeof(app->mount_error))
            len = sizeof(app->mount_error) - 1;
        memcpy(app->mount_error, msg, len);
        app->mount_error[len] = '\0';
        app->mount_error_time = liz_app_now();
    } else {
        app->mount_error_time = 0;
    }
}

/* Runs `argv` (executable + arguments, NULL-terminated) in a child of the
 * already-detached mount process, with the tool's stdout+stderr appended to
 * `out` (the result file), and returns its exit status (127 when the tool
 * itself is missing). */
static int liz_mount_run_cmd(int out, char* const* argv, const char* tool)
{
    pid_t c = fork();
    if (c == 0) {
        if (out >= 0) {
            dup2(out, STDOUT_FILENO);
            dup2(out, STDERR_FILENO);
        }
        execvp(argv[0], argv);
        fprintf(stderr, "lizaveta: %s not found\n", tool);
        _exit(127);
    }
    int status = 1;
    while (waitpid(c, &status, 0) < 0) { }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

/* True when the last mount command's captured output reports the gvfs
 * "shadow mount" staleness: gio prints "Location is already mounted" for a
 * device that has no actual FUSE mount. This is the one case worth clearing
 * -- tearing an MTP session down for any other kind of failure just makes a
 * finicky phone re-enumerate. */
static bool liz_mount_says_already(void)
{
    int fd = open(g_mount_temp, O_RDONLY);
    if (fd < 0)
        return false;
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n < 0)
        return false;
    buf[n] = '\0';
    return strstr(buf, "already mounted") != NULL;
}

/* Mounts a sidebar device entry: block devices through udisksctl (which
 * also asks udisks for the chosen mount point), GVFS devices such as MTP
 * phones through `gio mount`. Detached like the unmount path; the target
 * location is written to a temp file that the render loop polls so the
 * file manager can navigate there once the mount completes. */
void liz_app_mount_device(liz_app* app, const liz_sidebar_entry* e)
{
    if (!e)
        return;
    bool block = e->dev[0] != '\0';
    if (!block && e->uri[0] == '\0')
        return;

    liz_app_set_mount_error(app, NULL);

    snprintf(g_mount_temp, sizeof(g_mount_temp), "/tmp/lizaveta-mount-XXXXXX");
    int fd = mkstemp(g_mount_temp);
    if (fd < 0) {
        g_mount_temp[0] = '\0';
        return;
    }
    /* line 1: where to navigate afterwards -- empty for block devices,
     * whose location is parsed from udisksctl's output instead */
    dprintf(fd, "%s\n", block ? "" : e->path);
    close(fd);

    g_mount_started_at = liz_app_now();

    if (!liz_app_detach())
        return;

    /* detach() pointed stdio at /dev/null; open the result file so each
     * tool child's output -- and the final completion marker -- lands there. */
    int out = open(g_mount_temp, O_WRONLY | O_APPEND);

    /* run the tool(s) as children so the grandchild can report when they
     * have actually finished -- polling the file alone races the mount,
     * which for MTP takes seconds to negotiate with the device */
    if (block) {
        char* const mount_argv[] = { "udisksctl", "mount", "-b", (char*)e->dev, NULL };
        liz_mount_run_cmd(out, mount_argv, "udisksctl");
    } else {
        char* const mount_argv[] = { "gio", "mount", (char*)e->uri, NULL };
        if (liz_mount_run_cmd(out, mount_argv, "gio") != 0) {
            /* a phone that has since locked its screen makes gvfs keep a
             * stale "shadow mount": gio reports the device as already
             * mounted even though no FUSE mount exists. Clear it and retry
             * once. Any other failure is left alone -- tearing the MTP
             * session down makes a phone that is already negotiating its
             * USB mode re-enumerate into "charging". */
            if (liz_mount_says_already()) {
                char* const unmount_argv[] = { "gio", "mount", "-u", (char*)e->uri, NULL };
                liz_mount_run_cmd(out, unmount_argv, "gio");
                liz_mount_run_cmd(out, mount_argv, "gio");
            }
        }
    }
    if (out >= 0) {
        dprintf(out, "\n%s\n", LIZ_MOUNT_DONE_MARKER);
        close(out);
    }
    _exit(0);
}

/* Navigates to the location a finished liz_app_mount_device reported:
 * either the GVFS path recorded up front or the "Mounted <dev> at <path>"
 * mount point from udisksctl's output. The temp file is only trusted once
 * the detached child has appended its completion marker; a failed or timed
 * out mount surfaces the tool's error instead of failing silently. */
static void liz_app_poll_mount(liz_app* app)
{
    if (g_mount_temp[0] == '\0')
        return;
    struct stat st;
    if (stat(g_mount_temp, &st) != 0 || st.st_size == 0)
        return; /* mount still starting up */

    char* buf = malloc((size_t)st.st_size + 1);
    if (!buf)
        return;
    FILE* f = fopen(g_mount_temp, "rb");
    if (!f) {
        free(buf);
        return;
    }
    size_t got = fread(buf, 1, (size_t)st.st_size, f);
    fclose(f);
    buf[got] = '\0';

    /* not finished yet: the child is still running the tool */
    char* done = strstr(buf, LIZ_MOUNT_DONE_MARKER);
    if (!done) {
        if (liz_app_now() - g_mount_started_at > LIZ_MOUNT_TIMEOUT_SECS) {
            liz_app_set_mount_error(app, "mount did not finish in time");
            unlink(g_mount_temp);
            g_mount_temp[0] = '\0';
        }
        free(buf);
        return;
    }
    *done = '\0';

    /* line 1 is the hint, the rest is the tool's stdout/stderr */
    char* nl = strchr(buf, '\n');
    if (nl)
        *nl = '\0';
    const char* hint = buf;
    const char* output = nl ? nl + 1 : "";

    unlink(g_mount_temp);
    g_mount_temp[0] = '\0';

    /* block devices carry no hint -- the location comes from udisksctl's
     * "Mounted <dev> at <path>." line; only the sentence-final dot is
     * stripped, never dots inside the mount point itself */
    char dest[PATH_MAX];
    if (hint[0] != '\0') {
        snprintf(dest, sizeof(dest), "%s", hint);
    } else {
        dest[0] = '\0';
        const char* at = strstr(output, " at ");
        if (at) {
            at += 4;
            size_t len = strcspn(at, "\r\n");
            char* trimmed = (char*)at;
            trimmed[len] = '\0';
            if (len > 0 && trimmed[len - 1] == '.')
                trimmed[len - 1] = '\0';
            snprintf(dest, sizeof(dest), "%s", trimmed);
        }
    }

    struct stat dst;
    if (dest[0] != '\0' && stat(dest, &dst) == 0 && S_ISDIR(dst.st_mode)) {
        liz_app_navigate(app, dest);
    } else {
        /* the mount point never appeared: surface whatever the tool said */
        const char* err = output;
        while (*err == '\n' || *err == '\r')
            err++;
        const char* shown = err[0] ? err
            : "Mount reported success but the mount point never appeared "
              "(stale gvfs session bus?)";
        liz_app_set_mount_error(app, shown);
    }

    free(buf);
}

void liz_app_go_parent(liz_app* app)
{
#ifdef ARCHIVE_SUPPORT
    if (app->archive.inside) {
        if (app->archive.virtual_path[0] == '\0') {
            /* at archive root: exit to filesystem parent */
            char parent[PATH_MAX];
            if (liz_fs_parent(parent, sizeof(parent),
                              app->archive.archive_path) == 0)
                liz_app_navigate(app, parent);
            return;
        }
        char vpath[LIZ_ARCHIVE_PATH_MAX];
        snprintf(vpath, sizeof(vpath), "%s", app->archive.virtual_path);
        char* sl = strrchr(vpath, '/');
        if (sl && sl != vpath) {
            *sl = '\0';
        } else {
            vpath[0] = '\0';
        }
        char nav[LIZ_ARCHIVE_PATH_MAX + PATH_MAX + 32];
        snprintf(nav, sizeof(nav), "archive://%s:%s",
                 app->archive.archive_path, vpath);
        liz_app_navigate(app, nav);
        return;
    }
#endif
    char parent[PATH_MAX];
    if (liz_fs_parent(parent, sizeof(parent), app->cwd) == 0)
        liz_app_navigate(app, parent);
}

void liz_app_set_selected(liz_app* app, int row)
{
    if (app->entry_count == 0) {
        app->selected = -1;
        return;
    }
    if (row < 0)
        row = 0;
    if (row >= (int)app->entry_count)
        row = (int)app->entry_count - 1;
    app->selected = row;

    /* keep the selection visible */
    liz_list_keep_selection_visible(app);
}

void liz_app_clear_selection(liz_app* app)
{
    if (!app->sel)
        return;
    memset(app->sel, 0, app->entry_count * sizeof(bool));
}

void liz_app_select_range(liz_app* app, int a, int b)
{
    if (!app->sel || app->entry_count == 0)
        return;
    if (a > b) {
        int t = a;
        a = b;
        b = t;
    }
    if (a < 0)
        a = 0;
    if (b >= (int)app->entry_count)
        b = (int)app->entry_count - 1;
    for (int i = a; i <= b; i++)
        app->sel[i] = true;
}

void liz_app_toggle_selection(liz_app* app, int row)
{
    if (!app->sel || row < 0 || (size_t)row >= app->entry_count)
        return;
    app->sel[row] = !app->sel[row];
}

int liz_app_selection_count(const liz_app* app)
{
    if (!app->sel)
        return 0;
    int n = 0;
    for (size_t i = 0; i < app->entry_count; i++) {
        if (app->sel[i])
            n++;
    }
    return n;
}

bool liz_app_row_selected(const liz_app* app, int row)
{
    if (!app->sel || row < 0 || (size_t)row >= app->entry_count)
        return false;
    return app->sel[row];
}

int liz_app_collect_selection(liz_app* app, int* rows, int cap)
{
    int n = 0;
    if (app->sel) {
        for (size_t i = 0; i < app->entry_count && n < cap; i++) {
            if (app->sel[i])
                rows[n++] = (int)i;
        }
    }
    if (n == 0 && app->selected >= 0 && (size_t)app->selected < app->entry_count) {
        rows[0] = app->selected;
        n = 1;
    }
    return n;
}

static void liz_app_clip_clear(liz_app* app)
{
    if (app->fileclip.paths) {
        for (int i = 0; i < app->fileclip.count; i++)
            free(app->fileclip.paths[i]);
        free(app->fileclip.paths);
    }
    app->fileclip.paths = NULL;
#ifdef ARCHIVE_SUPPORT
    if (app->fileclip.is_virtual) {
        free(app->fileclip.is_virtual);
        app->fileclip.is_virtual = NULL;
    }
    if (app->fileclip.archive_paths) {
        for (int i = 0; i < app->fileclip.count; i++)
            free(app->fileclip.archive_paths[i]);
        free(app->fileclip.archive_paths);
        app->fileclip.archive_paths = NULL;
    }
#endif
    app->fileclip.count = 0;
}

/* Stages the selection as a copy or a cut. Paths are absolute, so the
 * clipboard survives navigation. */
static void liz_app_clip_set(liz_app* app, bool cut)
{
    int rows[4096];
    int n = liz_app_collect_selection(app, rows, 4096);
    if (n == 0)
        return;

    char** paths = (char**)malloc((size_t)n * sizeof(char*));
    if (!paths)
        return;
    int count = 0;
#ifdef ARCHIVE_SUPPORT
    bool* is_virtual = NULL;
    char** archive_paths = NULL;
    bool has_virtual = app->archive.inside;
    if (has_virtual) {
        is_virtual = (bool*)calloc((size_t)n, sizeof(bool));
        archive_paths = (char**)calloc((size_t)n, sizeof(char*));
    }
#endif
    for (int i = 0; i < n; i++) {
        char path[PATH_MAX];
        if (liz_fs_join(path, sizeof(path), app->cwd, app->entries[rows[i]].name) != 0)
            continue;
        paths[count] = strdup(path);
        if (!paths[count])
            continue;
#ifdef ARCHIVE_SUPPORT
        if (has_virtual && is_virtual && archive_paths) {
            is_virtual[count] = true;
            archive_paths[count] = strdup(app->archive.archive_path);
        }
#endif
        count++;
    }
    if (count == 0) {
        free(paths);
#ifdef ARCHIVE_SUPPORT
        free(is_virtual);
        free(archive_paths);
#endif
        return;
    }

    liz_app_clip_clear(app);
    app->fileclip.paths = paths;
    app->fileclip.count = count;
    app->fileclip.cut = cut;
#ifdef ARCHIVE_SUPPORT
    app->fileclip.is_virtual = is_virtual;
    app->fileclip.archive_paths = archive_paths;
#endif
}

void liz_app_copy_selection(liz_app* app)
{
    liz_app_clip_set(app, false);
}

void liz_app_cut_selection(liz_app* app)
{
    liz_app_clip_set(app, true);
}

/* Pastes the staged entries into the current directory. Existing targets are
 * skipped (never overwritten). A successful cut clears the clipboard; a copy
 * stays available for repeated pastes. */
void liz_app_paste(liz_app* app)
{
    if (app->fileclip.count == 0)
        return;

    bool ok = true;
    for (int i = 0; i < app->fileclip.count; i++) {
        const char* src = app->fileclip.paths[i];
        const char* base = strrchr(src, '/');
        base = (base && base[1]) ? base + 1 : src;

        char dst[PATH_MAX];
        if (liz_fs_join(dst, sizeof(dst), app->cwd, base) != 0) {
            ok = false;
            continue;
        }
        if (access(dst, F_OK) == 0) {
            ok = false;
            continue;
        }

#ifdef ARCHIVE_SUPPORT
        if (app->fileclip.is_virtual && app->fileclip.is_virtual[i]
            && app->fileclip.archive_paths && app->fileclip.archive_paths[i]) {
            /* extract from archive to the current directory */
            const char* archive_path = app->fileclip.archive_paths[i];
            /* src is like "archive:///path/to/archive.zip:dir/file.txt" */
            const char* archive_marker = strstr(src, "archive://");
            if (archive_marker) {
                const char* inner = src + 10; /* skip "archive://" */
                const char* colon = strchr(inner, ':');
                if (colon) {
                    const char* internal = colon + 1;
                    if (liz_archive_extract(archive_path, internal,
                                            app->cwd) != 0)
                        ok = false;
                } else {
                    ok = false;
                }
            }
        } else {
#endif
        int r = app->fileclip.cut ? rename(src, dst) : liz_fs_copy_recursive(src, dst);
        if (r != 0)
            ok = false;
#ifdef ARCHIVE_SUPPORT
        }
#endif
    }

    if (app->fileclip.cut && ok)
        liz_app_clip_clear(app);

    liz_app_navigate(app, app->cwd);
}

/* True for a keysym that is itself a modifier (Shift/Control/Alt/Super/...).
 * X11 delivers these as their own KeyPress events, separate from the key
 * they modify, and *before* the modifier bit shows up in that event's
 * `state` (state reflects modifiers held down *prior* to this key). Every
 * mode below (VISUAL, COMMAND, rename, ...) treats an unrecognized key as
 * "end the mode" -- without this guard, holding Ctrl to press Ctrl+C would
 * first deliver a bare Control_L press that VISUAL mode doesn't recognize,
 * exiting VISUAL and clearing the selection before the real Ctrl+C event
 * (the one carrying ControlMask) ever arrives. */
static bool liz_key_is_modifier(KeySym key)
{
    switch (key) {
    case XK_Shift_L:
    case XK_Shift_R:
    case XK_Control_L:
    case XK_Control_R:
    case XK_Alt_L:
    case XK_Alt_R:
    case XK_Meta_L:
    case XK_Meta_R:
    case XK_Super_L:
    case XK_Super_R:
    case XK_Hyper_L:
    case XK_Hyper_R:
    case XK_Caps_Lock:
    case XK_Shift_Lock:
    case XK_Num_Lock:
    case XK_Scroll_Lock:
    case XK_ISO_Level3_Shift:
    case XK_ISO_Level5_Shift:
        return true;
    default:
        return false;
    }
}

/* Requests the window to close: flags the app as finished and stops the
 * X11 run loop, exactly like the WM_DELETE_WINDOW XC_EVENT_CLOSE path. */
static void liz_app_close(liz_app* app)
{
    app->quit = true;
    app->win->running = false;
}

/* Execute a resolved action. Returns true if the action was handled. */
static bool liz_action_handle(liz_app* app, enum liz_action action)
{
    switch (action) {
    case LIZ_ACTION_QUIT:
        liz_app_close(app);
        return true;
    case LIZ_ACTION_NAV_EDIT:
        liz_nav_toggle_edit(app);
        return true;
    case LIZ_ACTION_TOGGLE_HIDDEN:
        liz_app_toggle_hidden(app);
        return true;
    case LIZ_ACTION_TOGGLE_SIDEBAR:
        liz_app_toggle_sidebar(app);
        return true;
    case LIZ_ACTION_HALF_DOWN: {
        if (app->vim.visual_active)
            return false;
        int n = liz_list_visible_count(app) / 2;
        if (n < 1)
            n = 1;
        liz_app_set_selected(app, app->selected + n);
        return true;
    }
    case LIZ_ACTION_HALF_UP: {
        if (app->vim.visual_active)
            return false;
        int n = liz_list_visible_count(app) / 2;
        if (n < 1)
            n = 1;
        liz_app_set_selected(app, app->selected - n);
        return true;
    }
    case LIZ_ACTION_HISTORY_BACK:
        liz_app_jump_back(app);
        return true;
    case LIZ_ACTION_HISTORY_FORWARD:
        liz_app_jump_fwd(app);
        return true;
    case LIZ_ACTION_COPY:
        if (app->nav_input.editing)
            return false;
        liz_app_copy_selection(app);
        return true;
    case LIZ_ACTION_CUT:
        if (app->nav_input.editing)
            return false;
        liz_app_cut_selection(app);
        return true;
    case LIZ_ACTION_PASTE:
        if (app->nav_input.editing)
            return false;
        liz_app_paste(app);
        return true;
    case LIZ_ACTION_PREVIEW:
        liz_preview_toggle(app);
        return true;
    case LIZ_ACTION_NEW_FOLDER:
        liz_newfolder_start(app);
        return true;
    case LIZ_ACTION_OPEN_TERMINAL:
        liz_app_open_terminal(app, app->cwd);
        return true;
    case LIZ_ACTION_OPEN_TERMINAL_DIR: {
        liz_fs_entry* e = (app->selected >= 0
                          && (size_t)app->selected < app->entry_count)
                             ? &app->entries[app->selected]
                             : NULL;
        if (e != NULL && e->type == LIZ_FS_DIR) {
            char path[PATH_MAX];
            if (liz_fs_join(path, sizeof(path), app->cwd, e->name) == 0)
                liz_app_open_terminal(app, path);
        }
        return true;
    }
    case LIZ_ACTION_RENAME:
        liz_rename_start(app);
        return true;
    case LIZ_ACTION_GO_HOME:
        liz_app_go_home(app);
        return true;
    case LIZ_ACTION_CLOSE_PREVIEW:
        liz_preview_close(app);
        return true;
    case LIZ_ACTION_DELETE:
        /* the standard file-manager Delete key: delete the selection, or
         * the focused row when there is none (same as vim's Delete) */
        if (liz_app_selection_count(app) > 0)
            liz_delete_start_selection(app);
        else
            liz_delete_start_range(app, app->selected, app->selected);
        return true;
    case LIZ_ACTION_NONE:
        break;
    }
    return false;
}

/* --- non-vim mode: incremental type-to-search --------------------------- */

static bool liz_novim_ci_contains(const char* hay, const char* needle)
{
    size_t hn = strlen(hay), nn = strlen(needle);
    if (nn > hn)
        return false;
    for (size_t i = 0; i + nn <= hn; i++) {
        size_t j = 0;
        for (; j < nn; j++) {
            if (tolower((unsigned char)hay[i + j]) != tolower((unsigned char)needle[j]))
                break;
        }
        if (j == nn)
            return true;
    }
    return false;
}

/* First row at or after `start` (wrapping) whose name contains `query`, or
 * -1 when there are no entries or no match anywhere. */
static int liz_novim_find_first(liz_app* app, const char* query, int start)
{
    int n = (int)app->entry_count;
    if (n == 0 || query[0] == '\0')
        return -1;
    for (int off = 0; off < n; off++) {
        int i = (start + off) % n;
        if (i < 0)
            i += n;
        if (liz_novim_ci_contains(app->entries[i].name, query))
            return i;
    }
    return -1;
}

/* Re-runs the search from the anchor, jumping the selection live to the
 * first match; clears the search once the query is emptied out. */
static void liz_novim_search_preview(liz_app* app)
{
    liz_novim_search* s = &app->novim_search;
    if (s->query[0] == '\0') {
        s->active = false;
        liz_app_set_selected(app, s->anchor);
        return;
    }
    s->active = true;
    int idx = liz_novim_find_first(app, s->query, s->anchor);
    if (idx >= 0)
        liz_app_set_selected(app, idx);
}

static void liz_novim_search_clear(liz_app* app)
{
    liz_novim_search* s = &app->novim_search;
    s->active = false;
    s->query[0] = '\0';
    liz_app_set_selected(app, s->anchor);
}

static void liz_novim_search_backspace(liz_app* app)
{
    liz_novim_search* s = &app->novim_search;
    int len = (int)strlen(s->query);
    if (len == 0)
        return;
    int i = len;
    do {
        i--;
    } while (i > 0 && ((s->query[i] & 0xC0) == 0x80));
    s->query[i] = '\0';
    liz_novim_search_preview(app);
}

/* Handles the non-vim mode keys: every printable keystroke feeds the
 * incremental search, and Escape/BackSpace clear it while it is active.
 * Returns true if the key was consumed. */
static bool liz_novim_handle_key(liz_app* app, xc_event ev)
{
    liz_novim_search* s = &app->novim_search;

    if (ev.key == XK_Escape) {
        if (s->active) {
            liz_novim_search_clear(app);
            return true;
        }
        return false; /* let CLOSE_PREVIEW / other plain bindings run */
    }

    if (ev.key == XK_BackSpace) {
        if (!s->active)
            return false; /* plain BackSpace goes to the parent directory */
        if (s->query[0] != '\0')
            liz_novim_search_backspace(app);
        else
            liz_novim_search_clear(app);
        return true;
    }

    /* printable text starts or extends the search */
    if (ev.nchars > 0 && (unsigned char)ev.chars[0] >= 0x20 && ev.chars[0] != 0x7F) {
        if (!s->active) {
            s->active = true;
            s->anchor = app->selected;
            s->query[0] = '\0';
        }
        int len = (int)strlen(s->query);
        if (len + ev.nchars < (int)sizeof(s->query)) {
            memcpy(s->query + len, ev.chars, (size_t)ev.nchars);
            s->query[len + ev.nchars] = '\0';
            liz_novim_search_preview(app);
        }
        return true;
    }

    return false;
}

static void liz_app_handle_key(liz_app* app, xc_event ev)
{
    /* a bare modifier key press/release carries no action anywhere; ignore
     * it before any mode gets a chance to treat it as "end the mode" */
    if (liz_key_is_modifier(ev.key))
        return;

    const struct liz_keybind* binds =
        app->vim_mode ? liz_keybinds : liz_novim_keybinds;
    int bind_count =
        app->vim_mode ? LIZ_KEYBIND_COUNT : LIZ_NOVIM_KEYBIND_COUNT;

    enum liz_action action = liz_keybind_resolve(ev.key, ev.state, binds, bind_count);

    /* quit works from absolutely anywhere */
    if (action == LIZ_ACTION_QUIT) {
        liz_app_close(app);
        return;
    }

    /* a context menu swallows the key that dismisses it */
    if (app->menu.active) {
        liz_menu_handle_key(app, ev);
        return;
    }

    /* an in-progress "new folder" prompt captures all input until
     * committed or cancelled */
    if (app->newfolder.active) {
        liz_newfolder_handle_key(app, ev);
        return;
    }

    /* an in-progress rename captures all input until committed or cancelled */
    if (app->rename.active) {
        liz_rename_handle_key(app, ev);
        return;
    }

    /* an in-progress delete confirmation captures all input until yes/no */
    if (app->del.active) {
        liz_delete_handle_key(app, ev);
        return;
    }

    /* Ctrl/Alt/Super shortcuts fire before nav/chooser/vim so they work
     * from any non-modal context.  COPY/CUT/PASTE return false when
     * nav_input.editing is true, letting the nav handler take over. */
    if (ev.state & (ControlMask | Mod1Mask | Mod4Mask)) {
        app->vim.pending_g = false;
        app->vim.pending_d = false;
        if (action != LIZ_ACTION_NONE && liz_action_handle(app, action))
            return;
    }

    /* location bar editing captures all input until committed or cancelled */
    if (app->nav_input.editing) {
        liz_nav_handle_key(app, ev);
        return;
    }

    /* file-picker mode intercepts its confirm/cancel keys and prompts */
    if (app->chooser.active) {
        if (liz_chooser_handle_key(app, ev))
            return;
    }

    if (app->vim_mode) {
        /* VISUAL mode captures movement keys; keys it does not handle exit
         * VISUAL and fall through to normal handling */
        if (app->vim.visual_active) {
            if (liz_vim_handle_visual_key(app, ev))
                return;
        }

        /* COMMAND mode (typing a search or an ex command) captures all input
         * until submitted or cancelled; nothing else in this function should
         * run. */
        if (app->vim.mode == LIZ_VIM_COMMAND) {
            liz_vim_handle_command_key(app, ev);
            return;
        }

        /* NORMAL mode: vim motions/commands first, then the remaining
         * configurable plain-key bindings. */
        if (liz_vim_handle_normal_key(app, ev))
            return;

        if (action != LIZ_ACTION_NONE && liz_action_handle(app, action))
            return;
    } else {
        /* non-vim mode: printable keys feed the incremental search, with
         * Escape/BackSpace clearing it; the (exclusively modifier/f-key)
         * bindings then run. */
        if (liz_novim_handle_key(app, ev))
            return;

        if (action != LIZ_ACTION_NONE && liz_action_handle(app, action))
            return;
    }

    switch (ev.key) {
    case XK_Down:
        liz_app_set_selected(app, app->selected + 1);
        break;
    case XK_Up:
        liz_app_set_selected(app, app->selected - 1);
        break;
    case XK_Page_Down:
        liz_app_set_selected(app, app->selected + liz_list_visible_count(app));
        break;
    case XK_Page_Up:
        liz_app_set_selected(app, app->selected - liz_list_visible_count(app));
        break;
    case XK_Home:
        liz_app_set_selected(app, 0);
        break;
    case XK_End:
        liz_app_set_selected(app, (int)app->entry_count - 1);
        break;
    case XK_Return:
    case XK_Right:
        liz_app_open_row(app, app->selected);
        break;
    case XK_BackSpace:
    case XK_Left:
        liz_app_go_parent(app);
        break;
    default:
        break;
    }
}

static void liz_app_handle_button(liz_app* app, xc_event ev)
{
    /* a context menu swallows every click while it's open: on target,
     * run the item; off target, just dismiss */
    if (app->menu.active) {
        liz_menu_handle_button(app, ev);
        return;
    }

    /* any click dismisses a pending delete confirmation */
    if (app->del.active) {
        liz_delete_cancel(app);
        return;
    }

    /* mouse wheel */
    if (ev.button == 4) {
        liz_list_scroll(app, -3);
        return;
    }
    if (ev.button == 5) {
        liz_list_scroll(app, 3);
        return;
    }

    if (ev.button == 3) {
        /* right click: dismiss any in-progress text prompt first (same as
         * a left click outside it), then open the context menu for whatever
         * was clicked. A menu only makes sense over an actual item; on
         * empty list space it still offers Create folder / Show hidden
         * files, matching most file managers. */
        if (app->rename.active)
            liz_rename_cancel(app);
        if (app->newfolder.active)
            liz_newfolder_cancel(app);
        if (app->nav_input.editing)
            app->nav_input.editing = false;

        /* right-click on the sidebar gets the panel's own menu: unmount
         * for devices, open in new window, hide/show the panel */
        if (app->sidebar_visible && ev.x < LIZ_UI_SIDEBAR_W) {
            int idx = liz_sidebar_item_at(app, ev.y);
            if (idx >= 0) {
                bool is_device = idx >= app->sidebar.pinned_count;
                int sidx = is_device ? idx - app->sidebar.pinned_count : idx;
                liz_menu_open_sidebar(app, ev.x, ev.y, sidx, is_device);
                return;
            }
            return; /* empty sidebar space: no menu */
        }

        int row = liz_list_row_at(app, ev.y);
        liz_menu_open(app, ev.x, ev.y, row);
        return;
    }

    if (ev.button != 1)
        return;

    /* an active rename is dismissed by any click outside the status bar;
     * clicking inside it places the cursor / starts a selection drag */
    if (app->rename.active) {
        if (ev.y >= app->win->height - LIZ_UI_STATUS_H) {
            liz_rename_click(app, ev.x);
            return;
        }
        liz_rename_cancel(app);
        return;
    }

    /* clicking inside the location bar places the cursor; clicking anywhere
     * else leaves editing without navigating */
    if (app->nav_input.editing) {
        if (ev.y >= 0 && ev.y < LIZ_UI_NAV_H) {
            liz_nav_edit_click(app, ev.x);
            return;
        }
        app->nav_input.editing = false;
    }

    /* navigation bar breadcrumbs */
    int seg = liz_nav_hit(app, ev.x, ev.y);
    if (seg >= 0) {
#ifdef ARCHIVE_SUPPORT
        if (app->archive.inside && strncmp(app->cwd, "archive://", 10) == 0) {
            const char* inner = app->cwd + 10;
            const char* colon = strchr(inner, ':');
            if (colon) {
                size_t alen = (size_t)(colon - inner);
                char apath[PATH_MAX];
                if (alen >= sizeof(apath))
                    alen = sizeof(apath) - 1;
                memcpy(apath, inner, alen);
                apath[alen] = '\0';
                const char* vpath = colon + 1;
                size_t vplen = strlen(vpath);
                size_t fs_end = 10 + alen;

                size_t end = app->nav_sg[seg].end;

                if (end == 0) {
                    /* ".." segment at archive root: exit to filesystem parent */
                    char parent[PATH_MAX];
                    if (liz_fs_parent(parent, sizeof(parent), apath) == 0)
                        liz_app_navigate(app, parent);
                } else if (end == fs_end) {
                    /* archive filename: navigate to the archive root */
                    char nav[LIZ_ARCHIVE_PATH_MAX + PATH_MAX + 32];
                    snprintf(nav, sizeof(nav), "archive://%s:", apath);
                    liz_app_navigate(app, nav);
                } else if (end < fs_end) {
                    /* filesystem path segment: navigate to that path */
                    char path[PATH_MAX];
                    size_t raw = end - 10; /* offset into inner[] */
                    if (raw <= alen && raw < sizeof(path)) {
                        memcpy(path, inner, raw);
                        path[raw] = '\0';
                        liz_app_navigate(app, path);
                    }
                } else if (vplen > 0) {
                    /* virtual path segment: navigate within the archive */
                    size_t voff = end - fs_end - 1;
                    if (voff <= vplen) {
                        char nav[LIZ_ARCHIVE_PATH_MAX + PATH_MAX + 32];
                        snprintf(nav, sizeof(nav), "archive://%.*s:%.*s",
                                 (int)alen, apath, (int)voff, vpath);
                        liz_app_navigate(app, nav);
                    }
                }
                return;
            }
        }
#endif

        size_t end = app->nav_sg[seg].end;
        if (end + 1 <= sizeof(app->cwd)) {
            char path[PATH_MAX];
            memcpy(path, app->cwd, end);
            path[end] = '\0';
            liz_app_navigate(app, path);
        }
        return;
    }

    /* sidebar quick links */
    if (liz_sidebar_click(app, ev.x, ev.y))
        return;

    /* file list */
    int row = liz_list_row_at(app, ev.y);
    if (row < 0) {
        app->press_row = -1;
        return;
    }

    /* any list click ends a vim VISUAL selection */
    app->vim.visual_active = false;
    app->vim.pending_g = false;

    app->selected = row;

    /* arm press/drag tracking; the actual "open on double click" decision
     * is made on release (see liz_app_handle_button_release), once we know
     * whether this turned into a drag */
    app->mouse_down = true;
    app->press_row = row;
    app->press_x = ev.x;
    app->press_y = ev.y;
    app->dragging = false;
    app->press_defer_collapse = false;
    app->press_plain = (ev.state & (ShiftMask | ControlMask)) == 0;

    if (ev.state & ShiftMask) {
        if (ev.state & ControlMask) {
            /* add the range anchor..row to the selection */
            liz_app_select_range(app, app->anchor_row, row);
        } else {
            /* Shift+Click: toggle this entry in the selection */
            liz_app_toggle_selection(app, row);
        }
    } else if (liz_app_row_selected(app, row) && liz_app_selection_count(app) > 1) {
        /* clicking inside an existing multi-selection: leave the whole
         * selection intact so a drag from here (see the MOTION handler)
         * carries all of it. Only collapse to just this row on release,
         * and only if it turns out to be a plain click rather than a
         * drag -- exactly like Nautilus/Thunar. */
        app->press_defer_collapse = true;
    } else {
        /* plain click on a row outside any multi-selection: select just
         * this row immediately, same as before */
        liz_app_clear_selection(app);
        if (app->sel)
            app->sel[row] = true;
        app->anchor_row = row;
    }
}

/* Builds the absolute path list for the current selection and runs the
 * XDND source loop. Blocks until the drag ends (dropped, rejected, or
 * cancelled with Escape); see xc_dnd_begin. */
static void liz_app_start_drag(liz_app* app)
{
    int rows[LIZ_DELETE_MAX];
    int n = liz_app_collect_selection(app, rows, LIZ_DELETE_MAX);
    if (n <= 0)
        return;

    char** paths = (char**)malloc(sizeof(char*) * (size_t)n);
    if (!paths)
        return;

    int m = 0;
    for (int i = 0; i < n; i++) {
        char path[PATH_MAX];
        if (liz_fs_join(path, sizeof(path), app->cwd, app->entries[rows[i]].name) != 0)
            continue;
        paths[m] = strdup(path);
        if (paths[m])
            m++;
    }

    if (m > 0) {
        app->dragging = true;
        app->press_defer_collapse = false; /* a drag always keeps the selection as-is */
        xc_dnd_begin(app->win, paths, m);
    }

    for (int i = 0; i < m; i++)
        free(paths[i]);
    free(paths);

    /* xc_dnd_begin's own nested loop already consumed the ButtonRelease
     * that ended the drag, so no XC_EVENT_BUTTON_RELEASE will follow for
     * this press -- reset the press/drag state here instead. */
    app->mouse_down = false;
    app->dragging = false;
}

/* Directory row under the pointer, or -1 when not over one. */
static int liz_app_dnd_row_at(liz_app* app, int ly)
{
    int row = liz_list_row_at(app, ly);
    if (row >= 0 && app->entries[row].type == LIZ_FS_DIR)
        return row;
    return -1;
}

static void liz_app_dnd_enter(void* data, int x_root, int y_root)
{
    liz_app* app = (liz_app*)data;
    app->dnd_active = false;
    app->dnd_row = -1;
    int lx, ly;
    if (xc_translate(app->win, x_root, y_root, &lx, &ly)) {
        app->dnd_x = lx;
        app->dnd_y = ly;
    }
}

static void liz_app_dnd_position(void* data, int x_root, int y_root, bool* accept)
{
    liz_app* app = (liz_app*)data;
    int lx, ly;
    if (!xc_translate(app->win, x_root, y_root, &lx, &ly)) {
        *accept = false;
        return;
    }
    app->dnd_x = lx;
    app->dnd_y = ly;

    /* only drops over the file list are accepted */
    if (lx < liz_list_area_left(app) || ly < LIZ_UI_NAV_H
        || ly >= app->win->height - LIZ_UI_STATUS_H) {
        *accept = false;
        app->dnd_active = false;
        app->dnd_row = -1;
        liz_app_render(app);
        return;
    }

#ifdef ARCHIVE_SUPPORT
    /* drops inside an archive are not supported */
    if (app->archive.inside) {
        *accept = false;
        app->dnd_active = false;
        app->dnd_row = -1;
        liz_app_render(app);
        return;
    }
#endif

    *accept = true;
    app->dnd_active = true;
    app->dnd_row = liz_app_dnd_row_at(app, ly);
    liz_app_render(app);
}

static void liz_app_dnd_leave(void* data)
{
    liz_app* app = (liz_app*)data;
    app->dnd_active = false;
    app->dnd_row = -1;
    liz_app_render(app);
}

/* Copies the dropped files into the current directory, or into the
 * directory row under the pointer, then refreshes the listing. Existing
 * targets are skipped, matching the paste behavior. */
static void liz_app_dnd_drop(void* data, char* const* paths, int count,
                            int x_root, int y_root)
{
    liz_app* app = (liz_app*)data;
    (void)x_root;
    (void)y_root;
    app->dnd_active = false;
    int target_row = app->dnd_row;
    app->dnd_row = -1;

    char target[PATH_MAX];
    bool into_row = target_row >= 0
                    && liz_fs_join(target, sizeof(target), app->cwd,
                                  app->entries[target_row].name) == 0;
    if (!into_row)
        snprintf(target, sizeof(target), "%s", app->cwd);

    for (int i = 0; i < count; i++) {
        const char* src = paths[i];
        const char* base = strrchr(src, '/');
        base = (base && base[1]) ? base + 1 : src;

        char dst[PATH_MAX];
        if (liz_fs_join(dst, sizeof(dst), target, base) != 0)
            continue;
        if (access(dst, F_OK) == 0)
            continue;

#ifdef ARCHIVE_SUPPORT
        if (strncmp(src, "archive://", 10) == 0) {
            const char* inner = src + 10;
            const char* colon = strchr(inner, ':');
            if (colon) {
                size_t alen = (size_t)(colon - inner);
                char apath[PATH_MAX];
                if (alen >= sizeof(apath))
                    alen = sizeof(apath) - 1;
                memcpy(apath, inner, alen);
                apath[alen] = '\0';
                const char* internal = colon + 1;
                (void)liz_archive_extract(apath, internal, target);
            }
            continue;
        }
#endif
        (void)liz_fs_copy_recursive(src, dst);
    }

    liz_app_navigate(app, target);
    liz_app_render(app);
}

/* Ends the current Button1 press: distinguishes a plain click (open on
 * double click, or apply a deferred selection collapse) from a drag
 * (nothing left to do -- xc_dnd_begin already handled it) or a release
 * past the drag threshold with no recognized drop target. */
static void liz_app_handle_button_release(liz_app* app, xc_event ev)
{
    if (ev.button != 1 || !app->mouse_down)
        return;
    app->mouse_down = false;

    if (app->dragging) {
        /* safety net; xc_dnd_begin normally resets this itself */
        app->dragging = false;
        return;
    }

    if (app->press_row < 0)
        return;

    int dx = ev.x - app->press_x;
    int dy = ev.y - app->press_y;
    bool moved = (dx * dx + dy * dy) > (LIZ_DRAG_THRESHOLD_PX * LIZ_DRAG_THRESHOLD_PX);
    int row = liz_list_row_at(app, ev.y);

    if (moved || row != app->press_row) {
        app->press_defer_collapse = false;
        return;
    }

    if (app->press_defer_collapse) {
        liz_app_clear_selection(app);
        if (app->sel)
            app->sel[row] = true;
        app->anchor_row = row;
        app->press_defer_collapse = false;
    }

    if (!app->press_plain)
        return; /* shift/ctrl clicks already applied their change at press time */

    double now = liz_app_now();
    bool is_double = row == app->last_click_row
                   && (now - app->last_click_time) <= LIZ_DOUBLE_CLICK_SECS;
    if (is_double) {
        liz_app_open_row(app, row);
        app->last_click_row = -1;
    } else {
        app->last_click_row = row;
        app->last_click_time = now;
    }
}

void liz_app_handle_event(liz_app* app, xc_event ev)
{
    switch (ev.type) {
    case XC_EVENT_CLOSE:
        liz_app_close(app);
        return;
    case XC_EVENT_KEY:
        liz_app_handle_key(app, ev);
        break;
    case XC_EVENT_BUTTON:
        liz_app_handle_button(app, ev);
        break;
    case XC_EVENT_BUTTON_RELEASE:
        liz_app_handle_button_release(app, ev);
        break;
    case XC_EVENT_MOTION:
        app->mouse_x = ev.x;
        app->mouse_y = ev.y;
        if (app->menu.active) {
            liz_menu_handle_motion(app, ev);
            break;
        }
        /* text selection drags in the status bar (rename) and nav bar */
        if (ev.state & Button1Mask) {
            if (app->rename.active
                && app->mouse_y >= app->win->height - LIZ_UI_STATUS_H) {
                liz_rename_drag(app, ev.x);
            } else if (app->nav_input.editing && app->mouse_y < LIZ_UI_NAV_H) {
                liz_nav_edit_drag(app, ev.x);
            } else if (app->mouse_down && !app->dragging && app->press_plain
                       && app->press_row >= 0) {
                /* plain-click drag-out of the file list: once the pointer
                 * has moved far enough from the press point, this becomes
                 * an XDND drag instead of a click */
                int dx = ev.x - app->press_x;
                int dy = ev.y - app->press_y;
                if (dx * dx + dy * dy > LIZ_DRAG_THRESHOLD_PX * LIZ_DRAG_THRESHOLD_PX)
                    liz_app_start_drag(app);
            }
        }
        break;
    default:
        break;
    }

    liz_app_render(app);
}

/* Adapter between xc's (xc_event, void*) callback and the app dispatcher. */
static void liz_app_on_event(xc_event ev, void* data)
{
    liz_app_handle_event((liz_app*)data, ev);
}

/* Adapter for the XDND drag repaint hook. */
static void liz_app_dnd_repaint(void* data)
{
    liz_app_render((liz_app*)data);
}

/* Asynchronous CLIPBOARD paste result: the text lands in whichever editor
 * is active, rename first, then the location bar. */
static void liz_app_on_clipboard(xwindow* w, const char* text, int len, void* userdata)
{
    (void)w;
    liz_app* app = (liz_app*)userdata;
    if (app->rename.active) {
        liz_editor_paste_text(&app->rename.ed, text, len);
        app->rename.err[0] = '\0';
    } else if (app->nav_input.editing) {
        liz_editor_paste_text(&app->nav_input.ed, text, len);
        liz_nav_refresh_complete(app);
    } else {
        return;
    }
    liz_app_render(app);
}

void liz_app_render(liz_app* app)
{
    xwindow* w = app->win;

#ifdef ARCHIVE_SUPPORT
    liz_app_poll_extract(app);
#endif

    /* pick up locations reported by finished mount operations */
    liz_app_poll_mount(app);

    /* keep the embedded preview pane in step with the selection */
    liz_preview_sync(app);

    /* refresh hover from the last known pointer position */
    app->hover_row = -1;
    if (app->mouse_x >= liz_list_area_left(app)
        && app->mouse_y >= LIZ_UI_NAV_H && app->mouse_y < w->height - LIZ_UI_STATUS_H) {
        int row = liz_list_row_at(app, app->mouse_y);
        if (row >= 0)
            app->hover_row = row;
    }

    xc_clear(w);
    liz_nav_draw(app);
    liz_sidebar_draw(app);
    liz_list_draw(app);
    liz_status_draw(app);
    liz_menu_draw(app); /* drawn last so it sits on top of everything else */
    xc_flip(w);
}

int liz_app_init(liz_app* app)
{
    memset(app, 0, sizeof(*app));
    app->last_list_visible = -1;

    /* translucent background: fold the configured opacity into the theme's
     * background alpha; xc_window_create picks an ARGB visual for it */
    xc_color bg = liz_theme_bg;
    bg.a = (uint8_t)(LIZ_BG_OPACITY * 255.0 + 0.5);
    xwindow* win = xc_window_create(120, 120, 900, 600, bg, "lizaveta");
    if (win)
        xc_set_class(win, "lizaveta", "lizaveta");
    if (!win)
        return -1;
#if LIZ_BG_BLUR
    /* the blur hint only matters on a window that actually has an ARGB
     * visual; the server may have fallen back to an opaque one */
    if (win->depth == 32)
        xc_set_blur_behind(win, true);
#endif
    app->win = win;
    win->events = liz_app_on_event;
    win->userdata = app;
    win->on_clipboard = liz_app_on_clipboard;
    win->dnd_repaint = liz_app_dnd_repaint;
    win->on_dnd_enter = liz_app_dnd_enter;
    win->on_dnd_position = liz_app_dnd_position;
    win->on_dnd_leave = liz_app_dnd_leave;
    win->on_dnd_drop = liz_app_dnd_drop;
    app->font = xc_font_load(win, LIZ_FONT, LIZ_FONT_SIZE, liz_theme_text);
    app->font_bold = xc_font_load_style(win, LIZ_FONT, LIZ_FONT_SIZE, "bold", liz_theme_text);
    app->font_dim = xc_font_load(win, LIZ_FONT, LIZ_FONT_SIZE, liz_theme_text_dim);
    app->font_accent = xc_font_load(win, LIZ_FONT, LIZ_FONT_SIZE, liz_theme_dir);
    app->font_error = xc_font_load(win, LIZ_FONT, LIZ_FONT_SIZE, liz_theme_error);

    if (!app->font || !app->font_bold || !app->font_dim || !app->font_accent
        || !app->font_error) {
        liz_app_quit(app);
        return -1;
    }

    app->selected = -1;
    app->hover_row = -1;
    app->last_click_row = -1;
    app->press_row = -1;
    app->sidebar_visible = true;

    liz_vim_init(&app->vim);
    app->vim_mode = LIZ_VIM_MODE_DEFAULT;
    liz_sidebar_init(app);
#ifdef ICON_SUPPORT
    liz_icons_init();
#endif
#ifdef ARCHIVE_SUPPORT
    memset(&app->archive, 0, sizeof(app->archive));
#endif

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
        snprintf(cwd, sizeof(cwd), "/");
    liz_app_navigate(app, cwd);

    return 0;
}

void liz_app_quit(liz_app* app)
{
    liz_app_clip_clear(app);
    liz_preview_shutdown(app);

    if (app->win) {
#ifdef ICON_SUPPORT
        liz_icons_shutdown(app->win);
#endif
        xc_font_free(app->win, app->font);
        xc_font_free(app->win, app->font_bold);
        xc_font_free(app->win, app->font_dim);
        xc_font_free(app->win, app->font_accent);
        xc_font_free(app->win, app->font_error);
        app->font = NULL;
        app->font_bold = NULL;
        app->font_dim = NULL;
        app->font_accent = NULL;
        app->font_error = NULL;
    }
    liz_fs_entries_free(app->entries, app->entry_count);
    app->entries = NULL;
    app->entry_count = 0;
    free(app->sel);
    app->sel = NULL;

    if (app->win) {
        xc_window_destroy(app->win);
        app->win = NULL;
    }

    /* release fontconfig's global caches (Xft uses fontconfig) */
    FcFini();
}
