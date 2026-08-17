/* mime.c - filename to MIME type via the shared-mime-info glob database.
 *
 * Only the text database under <datadir>/mime is read: globs2 for the
 * filename patterns, plus the icons and generic-icons tables that name the
 * themed icon for a type. The compiled mime.cache is deliberately ignored,
 * as its layout is a shared-mime-info implementation detail. */

#include "icons/mime.h"

#include <ctype.h>
#include <fnmatch.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "fs/fs.h"
#include "icons/xdg.h"

/* Almost every glob is a plain "*.ext", so those go in a hash keyed by the
 * lowercased extension and the handful of literals and real wildcards fall
 * back to a linear fnmatch scan. */
#define LIZ_MIME_EXT_SLOTS 4096
#define LIZ_MIME_EXT_MAX   32

typedef struct {
    char ext[LIZ_MIME_EXT_MAX]; /* empty slot when ext[0] == '\0' */
    char mime[LIZ_MIME_MAX];
    int weight;
} liz_mime_ext;

typedef struct {
    char* pattern;
    char* mime;
    int weight;
    bool case_sensitive;
} liz_mime_glob;

/* A "one name maps to another" row: mime type to icon name in the icon
 * tables, alias to canonical type in the alias table. */
typedef struct {
    char* key;
    char* value;
} liz_mime_pair;

static liz_mime_ext* g_ext;         /* LIZ_MIME_EXT_SLOTS entries */
static liz_mime_glob* g_globs;      /* patterns the extension hash cannot hold */
static size_t g_glob_count;
static liz_mime_pair* g_specific;   /* mime/icons */
static size_t g_specific_count;
static liz_mime_pair* g_generic;    /* mime/generic-icons */
static size_t g_generic_count;
static liz_mime_pair* g_aliases;    /* mime/aliases */
static size_t g_alias_count;
static liz_mime_pair* g_subclasses; /* mime/subclasses, key is the child */
static size_t g_subclass_count;
static bool g_ready;

