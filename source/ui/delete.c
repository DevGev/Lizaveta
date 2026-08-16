/* delete.c - delete confirmation implementation. */

#include "ui/delete.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>

#include "ui/theme.h"

void liz_delete_cancel(liz_app* app)
{
    app->del.active = false;
    app->del.row_count = 0;
    app->del.err[0] = '\0';
}

/* Starts the confirmation for `rows`/`count`, skipping rows that are out of
 * range or ".." and clamping to LIZ_DELETE_MAX. */
static void liz_delete_start_rows(liz_app* app, const int* rows, int count)
{
    if (count <= 0)
        return;

    liz_delete_state* ds = &app->del;
    ds->active = true;
    ds->row_count = 0;
    ds->err[0] = '\0';

    int n = count < LIZ_DELETE_MAX ? count : LIZ_DELETE_MAX;
    for (int i = 0; i < n; i++) {
        int r = rows[i];
        if (r < 0 || (size_t)r >= app->entry_count)
            continue;
        if (strcmp(app->entries[r].name, "..") == 0)
            continue;
        ds->rows[ds->row_count++] = r;
    }
    if (ds->row_count == 0) {
        ds->active = false;
        return;
    }
    snprintf(ds->first_name, sizeof(ds->first_name), "%s",
             app->entries[ds->rows[0]].name);
}

/* Deletes the inclusive row range [a, b] (clamped to the listing). */
void liz_delete_start_range(liz_app* app, int a, int b)
{
    if (b < a) {
        int t = a;
        a = b;
        b = t;
    }
    if (a < 0)
        a = 0;
    if (b >= (int)app->entry_count)
        b = (int)app->entry_count - 1;
    if (a > b || (size_t)a >= app->entry_count)
        return;

    int rows[LIZ_DELETE_MAX];
    int n = 0;
    for (int i = a; i <= b && n < LIZ_DELETE_MAX; i++)
        rows[n++] = i;
    liz_delete_start_rows(app, rows, n);
}

/* Deletes the selected rows. Unlike liz_app_collect_selection this does NOT
 * fall back to the focused row: `d` only acts on a real selection, and a
 * cursor-row delete is `dd`'s job. */
void liz_delete_start_selection(liz_app* app)
{
    if (liz_app_selection_count(app) <= 0)
        return;
    int rows[LIZ_DELETE_MAX];
    int n = liz_app_collect_selection(app, rows, LIZ_DELETE_MAX);
    liz_delete_start_rows(app, rows, n);
}

/* Deletes the entries and reloads the listing. On failure the prompt stays
 * open with the reason shown at the right edge; already-deleted rows are
 * skipped on retry (ENOENT). */
static void liz_delete_commit(liz_app* app)
{
    liz_delete_state* ds = &app->del;
    for (int i = 0; i < ds->row_count; i++) {
        int r = ds->rows[i];
        if (r < 0 || (size_t)r >= app->entry_count)
            continue;
        char path[PATH_MAX];
        if (liz_fs_join(path, sizeof(path), app->cwd, app->entries[r].name) != 0)
            continue;
        if (liz_fs_remove_recursive(path) != 0 && errno != ENOENT) {
            snprintf(ds->err, sizeof(ds->err), "delete failed: %s", strerror(errno));
            return;
        }
    }

    liz_delete_cancel(app);
    liz_app_navigate(app, app->cwd);
}

bool liz_delete_handle_key(liz_app* app, xc_event ev)
{
    if (!app->del.active)
        return false;

    switch (ev.key) {
    case XK_y:
    case XK_Y:
    case XK_Return:
    case XK_KP_Enter:
        liz_delete_commit(app);
        return true;
    default:
        /* any other key dismisses without deleting */
        liz_delete_cancel(app);
        return true;
    }
}

void liz_delete_draw(liz_app* app)
{
    xwindow* w = app->win;
    int h = LIZ_UI_STATUS_H;
    int y0 = w->height - h;
    liz_delete_state* ds = &app->del;

    int ascent = 0, descent = 0;
    xc_font_metrics(app->font_dim, &ascent, &descent);
    int text_y = y0 + (h - (ascent + descent)) / 2 + ascent;

    char left[LIZ_FS_NAME_MAX + 32];
    int leftlen;
    if (ds->row_count == 1)
        leftlen = snprintf(left, sizeof(left), "Delete %s?  [y/N]", ds->first_name);
    else
        leftlen = snprintf(left, sizeof(left), "Delete %d items?  [y/N]", ds->row_count);
    xc_text(w, LIZ_UI_PAD, text_y, left, leftlen, app->font);

    if (ds->err[0] != '\0') {
        int errlen = (int)strlen(ds->err);
        int ew = 0;
        xc_text_measure(w, ds->err, errlen, app->font_error, &ew, NULL);
        xc_text(w, w->width - LIZ_UI_PAD - ew, text_y, ds->err, errlen,
                app->font_error);
    }
}
