/* app.h - application state and the shared layout contract between the app loop and the UI widgets. */

#ifndef LIZ_APP_H
#define LIZ_APP_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

#include "rendering/x11/xc.h"
#include "fs/fs.h"
#include "ui/editor.h"
#include "ui/ui.h"
#include "ui/vim.h"

#define LIZ_NAV_SEGMENTS 64

/* A second Button1 click on the same row within this many seconds counts as
 * a double click (open); slower, and it's just a second single click. */
#define LIZ_DOUBLE_CLICK_SECS 0.4

/* Button1 must move at least this many pixels from where it went down
 * before a press-and-move is treated as the start of a drag rather than
 * (the first half of) a click. Keeps a deliberate second click meant to
 * grab and drag a file from being read as an instant double click. */
#define LIZ_DRAG_THRESHOLD_PX 4

/* A clickable breadcrumb in the navigation bar. `text`/`len` describe the
 * segment label (which points into liz_app.cwd), `end` is the offset into
 * cwd where the segment ends, so the path it represents is cwd[0..end).
 * `x0`/`x1` are the pixel bounds of the hit area. */
typedef struct {
    const char* text;
    int len;
    size_t end;
    int x0;
    int x1;
} liz_nav_segment;

/* A quick link in the sidebar: the label shown and the path it navigates to.
 * `dev` is the underlying block device for device entries; it is empty for
 * pinned entries. `icon` is the freedesktop icon name to draw beside the
 * label. */
typedef struct {
    char label[LIZ_FS_NAME_MAX];
    char path[PATH_MAX];
    char dev[64];
    char icon[64];
} liz_sidebar_entry;

#define LIZ_SIDEBAR_PINNED_MAX 16
#define LIZ_SIDEBAR_DEVICES_MAX 16

typedef struct {
    liz_sidebar_entry pinned[LIZ_SIDEBAR_PINNED_MAX];
    int pinned_count;
    liz_sidebar_entry devices[LIZ_SIDEBAR_DEVICES_MAX];
    int devices_count;
    int hover_item; /* flattened index into pinned+devices, -1 when none */
} liz_sidebar_state;

#define LIZ_NAV_TEXT_MAX PATH_MAX

/* Location-bar editing state: while `editing` is true the nav bar shows the
 * raw cwd path with a text cursor and, when possible, a gray completion
 * suffix for the last segment that Tab fills in. */
typedef struct {
    bool editing;
    liz_editor ed;        /* the raw path being edited */
    char complete[LIZ_NAV_TEXT_MAX]; /* gray completion suffix */
    int complete_len;
    bool complete_is_dir; /* completion target is a directory (tab appends '/') */
} liz_nav_input;

/* In-progress rename: the status bar shows `Rename {old_name} -> ` plus the
 * editor. Enter commits with rename(2), Escape cancels. Failures leave an
 * error message on the right instead of dismissing the prompt. */
typedef struct {
    bool active;
    int row;                  /* row being renamed (into app->entries) */
    char old_name[LIZ_FS_NAME_MAX];
    liz_editor ed;
    char err[256];            /* non-empty while a rename failed */
} liz_rename_state;

/* In-progress delete confirmation (vim `d`/`dd`): the status bar asks for a
 * yes/no. y/Enter deletes, n/Escape/anything else cancels. Failures keep
 * the prompt open with an error on the right. `rows` snapshots the entries
 * to delete at prompt time; the listing is stable while the prompt is up. */
#define LIZ_DELETE_MAX 4096

typedef struct {
    bool active;
    int rows[LIZ_DELETE_MAX];
    int row_count;
    char first_name[LIZ_FS_NAME_MAX];
    char err[256];            /* non-empty while a delete failed */
} liz_delete_state;

/* File clipboard for copy/cut/paste of the selection. Paths are absolute
 * and heap-owned. A cut moves the entries on paste and clears the clipboard;
 * a copy leaves it for repeated pastes. */
