/* newfolder.c - "create folder" prompt implementation. */

#include "ui/newfolder.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "ui/theme.h"

void liz_newfolder_cancel(liz_app* app)
{
    app->newfolder.active = false;
    app->newfolder.err[0] = '\0';
    liz_editor_clear_selection(&app->newfolder.ed);
}

void liz_newfolder_start(liz_app* app)
{
    app->newfolder.active = true;
    app->newfolder.err[0] = '\0';
    liz_editor_set_text(&app->newfolder.ed, "");
    app->vim.visual_active = false;
    app->vim.pending_g = false;
}

/* Commits the new folder. An empty input cancels (the prompt was a slip);
 * otherwise the name must not contain "/", must not collide with an
 * existing entry, and mkdir(2) must succeed. Failures keep the prompt open
 * with the reason shown at the right edge, same as rename. */
static void liz_newfolder_commit(liz_app* app)
{
    liz_newfolder_state* ns = &app->newfolder;
    const char* name = ns->ed.text;

    if (name[0] == '\0') {
        liz_newfolder_cancel(app);
        return;
    }
    if (strchr(name, '/') != NULL) {
        snprintf(ns->err, sizeof(ns->err), "name cannot contain '/'");
        return;
    }

    char path[PATH_MAX];
    if (liz_fs_join(path, sizeof(path), app->cwd, name) != 0) {
        snprintf(ns->err, sizeof(ns->err), "name too long");
        return;
    }
    if (access(path, F_OK) == 0) {
        int nl = (int)strlen(name);
        if (nl > 80)
            nl = 80;
        snprintf(ns->err, sizeof(ns->err), "'%.*s' already exists", nl, name);
        return;
    }
    if (mkdir(path, 0755) != 0) {
        snprintf(ns->err, sizeof(ns->err), "mkdir failed: %s", strerror(errno));
        return;
    }

    /* reload and select the new folder */
    char* saved = strdup(name);
    liz_newfolder_cancel(app);
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

bool liz_newfolder_handle_key(liz_app* app, xc_event ev)
{
    liz_newfolder_state* ns = &app->newfolder;
    if (!ns->active)
        return false;

    switch (ev.key) {
    case XK_Escape:
        liz_newfolder_cancel(app);
        return true;
    case XK_Return:
    case XK_KP_Enter:
        liz_newfolder_commit(app);
        return true;
    default:
        break;
    }

    liz_editor_handle_key(&ns->ed, app->win, ev);
    ns->err[0] = '\0';
    return true;
}

int liz_newfolder_click(liz_app* app, int x)
{
    liz_newfolder_state* ns = &app->newfolder;
    if (!ns->active)
        return -1;
    int offset = liz_editor_index_at(&ns->ed, app->win, app->font, x);
    liz_editor_drag_start(&ns->ed, offset);
    return offset;
}

void liz_newfolder_drag(liz_app* app, int x)
{
    liz_newfolder_state* ns = &app->newfolder;
    if (!ns->active)
        return;
    int offset = liz_editor_index_at(&ns->ed, app->win, app->font, x);
    liz_editor_drag_to(&ns->ed, offset);
}

void liz_newfolder_draw(liz_app* app)
{
    xwindow* w = app->win;
    int h = LIZ_UI_STATUS_H;
    int y0 = w->height - h;
    liz_newfolder_state* ns = &app->newfolder;

    int ascent = 0, descent = 0;
    xc_font_metrics(app->font_dim, &ascent, &descent);
    int text_y = y0 + (h - (ascent + descent)) / 2 + ascent;

    static const char left[] = "New folder: ";
    int leftlen = (int)sizeof(left) - 1;
    int lx = LIZ_UI_PAD;
    lx += xc_text(w, lx, text_y, left, leftlen, app->font_dim);

    int max_w = w->width - LIZ_UI_PAD - lx;
    if (ns->err[0] != '\0') {
        int errlen = (int)strlen(ns->err);
        int ew = 0;
        xc_text_measure(w, ns->err, errlen, app->font_error, &ew, NULL);
        int ex = w->width - LIZ_UI_PAD - ew;
        xc_text(w, ex, text_y, ns->err, errlen, app->font_error);
        max_w -= ew + LIZ_UI_PAD * 2;
    }
    if (max_w < 0)
        max_w = 0;

    ns->ed.suffix = NULL;
    ns->ed.suffix_len = 0;
    liz_editor_draw(&ns->ed, w, lx, text_y, max_w, app->font, app->font_dim);
}
