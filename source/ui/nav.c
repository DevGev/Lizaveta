/* nav.c - breadcrumb navigation bar implementation. */

#include "ui/nav.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>

#include "ui/theme.h"

#define LIZ_NAV_SEG_PAD 6
#define LIZ_NAV_CHEVRON_W 13 /* gap between two segments, holding the chevron */
#define LIZ_NAV_CHEVRON_ARM 3

static void liz_nav_draw_edit(liz_app* app);

/* Builds the segment list for the current cwd and computes each segment's
 * clickable pixel range. Stops when the bar is full. Returns the number of
 * segments placed in app->nav_sg. */
static int liz_nav_build(liz_app* app)
{
    xwindow* w = app->win;
    const char* cwd = app->cwd;
    size_t len = strlen(cwd);

    int n = 0;
    app->nav_sg[n].text = cwd;
    app->nav_sg[n].len = 1;
    app->nav_sg[n].end = 1;
    n++;

    size_t i = 1;
    while (i < len && n < LIZ_NAV_SEGMENTS) {
        size_t j = i;
        while (j < len && cwd[j] != '/')
            j++;
        app->nav_sg[n].text = cwd + i;
        app->nav_sg[n].len = (int)(j - i);
        app->nav_sg[n].end = j;
        n++;
        i = j;
        while (i < len && cwd[i] == '/')
            i++;
    }

    int x = LIZ_UI_PAD;
    int placed = 0;
    for (int k = 0; k < n; k++) {
        int tw = 0;
        xc_text_measure(w, app->nav_sg[k].text, app->nav_sg[k].len, app->font, &tw, NULL);
        int bw = tw + LIZ_NAV_SEG_PAD * 2;
        int lead = k > 0 ? LIZ_NAV_CHEVRON_W : 0;
        if (k > 0 && x + lead + bw > w->width - LIZ_UI_PAD)
            break;
        app->nav_sg[k].x0 = x + lead;
        app->nav_sg[k].x1 = x + lead + bw;
        x = app->nav_sg[k].x1;
        placed++;
    }

    /* segments past the edge never got coordinates, so they are dropped
     * rather than drawn and hit-tested at stale positions */
    app->nav_segments = placed;
    return placed;
}

/* The ">" between two segments. Drawn from lines rather than a glyph so it
 * stays crisp and lines up with the text whatever the font is. */
static void liz_nav_draw_chevron(xwindow* w, int cx, int cy)
{
    const int arm = LIZ_NAV_CHEVRON_ARM;
    xc_line(w, cx - arm, cy - arm, cx, cy, liz_theme_text_dim);
    xc_line(w, cx, cy, cx - arm, cy + arm, liz_theme_text_dim);
}

void liz_nav_draw(liz_app* app)
{
    xwindow* w = app->win;
    int h = LIZ_UI_NAV_H;

    xc_rect(w, 0, 0, w->width, h, liz_theme_panel);
    xc_rect(w, 0, h - 1, w->width, 1, liz_theme_panel_edge);

    if (app->nav_input.editing) {
        liz_nav_draw_edit(app);
        return;
    }

    int n = liz_nav_build(app);

    int ascent = 0, descent = 0;
    xc_font_metrics(app->font, &ascent, &descent);
    int text_y = (h - (ascent + descent)) / 2 + ascent;

    for (int k = 0; k < n; k++) {
        liz_nav_segment* sg = &app->nav_sg[k];

        if (k > 0)
            liz_nav_draw_chevron(w, (app->nav_sg[k - 1].x1 + sg->x0) / 2, h / 2);

        bool hover = app->mouse_y >= 0 && app->mouse_y < h
                  && app->mouse_x >= sg->x0 && app->mouse_x < sg->x1;
        if (hover)
            xc_rect(w, sg->x0, 1, sg->x1 - sg->x0, h - 2, liz_theme_accent_dim);

        int tw = 0;
        xc_text_measure(w, sg->text, sg->len, app->font, &tw, NULL);
        int tx = sg->x0 + ((sg->x1 - sg->x0) - tw) / 2;
        xc_text(w, tx, text_y, sg->text, sg->len, app->font);
    }
}

int liz_nav_hit(liz_app* app, int x, int y)
{
    if (app->nav_input.editing)
        return -1;
    liz_nav_build(app);
    if (y > LIZ_UI_NAV_H)
        return -1;
    for (int k = 0; k < app->nav_segments; k++) {
        if (x >= app->nav_sg[k].x0 && x < app->nav_sg[k].x1)
            return k;
    }
    return -1;
}

