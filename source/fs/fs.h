/* fs.h - filesystem layer: directory listing, sorting and path helpers. */

#ifndef LIZ_FS_H
#define LIZ_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define LIZ_FS_NAME_MAX 256

typedef enum {
    LIZ_FS_FILE,    /* regular file */
    LIZ_FS_DIR,     /* directory */
    LIZ_FS_LINK,    /* symbolic link */
    LIZ_FS_SPECIAL, /* socket, fifo, device, ... */
} liz_fs_type;

typedef struct {
    char name[LIZ_FS_NAME_MAX];
    liz_fs_type type;
    off_t size;
    bool hidden;
} liz_fs_entry;

/* Reads the directory at `path` into a freshly allocated, sorted array.
 * Entries are grouped: visible dirs, visible files, hidden dirs, hidden
 * files, alphabetical within each group. When `include_hidden` is false,
 * entries whose name starts with "." are skipped entirely. A ".." entry is
 * prepended unless `path` is the filesystem root. The list never contains
 * "." or ".." beyond that one navigation entry.
 *
 * Returns 0 on success and stores the array in *out (free with
 * liz_fs_entries_free), or -1 if the directory cannot be read. */
int liz_fs_read(const char* path, bool include_hidden, liz_fs_entry** out, size_t* count);

/* Frees an array returned by liz_fs_read. `entries` may be NULL. */
void liz_fs_entries_free(liz_fs_entry* entries, size_t count);

/* Recursively deletes `path` (a file, symlink, or directory tree).
 * Returns 0 on success, -1 on failure with errno set. */
int liz_fs_remove_recursive(const char* path);

/* Recursively copies `src` to `dst` (files, symlinks and directories).
 * Directories are created as needed; symlinks are recreated, not followed.
 * Returns 0 on success, -1 on failure with errno set. */
int liz_fs_copy_recursive(const char* src, const char* dst);

/* Joins `dir` + "/" + `name` into `buf`. Returns 0 on success, -1 if it
 * would not fit (in which case buf is left untouched). */
int liz_fs_join(char* buf, size_t bufsz, const char* dir, const char* name);

/* Writes the parent directory of `path` into `out`. The parent of a root
 * path is the root itself. Returns 0 on success, -1 on truncation. */
int liz_fs_parent(char* out, size_t bufsz, const char* path);

/* Canonicalizes `path` (resolving symlinks and redundant separators) into
 * `buf`. Returns 0 on success, -1 if the path cannot be resolved. */
int liz_fs_canonical(char* buf, size_t bufsz, const char* path);

#endif /* LIZ_FS_H */
