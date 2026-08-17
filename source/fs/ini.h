/* ini.h - the desktop-file / index.theme flavour of INI. */

#ifndef LIZ_INI_H
#define LIZ_INI_H

#include <stdbool.h>
#include <stddef.h>

/* Reads the value of `key` from `[section]` of `text` into `out`.
 * A NULL `section` searches the whole file, which is what the sectionless
 * files (gtkrc-2.0) and single-section files need. Returns false when the
 * key is absent, leaving `out` untouched. */
bool liz_ini_get(const char* text, const char* section, const char* key,
                 char* out, size_t outsz);

#endif /* LIZ_INI_H */
