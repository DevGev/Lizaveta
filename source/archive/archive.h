/* archive.h - archive browsing and extraction via libarchive. */

#ifndef LIZ_ARCHIVE_H
#define LIZ_ARCHIVE_H

#include <stdbool.h>
#include <stddef.h>

#include "fs/fs.h"

#define LIZ_ARCHIVE_PATH_MAX 4096

/* True when `name` looks like an archive we can open (by extension). */
bool liz_archive_is(const char* name);

/* Lists the contents of `archive_path` at the virtual directory `virtual_path`.
 * On success, returns 0 and fills `out`/`*count` with heap-allocated entries
 * (free with liz_archive_entries_free). A trailing '/' in virtual_path means
 * the root. */
int liz_archive_read(const char* archive_path, const char* virtual_path,
                     liz_fs_entry** out, size_t* count);

void liz_archive_entries_free(liz_fs_entry* entries, size_t count);

/* Extracts `entry_path` from `archive_path` into `dest_dir`.
 * entry_path is the full path inside the archive (leading '/' ok).
 * For directories, extracts the entire subtree.
 * Returns 0 on success. */
int liz_archive_extract(const char* archive_path, const char* entry_path,
                        const char* dest_dir);

/* Extracts all contents of `archive_path` into `dest_dir`.
 * Returns 0 on success. */
int liz_archive_extract_all(const char* archive_path, const char* dest_dir);

#endif /* LIZ_ARCHIVE_H */
