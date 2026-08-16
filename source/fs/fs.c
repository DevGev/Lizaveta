#include "fs/fs.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>

int liz_fs_join(char* buf, size_t bufsz, const char* dir, const char* name)
{
    size_t dlen = strlen(dir);
    int n = snprintf(buf, bufsz, "%s%s%s", dir,
                     (dlen == 0 || dir[dlen - 1] == '/') ? "" : "/", name);
    if (n < 0 || (size_t)n >= bufsz)
        return -1;
    return 0;
}

int liz_fs_parent(char* out, size_t bufsz, const char* path)
{
    size_t len = strlen(path);

    while (len > 0 && path[len - 1] == '/')
        len--;

    if (len == 0) {
        if (bufsz < 2)
            return -1;
        out[0] = '/';
        out[1] = '\0';
        return 0;
    }

    size_t slash = len;
    while (slash > 0 && path[slash - 1] != '/')
        slash--;

    if (slash == 0) {
        if (bufsz < 2)
            return -1;
        out[0] = '/';
        out[1] = '\0';
        return 0;
    }

    if (slash + 1 > bufsz)
        return -1;
    memcpy(out, path, slash);
    out[slash] = '\0';
    return 0;
}

int liz_fs_canonical(char* buf, size_t bufsz, const char* path)
{
    char* resolved = realpath(path, NULL);
    if (!resolved)
        return -1;
    int n = snprintf(buf, bufsz, "%s", resolved);
    free(resolved);
    if (n < 0 || (size_t)n >= bufsz)
        return -1;
    return 0;
}

/* Group order: visible dirs < visible files < hidden dirs < hidden files. */
static int liz_fs_entry_group(const liz_fs_entry* e)
{
    int group = e->hidden ? 2 : 0;
    if (e->type != LIZ_FS_DIR)
        group++;
    return group;
}

static int liz_fs_entry_cmp(const void* a, const void* b)
{
    const liz_fs_entry* ea = (const liz_fs_entry*)a;
    const liz_fs_entry* eb = (const liz_fs_entry*)b;
    int ga = liz_fs_entry_group(ea);
    int gb = liz_fs_entry_group(eb);
    if (ga != gb)
        return ga - gb;
    return strcoll(ea->name, eb->name);
}

int liz_fs_read(const char* path, bool include_hidden, liz_fs_entry** out, size_t* count)
{
    DIR* dir = opendir(path);
    if (!dir)
        return -1;

    size_t cap = 64;
    size_t n = 0;
    liz_fs_entry* entries = (liz_fs_entry*)malloc(cap * sizeof(liz_fs_entry));
    if (!entries) {
        closedir(dir);
        return -1;
    }

    struct dirent* de;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        if (!include_hidden && de->d_name[0] == '.')
            continue;

        if (n == cap) {
            cap *= 2;
            liz_fs_entry* grown = (liz_fs_entry*)realloc(entries, cap * sizeof(liz_fs_entry));
            if (!grown) {
                free(entries);
                closedir(dir);
                return -1;
            }
            entries = grown;
        }

        liz_fs_entry* e = &entries[n++];
        snprintf(e->name, sizeof(e->name), "%s", de->d_name);
        e->hidden = e->name[0] == '.';

        char full[PATH_MAX];
        if (liz_fs_join(full, sizeof(full), path, e->name) == 0) {
            struct stat st;
            if (lstat(full, &st) == 0) {
                e->size = st.st_size;
                if (S_ISDIR(st.st_mode))
                    e->type = LIZ_FS_DIR;
                else if (S_ISLNK(st.st_mode))
                    e->type = LIZ_FS_LINK;
                else if (S_ISREG(st.st_mode))
                    e->type = LIZ_FS_FILE;
                else
                    e->type = LIZ_FS_SPECIAL;
            } else {
                e->type = LIZ_FS_FILE;
                e->size = 0;
            }
        } else {
            e->type = LIZ_FS_FILE;
            e->size = 0;
        }
    }
    closedir(dir);

    /* add the navigation entry, unless we are at the root */
    if (!(path[0] == '/' && path[1] == '\0')) {
        /* the memmove below writes n + 1 slots; grow if the array is full
         * (a directory can contain exactly `cap` entries) */
        if (n == cap) {
            cap++;
            liz_fs_entry* grown = (liz_fs_entry*)realloc(entries, cap * sizeof(liz_fs_entry));
            if (!grown) {
                free(entries);
                return -1;
            }
            entries = grown;
        }
        memmove(entries + 1, entries, n * sizeof(liz_fs_entry));
        liz_fs_entry* up = &entries[0];
        snprintf(up->name, sizeof(up->name), "..");
        up->type = LIZ_FS_DIR;
        up->size = 0;
        up->hidden = false;
        n++;
    }

    qsort(entries, n, sizeof(liz_fs_entry), liz_fs_entry_cmp);

    *out = entries;
    *count = n;
    return 0;
}

