#include "ui/editor.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "ui/theme.h"

void liz_editor_init(liz_editor* e)
{
    memset(e, 0, sizeof(*e));
    e->sel_start = -1;
    e->sel_end = -1;
    e->drag_anchor = -1;
}

void liz_editor_set_text(liz_editor* e, const char* text)
{
    size_t n = strlen(text);
    if (n >= LIZ_EDITOR_TEXT_MAX)
        n = LIZ_EDITOR_TEXT_MAX - 1;
    memcpy(e->text, text, n);
    e->text[n] = '\0';
    e->len = (int)n;
    e->cursor = e->len;
    e->sel_start = -1;
    e->sel_end = -1;
    e->drag_anchor = -1;
    e->scroll_x = 0;
    e->undo_count = 0;
    e->undo_pos = 0;
}

void liz_editor_clear_selection(liz_editor* e)
{
    e->sel_start = -1;
    e->sel_end = -1;
    e->drag_anchor = -1;
}

char* liz_editor_selected_text(const liz_editor* e)
{
    if (e->sel_start < 0)
        return NULL;
    int a = e->sel_start, b = e->sel_end;
    if (a > b) {
        int t = a;
        a = b;
        b = t;
    }
    if (a >= b)
        return NULL;
    char* out = (char*)malloc((size_t)(b - a) + 1);
    if (!out)
        return NULL;
    memcpy(out, e->text + a, (size_t)(b - a));
    out[b - a] = '\0';
    return out;
}

static void liz_editor_sel_range(const liz_editor* e, int* out_a, int* out_b)
{
    if (e->sel_start < 0) {
        *out_a = *out_b = e->cursor;
        return;
    }
    int a = e->sel_start, b = e->sel_end;
    if (a > b) {
        int t = a;
        a = b;
        b = t;
    }
    *out_a = a;
    *out_b = b;
}

/* Records the current text/cursor so the next modification can be undone.
 * Idempotent: consecutive pushes of identical state collapse to one. */
static void liz_editor_push_snapshot(liz_editor* e)
{
    if (e->undo_pos > 0) {
        liz_editor_snapshot* top = &e->undo_stack[e->undo_pos - 1];
        if (top->len == e->len && top->cursor == e->cursor
            && memcmp(top->text, e->text, (size_t)e->len) == 0)
            return;
    }

    /* truncate the redo tail (editing after an undo discards it) */
    if (e->undo_pos < e->undo_count)
        e->undo_count = e->undo_pos;

    /* drop the oldest entry when the history is full */
    if (e->undo_count >= LIZ_EDITOR_UNDO_MAX) {
        memmove(e->undo_stack, e->undo_stack + 1,
                (size_t)(LIZ_EDITOR_UNDO_MAX - 1) * sizeof(liz_editor_snapshot));
        e->undo_count--;
        e->undo_pos--;
    }

    liz_editor_snapshot* s = &e->undo_stack[e->undo_count];
    memcpy(s->text, e->text, (size_t)e->len + 1);
    s->len = e->len;
    s->cursor = e->cursor;
    e->undo_count++;
    e->undo_pos = e->undo_count;
}

void liz_editor_commit_change(liz_editor* e)
{
    liz_editor_push_snapshot(e);
}

void liz_editor_undo(liz_editor* e)
{
    if (e->undo_pos <= 0)
        return;
    e->undo_pos--;
    liz_editor_snapshot* s = &e->undo_stack[e->undo_pos];
    memcpy(e->text, s->text, (size_t)s->len + 1);
    e->len = s->len;
    e->cursor = s->cursor;
    e->sel_start = -1;
    e->sel_end = -1;
    e->drag_anchor = -1;
}

/* Deletes the range [a, b) and places the cursor at a, without recording an
 * undo step (callers that push first). */
static void liz_editor_delete_range_raw(liz_editor* e, int a, int b)
{
    if (a < 0)
        a = 0;
    if (b > e->len)
        b = e->len;
    if (b <= a)
        return;
    memmove(e->text + a, e->text + b, (size_t)(e->len - b));
    e->len -= b - a;
    e->cursor = a;
    e->text[e->len] = '\0';
    e->sel_start = -1;
    e->sel_end = -1;
}

