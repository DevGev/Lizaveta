#include "ui/chooser.h"

#include <errno.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pwd.h>
#include <sys/stat.h>

#include "app/app.h"

/* The home directory, falling back to getpwuid when HOME is unset. */
static const char* liz_chooser_home(char* buf, size_t bufsz)
{
    const char* h = getenv("HOME");
    if (!h || !h[0]) {
        struct passwd* pw = getpwuid(getuid());
        h = (pw && pw->pw_dir) ? pw->pw_dir : "/";
    }
    snprintf(buf, bufsz, "%s", h);
    return buf;
}

void liz_chooser_start(liz_app* app)
{
    liz_chooser* c = &app->chooser;
    c->active = true;

    char dir[PATH_MAX];
    char home[PATH_MAX];
    liz_chooser_home(home, sizeof(home));

    if (c->start_dir[0] != '\0') {
        char canon[PATH_MAX];
        if (liz_fs_canonical(canon, sizeof(canon), c->start_dir) == 0)
            snprintf(dir, sizeof(dir), "%s", canon);
        else
            snprintf(dir, sizeof(dir), "%s", home);

        /* a file start point opens its parent directory */
        struct stat st;
        if (stat(dir, &st) == 0 && !S_ISDIR(st.st_mode)) {
            char parent[PATH_MAX];
            if (liz_fs_parent(parent, sizeof(parent), dir) == 0)
                snprintf(dir, sizeof(dir), "%s", parent);
        }
    } else {
        snprintf(dir, sizeof(dir), "%s", home);
    }

    liz_app_navigate(app, dir);
    app->vim.mode = LIZ_VIM_NORMAL;
    app->vim.visual_active = false;
    app->vim.pending_g = false;
    app->vim.pending_d = false;

    /* SAVE mode: the name prompt is deliberately *not* opened here. Browse
     * to the target folder first (normal navigation, nothing intercepted),
     * then Ctrl+Enter opens the name field as the last step before saving
     * -- see the Ctrl+Enter handling in liz_chooser_handle_key. 'r' still
     * opens it early too, for anyone who wants to name the file up front. */
}

bool liz_chooser_add_filter(liz_app* app, const char* spec)
{
    liz_chooser* c = &app->chooser;
    if (c->filter_count >= LIZ_CHOOSER_FILTERS_MAX)
        return false;

    const char* colon = strchr(spec, ':');
    if (!colon)
        return false;

    liz_chooser_filter* f = &c->filters[c->filter_count];
    memset(f, 0, sizeof(*f));

    size_t namelen = (size_t)(colon - spec);
    if (namelen >= sizeof(f->name))
        namelen = sizeof(f->name) - 1;
    memcpy(f->name, spec, namelen);
    f->name[namelen] = '\0';

    const char* p = colon + 1;
    while (*p != '\0' && f->pattern_count < LIZ_CHOOSER_FILTER_PATTERNS_MAX) {
        const char* sep = strchr(p, ';');
        size_t len = sep ? (size_t)(sep - p) : strlen(p);
        if (len > 0) {
            if (len >= sizeof(f->patterns[0]))
                len = sizeof(f->patterns[0]) - 1;
            memcpy(f->patterns[f->pattern_count], p, len);
            f->patterns[f->pattern_count][len] = '\0';
            f->pattern_count++;
        }
        p = sep ? sep + 1 : p + strlen(p);
    }

    if (f->pattern_count == 0)
        return false;

    c->filter_count++;
    return true;
}

bool liz_chooser_name_matches_filter(const liz_app* app, const char* name)
{
    const liz_chooser* c = &app->chooser;
    if (c->filter_count == 0)
        return true;
    int idx = c->current_filter;
    if (idx < 0 || idx >= c->filter_count)
        idx = 0;
    const liz_chooser_filter* f = &c->filters[idx];
    for (int i = 0; i < f->pattern_count; i++) {
        /* FNM_CASEFOLD (GNU extension, already relying on _GNU_SOURCE) so
         * "*.JPG" matches a pattern of "*.jpg" -- extensions are rarely
         * typed consistently and a picky filter is just an annoyance. */
        if (fnmatch(f->patterns[i], name, FNM_CASEFOLD) == 0)
            return true;
    }
    return false;
}

void liz_chooser_cycle_filter(liz_app* app)
{
    liz_chooser* c = &app->chooser;
    if (c->filter_count <= 1)
        return;
    c->current_filter = (c->current_filter + 1) % c->filter_count;
    liz_app_navigate(app, app->cwd); /* reload so the new filter takes effect */
}