static char* liz_mime_slurp(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    char* buf = (char*)malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

/* Splits `text` in place at newlines, handing each non-empty, non-comment
 * line to `visit`. */
static void liz_mime_each_line(char* text, void (*visit)(char* line))
{
    char* p = text;
    while (*p) {
        char* end = strchr(p, '\n');
        if (end)
            *end = '\0';
        if (*p && *p != '#')
            visit(p);
        if (!end)
            break;
        p = end + 1;
    }
}

static unsigned liz_mime_hash(const char* s)
{
    unsigned h = 2166136261u;
    for (; *s; s++) {
        h ^= (unsigned char)tolower((unsigned char)*s);
        h *= 16777619u;
    }
    return h;
}

static bool liz_mime_is_lower(const char* s)
{
    for (; *s; s++) {
        if (isupper((unsigned char)*s))
            return false;
    }
    return true;
}

/* Every type claiming an extension gets its own slot. They cluster
 * together, since they share a hash, so a lookup that walks to the first
 * free slot sees all of them. Nothing is ever removed, which is what makes
 * that walk safe. */
static void liz_mime_ext_add(const char* ext, const char* mime, int weight)
{
    if (!ext[0] || strlen(ext) >= LIZ_MIME_EXT_MAX || strlen(mime) >= LIZ_MIME_MAX)
        return;
    unsigned i = liz_mime_hash(ext) % LIZ_MIME_EXT_SLOTS;
    for (unsigned probe = 0; probe < LIZ_MIME_EXT_SLOTS; probe++) {
        liz_mime_ext* slot = &g_ext[(i + probe) % LIZ_MIME_EXT_SLOTS];
        if (!slot->ext[0]) {
            snprintf(slot->ext, sizeof(slot->ext), "%s", ext);
            snprintf(slot->mime, sizeof(slot->mime), "%s", mime);
            slot->weight = weight;
            return;
        }
        if (strcasecmp(slot->ext, ext) == 0 && strcmp(slot->mime, mime) == 0)
            return;
    }
}

/* The single best type for an extension: heaviest wins, and on a tie the
 * lowercase spelling is the everyday one, since "*.c" and "*.C" both weigh
 * 50 and a file named .c is C rather than C++. */
static const char* liz_mime_ext_best(const char* ext)
{
    if (!g_ext)
        return NULL;
    const liz_mime_ext* best = NULL;
    unsigned i = liz_mime_hash(ext) % LIZ_MIME_EXT_SLOTS;
    for (unsigned probe = 0; probe < LIZ_MIME_EXT_SLOTS; probe++) {
        const liz_mime_ext* slot = &g_ext[(i + probe) % LIZ_MIME_EXT_SLOTS];
        if (!slot->ext[0])
            break;
        if (strcasecmp(slot->ext, ext) != 0)
            continue;
        if (!best || slot->weight > best->weight
            || (slot->weight == best->weight && liz_mime_is_lower(slot->ext)
                && !liz_mime_is_lower(best->ext)))
            best = slot;
    }
    return best ? best->mime : NULL;
}

/* One globs2 line: "weight:mimetype:glob[:flags]". */
static void liz_mime_glob_line(char* line)
{
    char* weight_s = line;
    char* mime = strchr(line, ':');
    if (!mime)
        return;
    *mime++ = '\0';
    char* pattern = strchr(mime, ':');
    if (!pattern)
        return;
    *pattern++ = '\0';

    bool case_sensitive = false;
    char* flags = strchr(pattern, ':');
    if (flags) {
        *flags++ = '\0';
        case_sensitive = strstr(flags, "cs") != NULL;
    }
    if (!pattern[0] || strcmp(pattern, "__NOGLOBS__") == 0)
        return;

    int weight = atoi(weight_s);

    /* "*.ext" with no further wildcards is the fast path */
    if (pattern[0] == '*' && pattern[1] == '.'
        && strpbrk(pattern + 2, "*?[") == NULL) {
        liz_mime_ext_add(pattern + 2, mime, weight);
        return;
    }

    liz_mime_glob* grown = (liz_mime_glob*)realloc(g_globs,
                                                   (g_glob_count + 1) * sizeof(*g_globs));
    if (!grown)
        return;
    g_globs = grown;
    g_globs[g_glob_count].pattern = strdup(pattern);
    g_globs[g_glob_count].mime = strdup(mime);
    g_globs[g_glob_count].weight = weight;
    g_globs[g_glob_count].case_sensitive = case_sensitive;
    if (g_globs[g_glob_count].pattern && g_globs[g_glob_count].mime)
        g_glob_count++;
}

static void liz_mime_pair_add(liz_mime_pair** list, size_t* count,
                              const char* key, const char* value)
{
    if (!key[0] || !value[0])
        return;
    liz_mime_pair* grown = (liz_mime_pair*)realloc(*list, (*count + 1) * sizeof(**list));
    if (!grown)
        return;
    *list = grown;
    (*list)[*count].key = strdup(key);
    (*list)[*count].value = strdup(value);
    if ((*list)[*count].key && (*list)[*count].value)
        (*count)++;
}

/* One "mimetype:iconname" line from the icons or generic-icons table. */
static void liz_mime_icon_line(char* line, liz_mime_pair** list, size_t* count)
{
    char* icon = strchr(line, ':');
    if (!icon)
        return;
    *icon++ = '\0';
    liz_mime_pair_add(list, count, line, icon);
}

/* One "child parent" line from the subclasses table. */
static void liz_mime_subclass_line(char* line)
{
    char* parent = strchr(line, ' ');
    if (!parent)
        return;
    *parent++ = '\0';
    while (*parent == ' ')
        parent++;
    liz_mime_pair_add(&g_subclasses, &g_subclass_count, line, parent);
}

/* One "alias canonical" line from the aliases table. */
static void liz_mime_alias_line(char* line)
{
    char* canonical = strchr(line, ' ');
    if (!canonical)
        return;
    *canonical++ = '\0';
    while (*canonical == ' ')
        canonical++;
    liz_mime_pair_add(&g_aliases, &g_alias_count, line, canonical);
}

static void liz_mime_specific_line(char* line)
{
    liz_mime_icon_line(line, &g_specific, &g_specific_count);
}

static void liz_mime_generic_line(char* line)
{
    liz_mime_icon_line(line, &g_generic, &g_generic_count);
}

static const char* liz_mime_icon_find(const liz_mime_pair* list, size_t count,
                                      const char* mime)
{
    for (size_t i = 0; i < count; i++) {
        if (strcmp(list[i].key, mime) == 0)
            return list[i].value;
    }
    return NULL;
}

void liz_mime_init(void)
{
    if (g_ready)
        return;
    g_ready = true;

    g_ext = (liz_mime_ext*)calloc(LIZ_MIME_EXT_SLOTS, sizeof(*g_ext));
    if (!g_ext)
        return;

    char dirs[LIZ_XDG_DIRS_MAX][LIZ_XDG_PATH_MAX];
    int ndirs = liz_xdg_data_dirs(dirs, LIZ_XDG_DIRS_MAX);
    char base[LIZ_XDG_PATH_MAX];
    char path[PATH_MAX];

    /* Later directories are lower priority, so the first database that
     * provides a table wins and the rest are skipped. */
    for (int i = 0; i < ndirs; i++) {
        if (liz_fs_join(base, sizeof(base), dirs[i], "mime") != 0)
            continue;
        struct {
            const char* leaf;
            void (*visit)(char* line);
            bool loaded;
        } tables[] = {
            { "globs2", liz_mime_glob_line, g_glob_count > 0 },
            { "icons", liz_mime_specific_line, g_specific_count > 0 },
            { "generic-icons", liz_mime_generic_line, g_generic_count > 0 },
            { "aliases", liz_mime_alias_line, g_alias_count > 0 },
            { "subclasses", liz_mime_subclass_line, g_subclass_count > 0 },
        };
        for (size_t t = 0; t < sizeof(tables) / sizeof(tables[0]); t++) {
            if (tables[t].loaded)
                continue;
            if (liz_fs_join(path, sizeof(path), base, tables[t].leaf) != 0)
                continue;
            char* text = liz_mime_slurp(path);
            if (!text)
                continue;
            liz_mime_each_line(text, tables[t].visit);
            free(text);
        }
    }
}

void liz_mime_shutdown(void)
{
    for (size_t i = 0; i < g_glob_count; i++) {
        free(g_globs[i].pattern);
        free(g_globs[i].mime);
    }
    free(g_globs);
    for (size_t i = 0; i < g_specific_count; i++) {
        free(g_specific[i].key);
        free(g_specific[i].value);
    }
    free(g_specific);
    for (size_t i = 0; i < g_generic_count; i++) {
        free(g_generic[i].key);
        free(g_generic[i].value);
    }
    free(g_generic);
    for (size_t i = 0; i < g_alias_count; i++) {
        free(g_aliases[i].key);
        free(g_aliases[i].value);
    }
    free(g_aliases);
    for (size_t i = 0; i < g_subclass_count; i++) {
        free(g_subclasses[i].key);
        free(g_subclasses[i].value);
    }
    free(g_subclasses);
    free(g_ext);

    g_globs = NULL;
    g_glob_count = 0;
    g_specific = NULL;
    g_specific_count = 0;
    g_generic = NULL;
    g_generic_count = 0;
    g_aliases = NULL;
    g_alias_count = 0;
    g_subclasses = NULL;
    g_subclass_count = 0;
    g_ext = NULL;
    g_ready = false;
}

/* A literal pattern beats every wildcard; between two wildcards the longer
 * pattern is the more specific one and weight breaks the remaining ties. */
static const char* liz_mime_match(const char* filename)
{
    const char* literal = NULL;
    int literal_weight = -1;
    const char* wild = NULL;
    size_t wild_len = 0;
    int wild_weight = -1;

    for (size_t i = 0; i < g_glob_count; i++) {
        const liz_mime_glob* g = &g_globs[i];
        int flags = g->case_sensitive ? 0 : FNM_CASEFOLD;
        if (fnmatch(g->pattern, filename, flags) != 0)
            continue;
        if (strpbrk(g->pattern, "*?[") == NULL) {
            if (g->weight > literal_weight) {
                literal = g->mime;
                literal_weight = g->weight;
            }
            continue;
        }
        size_t len = strlen(g->pattern);
        if (len > wild_len || (len == wild_len && g->weight > wild_weight)) {
            wild = g->mime;
            wild_len = len;
            wild_weight = g->weight;
        }
    }
    if (literal)
        return literal;

    /* The extension hash holds only "*.ext" patterns. Starting at the
     * leftmost dot finds the longest extension first, so "tar.gz" is
     * preferred over "gz". Lengths are compared as if the "*." were still
     * on the front, to stay on the same scale as the wildcard patterns. */
    const char* best_ext = NULL;
    size_t best_ext_len = 0;
    for (const char* dot = strchr(filename, '.'); dot; dot = strchr(dot + 1, '.')) {
        const char* mime = liz_mime_ext_best(dot + 1);
        if (mime) {
            best_ext = mime;
            best_ext_len = strlen(dot + 1) + 2;
            break;
        }
    }
    if (best_ext && best_ext_len >= wild_len)
        return best_ext;
    return wild ? wild : best_ext;
}

static int liz_mime_push(char out[][LIZ_MIME_MAX], int max, int n, const char* name)
{
    if (n >= max || !name || !name[0])
        return n;
    for (int i = 0; i < n; i++) {
        if (strcmp(out[i], name) == 0)
            return n;
    }
    snprintf(out[n], LIZ_MIME_MAX, "%s", name);
    return n + 1;
}

/* How many steps a type is below the root of the subclass graph. A type
 * with more ancestors is the more specific one, which is what orders the
 * matches for an ambiguous extension. */
static int liz_mime_depth(const char* mime, int budget)
{
    if (budget <= 0)
        return 0;
    int best = 0;
    for (size_t i = 0; i < g_subclass_count; i++) {
        if (strcmp(g_subclasses[i].key, mime) != 0)
            continue;
        int d = 1 + liz_mime_depth(g_subclasses[i].value, budget - 1);
        if (d > best)
            best = d;
    }
    return best;
}

int liz_mime_types(const char* filename, char out[][LIZ_MIME_MAX], int max)
{
    if (max <= 0)
        return 0;
    if (!g_ext)
        liz_mime_init();

    int n = 0;

    for (size_t i = 0; i < g_glob_count && n < max; i++) {
        const liz_mime_glob* g = &g_globs[i];
        int flags = g->case_sensitive ? 0 : FNM_CASEFOLD;
        if (fnmatch(g->pattern, filename, flags) == 0)
            n = liz_mime_push(out, max, n, g->mime);
    }

    /* the longest matching extension is the one that identifies the file,
     * so only that cluster is collected, not every trailing suffix */
    for (const char* dot = strchr(filename, '.'); dot && n < max;
         dot = strchr(dot + 1, '.')) {
        const char* ext = dot + 1;
        if (!liz_mime_ext_best(ext))
            continue;
        unsigned start = liz_mime_hash(ext) % LIZ_MIME_EXT_SLOTS;
        for (unsigned probe = 0; probe < LIZ_MIME_EXT_SLOTS && n < max; probe++) {
            const liz_mime_ext* slot = &g_ext[(start + probe) % LIZ_MIME_EXT_SLOTS];
            if (!slot->ext[0])
                break;
            if (strcasecmp(slot->ext, ext) == 0)
                n = liz_mime_push(out, max, n, slot->mime);
        }
        break;
    }

    if (n == 0)
        n = liz_mime_push(out, max, 0, "application/octet-stream");

    /* insertion sort by specificity; the list is a handful of entries and
     * equally specific types keep the order the database listed them in */
    for (int i = 1; i < n; i++) {
        char key[LIZ_MIME_MAX];
        snprintf(key, sizeof(key), "%s", out[i]);
        int key_depth = liz_mime_depth(key, 8);
        int j = i - 1;
        while (j >= 0 && liz_mime_depth(out[j], 8) < key_depth) {
            snprintf(out[j + 1], LIZ_MIME_MAX, "%s", out[j]);
            j--;
        }
        snprintf(out[j + 1], LIZ_MIME_MAX, "%s", key);
    }
    return n;
}

int liz_mime_alias_names(const char* mime, char out[][LIZ_MIME_MAX], int max)
{
    if (!g_ext)
        liz_mime_init();

    int n = liz_mime_push(out, max, 0, mime);
    for (size_t i = 0; i < g_alias_count; i++) {
        if (strcmp(g_aliases[i].value, mime) == 0)
            n = liz_mime_push(out, max, n, g_aliases[i].key);
        else if (strcmp(g_aliases[i].key, mime) == 0)
            n = liz_mime_push(out, max, n, g_aliases[i].value);
    }
    return n;
}

void liz_mime_type(const char* filename, char* out, size_t outsz)
{
    if (!g_ext)
        liz_mime_init();
    const char* mime = liz_mime_match(filename);
    snprintf(out, outsz, "%s", mime ? mime : "application/octet-stream");
}

int liz_mime_icon_names(const char* filename, char out[][LIZ_MIME_MAX], int max)
{
    if (max <= 0)
        return 0;
    if (!g_ext)
        liz_mime_init();

    const char* mime = liz_mime_match(filename);
    if (!mime)
        mime = "application/octet-stream";

    int n = 0;
    n = liz_mime_push(out, max, n, liz_mime_icon_find(g_specific, g_specific_count, mime));

    /* the icon naming spec spells a type out with the slash turned into a
     * dash: "text/plain" is drawn by "text-plain" */
    char dashed[LIZ_MIME_MAX];
    snprintf(dashed, sizeof(dashed), "%s", mime);
    for (char* p = dashed; *p; p++) {
        if (*p == '/')
            *p = '-';
    }
    n = liz_mime_push(out, max, n, dashed);

    n = liz_mime_push(out, max, n, liz_mime_icon_find(g_generic, g_generic_count, mime));

    /* last resort: the whole media type, e.g. "image-x-generic" */
    char generic[LIZ_MIME_MAX];
    const char* slash = strchr(mime, '/');
    int media_len = slash ? (int)(slash - mime) : (int)strlen(mime);
    snprintf(generic, sizeof(generic), "%.*s-x-generic", media_len, mime);
    n = liz_mime_push(out, max, n, generic);
    n = liz_mime_push(out, max, n, "application-x-generic");
    return n;
}