typedef struct {
    char** paths;
    int count;
    bool cut;
} liz_fileclip;

/* In-progress "create folder" prompt (right-click menu): the status bar
 * shows "New folder: " plus the editor, same shape as the rename prompt.
 * Enter commits with mkdir(2), Escape cancels. */
typedef struct {
    bool active;
    liz_editor ed;
    char err[256];
} liz_newfolder_state;

/* Right-click context menu. `row` is the list row that was right-clicked
 * (-1 for empty space); the item set is decided once at open time based on
 * the selection at that point. Sidebar menus (source == LIZ_MENU_SRC_SIDEBAR)
 * instead carry the sidebar entry they were opened on. */
typedef enum {
    LIZ_MENU_OPEN,
    LIZ_MENU_RENAME,
    LIZ_MENU_COPY,
    LIZ_MENU_CUT,
    LIZ_MENU_NEW_FOLDER,
    LIZ_MENU_TOGGLE_HIDDEN,
    LIZ_MENU_UNMOUNT,
    LIZ_MENU_OPEN_NEW_WINDOW,
    LIZ_MENU_TOGGLE_SIDEBAR,
} liz_menu_action;

typedef enum {
    LIZ_MENU_SRC_LIST,
    LIZ_MENU_SRC_SIDEBAR,
} liz_menu_source;

typedef struct {
    liz_menu_action action;
    const char* label; /* points at a string literal; never owned/freed */
} liz_menu_item;

#define LIZ_MENU_ITEMS_MAX 8

typedef struct {
    bool active;
    int x, y;       /* top-left pixel position, clamped to stay on screen */
    liz_menu_item items[LIZ_MENU_ITEMS_MAX];
    int item_count;
    int hover;      /* hovered item index, -1 when none */
    int row;        /* list row right-clicked, -1 when the click was on empty space */
    liz_menu_source source;  /* which view the menu was opened from */
    int sidebar_index;      /* sidebar entry index (sidebar menus) */
    bool sidebar_is_device; /* true: devices[] entry, false: pinned[] */
} liz_context_menu;

/* One recorded jump-history location: a directory plus the row that was
 * focused there. */
typedef struct {
    char path[PATH_MAX];
    int row;
} liz_jump_entry;

#define LIZ_JUMPLIST_MAX 32

/* File-picker mode (`lizaveta --filechooser`): instead of a general file
 * manager the app confirms a choice (Enter/l/q) or cancels (Esc), writes the
 * chosen absolute paths one per line to an output file or stdout, and quits.
 *
 * Per-mode semantics (keys behave like normal mode; opening a file selects
 * it):
 *   OPEN        - Enter/l/double-click on a file confirms it; directories
 *                 navigate. A multi-selection (vim VISUAL, drag, or
 *                 shift-click) is confirmed in full with Enter.
 *   DIRECTORY   - l/Enter navigates into directories; q chooses the current
 *                 directory (the one being viewed); files are not selectable.
 *   SAVE        - Enter confirms <cwd>/<save_name>; r edits the name; l/Enter
 *                 on an existing file chooses that path as the save target. */
typedef enum {
    LIZ_CHOOSER_OPEN,
    LIZ_CHOOSER_DIRECTORY,
    LIZ_CHOOSER_SAVE,
} liz_chooser_mode;

/* One extension/glob filter group (what a portal FileChooser request's
 * "filters" option or --filter on the command line supplies): a label shown
 * in the status bar and the shell-style glob patterns that decide which
 * files are listed. Directories always pass a filter regardless of its
 * patterns, so navigation is never blocked. */
#define LIZ_CHOOSER_FILTER_NAME_MAX 128
#define LIZ_CHOOSER_FILTER_PATTERNS_MAX 16
#define LIZ_CHOOSER_FILTER_PATTERN_MAX 64
#define LIZ_CHOOSER_FILTERS_MAX 16

