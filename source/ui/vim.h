/* vim.h - vim-style modal input for the file list. */

#ifndef LIZ_VIM_H
#define LIZ_VIM_H

#include <stdbool.h>

struct liz_app;
typedef struct liz_app liz_app;

#include "rendering/x11/xc.h"

#define LIZ_VIM_CMDLINE_MAX 256
#define LIZ_VIM_QUERY_MAX   LIZ_VIM_CMDLINE_MAX

typedef enum {
    LIZ_VIM_NORMAL,
    LIZ_VIM_COMMAND,
} liz_vim_mode;

typedef struct {
    liz_vim_mode mode;

    /* live COMMAND-mode text entry */
    char cmd_prefix;    /* '/' or ':', valid while mode == LIZ_VIM_COMMAND */
    char cmdline[LIZ_VIM_CMDLINE_MAX];
    int  cmdline_len;

    /* search */
    int  search_anchor;                  /* selected row when '/' was pressed */
    bool search_active;                  /* highlight + n/N enabled */
    char search_query[LIZ_VIM_QUERY_MAX]; /* pattern currently highlighted */
    bool prev_search_active;             /* saved on '/', restored on cancel */
    char prev_query[LIZ_VIM_QUERY_MAX];

    bool pending_g; /* first 'g' of a 'gg' motion seen */
    bool pending_d; /* first 'd' of a 'dd' delete command seen */

    /* VISUAL mode: a selection anchored at visual_anchor spans to the
     * focused row (app->selected). visual_line distinguishes V from v. */
    bool visual_active;
    bool visual_line;
    int visual_anchor;

    /* "." repeat: the last command line submitted via '/' or ':' */
    bool has_last_cmd;
    char last_cmd_prefix;
    char last_cmd_text[LIZ_VIM_CMDLINE_MAX];
} liz_vim_state;

/* Resets to NORMAL mode with no active search. */
void liz_vim_init(liz_vim_state* vim);

/* Handles one key event while in NORMAL mode.
 * Returns true if the key was a vim binding and was consumed. */
bool liz_vim_handle_normal_key(liz_app* app, xc_event ev);

/* Handles one key event while in VISUAL mode.
 * Returns true if consumed; keys it does not handle exit VISUAL and return
 * false so normal mode processing continues. */
bool liz_vim_handle_visual_key(liz_app* app, xc_event ev);

/* Handles one key event while in COMMAND mode (typing a search or an ex
 * command). */
void liz_vim_handle_command_key(liz_app* app, xc_event ev);

/* True if `name` contains the active search query (case-insensitive).
 * Always false when no search is active. Used by the file list to draw
 * search highlights. */
bool liz_vim_name_matches(const liz_app* app, const char* name);

#endif /* LIZ_VIM_H */