/* Deletes the range [a, b), recording an undo step. */
static void liz_editor_delete_range(liz_editor* e, int a, int b)
{
    if (a < 0)
        a = 0;
    if (b > e->len)
        b = e->len;
    if (b <= a)
        return;
    liz_editor_push_snapshot(e);
    liz_editor_delete_range_raw(e, a, b);
}

/* Inserts `text` at the cursor, replacing any active selection, recording a
 * single undo step. */
static void liz_editor_insert(liz_editor* e, const char* text, int len)
{
    int a, b;
    liz_editor_sel_range(e, &a, &b);
    if (len > LIZ_EDITOR_TEXT_MAX - 1)
        len = LIZ_EDITOR_TEXT_MAX - 1;

    if (a == b && len == 0)
        return;

    liz_editor_push_snapshot(e);
    if (a != b)
        liz_editor_delete_range_raw(e, a, b);
    if (len > 0) {
        int room = LIZ_EDITOR_TEXT_MAX - 1 - e->len;
        if (room <= 0)
            return;
        if (len > room)
            len = room;
        memmove(e->text + e->cursor + len, e->text + e->cursor,
                (size_t)(e->len - e->cursor));
        memcpy(e->text + e->cursor, text, (size_t)len);
        e->cursor += len;
        e->len += len;
        e->text[e->len] = '\0';
    }
}

/* Moves the cursor to `newc`, optionally extending the selection from its
 * current anchor. */
static void liz_editor_move_to(liz_editor* e, int newc, bool extend)
{
    if (newc < 0)
        newc = 0;
    if (newc > e->len)
        newc = e->len;
    if (newc == e->cursor && !extend)
        return;
    if (extend) {
        if (e->sel_start < 0)
            e->sel_start = e->cursor;
        e->cursor = newc;
        e->sel_end = newc;
    } else {
        e->cursor = newc;
        e->sel_start = -1;
        e->sel_end = -1;
    }
}

static void liz_editor_move(liz_editor* e, int dir, bool extend)
{
    int newc = e->cursor;
    if (dir < 0) {
        if (newc > 0) {
            newc--;
            while (newc > 0 && ((unsigned char)e->text[newc] & 0xC0) == 0x80)
                newc--;
        }
    } else {
        if (newc < e->len) {
            newc++;
            while (newc < e->len && ((unsigned char)e->text[newc] & 0xC0) == 0x80)
                newc++;
        }
    }
    liz_editor_move_to(e, newc, extend);
}

static void liz_editor_jump(liz_editor* e, bool to_start, bool extend)
{
    liz_editor_move_to(e, to_start ? 0 : e->len, extend);
}

/* Word boundary: whitespace or '/', so path segments and filenames read
 * naturally. */
static bool liz_editor_wordsep(char c)
{
    return c == '/' || isspace((unsigned char)c);
}

/* Byte offset just before the word to the left of the cursor. */
static int liz_editor_word_left(const liz_editor* e)
{
    int i = e->cursor;
    while (i > 0 && liz_editor_wordsep(e->text[i - 1]))
        i--;
    while (i > 0 && !liz_editor_wordsep(e->text[i - 1]))
        i--;
    return i;
}

/* Byte offset just after the word to the right of the cursor. */
static int liz_editor_word_right(const liz_editor* e)
{
    int i = e->cursor;
    while (i < e->len && !liz_editor_wordsep(e->text[i]))
        i++;
    while (i < e->len && liz_editor_wordsep(e->text[i]))
        i++;
    return i;
}

/* Deletes backward (or forward) to the previous/next word boundary. */
static void liz_editor_delete_word(liz_editor* e, bool forward)
{
    if (forward) {
        int i = e->cursor;
        while (i < e->len && !liz_editor_wordsep(e->text[i]))
            i++;
        while (i < e->len && liz_editor_wordsep(e->text[i]))
            i++;
        liz_editor_delete_range(e, e->cursor, i);
    } else {
        int i = e->cursor;
        if (i > 0 && liz_editor_wordsep(e->text[i - 1])) {
            /* a separator sits directly before the cursor: drop just it,
             * so repeated Ctrl+Backspace clears trailing slashes one at a
             * time instead of stalling */
            i--;
        } else {
            while (i > 0 && !liz_editor_wordsep(e->text[i - 1]))
                i--;
        }
        liz_editor_delete_range(e, i, e->cursor);
    }
}

