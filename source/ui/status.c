#include "ui/status.h"

#include <stdio.h>
#include <string.h>

#include "ui/delete.h"
#include "ui/newfolder.h"
#include "ui/rename.h"
#include "ui/theme.h"

void liz_status_draw(liz_app* app)
{
    xwindow* w = app->win;
    int h = LIZ_UI_STATUS_H;
    int y0 = w->height - h;

    xc_rect(w, 0, y0, w->width, h, liz_theme_panel);
    xc_rect(w, 0, y0, w->width, 1, liz_theme_panel_edge);

    if (app->rename.active) {
        liz_rename_draw(app);
        return;
    }

    if (app->newfolder.active) {
        liz_newfolder_draw(app);
        return;
    }

    if (app->del.active) {
        liz_delete_draw(app);
        return;
    }

    if (app->chooser.name_editing) {
        /* "Save name: <editor>" prompt, mirroring the rename prompt */
        int ascent = 0, descent = 0;
        xc_font_metrics(app->font_dim, &ascent, &descent);
        int text_y = y0 + (h - (ascent + descent)) / 2 + ascent;

        char left[LIZ_FS_NAME_MAX + 16];
        int leftlen = snprintf(left, sizeof(left), "Save name: ");
        int lx = LIZ_UI_PAD;
        lx += xc_text(w, lx, text_y, left, leftlen, app->font_dim);
        int max_w = w->width - LIZ_UI_PAD - lx;
        if (app->chooser.name_err[0] != '\0') {
            int errlen = (int)strlen(app->chooser.name_err);
            int ew = 0;
            xc_text_measure(w, app->chooser.name_err, errlen, app->font_error, &ew, NULL);
            int ex = w->width - LIZ_UI_PAD - ew;
            xc_text(w, ex, text_y, app->chooser.name_err, errlen, app->font_error);
            max_w -= ew + LIZ_UI_PAD * 2;
        }
        if (max_w < 0)
            max_w = 0;
        liz_editor_draw(&app->chooser.name_ed, w, lx, text_y, max_w,
                       app->font, app->font_dim);
        return;
    }

    int ascent = 0, descent = 0;
    xc_font_metrics(app->font_dim, &ascent, &descent);
    int text_y = y0 + (h - (ascent + descent)) / 2 + ascent;

    /* in file-picker mode the requested type is prepended to whatever the
     * status bar would normally show, so vim mode/command/search stay put */
    char picker_buf[LIZ_FS_NAME_MAX + 48];
    const char* picker = NULL;
    if (app->chooser.active) {
        if (app->chooser.mode == LIZ_CHOOSER_DIRECTORY) {
            snprintf(picker_buf, sizeof(picker_buf), "[SELECT DIRECTORY]");
        } else if (app->chooser.mode == LIZ_CHOOSER_SAVE) {
            snprintf(picker_buf, sizeof(picker_buf), "[SAVE AS '%s', Ctrl+Enter here]",
                     app->chooser.save_name);
        } else if (app->chooser.multiple) {
            snprintf(picker_buf, sizeof(picker_buf), "[SELECT FILES]");
        } else {
            snprintf(picker_buf, sizeof(picker_buf), "[SELECT FILE]");
        }
        if (app->chooser.filter_count > 0) {
            int idx = app->chooser.current_filter;
            if (idx < 0 || idx >= app->chooser.filter_count)
                idx = 0;
            size_t used = strlen(picker_buf);
            snprintf(picker_buf + used, sizeof(picker_buf) - used,
                     "  %s%s", app->chooser.filters[idx].name,
                     app->chooser.filter_count > 1 ? " (Tab)" : "");
        }
        picker = picker_buf;
    }

    if (app->vim.mode == LIZ_VIM_COMMAND) {
        /* vim-style command line: the literal "/query" or ":cmd" being
         * typed, replacing the item count until it's submitted */
        char left[LIZ_VIM_CMDLINE_MAX + LIZ_FS_NAME_MAX + 64];
        int leftlen = 0;
        if (picker)
            leftlen += snprintf(left + leftlen, sizeof(left) - (size_t)leftlen,
                                "%s   ", picker);
        leftlen += snprintf(left + leftlen, sizeof(left) - (size_t)leftlen,
                            "%c%s", app->vim.cmd_prefix, app->vim.cmdline);
        int lx = LIZ_UI_PAD;
        lx += xc_text(w, lx, text_y, left, leftlen, app->font);
        xc_rect(w, lx + 1, y0 + 4, 2, h - 8, liz_theme_text_dim); /* cursor */
    } else {
        char left[LIZ_FS_NAME_MAX + 128];
        const char* mode = "NORMAL";
        if (app->vim.visual_active)
            mode = app->vim.visual_line ? "VISUAL LINE" : "VISUAL";
        int leftlen = 0;
        if (picker)
            leftlen += snprintf(left + leftlen, sizeof(left) - (size_t)leftlen,
                                "%s   ", picker);
        leftlen += snprintf(left + leftlen, sizeof(left) - (size_t)leftlen,
                            "%s   %zu items", mode, app->entry_count);
        int selcount = liz_app_selection_count(app);
        if (selcount > 0)
            leftlen += snprintf(left + leftlen, sizeof(left) - (size_t)leftlen,
                                "   %d selected", selcount);
        if (app->fileclip.count > 0)
            leftlen += snprintf(left + leftlen, sizeof(left) - (size_t)leftlen,
                                "   [%d %s]", app->fileclip.count,
                                app->fileclip.cut ? "cut" : "copied");
        xc_text(w, LIZ_UI_PAD, text_y, left, leftlen, app->font_dim);
    }

    if (app->selected >= 0 && (size_t)app->selected < app->entry_count) {
        liz_fs_entry* e = &app->entries[app->selected];

        char right[512];
        int rightlen;
        if (e->type == LIZ_FS_DIR || e->type == LIZ_FS_LINK
            || e->type == LIZ_FS_VIRTUAL) {
            rightlen = snprintf(right, sizeof(right), "%s", e->name);
        } else {
            char sizebuf[16];
            liz_ui_format_size(sizebuf, sizeof(sizebuf), e->size);
            rightlen = snprintf(right, sizeof(right), "%s  (%s)", e->name, sizebuf);
        }

        int tw = 0;
        xc_text_measure(w, right, rightlen, app->font_dim, &tw, NULL);
        xc_text(w, w->width - LIZ_UI_PAD - tw, text_y, right, rightlen, app->font_dim);
    }
}
