/* archive.c - archive browsing and extraction via libarchive. */

#ifdef ARCHIVE_SUPPORT

#include "archive/archive.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <archive.h>
#include <archive_entry.h>

bool liz_archive_is(const char* name)
{
    if (!name)
        return false;
    const char* dot = strrchr(name, '.');
    if (!dot || dot == name)
        return false;
    dot++;
    static const char* exts[] = {
        "zip", "tar", "tar.gz", "tgz", "tar.bz2", "tbz2", "tar.xz", "txz",
        "tar.zst", "tar.lz", "tar.lzma", "tar.zstd",
        "gz", "bz2", "xz", "zst", "lz", "lzma", "zstd",
        "7z", "rar",
        NULL
    };
    for (int i = 0; exts[i]; i++) {
        size_t n = strlen(exts[i]);
        size_t d = strlen(dot);
        if (d == n && strncasecmp(dot, exts[i], n) == 0)
            return true;
        /* handle compound extensions like tar.gz: check "tar.gz" substring */
        if (d > n) {
            const char* p = dot + d - n - 1;
            if (p > dot && *p == '.' && strncasecmp(p + 1, exts[i], n) == 0)
                return true;
        }
    }
    return false;
}

/* Dynamic entry array used during liz_archive_read. */
typedef struct {
    liz_fs_entry* items;
    size_t count;
    size_t cap;
} liz_archive_vec;

static int liz_archive_vec_push(liz_archive_vec* v, const liz_fs_entry* e)
{
    if (v->count == v->cap) {
        size_t newcap = v->cap ? v->cap * 2 : 64;
        liz_fs_entry* grown = (liz_fs_entry*)realloc(v->items, newcap * sizeof(liz_fs_entry));
        if (!grown)
            return -1;
        v->items = grown;
        v->cap = newcap;
    }
    v->items[v->count++] = *e;
    return 0;
}

/* Entries we've already seen (for deduplication of directory entries). */
static bool liz_archive_vec_has(const liz_archive_vec* v, const char* name)
{
    for (size_t i = 0; i < v->count; i++) {
        if (strcmp(v->items[i].name, name) == 0)
            return true;
    }
    return false;
}

/* Strips a leading '/' from path and normalizes consecutive slashes. */
static void liz_archive_norm(char* path)
{
    char* p = path;
    while (*p == '/')
        p++;
    if (p != path)
        memmove(path, p, strlen(p) + 1);
    size_t len = strlen(path);
    while (len > 0 && path[len - 1] == '/')
        path[--len] = '\0';
}