static void liz_editor_backspace(liz_editor* e, bool word)
{
    int a, b;
    liz_editor_sel_range(e, &a, &b);
    if (a != b) {
        liz_editor_delete_range(e, a, b);
        return;
    }
    if (word)
        liz_editor_delete_word(e, false);
    else if (e->cursor > 0) {
        int i = e->cursor;
        do {
            i--;
        } while (i > 0 && ((unsigned char)e->text[i] & 0xC0) == 0x80);
        liz_editor_delete_range(e, i, e->cursor);
    }
}

static void liz_editor_delete(liz_editor* e, bool word)
{
    int a, b;
    liz_editor_sel_range(e, &a, &b);
    if (a != b) {
        liz_editor_delete_range(e, a, b);
        return;
    }
    if (word)
        liz_editor_delete_word(e, true);
    else if (e->cursor < e->len) {
        int i = e->cursor + 1;
        while (i < e->len && ((unsigned char)e->text[i] & 0xC0) == 0x80)
            i++;
        liz_editor_delete_range(e, e->cursor, i);
    }
}

/* Copies the selection to the CLIPBOARD. Returns true when something was
 * copied. */
static bool liz_editor_copy(liz_editor* e, xwindow* w)
{
    char* sel = liz_editor_selected_text(e);
    if (!sel)
        return false;
    int len = (int)strlen(sel);
    xc_clipboard_set(w, sel, len);
    free(sel);
    return true;
}

bool liz_editor_handle_key(liz_editor* e, xwindow* w, xc_event ev)
{
    bool ctrl = (ev.state & ControlMask) != 0;
    bool shift = (ev.state & ShiftMask) != 0;

    /* these keys belong to the caller: commit / cancel / complete */
    if (ev.key == XK_Escape || ev.key == XK_Return || ev.key == XK_KP_Enter
        || ev.key == XK_Tab)
        return false;

    switch (ev.key) {
    case XK_BackSpace:
        liz_editor_backspace(e, ctrl);
        return true;
    case XK_Delete:
        liz_editor_delete(e, ctrl);
        return true;
    case XK_Left:
        if (ctrl)
            liz_editor_move_to(e, liz_editor_word_left(e), shift);
        else
            liz_editor_move(e, -1, shift);
        return true;
    case XK_Right:
        if (ctrl)
            liz_editor_move_to(e, liz_editor_word_right(e), shift);
        else
            liz_editor_move(e, +1, shift);
        return true;
    case XK_Home:
        liz_editor_jump(e, true, shift);
        return true;
    case XK_End:
        liz_editor_jump(e, false, shift);
        return true;
    case XK_z:
        if (ctrl) {
            liz_editor_undo(e);
            return true;
        }
        break;
    case XK_a:
        if (ctrl) {
            e->sel_start = 0;
            e->sel_end = e->len;
            e->cursor = e->len;
            return true;
        }
        break;
    case XK_c:
        if (ctrl) {
            liz_editor_copy(e, w);
            return true;
        }
        break;
    case XK_x:
        if (ctrl) {
            if (liz_editor_copy(e, w))
                liz_editor_delete_range(e, e->sel_start < e->sel_end
                                           ? e->sel_start : e->sel_end,
                                       e->sel_start > e->sel_end
                                           ? e->sel_start : e->sel_end);
            return true;
        }
        break;
    case XK_v:
        if (ctrl) {
            xc_clipboard_request(w);
            return true;
        }
        break;
    case XK_Insert:
        if (shift) {
            xc_clipboard_request(w);
            return true;
        }
        if (ctrl) {
            liz_editor_copy(e, w);
            return true;
        }
        break;
    default:
        break;
    }

    /* printable text, inserted at the cursor (replacing any selection) */
    if (ev.nchars > 0 && (unsigned char)ev.chars[0] >= 0x20
        && ev.chars[0] != 0x7F) {
        liz_editor_insert(e, ev.chars, ev.nchars);
        return true;
    }
    return false;
}

