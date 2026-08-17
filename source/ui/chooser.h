/* chooser.h - file-picker mode (`lizaveta --filechooser`). */

#ifndef LIZ_CHOOSER_H
#define LIZ_CHOOSER_H

#include <stdbool.h>

struct liz_app;
typedef struct liz_app liz_app;

#include "rendering/x11/xc.h"

/* The picker floats, so it asks for a size of its own rather than taking
 * whatever a tiling window manager would hand it. */
#define LIZ_CHOOSER_WIDTH  820
#define LIZ_CHOOSER_HEIGHT 540

/* Activates chooser mode and navigates to the configured start directory.
 * The caller sets app->chooser fields (mode, out_path, save_name, ...)
 * beforehand; the start directory may be relative or a file-selector://
 * URI with the scheme stripped. */
void liz_chooser_start(liz_app* app);

/* Intercepts keys while chooser mode is active. Returns true when the key
 * was consumed; other keys fall through to the normal vim handling so
 * navigation still works. */
bool liz_chooser_handle_key(liz_app* app, xc_event ev);

/* Row-open path for chooser mode: directories navigate, files select or
 * toggle into the selection depending on the mode. */
void liz_chooser_open_row(liz_app* app, int row);

/* Adds a filter group to app->chooser (see liz_chooser_filter in app.h).
 * `spec` is "NAME:PATTERN1;PATTERN2;..." (as accepted on the command line
 * via --filter); patterns beyond LIZ_CHOOSER_FILTER_PATTERNS_MAX are
 * dropped. Returns true on success, false if the filter table is full or
 * `spec` is malformed (no ':'). Does not activate the filter; the caller
 * still owns current_filter (defaults to 0, the first filter added). */
bool liz_chooser_add_filter(liz_app* app, const char* spec);

/* True when `name` should be listed under the currently active filter.
 * Always true when the chooser has no filters configured. */
bool liz_chooser_name_matches_filter(const liz_app* app, const char* name);

/* Tab, when more than one filter is configured: advances to the next
 * filter and reloads the current directory so the listing reflects it. */
void liz_chooser_cycle_filter(liz_app* app);

#endif /* LIZ_CHOOSER_H */