int liz_archive_read(const char* archive_path, const char* virtual_path,
                     liz_fs_entry** out, size_t* count)
{
    struct archive* a = archive_read_new();
    if (!a)
        return -1;
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    if (archive_read_open_filename(a, archive_path, 10240) != ARCHIVE_OK) {
        archive_read_free(a);
        return -1;
    }

    char vpath[LIZ_ARCHIVE_PATH_MAX];
    snprintf(vpath, sizeof(vpath), "%s", virtual_path ? virtual_path : "/");
    liz_archive_norm(vpath);

    liz_archive_vec vec = {0};
    struct archive_entry* entry;
    int r;

    while ((r = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        const char* pathname = archive_entry_pathname(entry);
        if (!pathname || pathname[0] == '\0')
            continue;

        char normalized[LIZ_ARCHIVE_PATH_MAX];
        snprintf(normalized, sizeof(normalized), "%s", pathname);
        liz_archive_norm(normalized);

        /* root: skip the directory entry for the archive root itself */
        if (vpath[0] == '\0') {
            if (normalized[0] == '\0')
                continue;
        }

        /* we want entries directly inside vpath */
        const char* rel = NULL;
        if (vpath[0] == '\0') {
            rel = normalized;
        } else if (strncmp(normalized, vpath, strlen(vpath)) == 0) {
            rel = normalized + strlen(vpath);
            while (*rel == '/')
                rel++;
        }
        if (!rel || rel[0] == '\0')
            continue;

        /* check if this is a direct child (no more '/' in rel) or a nested entry */
        const char* slash = strchr(rel, '/');
        bool is_direct_child = (slash == NULL);
        bool ends_with_slash = (rel[strlen(rel) - 1] == '/');

        char child_name[LIZ_FS_NAME_MAX];
        if (is_direct_child) {
            snprintf(child_name, sizeof(child_name), "%s", rel);
        } else {
            size_t nlen = (size_t)(slash - rel);
            if (nlen >= sizeof(child_name))
                nlen = sizeof(child_name) - 1;
            memcpy(child_name, rel, nlen);
            child_name[nlen] = '\0';
        }

        if (child_name[0] == '\0')
            continue;
        if (strcmp(child_name, ".") == 0 || strcmp(child_name, "..") == 0)
            continue;

        if (liz_archive_vec_has(&vec, child_name))
            continue;

        liz_fs_entry e;
        memset(&e, 0, sizeof(e));
        snprintf(e.name, sizeof(e.name), "%s", child_name);
        e.hidden = e.name[0] == '.';

        if (!is_direct_child || ends_with_slash
            || archive_entry_filetype(entry) == AE_IFDIR) {
            e.type = LIZ_FS_DIR;
            e.size = 0;
        } else {
            switch (archive_entry_filetype(entry)) {
            case AE_IFLNK:
                e.type = LIZ_FS_LINK;
                break;
            case AE_IFIFO:
            case AE_IFCHR:
            case AE_IFBLK:
            case AE_IFSOCK:
                e.type = LIZ_FS_SPECIAL;
                break;
            default:
                e.type = LIZ_FS_FILE;
                break;
            }
            e.size = (off_t)archive_entry_size(entry);
        }

        if (liz_archive_vec_push(&vec, &e) != 0) {
            free(vec.items);
            archive_read_free(a);
            return -1;
        }
    }

    archive_read_free(a);

    /* add ".." entry at the beginning */
    {
        liz_fs_entry up;
        memset(&up, 0, sizeof(up));
        snprintf(up.name, sizeof(up.name), "..");
        up.type = LIZ_FS_DIR;
        up.size = 0;
        up.hidden = false;
        if (liz_archive_vec_push(&vec, &up) != 0) {
            free(vec.items);
            return -1;
        }
        /* move ".." to the front */
        liz_fs_entry last = vec.items[vec.count - 1];
        memmove(&vec.items[1], &vec.items[0], (vec.count - 1) * sizeof(liz_fs_entry));
        vec.items[0] = last;
    }

    *out = vec.items;
    *count = vec.count;
    return 0;
}

void liz_archive_entries_free(liz_fs_entry* entries, size_t count)
{
    (void)count;
    free(entries);
}

int liz_archive_extract(const char* archive_path, const char* entry_path,
                        const char* dest_dir)
{
    struct archive* a = archive_read_new();
    if (!a)
        return -1;
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    if (archive_read_open_filename(a, archive_path, 10240) != ARCHIVE_OK) {
        archive_read_free(a);
        return -1;
    }

    /* normalize the target entry path for comparison */
    char target[LIZ_ARCHIVE_PATH_MAX];
    snprintf(target, sizeof(target), "%s", entry_path);
    liz_archive_norm(target);
    size_t target_len = strlen(target);

    struct archive_entry* entry;
    int result = 0;

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char* pathname = archive_entry_pathname(entry);
        if (!pathname)
            continue;

        char normalized[LIZ_ARCHIVE_PATH_MAX];
        snprintf(normalized, sizeof(normalized), "%s", pathname);
        liz_archive_norm(normalized);

        bool match = false;
        if (strcmp(normalized, target) == 0) {
            match = true;
        } else if (strncmp(normalized, target, target_len) == 0
                   && normalized[target_len] == '/') {
            match = true;
        }

        if (!match)
            continue;

        /* build the destination path */
        char destpath[PATH_MAX];
        const char* rel = normalized + target_len;
        while (*rel == '/')
            rel++;
        if (rel[0] == '\0') {
            /* it IS the target entry itself */
            snprintf(destpath, sizeof(destpath), "%s/%s", dest_dir,
                     strrchr(target, '/') ? strrchr(target, '/') + 1 : target);
        } else {
            snprintf(destpath, sizeof(destpath), "%s/%s", dest_dir, rel);
        }

        /* create parent directories as needed */
        {
            char parent[PATH_MAX];
            snprintf(parent, sizeof(parent), "%s", destpath);
            char* sl = strrchr(parent, '/');
            if (sl && sl != parent) {
                *sl = '\0';
                /* recursive mkdir -p */
                for (char* p = parent + 1; *p; p++) {
                    if (*p == '/') {
                        *p = '\0';
                        mkdir(parent, 0700);
                        *p = '/';
                    }
                }
                mkdir(parent, 0700);
            }
        }

        mode_t mode = (mode_t)archive_entry_mode(entry);
        if (mode == 0)
            mode = 0666;

        switch (archive_entry_filetype(entry)) {
        case AE_IFDIR:
            mkdir(destpath, mode);
            break;
        case AE_IFLNK: {
            const char* link = archive_entry_symlink(entry);
            if (link)
                symlink(link, destpath);
            break;
        }
        case AE_IFREG: {
            int fd = open(destpath, O_WRONLY | O_CREAT | O_TRUNC, mode);
            if (fd < 0) {
                result = -1;
                continue;
            }
            const void* buf;
            size_t sz;
            la_int64_t off;
            while (archive_read_data_block(a, &buf, &sz, &off) == ARCHIVE_OK) {
                ssize_t w = 0;
                while ((size_t)w < sz) {
                    ssize_t r = write(fd, (const char*)buf + w, sz - (size_t)w);
                    if (r < 0) {
                        if (errno == EINTR)
                            continue;
                        close(fd);
                        result = -1;
                        goto next;
                    }
                    w += r;
                }
            }
            close(fd);
            break;
        }
        default:
            break;
        }
    next:;
    }

    archive_read_free(a);
    return result;
}

