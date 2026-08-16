#include "ui/rename.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "ui/theme.h"

void liz_rename_cancel(liz_app* app)
{
    app->rename.active = false;
    app->rename.err[0] = '\0';
    liz_editor_clear_selection(&app->rename.ed);
}

void liz_rename_start(liz_app* app)
{
    if (app->selected < 0 || (size_t)app->selected >= app->entry_count)
        return;
    liz_fs_entry* e = &app->entries[app->selected];
    if (strcmp(e->name, "..") == 0)
        return;

    app->rename.active = true;
    app->rename.row = app->selected;
    snprintf(app->rename.old_name, sizeof(app->rename.old_name), "%s", e->name);
    app->rename.err[0] = '\0';
    liz_editor_set_text(&app->rename.ed, e->name);
    app->vim.visual_active = false;
    app->vim.pending_g = false;
}

/* Commits the rename. An empty input cancels (the prompt was a slip);
 * otherwise the new name must not contain "/", must not collide with an
 * existing entry, and must be a legal filesystem rename. Failures keep the
 * prompt open with the reason shown at the right edge. */
static void liz_rename_commit(liz_app* app)
{
    liz_rename_state* rs = &app->rename;
    const char* name = rs->ed.text;

    if (name[0] == '\0') {
        liz_rename_cancel(app);
        return;
    }
    if (strchr(name, '/') != NULL) {
        snprintf(rs->err, sizeof(rs->err), "name cannot contain '/'");
        return;
    }
    if (rs->row < 0 || (size_t)rs->row >= app->entry_count) {
        liz_rename_cancel(app);
        return;
    }

    char new_path[PATH_MAX];
    if (liz_fs_join(new_path, sizeof(new_path), app->cwd, name) != 0) {
        snprintf(rs->err, sizeof(rs->err), "name too long");
        return;
    }
    char old_path[PATH_MAX];
    if (liz_fs_join(old_path, sizeof(old_path), app->cwd, rs->old_name) != 0) {
        liz_rename_cancel(app);
        return;
    }

    if (access(new_path, F_OK) == 0) {
        int nl = (int)strlen(name);
        if (nl > 80)
            nl = 80;
        snprintf(rs->err, sizeof(rs->err), "'%.*s' already exists", nl, name);
        return;
    }
    if (rename(old_path, new_path) != 0) {
        snprintf(rs->err, sizeof(rs->err), "rename failed: %s", strerror(errno));
        return;
    }

    /* reload and reselect the entry under its new name */
    char* saved = strdup(name);
    liz_rename_cancel(app);
    liz_app_navigate(app, app->cwd);
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

bool liz_rename_handle_key(liz_app* app, xc_event ev)
{
    liz_rename_state* rs = &app->rename;
    if (!rs->active)
        return false;

    switch (ev.key) {
    case XK_Escape:
        liz_rename_cancel(app);
        return true;
    case XK_Return:
    case XK_KP_Enter:
        liz_rename_commit(app);
        return true;
    default:
        break;
    }

    liz_editor_handle_key(&rs->ed, app->win, ev);
    rs->err[0] = '\0';
    return true;
}

int liz_rename_click(liz_app* app, int x)
{
    liz_rename_state* rs = &app->rename;
    if (!rs->active)
        return -1;
    int offset = liz_editor_index_at(&rs->ed, app->win, app->font, x);
    liz_editor_drag_start(&rs->ed, offset);
    return offset;
}

void liz_rename_drag(liz_app* app, int x)
{
    liz_rename_state* rs = &app->rename;
    if (!rs->active)
        return;
    int offset = liz_editor_index_at(&rs->ed, app->win, app->font, x);
    liz_editor_drag_to(&rs->ed, offset);
}

void liz_rename_draw(liz_app* app)
{
    xwindow* w = app->win;
    int h = LIZ_UI_STATUS_H;
    int y0 = w->height - h;
    liz_rename_state* rs = &app->rename;

    int ascent = 0, descent = 0;
    xc_font_metrics(app->font_dim, &ascent, &descent);
    int text_y = y0 + (h - (ascent + descent)) / 2 + ascent;

    char left[LIZ_FS_NAME_MAX + 16];
    int leftlen = snprintf(left, sizeof(left), "Rename %s -> ", rs->old_name);
    int lx = LIZ_UI_PAD;
    lx += xc_text(w, lx, text_y, left, leftlen, app->font_dim);

    int max_w = w->width - LIZ_UI_PAD - lx;
    if (rs->err[0] != '\0') {
        int errlen = (int)strlen(rs->err);
        int ew = 0;
        xc_text_measure(w, rs->err, errlen, app->font_error, &ew, NULL);
        int ex = w->width - LIZ_UI_PAD - ew;
        xc_text(w, ex, text_y, rs->err, errlen, app->font_error);
        max_w -= ew + LIZ_UI_PAD * 2;
    }
    if (max_w < 0)
        max_w = 0;

    rs->ed.suffix = NULL;
    rs->ed.suffix_len = 0;
    liz_editor_draw(&rs->ed, w, lx, text_y, max_w, app->font, app->font_dim);
}