/* location bar: raw-path editing with tab completion */

static bool liz_nav_ci_prefix(const char* name, const char* seg, int seglen)
{
    if ((int)strlen(name) < seglen)
        return false;
    for (int i = 0; i < seglen; i++) {
        if (tolower((unsigned char)name[i]) != tolower((unsigned char)seg[i]))
            return false;
    }
    return true;
}

/* Recomputes the gray completion suffix for the last path segment. The
 * directory being completed is everything before the last "/" (or the cwd
 * when there is no slash). Three cases:
 *   - the last segment is a partial name: suffix is the remainder of the
 *     first entry that starts with it;
 *   - the last segment is empty or names an existing directory: suffix is
 *     the first child (with a leading "/" when extending), so a suggestion
 *     appears before the next character is even typed.
 * Cleared whenever the cursor is not at the end of the text. */
static void liz_nav_compute_complete(liz_app* app)
{
    liz_nav_input* ni = &app->nav_input;
    liz_editor* e = &ni->ed;
    ni->complete_len = 0;
    if (!ni->editing || e->cursor != e->len || e->len == 0)
        return;

    const char* text = e->text;
    const char* slash = NULL;
    for (int i = 0; i < e->len; i++) {
        if (text[i] == '/')
            slash = text + i;
    }

    const char* seg = slash ? slash + 1 : text;
    int seglen = e->len - (int)(seg - text);

    char dir[PATH_MAX];
    if (slash) {
        int dirlen = (int)(slash - text);
        if (dirlen == 0) {
            dir[0] = '/';
            dir[1] = '\0';
        } else {
            memcpy(dir, text, (size_t)dirlen);
            dir[dirlen] = '\0';
        }
    } else {
        snprintf(dir, sizeof(dir), "%s", app->cwd);
    }

    char canon[PATH_MAX];
    if (liz_fs_canonical(canon, sizeof(canon), dir) != 0)
        return;

    /* a complete last segment that names an existing directory is closed by
     * just "/" (Tab continues into it, it does not fill a child) */
    bool seg_is_dir = false;
    if (seglen > 0) {
        char full[PATH_MAX];
        if (liz_fs_join(full, sizeof(full), canon, seg) == 0) {
            struct stat st;
            if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
                seg_is_dir = true;
        }
    }

    if (seg_is_dir) {
        if (e->len + 1 < LIZ_EDITOR_TEXT_MAX) {
            ni->complete[0] = '/';
            ni->complete_len = 1;
            ni->complete_is_dir = true;
        }
        return;
    }

    liz_fs_entry* entries = NULL;
    size_t count = 0;
    if (liz_fs_read(canon, app->show_hidden, &entries, &count) != 0)
        return;

    if (seglen == 0) {
        /* empty last segment (the text ends in "/"): suggest the first child */
        const char* child = NULL;
        bool child_dir = false;
        for (size_t i = 0; i < count; i++) {
            if (strcmp(entries[i].name, "..") == 0)
                continue;
            child = entries[i].name;
            child_dir = entries[i].type == LIZ_FS_DIR;
            break;
        }
        if (child) {
            int cl = (int)strlen(child);
            if (e->len + cl < LIZ_EDITOR_TEXT_MAX) {
                memcpy(ni->complete, child, strlen(child));
                ni->complete_len = cl;
                ni->complete_is_dir = child_dir;
            }
        }
        liz_fs_entries_free(entries, count);
        return;
    }

    for (size_t i = 0; i < count; i++) {
        if (strcmp(entries[i].name, "..") == 0)
            continue;
        if (liz_nav_ci_prefix(entries[i].name, seg, seglen)
            && entries[i].name[seglen] != '\0') {
            const char* rem = entries[i].name + seglen;
            int cl = (int)strlen(rem);
            if (cl > 0 && e->len + cl < LIZ_EDITOR_TEXT_MAX) {
                memcpy(ni->complete, rem, (size_t)cl);
                ni->complete_len = cl;
                ni->complete_is_dir = entries[i].type == LIZ_FS_DIR;
            }
            break;
        }
    }
    liz_fs_entries_free(entries, count);
}

/* Fills the gray completion suffix into the text and moves the cursor to
 * the end. Directories get a trailing "/" so the next segment can be typed
 * (and completed) immediately. */