typedef struct {
    char name[LIZ_CHOOSER_FILTER_NAME_MAX];
    char patterns[LIZ_CHOOSER_FILTER_PATTERNS_MAX][LIZ_CHOOSER_FILTER_PATTERN_MAX];
    int pattern_count;
} liz_chooser_filter;

typedef struct {
    bool active;
    liz_chooser_mode mode;
    bool multiple;             /* OPEN mode only: accumulate a multi-selection */
    char out_path[PATH_MAX];   /* file to write chosen paths to; "" = stdout */
    char start_dir[PATH_MAX];  /* initial directory */
    char save_name[LIZ_FS_NAME_MAX]; /* suggested filename in SAVE mode */
    int exit_code;             /* process exit code once done/cancelled */

    /* extension filtering (--filter / --filter-index, or a portal request's
     * "filters"/"current_filter" options): entries whose name matches none
     * of filters[current_filter]'s patterns are hidden from the listing.
     * filter_count == 0 means "no filtering, show everything". */
    liz_chooser_filter filters[LIZ_CHOOSER_FILTERS_MAX];
    int filter_count;
    int current_filter;

    /* in-progress "save as" name prompt (SAVE mode, or auto-opened when
     * SAVE mode starts) */
    bool name_editing;
    liz_editor name_ed;
    char name_err[256];
} liz_chooser;

typedef struct liz_app {
    xwindow* win;

    xc_font* font;       /* primary text */
    xc_font* font_bold;  /* selected rows, titles */
    xc_font* font_dim;   /* secondary text: sizes, status */
    xc_font* font_accent;/* directories, links */
    xc_font* font_error; /* error messages */

    char cwd[PATH_MAX];
    liz_fs_entry* entries;
    size_t entry_count;

    int selected;   /* row index into entries, -1 when empty */
    int scroll;     /* first visible row */
    int hover_row;  /* row under the pointer, -1 when none */
    int last_list_visible; /* visible rows last frame; area-changes reframe the selection */

    /* multi-selection: `selected` is the focused row, `sel` marks every row
     * that is part of the selection. `anchor_row` is the base of range
     * selections (Ctrl+Shift+Click) and VISUAL mode. */
    bool* sel;
    int anchor_row;

    liz_nav_segment nav_sg[LIZ_NAV_SEGMENTS];
    int nav_segments;

    liz_nav_input nav_input;

    liz_rename_state rename;

    liz_sidebar_state sidebar;

    bool show_hidden;     /* hidden entries shown/grouped in the list */
    bool sidebar_visible; /* left Pinned+Devices panel (default on) */

    /* Jump history: `back` holds locations we came from, `fwd` the ones we
     * can return to. A manual navigation pushes the previous location onto
     * `back` and clears `fwd`. */
    liz_jump_entry jump_back[LIZ_JUMPLIST_MAX];
    int jump_back_count;
    liz_jump_entry jump_fwd[LIZ_JUMPLIST_MAX];
    int jump_fwd_count;
    bool jump_suppress; /* set while a jump navigates, so it is not re-recorded */

    liz_delete_state del;
    liz_fileclip fileclip;
    liz_newfolder_state newfolder;
    liz_context_menu menu;

    liz_chooser chooser; /* active only in --filechooser mode */

    int mouse_x;
    int mouse_y;

    /* click / drag tracking for the file list: `press_*` describe the most
     * recent Button1 press so motion can tell a click apart from the start
     * of a drag, and `last_click_*` remember the previous *release* so a
     * second click can be recognized as a double click only within
     * LIZ_DOUBLE_CLICK_SECS of it (see file_list press/release handling). */
    bool mouse_down;
    int press_row;
    int press_x;
    int press_y;
    bool press_defer_collapse; /* plain click landed on an already multi-selected row */
    bool press_plain;          /* press had no Shift/Control -- only these open on release */
    bool dragging;             /* an XDND drag is (or was) in flight for this press */

    double last_click_time;
    int last_click_row;

    /* XDND drop-target feedback (driven by the on_dnd_* window callbacks):
     * while an accepted drag is over the file list, dnd_row is the directory
     * row that will receive the drop (-1 means the current directory). */
    bool dnd_active;
    int dnd_row;
    int dnd_x, dnd_y;

    liz_vim_state vim;

    bool quit;
} liz_app;

