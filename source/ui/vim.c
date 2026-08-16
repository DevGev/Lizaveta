/* vim.c - vim-style modal input for the file list. */

#include "ui/vim.h"

#include <ctype.h>
#include <string.h>

#include "app/app.h"
#include "ui/delete.h"

static bool liz_str_ci_contains(const char* hay, const char* needle)
{
    if (!needle || !*needle)
        return false;
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

/* First row at or after `start` (wrapping) whose name matches `query`, or
 * -1 if there are no entries or no match anywhere. */
static int liz_vim_find_first(liz_app* app, const char* query, int start)
{
    int n = (int)app->entry_count;
    if (n == 0 || query[0] == '\0')
        return -1;
    for (int off = 0; off < n; off++) {
        int i = (start + off) % n;
        if (i < 0)
            i += n;
        if (liz_str_ci_contains(app->entries[i].name, query))
            return i;
    }
    return -1;
}

/* Next match strictly after `from` in direction `dir` (+1/-1), wrapping
 * around, `from` itself included last (so a single match still "wraps"
 * back to itself instead of doing nothing). */
static int liz_vim_find_match(liz_app* app, const char* query, int from, int dir)
{
    int n = (int)app->entry_count;
    if (n == 0 || query[0] == '\0')
        return -1;
    int i = from;
    for (int step = 0; step < n; step++) {
        i += dir;
        if (i < 0)
            i = n - 1;
        if (i >= n)
            i = 0;
        if (liz_str_ci_contains(app->entries[i].name, query))
            return i;
    }
    return -1;
}

bool liz_vim_name_matches(const liz_app* app, const char* name)
{
    if (!app->vim.search_active || app->vim.search_query[0] == '\0')
        return false;
    return liz_str_ci_contains(name, app->vim.search_query);
}

/* Re-runs the in-progress search from the anchor and moves the selection
 * live, or turns highlighting off once the query is emptied out. */
static void liz_vim_search_preview(liz_app* app)
{
    if (app->vim.cmdline_len == 0) {
        app->vim.search_active = false;
        liz_app_set_selected(app, app->vim.search_anchor);
        return;
    }
    snprintf(app->vim.search_query, sizeof(app->vim.search_query), "%s", app->vim.cmdline);
    app->vim.search_active = true;
    int idx = liz_vim_find_first(app, app->vim.search_query, app->vim.search_anchor);
    if (idx >= 0)
        liz_app_set_selected(app, idx);
}

/* Discards the in-progress search, restoring whatever was active before
 * "/" was pressed. Used by Escape and by backspacing past an empty line. */
static void liz_vim_search_cancel(liz_app* app)
{
    app->vim.search_active = app->vim.prev_search_active;
    snprintf(app->vim.search_query, sizeof(app->vim.search_query), "%s", app->vim.prev_query);
    liz_app_set_selected(app, app->vim.search_anchor);
}

static void liz_vim_enter_search(liz_app* app)
{
    app->vim.mode = LIZ_VIM_COMMAND;
    app->vim.cmd_prefix = '/';
    app->vim.cmdline[0] = '\0';
    app->vim.cmdline_len = 0;
    app->vim.search_anchor = app->selected;
    app->vim.prev_search_active = app->vim.search_active;
    snprintf(app->vim.prev_query, sizeof(app->vim.prev_query), "%s", app->vim.search_query);
    liz_vim_search_preview(app); /* empty query: clears highlight until typing starts */
}

static void liz_vim_enter_ex(liz_app* app)
{
    app->vim.mode = LIZ_VIM_COMMAND;
    app->vim.cmd_prefix = ':';
    app->vim.cmdline[0] = '\0';
    app->vim.cmdline_len = 0;
}

static void liz_vim_run_ex(liz_app* app, const char* cmd)
{
    if (strcmp(cmd, "noh") == 0 || strcmp(cmd, "nohlsearch") == 0) {
        app->vim.search_active = false;
    } else if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
        app->quit = true;
        app->win->running = false;
    }
    /* unknown commands are ignored, same as vim's "E492" case would be
     * for a modal file manager: no-op rather than an error dialog */
}

static void liz_vim_commit(liz_app* app, char prefix, const char* text)
{
    if (prefix == '/') {
        if (text[0] != '\0') {
            snprintf(app->vim.search_query, sizeof(app->vim.search_query), "%s", text);
            app->vim.search_active = true;
            int idx = liz_vim_find_first(app, app->vim.search_query, app->vim.search_anchor);
            if (idx >= 0)
                liz_app_set_selected(app, idx);
        }
    } else if (prefix == ':') {
        liz_vim_run_ex(app, text);
    }

    app->vim.last_cmd_prefix = prefix;
    snprintf(app->vim.last_cmd_text, sizeof(app->vim.last_cmd_text), "%s", text);
    app->vim.has_last_cmd = true;
}