void liz_editor_paste_text(liz_editor* e, const char* text, int len)
{
    if (!text || len <= 0)
        return;
    char cleaned[LIZ_EDITOR_TEXT_MAX];
    int n = 0;
    for (int i = 0; i < len && n < LIZ_EDITOR_TEXT_MAX - 1; i++) {
        char c = text[i];
        if (c == '\r' || c == '\n' || c == '\t')
            c = ' ';
        cleaned[n++] = c;
    }
    cleaned[n] = '\0';
    liz_editor_insert(e, cleaned, n);
}

/* Byte offset under window pixel x (which is relative to the editor's draw
 * origin, as set by the last liz_editor_draw call). */
int liz_editor_index_at(liz_editor* e, xwindow* w, xc_font* f, int x)
{
    int rel = (x - e->origin_x) + e->scroll_x;
    if (rel < 0)
        rel = 0;

    int best = 0;
    int bestd = rel;
    int i = 0;
    while (i <= e->len) {
        int w2 = 0;
        xc_text_measure(w, e->text, i, f, &w2, NULL);
        int d = w2 - rel;
        if (d < 0)
            d = -d;
        if (d <= bestd) {
            bestd = d;
            best = i;
        }
        if (i == e->len)
            break;
        i++;
        while (i < e->len && ((unsigned char)e->text[i] & 0xC0) == 0x80)
            i++;
    }
    return best;
}

void liz_editor_drag_start(liz_editor* e, int offset)
{
    if (offset < 0)
        offset = 0;
    if (offset > e->len)
        offset = e->len;
    e->sel_start = offset;
    e->sel_end = offset;
    e->cursor = offset;
    e->drag_anchor = offset;
}

void liz_editor_drag_to(liz_editor* e, int offset)
{
    if (e->drag_anchor < 0)
        return;
    if (offset < 0)
        offset = 0;
    if (offset > e->len)
        offset = e->len;
    e->cursor = offset;
    e->sel_end = offset;
}

void liz_editor_drag_end(liz_editor* e)
{
    e->drag_anchor = -1;
    /* a plain click: collapse the zero-width selection */
    if (e->sel_start >= 0 && e->sel_start == e->sel_end)
        liz_editor_clear_selection(e);
}

void liz_editor_draw(liz_editor* e, xwindow* w, int x, int y, int max_width,
                    xc_font* f, xc_font* fdim)
{
    if (max_width <= 0)
        return;

    int tw = 0, cw = 0, prew = 0;
    xc_text_measure(w, e->text, e->len, f, &tw, NULL);
    if (e->suffix && e->suffix_len > 0)
        xc_text_measure(w, e->suffix, e->suffix_len, fdim, &cw, NULL);
    xc_text_measure(w, e->text, e->cursor, f, &prew, NULL);

    int end = prew + (e->cursor == e->len ? cw : 0);
    int off = end > max_width ? end - max_width : 0;
    e->scroll_x = off;
    e->origin_x = x;
    int tx = x - off;

    int ascent = 0, descent = 0;
    xc_font_metrics(f, &ascent, &descent);
    int x1 = x + max_width;

    /* selection highlight, clipped to the view */
    if (e->sel_start >= 0 && e->sel_start != e->sel_end) {
        int a = e->sel_start, b = e->sel_end;
        if (a > b) {
            int t = a;
            a = b;
            b = t;
        }
        int wa = 0, wb = 0;
        xc_text_measure(w, e->text, a, f, &wa, NULL);
        xc_text_measure(w, e->text, b, f, &wb, NULL);
        int hx0 = tx + wa;
        int hx1 = tx + wb;
        if (hx0 < x)
            hx0 = x;
        if (hx1 > x1)
            hx1 = x1;
        if (hx1 > hx0)
            xc_rect(w, hx0, y - ascent, hx1 - hx0, ascent + descent,
                    liz_theme_sel_bg);
    }

    if (e->len > 0)
        xc_text(w, tx, y, e->text, e->len, f);
    if (e->suffix && e->suffix_len > 0)
        xc_text(w, tx + tw, y, e->suffix, e->suffix_len, fdim);

    /* caret */
    int cx = tx + prew;
    if (cx >= x && cx <= x1)
        xc_rect(w, cx, y - ascent, 2, ascent + descent, liz_theme_text);
}
