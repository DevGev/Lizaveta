/* editor.h - shared single-line text editor with selection and clipboard. */

#ifndef LIZ_EDITOR_H
#define LIZ_EDITOR_H

#include <limits.h>
#include <stdbool.h>

#include "rendering/x11/xc.h"

#define LIZ_EDITOR_TEXT_MAX PATH_MAX

/* Snapshot of the editor before a modification, for undo. */
typedef struct {
    char text[LIZ_EDITOR_TEXT_MAX];
    int len;
    int cursor;
} liz_editor_snapshot;

#define LIZ_EDITOR_UNDO_MAX 64

/* A single-line, horizontal-scrolling text field.
 *
 * Selection: when sel_start >= 0 the selected byte range is
 * [min(sel_start, sel_end), max(sel_start, sel_end)). A drag selection
 * starts at drag_anchor; drag_to/ drag_end close it. The selection is
 * highlighted in the theme's selection background and is copied to the X
 * CLIPBOARD with Ctrl+C / Ctrl+Insert, cut with Ctrl+X, replaced by typed
 * text or paste. Pasting is asynchronous: the app routes the clipboard
 * callback to liz_editor_paste_text. */
typedef struct {
    char text[LIZ_EDITOR_TEXT_MAX];
    int len;           /* bytes in text */
    int cursor;        /* byte offset of the text cursor */
    int sel_start;     /* selection anchor offset, -1 = none */
    int sel_end;       /* selection limit offset */
    int drag_anchor;   /* byte offset where the current drag started, -1 */
    int scroll_x;      /* left edge offset in pixels, set by liz_editor_draw */
    int origin_x;      /* draw x position in window pixels, set by liz_editor_draw */
    const char* suffix;    /* gray completion suffix after the text, may be NULL */
    int suffix_len;

    /* undo history: snapshots pushed before each modification (the state at
     * undo_pos-1 is what a Ctrl+Z restores). Cleared by init/set_text. */
    liz_editor_snapshot undo_stack[LIZ_EDITOR_UNDO_MAX];
    int undo_count;
    int undo_pos;
} liz_editor;

void liz_editor_init(liz_editor* e);

/* Replaces the text and places the cursor at the end, clearing the
 * selection. `text` is truncated to the editor capacity. */
void liz_editor_set_text(liz_editor* e, const char* text);

/* Clears the active selection without touching the text (used when an
 * editing session ends). */
void liz_editor_clear_selection(liz_editor* e);

/* Returns a heap-allocated copy of the selected text, or NULL when there is
 * no selection. Caller frees. */
char* liz_editor_selected_text(const liz_editor* e);

/* Handles one key event. Returns true when the key was consumed. Escape,
 * Return and Tab are never consumed (the caller uses them to commit or
 * cancel the editing session). */
bool liz_editor_handle_key(liz_editor* e, xwindow* w, xc_event ev);

/* Restores the editor to the state before the last modification. */
void liz_editor_undo(liz_editor* e);

/* Pushes an undo snapshot of the current text/cursor. Call this before
 * code that edits e->text directly, so the change can be undone as its own
 * step. */
void liz_editor_commit_change(liz_editor* e);

/* Inserts pasted clipboard data at the cursor, replacing any selection.
 * CR, LF and TAB are collapsed into spaces. */
void liz_editor_paste_text(liz_editor* e, const char* text, int len);

/* Byte offset under pixel x, based on the scroll offset from the last
 * liz_editor_draw call. */
int liz_editor_index_at(liz_editor* e, xwindow* w, xc_font* f, int x);

/* Mouse selection drag. offset is the byte offset from liz_editor_index_at.
 * drag_start begins a selection (a plain click leaves a zero-width one,
 * which drag_end collapses); drag_to extends it as the pointer moves. */
void liz_editor_drag_start(liz_editor* e, int offset);
void liz_editor_drag_to(liz_editor* e, int offset);
void liz_editor_drag_end(liz_editor* e);

/* Draws the editor at baseline (x, y) with the caret and a gray completion
 * suffix, scrolling horizontally so the caret stays inside max_width
 * pixels. The selection is filled with liz_theme_sel_bg. */
void liz_editor_draw(liz_editor* e, xwindow* w, int x, int y, int max_width,
                    xc_font* f, xc_font* fdim);

#endif /* LIZ_EDITOR_H */