static void liz_vim_repeat_last(liz_app* app)
{
    if (!app->vim.has_last_cmd)
        return;
    if (app->vim.last_cmd_prefix == '/')
        app->vim.search_anchor = app->selected; /* repeat from where we are now */
    liz_vim_commit(app, app->vim.last_cmd_prefix, app->vim.last_cmd_text);
}

static void liz_vim_search_step(liz_app* app, int dir)
{
    if (!app->vim.search_active || app->vim.search_query[0] == '\0')
        return;
    int idx = liz_vim_find_match(app, app->vim.search_query, app->selected, dir);
    if (idx >= 0)
        liz_app_set_selected(app, idx);
}

/* Rebuilds the selection as the span from the visual anchor to the focused
 * row, and updates the click-range anchor.
 *
 * Linewise (V) selects the whole span inclusive of the anchor, so the current
 * line is highlighted the moment V is pressed. Charwise (v) follows vim's
 * "start here but don't select it yet": the anchor row is excluded, so the
 * first motion highlights only the row moved onto. */
static void liz_vim_visual_sync(liz_app* app)
{
    int a = app->vim.visual_anchor;
    int b = app->selected;
    int r0, r1; /* inclusive range; r0 > r1 means empty */

    if (app->vim.visual_line) {
        if (a > b) {
            int t = a;
            a = b;
            b = t;
        }
        r0 = a;
        r1 = b;
    } else if (b > a) {
        r0 = a + 1; /* moved down: rows below the anchor up to the cursor */
        r1 = b;
    } else if (b < a) {
        r0 = b; /* moved up: rows from the cursor up to, but not over, the anchor */
        r1 = a - 1;
    } else {
        r0 = 1;
        r1 = 0;
    }

    liz_app_clear_selection(app);
    if (r0 <= r1)
        liz_app_select_range(app, r0, r1);
    app->anchor_row = r0 <= r1 ? r0 : app->selected;
}

/* Exits VISUAL mode back to NORMAL and clears the selection. */
static void liz_vim_exit_visual(liz_app* app)
{
    app->vim.visual_active = false;
    app->vim.pending_g = false;
    liz_app_clear_selection(app);
}

static void liz_vim_enter_visual(liz_app* app, bool line)
{
    app->vim.visual_active = true;
    app->vim.visual_line = line;
    app->vim.visual_anchor = app->selected;
    app->vim.pending_g = false;
    liz_vim_visual_sync(app); /* V highlights the line; v starts empty */
}

bool liz_vim_handle_visual_key(liz_app* app, xc_event ev)
{
    bool was_pending_g = app->vim.pending_g;
    app->vim.pending_g = false;
    app->vim.pending_d = false;

    switch (ev.key) {
    case XK_Escape:
        liz_vim_exit_visual(app);
        return true;
    case XK_v:
        if (app->vim.visual_line) {
            app->vim.visual_line = false; /* switch to charwise, keep anchor */
            liz_vim_visual_sync(app);      /* anchor row drops out of the selection */
        } else {
            liz_vim_exit_visual(app);
        }
        return true;
    case XK_V:
        if (!app->vim.visual_line) {
            app->vim.visual_line = true; /* switch to linewise, keep anchor */
            liz_vim_visual_sync(app);
        } else {
            liz_vim_exit_visual(app);
        }
        return true;
    case XK_j:
    case XK_Down:
        liz_app_set_selected(app, app->selected + 1);
        liz_vim_visual_sync(app);
        return true;
    case XK_k:
    case XK_Up:
        liz_app_set_selected(app, app->selected - 1);
        liz_vim_visual_sync(app);
        return true;
    case XK_g:
        if (was_pending_g) {
            liz_app_set_selected(app, 0);
            liz_vim_visual_sync(app);
        } else {
            app->vim.pending_g = true;
        }
        return true;
    case XK_G:
        liz_app_set_selected(app, (int)app->entry_count - 1);
        liz_vim_visual_sync(app);
        return true;
    case XK_n:
        liz_vim_search_step(app, +1);
        liz_vim_visual_sync(app);
        return true;
    case XK_N:
        liz_vim_search_step(app, -1);
        liz_vim_visual_sync(app);
        return true;
    case XK_h:
    case XK_l:
        return true; /* a list has no horizontal axis to extend along */
    case XK_d:
    case XK_Delete:
        /* delete the visual selection: snapshot the selected rows before
         * exiting VISUAL (which clears the selection) */
        liz_delete_start_selection(app);
        liz_vim_exit_visual(app);
        return true;
    case XK_Return:
    case XK_KP_Enter:
        liz_vim_exit_visual(app);
        return true;
    default:
        /* everything else (/, :, ..., ) leaves VISUAL first and is then
         * handled by normal mode */
        liz_vim_exit_visual(app);
        return false;
    }
}

