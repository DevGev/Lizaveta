/* xdg.h - XDG base directory lookup. */

#ifndef LIZ_XDG_H
#define LIZ_XDG_H

#include <limits.h>

#define LIZ_XDG_DIRS_MAX 12

/* XDG directories are short in practice. Keeping them well under PATH_MAX
 * leaves the compiler room to prove that appending a file name to one of
 * them cannot overflow. */
#define LIZ_XDG_PATH_MAX 512

/* Writes the XDG data directories in search order (the user's own data
 * directory first, then $XDG_DATA_DIRS) and returns how many were written.
 * Duplicates are dropped. */
int liz_xdg_data_dirs(char out[][LIZ_XDG_PATH_MAX], int max);

/* Same, for the configuration directories: $XDG_CONFIG_HOME then
 * $XDG_CONFIG_DIRS. */
int liz_xdg_config_dirs(char out[][LIZ_XDG_PATH_MAX], int max);

#endif /* LIZ_XDG_H */
