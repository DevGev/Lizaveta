#include "app/app.h"

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

#include "apps/apps.h"
#include "icons/icons.h"
#include "ui/chooser.h"
#include "ui/delete.h"
#include "ui/file_list.h"
#include "ui/menu.h"
#include "ui/nav.h"
#include "ui/newfolder.h"
#include "ui/preview.h"
#include "ui/rename.h"
#include "ui/sidebar.h"
#include "ui/status.h"
#include "ui/theme.h"

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
    if (liz_fs_canonical(canon, sizeof(canon), path) != 0)
        return;

    liz_fs_entry* entries = NULL;
    size_t count = 0;
    if (liz_fs_read(canon, app->show_hidden, &entries, &count) != 0)
        return;

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
    bool resolved = liz_apps_for(path, &handler);

    if (!liz_app_detach())
        return;
    if (resolved)
        liz_apps_exec(&handler, path); /* only returns if it could not run */
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
    liz_apps_exec(with, path);
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

void liz_app_go_parent(liz_app* app)
{
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
    for (int i = 0; i < n; i++) {
        char path[PATH_MAX];
        if (liz_fs_join(path, sizeof(path), app->cwd, app->entries[rows[i]].name) != 0)
            continue;
        paths[count] = strdup(path);
        if (paths[count])
            count++;
    }
    if (count == 0) {
        free(paths);
        return;
    }

    liz_app_clip_clear(app);
    app->fileclip.paths = paths;
    app->fileclip.count = count;
    app->fileclip.cut = cut;
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

        int r = app->fileclip.cut ? rename(src, dst) : liz_fs_copy_recursive(src, dst);
        if (r != 0)
            ok = false;
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

static void liz_app_handle_key(liz_app* app, xc_event ev)
{
    /* a bare modifier key press/release carries no action anywhere; ignore
     * it before any mode gets a chance to treat it as "end the mode" */
    if (liz_key_is_modifier(ev.key))
        return;

    if (ev.key == XK_d
        && (ev.state & ControlMask)
        && (ev.state & Mod1Mask)
        && !(ev.state & (Mod2Mask | Mod3Mask | Mod4Mask | Mod5Mask))) {
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

    /* Handled before any modal capture so they work from anywhere except
     * the rename prompt (which captures everything). */
    if ((ev.state & ControlMask)
        && !(ev.state & (Mod1Mask | Mod2Mask | Mod3Mask
                         | Mod4Mask | Mod5Mask))) {
        app->vim.pending_g = false;
        app->vim.pending_d = false;
        switch (ev.key) {
        case XK_l:
            liz_nav_toggle_edit(app);
            return;
        case XK_h:
            liz_app_toggle_hidden(app);
            return;
        case XK_p:
            liz_app_toggle_sidebar(app);
            return;
        case XK_d: { /* bulk scroll down, like vim */
            int n = liz_list_visible_count(app) / 2;
            if (n < 1)
                n = 1;
            liz_app_set_selected(app, app->selected + n);
            return;
        }
        case XK_u: { /* bulk scroll up, like vim */
            int n = liz_list_visible_count(app) / 2;
            if (n < 1)
                n = 1;
            liz_app_set_selected(app, app->selected - n);
            return;
        }
        case XK_o:
            liz_app_jump_back(app);
            return;
        case XK_i:
            liz_app_jump_fwd(app);
            return;
        case XK_c: /* copy the selection to the file clipboard */
            if (app->nav_input.editing)
                break; /* let the location editor handle its own clipboard */
            liz_app_copy_selection(app);
            return;
        case XK_x: /* cut the selection (move on paste) */
            if (app->nav_input.editing)
                break;
            liz_app_cut_selection(app);
            return;
        case XK_v: /* paste the staged entries into the current directory */
            if (app->nav_input.editing)
                break;
            liz_app_paste(app);
            return;
        default:
            break;
        }
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

    /* NORMAL mode: vim motions/commands first, then the remaining plain-key
     * bindings. */
    if (liz_vim_handle_normal_key(app, ev))
        return;

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
    case XK_p:
        liz_preview_toggle(app);
        break;
    case XK_o:
        liz_newfolder_start(app);
        break;
    case XK_t:
        liz_app_open_terminal(app, app->cwd);
        break;
    case XK_T: { /* open terminal in the directory under the cursor */
        liz_fs_entry* e = (app->selected >= 0
                          && (size_t)app->selected < app->entry_count)
                             ? &app->entries[app->selected]
                             : NULL;
        if (e != NULL && e->type == LIZ_FS_DIR) {
            char path[PATH_MAX];
            if (liz_fs_join(path, sizeof(path), app->cwd, e->name) == 0)
                liz_app_open_terminal(app, path);
        }
        break;
    }
    case XK_r:
        liz_rename_start(app);
        break;
    case XK_H:
        liz_app_go_home(app);
        break;
    case XK_Escape:
        liz_preview_close(app);
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
            continue; /* never overwrite an existing target */
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

    xwindow* win = xc_window_create(120, 120, 900, 600, liz_theme_bg, "lizaveta");
    if (win)
        xc_set_class(win, "lizaveta", "lizaveta");
    if (!win)
        return -1;
    app->win = win;
    win->events = liz_app_on_event;
    win->userdata = app;
    win->on_clipboard = liz_app_on_clipboard;
    win->dnd_repaint = liz_app_dnd_repaint;
    win->on_dnd_enter = liz_app_dnd_enter;
    win->on_dnd_position = liz_app_dnd_position;
    win->on_dnd_leave = liz_app_dnd_leave;
    win->on_dnd_drop = liz_app_dnd_drop;
    app->font = xc_font_load(win, LIZ_UI_FONT, LIZ_UI_FONT_PX, liz_theme_text);
    app->font_bold = xc_font_load_style(win, LIZ_UI_FONT, LIZ_UI_FONT_PX, "bold", liz_theme_text);
    app->font_dim = xc_font_load(win, LIZ_UI_FONT, LIZ_UI_FONT_PX, liz_theme_text_dim);
    app->font_accent = xc_font_load(win, LIZ_UI_FONT, LIZ_UI_FONT_PX, liz_theme_dir);
    app->font_error = xc_font_load(win, LIZ_UI_FONT, LIZ_UI_FONT_PX, liz_theme_error);

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
    liz_sidebar_init(app);
    liz_icons_init();

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
        liz_icons_shutdown(app->win);
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