void liz_vim_init(liz_vim_state* vim)
{
    memset(vim, 0, sizeof(*vim));
    vim->mode = LIZ_VIM_NORMAL;
}

bool liz_vim_handle_normal_key(liz_app* app, xc_event ev)
{
    bool g_pending = app->vim.pending_g;
    bool d_pending = app->vim.pending_d;

    /* `d` is only an operator prefix for a second `d` (dd); `g` only for a
     * second `g` (gg). Anything else cancels any pending key and is
     * processed normally below, so `d` followed by a motion never deletes. */
    switch (ev.key) {
    case XK_d:
        app->vim.pending_g = false;
        break; /* keep pending_d: a second d completes dd */
    case XK_g:
        app->vim.pending_d = false;
        break; /* keep pending_g: a second g completes gg */
    default:
        app->vim.pending_d = false;
        app->vim.pending_g = false;
        break;
    }

    switch (ev.key) {
    case XK_h:
        liz_app_go_parent(app);
        return true;
    case XK_j:
        liz_app_set_selected(app, app->selected + 1);
        return true;
    case XK_k:
        liz_app_set_selected(app, app->selected - 1);
        return true;
    case XK_l:
        liz_app_open_row(app, app->selected);
        return true;
    case XK_g:
        if (g_pending) {
            app->vim.pending_g = false;
            liz_app_set_selected(app, 0);
        } else {
            app->vim.pending_g = true;
        }
        return true;
    case XK_G:
        liz_app_set_selected(app, (int)app->entry_count - 1);
        return true;
    case XK_slash:
        liz_vim_enter_search(app);
        return true;
    case XK_colon:
        liz_vim_enter_ex(app);
        return true;
    case XK_period:
        liz_vim_repeat_last(app);
        return true;
    case XK_n:
        liz_vim_search_step(app, +1);
        return true;
    case XK_N:
        liz_vim_search_step(app, -1);
        return true;
    case XK_v:
        liz_vim_enter_visual(app, false);
        return true;
    case XK_V:
        liz_vim_enter_visual(app, true);
        return true;
    case XK_d:
        if (d_pending) {
            app->vim.pending_d = false;
            liz_delete_start_range(app, app->selected, app->selected); /* dd */
        } else if (liz_app_selection_count(app) > 0) {
            /* `d` deletes the selection, and only the selection */
            liz_delete_start_selection(app);
        } else {
            app->vim.pending_d = true; /* first d of dd */
        }
        return true;
    case XK_Delete:
        /* the Delete key is the `d` action: delete the selection, or the
         * focused row when there is none */
        if (liz_app_selection_count(app) > 0)
            liz_delete_start_selection(app);
        else
            liz_delete_start_range(app, app->selected, app->selected);
        return true;
    default:
        return false;
    }
}

void liz_vim_handle_command_key(liz_app* app, xc_event ev)
{
    if (ev.key == XK_Escape) {
        if (app->vim.cmd_prefix == '/')
            liz_vim_search_cancel(app);
        app->vim.mode = LIZ_VIM_NORMAL;
        app->vim.cmdline[0] = '\0';
        app->vim.cmdline_len = 0;
        return;
    }

    if (ev.key == XK_Return) {
        char prefix = app->vim.cmd_prefix;
        char text[LIZ_VIM_CMDLINE_MAX];
        snprintf(text, sizeof(text), "%s", app->vim.cmdline);
        app->vim.mode = LIZ_VIM_NORMAL;
        app->vim.cmdline[0] = '\0';
        app->vim.cmdline_len = 0;
        liz_vim_commit(app, prefix, text);
        return;
    }

    if (ev.key == XK_BackSpace) {
        if (app->vim.cmdline_len > 0) {
            int i = app->vim.cmdline_len;
            do {
                i--;
            } while (i > 0 && ((app->vim.cmdline[i] & 0xC0) == 0x80));
            app->vim.cmdline_len = i;
            app->vim.cmdline[i] = '\0';
            if (app->vim.cmd_prefix == '/')
                liz_vim_search_preview(app);
        } else {
            /* backspacing past an empty command line leaves COMMAND mode,
             * same as vim */
            if (app->vim.cmd_prefix == '/')
                liz_vim_search_cancel(app);
            app->vim.mode = LIZ_VIM_NORMAL;
        }
        return;
    }

    /* printable text input */
    if (ev.nchars > 0 && (unsigned char)ev.chars[0] >= 0x20 && ev.chars[0] != 0x7F) {
        int add = ev.nchars;
        if (app->vim.cmdline_len + add < (int)sizeof(app->vim.cmdline) - 1) {
            memcpy(app->vim.cmdline + app->vim.cmdline_len, ev.chars, (size_t)add);
            app->vim.cmdline_len += add;
            app->vim.cmdline[app->vim.cmdline_len] = '\0';
            if (app->vim.cmd_prefix == '/')
                liz_vim_search_preview(app);
        }
    }
}
