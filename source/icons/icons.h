/* icons.h - freedesktop icon theme lookup, rendered through nanosvg. */

#ifndef LIZ_ICONS_H
#define LIZ_ICONS_H

#include "fs/fs.h"
#include "rendering/x11/xc.h"

#define LIZ_ICON_SIZE 16

/* Resolves the active icon theme and loads the MIME database. Both are
 * optional: without them every lookup returns NULL and the caller falls
 * back to whatever it drew before. */
void liz_icons_init(void);
void liz_icons_shutdown(xwindow* w);

/* Icon for a directory entry, or NULL when the theme has nothing for it.
 * `dir` is the directory the entry lives in, used to recognise the user's
 * home folders. `tint` paints the symbolic icons, which are drawn entirely
 * in the inherited text color, so pass the color the entry's label uses.
 * The returned image is cached and owned by this module. */
const xc_image* liz_icons_for_entry(xwindow* w, const char* dir, const liz_fs_entry* e,
                                    xc_color tint);

/* Icon looked up directly by freedesktop name, e.g. "user-home". */
const xc_image* liz_icons_by_name(xwindow* w, const char* name, xc_color tint);

#endif /* LIZ_ICONS_H */