/* Lifecycle. Returns 0 on success, -1 on failure. */
int liz_app_init(liz_app* app);
void liz_app_quit(liz_app* app);

/* Redraws the whole window. */
void liz_app_render(liz_app* app);

/* Handles a single window event, mutating state and repainting as needed. */
void liz_app_handle_event(liz_app* app, xc_event ev);

/* Changes the current directory and reloads the listing. */
void liz_app_navigate(liz_app* app, const char* path);

/* Like liz_app_navigate, but for a path that may name a file rather than a
 * directory: navigates to the parent and selects that entry, instead of
 * failing or silently opening the wrong place. Directories behave exactly
 * like liz_app_navigate. Used for `lizaveta PATH` on the command line and by
 * the FileManager1 D-Bus interface ("Show in folder"). */
void liz_app_navigate_and_select(liz_app* app, const char* path);

/* Opens the entry at `row`: directories navigate, files are opened with the
 * desktop default application (xdg-open), symlinks follow their target. */
void liz_app_open_row(liz_app* app, int row);

/* Opens `path` with the default application, detached from the app. */
void liz_app_open_file(liz_app* app, const char* path);

/* Opens a terminal emulator whose working directory is `path`, detached
 * from the app (st is used if $TERMINAL is unset). */
void liz_app_open_terminal(liz_app* app, const char* path);

/* Opens a second lizaveta window at `path`, detached from the app. */
void liz_app_open_new_window(liz_app* app, const char* path);

/* Safely unmounts a device: udisksctl when its block device is known,
 * plain umount on the mount point as a fallback. Detached from the app. */
void liz_app_unmount_device(liz_app* app, const char* dev, const char* mountpoint);

/* Navigates to the parent of the current directory. */
void liz_app_go_parent(liz_app* app);

/* Sets the selected row, clamping to the entry range and scrolling it into
 * view. Shared by mouse/keyboard navigation and vim motions. Does not touch
 * the multi-selection. */
void liz_app_set_selected(liz_app* app, int row);

/* Navigates to the user's home directory. */
void liz_app_go_home(liz_app* app);

/* Toggles whether hidden entries appear in the listing. */
void liz_app_toggle_hidden(liz_app* app);

/* Toggles the left sidebar (Pinned+Devices panel). */
void liz_app_toggle_sidebar(liz_app* app);

/* Walks backwards/forwards through previously visited locations, restoring
 * the focused row in each directory. */
void liz_app_jump_back(liz_app* app);
void liz_app_jump_fwd(liz_app* app);

/* Deselects every row. */
void liz_app_clear_selection(liz_app* app);

/* Selects the range between `a` and `b`. */
void liz_app_select_range(liz_app* app, int a, int b);

/* Flips the selected state of `row`. */
void liz_app_toggle_selection(liz_app* app, int row);

/* Number of selected rows (excludes the focused row when it is not marked). */
int liz_app_selection_count(const liz_app* app);

/* True when `row` is part of the selection. */
bool liz_app_row_selected(const liz_app* app, int row);

/* Fills `rows` (capacity `cap`) with the selected row indices, or the
 * focused row when nothing is selected. Returns the count. */
int liz_app_collect_selection(liz_app* app, int* rows, int cap);

/* File clipboard operations on the selection: stages a copy or a cut
 * (move), then pastes the staged entries into the current directory
 * (skipping existing targets). A successful cut-paste clears the clipboard;
 * a copy stays for repeated pastes. */
void liz_app_copy_selection(liz_app* app);
void liz_app_cut_selection(liz_app* app);
void liz_app_paste(liz_app* app);

/* Helper shared by the app loop and widgets. */
double liz_app_now(void);

#endif /* LIZ_APP_H */