static void liz_nav_complete_fill(liz_app* app)
{
    liz_nav_input* ni = &app->nav_input;
    liz_editor* e = &ni->ed;
    if (ni->complete_len <= 0)
        return;
    int cl = ni->complete_len;
    if (e->len + cl >= LIZ_EDITOR_TEXT_MAX)
        return;
    liz_editor_commit_change(e); /* undoable as its own step */
    memcpy(e->text + e->len, ni->complete, (size_t)cl);
    e->len += cl;
    if (ni->complete_is_dir && (cl == 0 || ni->complete[cl - 1] != '/')
        && e->len + 1 < LIZ_EDITOR_TEXT_MAX) {
        e->text[e->len] = '/';
        e->len++;
    }
    e->text[e->len] = '\0';
    e->cursor = e->len;
    liz_nav_compute_complete(app);
}

void liz_nav_toggle_edit(liz_app* app)
{
    liz_nav_input* ni = &app->nav_input;
    if (ni->editing) {
        ni->editing = false;
        liz_editor_clear_selection(&ni->ed);
        return;
    }
    ni->editing = true;
    liz_editor_set_text(&ni->ed, app->cwd);
    ni->complete_len = 0;
    app->vim.visual_active = false;
    app->vim.pending_g = false;
}

/* Starts a new path segment when the user types a name at the end of a path
 * that already points at an existing directory: "/home/terry/Pictures" + "Si"
 * becomes "/home/terry/Pictures/Si" so the location bar completes like a
 * browser's. */
static void liz_nav_autoslash(liz_app* app)
{
    liz_editor* e = &app->nav_input.ed;
    if (e->cursor != e->len || e->len == 0 || e->text[e->len - 1] == '/')
        return;
    if (e->len + 1 >= LIZ_EDITOR_TEXT_MAX)
        return;
    char canon[PATH_MAX];
    if (liz_fs_canonical(canon, sizeof(canon), e->text) != 0)
        return;
    struct stat st;
    if (stat(canon, &st) != 0 || !S_ISDIR(st.st_mode))
        return;
    e->text[e->len] = '/';
    e->len++;
    e->text[e->len] = '\0';
    e->cursor = e->len;
}

bool liz_nav_handle_key(liz_app* app, xc_event ev)
{
    liz_nav_input* ni = &app->nav_input;
    if (!ni->editing)
        return false;

    switch (ev.key) {
    case XK_Escape:
        ni->editing = false;
        liz_editor_clear_selection(&ni->ed);
        return true;
    case XK_Return:
    case XK_KP_Enter:
        ni->editing = false;
        liz_editor_clear_selection(&ni->ed);
        liz_app_navigate(app, ni->ed.text);
        return true;
    case XK_Tab:
        liz_nav_complete_fill(app);
        return true;
    default:
        break;
    }

    /* typing a name at the end of a path that already names an existing
     * directory starts a new segment (a "/" is inserted first) */
    if (ev.nchars > 0 && (unsigned char)ev.chars[0] >= 0x20
        && ev.chars[0] != 0x7F && ev.chars[0] != '/'
        && ni->ed.cursor == ni->ed.len) {
        liz_nav_autoslash(app);
    }

    liz_editor_handle_key(&ni->ed, app->win, ev);
    liz_nav_compute_complete(app);
    return true;
}

int liz_nav_edit_click(liz_app* app, int x)
{
    liz_nav_input* ni = &app->nav_input;
    if (!ni->editing)
        return -1;

    int offset = liz_editor_index_at(&ni->ed, app->win, app->font, x);
    liz_editor_drag_start(&ni->ed, offset);
    return offset;
}

void liz_nav_edit_drag(liz_app* app, int x)
{
    liz_editor* e = &app->nav_input.ed;
    int offset = liz_editor_index_at(e, app->win, app->font, x);
    liz_editor_drag_to(e, offset);
}

void liz_nav_refresh_complete(liz_app* app)
{
    liz_nav_compute_complete(app);
}

/* Draws the raw path being edited with a text cursor, the gray completion
 * suffix and the active selection, scrolling horizontally so the cursor
 * stays visible. */
static void liz_nav_draw_edit(liz_app* app)
{
    xwindow* w = app->win;
    int h = LIZ_UI_NAV_H;
    liz_nav_input* ni = &app->nav_input;

    int ascent = 0, descent = 0;
    xc_font_metrics(app->font, &ascent, &descent);
    int text_y = (h - (ascent + descent)) / 2 + ascent;

    int view_w = w->width - LIZ_UI_PAD * 2;
    ni->ed.suffix = ni->complete_len > 0 ? ni->complete : NULL;
    ni->ed.suffix_len = ni->complete_len;
    liz_editor_draw(&ni->ed, w, LIZ_UI_PAD, text_y, view_w, app->font,
                   app->font_dim);
}