/* Ends the session: writes the chosen absolute paths one per line to the
 * output file (or stdout) and quits. An unwritable output file is a hard
 * error: the message goes to stderr and the process exits nonzero. */
static void liz_chooser_done(liz_app* app, char** paths, int n)
{
    liz_chooser* c = &app->chooser;

    if (c->out_path[0]) {
        FILE* f = fopen(c->out_path, "w");
        if (!f) {
            fprintf(stderr, "lizaveta: cannot write %s: %s\n",
                    c->out_path, strerror(errno));
            c->exit_code = 1;
        } else {
            for (int i = 0; i < n; i++)
                fprintf(f, "%s\n", paths[i]);
            fclose(f);
        }
    } else {
        for (int i = 0; i < n; i++)
            printf("%s\n", paths[i]);
        fflush(stdout);
    }

    app->vim.visual_active = false;
    app->quit = true;
    if (app->win)
        app->win->running = false;
}

void liz_chooser_cancel(liz_app* app)
{
    app->vim.visual_active = false;
    app->quit = true;
    if (app->win)
        app->win->running = false;
}

/* Absolute path of the entry at `row`; empty string when it cannot be built. */
static void liz_chooser_row_path(const liz_app* app, int row, char* buf, size_t bufsz)
{
    buf[0] = '\0';
    if (row < 0 || (size_t)row >= app->entry_count)
        return;
    liz_fs_join(buf, bufsz, app->cwd, app->entries[row].name);
}

/* True when `row` is a directory (following symlinks), so OPEN mode can
 * navigate into it instead of selecting it. */
static bool liz_chooser_row_is_dir(const liz_app* app, int row)
{
    if (row < 0 || (size_t)row >= app->entry_count)
        return false;
    liz_fs_entry* e = &app->entries[row];
    if (e->type == LIZ_FS_DIR)
        return true;
    if (e->type == LIZ_FS_LINK) {
        char path[PATH_MAX];
        if (liz_fs_join(path, sizeof(path), app->cwd, e->name) != 0)
            return false;
        char canon[PATH_MAX];
        struct stat st;
        if (liz_fs_canonical(canon, sizeof(canon), path) == 0
            && stat(canon, &st) == 0 && S_ISDIR(st.st_mode))
            return true;
    }
    return false;
}

/* OPEN mode: confirm the marked rows, falling back to the focused row when
 * nothing is marked (liz_app_collect_selection does this). */
static void liz_chooser_confirm_selection(liz_app* app)
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
        if (liz_chooser_row_is_dir(app, rows[i]))
            continue; /* only files are selectable in OPEN mode */
        char path[PATH_MAX];
        liz_chooser_row_path(app, rows[i], path, sizeof(path));
        if (path[0])
            paths[m++] = strdup(path);
    }
    liz_chooser_done(app, paths, m);
    for (int i = 0; i < m; i++)
        free(paths[i]);
    free(paths);
}

/* SAVE mode: confirm <cwd>/<save_name>. */
static void liz_chooser_confirm_save(liz_app* app)
{
    liz_chooser* c = &app->chooser;
    char path[PATH_MAX];
    if (liz_fs_join(path, sizeof(path), app->cwd, c->save_name) == 0) {
        char* p = path;
        liz_chooser_done(app, &p, 1);
    }
}

/* Commits the in-progress "save as" name edit into save_name. */
static void liz_chooser_commit_name(liz_app* app)
{
    liz_chooser* c = &app->chooser;
    const char* name = c->name_ed.text;
    if (name[0] == '\0') {
        snprintf(c->name_err, sizeof(c->name_err), "name cannot be empty");
        return;
    }
    if (strchr(name, '/') != NULL) {
        snprintf(c->name_err, sizeof(c->name_err), "name cannot contain '/'");
        return;
    }
    snprintf(c->save_name, sizeof(c->save_name), "%s", name);
    c->name_err[0] = '\0';
    liz_editor_clear_selection(&c->name_ed);
    c->name_editing = false;
}