int liz_archive_extract_all(const char* archive_path, const char* dest_dir)
{
    struct archive* a = archive_read_new();
    if (!a)
        return -1;
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    if (archive_read_open_filename(a, archive_path, 10240) != ARCHIVE_OK) {
        archive_read_free(a);
        return -1;
    }

    int result = 0;
    struct archive_entry* entry;

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char* pathname = archive_entry_pathname(entry);
        if (!pathname || pathname[0] == '\0')
            continue;

        char normalized[LIZ_ARCHIVE_PATH_MAX];
        snprintf(normalized, sizeof(normalized), "%s", pathname);
        liz_archive_norm(normalized);
        if (normalized[0] == '\0')
            continue;

        char destpath[PATH_MAX];
        snprintf(destpath, sizeof(destpath), "%s/%s", dest_dir, normalized);

        /* create parent directories as needed */
        {
            char parent[PATH_MAX];
            snprintf(parent, sizeof(parent), "%s", destpath);
            char* sl = strrchr(parent, '/');
            if (sl && sl != parent) {
                *sl = '\0';
                for (char* p = parent + 1; *p; p++) {
                    if (*p == '/') {
                        *p = '\0';
                        mkdir(parent, 0700);
                        *p = '/';
                    }
                }
                mkdir(parent, 0700);
            }
        }

        mode_t mode = (mode_t)archive_entry_mode(entry);
        if (mode == 0)
            mode = 0666;

        switch (archive_entry_filetype(entry)) {
        case AE_IFDIR:
            mkdir(destpath, mode);
            break;
        case AE_IFLNK: {
            const char* link = archive_entry_symlink(entry);
            if (link)
                symlink(link, destpath);
            break;
        }
        case AE_IFREG: {
            int fd = open(destpath, O_WRONLY | O_CREAT | O_TRUNC, mode);
            if (fd < 0) {
                result = -1;
                continue;
            }
            const void* buf;
            size_t sz;
            la_int64_t off;
            while (archive_read_data_block(a, &buf, &sz, &off) == ARCHIVE_OK) {
                ssize_t w = 0;
                while ((size_t)w < sz) {
                    ssize_t r = write(fd, (const char*)buf + w, sz - (size_t)w);
                    if (r < 0) {
                        if (errno == EINTR)
                            continue;
                        close(fd);
                        result = -1;
                        goto next;
                    }
                    w += r;
                }
            }
            close(fd);
            break;
        }
        default:
            break;
        }
    next:;
    }

    archive_read_free(a);
    return result;
}

#endif /* ARCHIVE_SUPPORT */