void liz_fs_entries_free(liz_fs_entry* entries, size_t count)
{
    (void)count;
    free(entries);
}

/* nftw with FTW_DEPTH walks post-order, so a directory is removed only after
 * its contents are gone. remove() handles files, symlinks and empty dirs. */
static int liz_fs_remove_cb(const char* path, const struct stat* st, int flag,
                           struct FTW* ftw)
{
    (void)st;
    (void)ftw;
    if (flag == FTW_DNR || flag == FTW_NS)
        return -1;
    return remove(path);
}

int liz_fs_remove_recursive(const char* path)
{
    struct stat st;
    if (lstat(path, &st) != 0)
        return -1;
    if (!S_ISDIR(st.st_mode))
        return remove(path);
    return nftw(path, liz_fs_remove_cb, 32, FTW_DEPTH | FTW_PHYS);
}

static int liz_fs_copy_file(const char* src, const char* dst)
{
    int in = open(src, O_RDONLY);
    if (in < 0)
        return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) {
        close(in);
        return -1;
    }
    char buf[65536];
    ssize_t r;
    while ((r = read(in, buf, sizeof(buf))) > 0) {
        ssize_t w = 0;
        while (w < r) {
            ssize_t wr = write(out, buf + w, (size_t)(r - w));
            if (wr < 0) {
                if (errno == EINTR)
                    continue;
                close(in);
                close(out);
                return -1;
            }
            w += wr;
        }
    }
    close(in);
    close(out);
    return r < 0 ? -1 : 0;
}

/* Root of the copy walk, so the callback can map absolute walk paths back to
 * the destination tree. Single-threaded app, so file-scope is fine. */
static char liz_fs_copy_src[PATH_MAX];
static char liz_fs_copy_dst_root[PATH_MAX];

static int liz_fs_copy_cb(const char* path, const struct stat* st, int flag,
                         struct FTW* ftw)
{
    (void)st;
    (void)ftw;

    size_t sl = strlen(liz_fs_copy_src);
    if (strncmp(path, liz_fs_copy_src, sl) != 0)
        return -1;
    const char* rel = path + sl;
    while (*rel == '/')
        rel++;
    if (rel[0] == '\0')
        return 0; /* the source root itself, already created */

    char dst[PATH_MAX];
    if (liz_fs_join(dst, sizeof(dst), liz_fs_copy_dst_root, rel) != 0)
        return -1;

    switch (flag) {
    case FTW_D:
        if (mkdir(dst, 0700) != 0 && errno != EEXIST)
            return -1;
        return 0;
    case FTW_SL: {
        char target[PATH_MAX];
        ssize_t tl = readlink(path, target, sizeof(target) - 1);
        if (tl < 0)
            return -1;
        target[tl] = '\0';
        if (symlink(target, dst) != 0 && errno != EEXIST)
            return -1;
        return 0;
    }
    case FTW_F:
        return liz_fs_copy_file(path, dst);
    case FTW_DNR:
    case FTW_NS:
        return -1;
    default:
        return 0; /* special files (sockets, fifos, ...) are skipped */
    }
}

int liz_fs_copy_recursive(const char* src, const char* dst)
{
    struct stat st;
    if (lstat(src, &st) != 0)
        return -1;
    if (!S_ISDIR(st.st_mode))
        return liz_fs_copy_file(src, dst);

    snprintf(liz_fs_copy_src, sizeof(liz_fs_copy_src), "%s", src);
    snprintf(liz_fs_copy_dst_root, sizeof(liz_fs_copy_dst_root), "%s", dst);
    if (mkdir(dst, 0700) != 0 && errno != EEXIST)
        return -1;
    return nftw(src, liz_fs_copy_cb, 32, FTW_PHYS);
}