bool liz_chooser_handle_key(liz_app* app, xc_event ev)
{
    liz_chooser* c = &app->chooser;
    if (!c->active)
        return false;

    /* the in-progress "save as" name prompt captures all input */
    if (c->name_editing) {
        switch (ev.key) {
        case XK_Escape:
            c->name_editing = false;
            c->name_err[0] = '\0';
            liz_editor_clear_selection(&c->name_ed);
            return true;
        case XK_Return:
        case XK_KP_Enter:
            /* committing here only records the name so a mistyped extension
             * can still be reviewed before the final save */
            liz_chooser_commit_name(app);
            if (c->name_err[0] == '\0')
                liz_chooser_confirm_save(app);
            return true;
        default:
            liz_editor_handle_key(&c->name_ed, app->win, ev);
            c->name_err[0] = '\0';
            return true;
        }
    }

    /* first exit a vim VISUAL selection or search command line, then cancel
     * the chooser on the next press */
    if (app->vim.visual_active || app->vim.mode == LIZ_VIM_COMMAND)
        return false;

    /* committing "I'm done, this is the path I'm choosing" works the same in
     * every mode: it commits right now, regardless of what row is focused. */
    if ((ev.key == XK_Return || ev.key == XK_KP_Enter)
        && (ev.state & ControlMask)) {
        switch (c->mode) {
        case LIZ_CHOOSER_DIRECTORY: {
            char* p = app->cwd;
            liz_chooser_done(app, &p, 1);
            return true;
        }
        case LIZ_CHOOSER_OPEN:
            /* confirms the marked selection, or the focused row, if it is
             * (or resolves to) at least one file; a no-op on a lone
             * directory, since OPEN mode never selects directories */
            liz_chooser_confirm_selection(app);
            return true;
        case LIZ_CHOOSER_SAVE:
            /* opens the name field as the last step, prefilled with the
             * current suggested name in the folder that was just browsed to;
             * a second commit (or the name field's own) actually saves */
            c->name_editing = true;
            c->name_err[0] = '\0';
            liz_editor_set_text(&c->name_ed, c->save_name);
            return true;
        }
        return true;
    }

    switch (ev.key) {
    case XK_Escape:
        liz_chooser_cancel(app);
        return true;
    case XK_Return:
    case XK_KP_Enter:
        if (c->mode == LIZ_CHOOSER_OPEN) {
            /* confirm an active selection; a lone click on a directory
             * navigates into it instead of selecting it */
            int n = liz_app_selection_count(app);
            if (n > 0) {
                if (n == 1 && liz_app_row_selected(app, app->selected)
                    && liz_chooser_row_is_dir(app, app->selected))
                    return false;
                liz_chooser_confirm_selection(app);
                return true;
            }
        }
        /* SAVE and DIRECTORY modes (and OPEN with nothing marked) fall
         * through to the normal open_row path: a directory navigates into
         * itself, a file is confirmed as the target (SAVE) or opened
         * (OPEN) -- Enter never silently commits a directory as the
         * answer, only Ctrl+Enter (above) or 'q' (DIRECTORY) do that. */
        return false;
    case XK_q:
        if (c->mode == LIZ_CHOOSER_DIRECTORY) {
            char* p = app->cwd;
            liz_chooser_done(app, &p, 1);
        }
        return true;
    case XK_r:
        if (c->mode == LIZ_CHOOSER_SAVE) {
            c->name_editing = true;
            c->name_err[0] = '\0';
            liz_editor_set_text(&c->name_ed, c->save_name);
            return true;
        }
        return false;
    case XK_Tab:
        if (c->filter_count > 1) {
            liz_chooser_cycle_filter(app);
            return true;
        }
        return false;
    default:
        return false;
    }
}

void liz_chooser_open_row(liz_app* app, int row)
{
    liz_chooser* c = &app->chooser;
    if (!c->active || row < 0 || (size_t)row >= app->entry_count)
        return;

    if (liz_chooser_row_is_dir(app, row)) {
        char path[PATH_MAX];
        liz_chooser_row_path(app, row, path, sizeof(path));
        if (path[0])
            liz_app_navigate(app, path);
        return;
    }

    switch (c->mode) {
    case LIZ_CHOOSER_OPEN: {
        /* a click on a file selects it, like opening it */
        char path[PATH_MAX];
        liz_chooser_row_path(app, row, path, sizeof(path));
        if (path[0]) {
            char* p = path;
            liz_chooser_done(app, &p, 1);
        }
        break;
    }
    case LIZ_CHOOSER_DIRECTORY:
        break; /* only directories are selectable, via q */
    case LIZ_CHOOSER_SAVE: {
        char path[PATH_MAX];
        liz_chooser_row_path(app, row, path, sizeof(path));
        if (path[0]) {
            char* p = path;
            liz_chooser_done(app, &p, 1);
        }
        break;
    }
    }
}
