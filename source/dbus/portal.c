/* portal.c - org.freedesktop.impl.portal.FileChooser backend implementation. */

#include "dbus/portal.h"

#include "dbus/filemanager1.h"

#include <dbus/dbus.h>

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/wait.h>

#define LIZ_PORTAL_BUS_NAME "org.freedesktop.impl.portal.desktop.lizaveta"
#define LIZ_PORTAL_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define LIZ_PORTAL_IFACE "org.freedesktop.impl.portal.FileChooser"

#define LIZ_PORTAL_FILTERS_MAX 16
#define LIZ_PORTAL_FILTER_SPEC_MAX 1024 /* "NAME:pat1;pat2;..." passed to --filter */

typedef struct {
    bool multiple;
    bool directory;
    char current_name[512];
    char current_folder[PATH_MAX];
    char filter_specs[LIZ_PORTAL_FILTERS_MAX][LIZ_PORTAL_FILTER_SPEC_MAX];
    int filter_count;
    int current_filter_index; /* index into filter_specs, -1 if unset/unmatched */
} liz_portal_options;

/* Best-effort glob for the handful of MIME types apps commonly hand a file
 * picker instead of an explicit glob (portal filters can be `(1, "image/png")`
 * as well as `(0, "*.png")`). Anything not in this tiny table is dropped
 * rather than guessed at -- a filter that shows too little is a nuisance,
 * but showing everything (the fallback when a filter group ends up with no
 * usable patterns at all) is never wrong. */
static const char* liz_portal_mime_glob(const char* mime)
{
    static const struct { const char* mime; const char* glob; } table[] = {
        { "image/*", "*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.webp;*.svg" },
        { "image/png", "*.png" },
        { "image/jpeg", "*.jpg;*.jpeg" },
        { "image/gif", "*.gif" },
        { "image/webp", "*.webp" },
        { "image/svg+xml", "*.svg" },
        { "text/*", "*.txt;*.md;*.log;*.conf;*.cfg" },
        { "text/plain", "*.txt" },
        { "application/pdf", "*.pdf" },
        { "application/zip", "*.zip" },
        { "application/json", "*.json" },
        { "audio/*", "*.mp3;*.flac;*.wav;*.ogg;*.m4a" },
        { "video/*", "*.mp4;*.mkv;*.webm;*.avi;*.mov" },
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
        if (strcmp(mime, table[i].mime) == 0)
            return table[i].glob;
    return NULL;
}

/* Appends "name:pat1;pat2;..." built from one portal filter group -- an
 * "(sa(us))" struct: a display name plus an array of (type, pattern) pairs,
 * type 0 = glob, type 1 = MIME type -- into `out`. Returns false (and
 * leaves `out` untouched) if the group yields no usable pattern at all. */
static bool liz_portal_build_filter_spec(DBusMessageIter* group_iter, char* out, size_t outsz)
{
    DBusMessageIter st;
    dbus_message_iter_recurse(group_iter, &st);

    const char* name = NULL;
    dbus_message_iter_get_basic(&st, &name);
    dbus_message_iter_next(&st);

    char patterns[LIZ_PORTAL_FILTER_SPEC_MAX];
    patterns[0] = '\0';
    size_t plen = 0;

    DBusMessageIter arr;
    dbus_message_iter_recurse(&st, &arr);
    while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRUCT) {
        DBusMessageIter pair;
        dbus_message_iter_recurse(&arr, &pair);
        dbus_uint32_t type = 0;
        dbus_message_iter_get_basic(&pair, &type);
        dbus_message_iter_next(&pair);
        const char* pattern = NULL;
        dbus_message_iter_get_basic(&pair, &pattern);

        const char* glob = (type == 0) ? pattern : liz_portal_mime_glob(pattern);
        if (glob && glob[0]) {
            int n = snprintf(patterns + plen, sizeof(patterns) - plen,
                             "%s%s", plen > 0 ? ";" : "", glob);
            if (n > 0 && (size_t)n < sizeof(patterns) - plen)
                plen += (size_t)n;
        }
        dbus_message_iter_next(&arr);
    }

    if (plen == 0)
        return false;
    snprintf(out, outsz, "%s:%s", name ? name : "Filter", patterns);
    return true;
}

/* Byte array (ay) -> NUL-terminated string; portal current_folder/
 * current_file options are a POSIX path encoded this way (the array
 * includes its own trailing NUL, which we just stop at). */
static void liz_portal_read_byte_path(DBusMessageIter* it, char* out, size_t outsz)
{
    out[0] = '\0';
    DBusMessageIter arr;
    dbus_message_iter_recurse(it, &arr);
    size_t n = 0;
    while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_BYTE && n + 1 < outsz) {
        unsigned char b = 0;
        dbus_message_iter_get_basic(&arr, &b);
        if (b == '\0')
            break;
        out[n++] = (char)b;
        dbus_message_iter_next(&arr);
    }
    out[n] = '\0';
}

/* Parses the `a{sv}` options dict of an OpenFile/SaveFile/SaveFiles call. */
static void liz_portal_parse_options(DBusMessageIter* dict_iter, liz_portal_options* opt)
{
    memset(opt, 0, sizeof(*opt));
    opt->current_filter_index = -1;

    char current_filter_spec[LIZ_PORTAL_FILTER_SPEC_MAX];
    current_filter_spec[0] = '\0';

    DBusMessageIter it;
    dbus_message_iter_recurse(dict_iter, &it);
    while (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&it, &entry);

        const char* key = NULL;
        dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);

        DBusMessageIter val;
        dbus_message_iter_recurse(&entry, &val); /* unwrap the variant */

        if (strcmp(key, "multiple") == 0) {
            dbus_bool_t b = FALSE;
            dbus_message_iter_get_basic(&val, &b);
            opt->multiple = b != FALSE;
        } else if (strcmp(key, "directory") == 0) {
            dbus_bool_t b = FALSE;
            dbus_message_iter_get_basic(&val, &b);
            opt->directory = b != FALSE;
        } else if (strcmp(key, "current_name") == 0) {
            const char* s = NULL;
            dbus_message_iter_get_basic(&val, &s);
            if (s)
                snprintf(opt->current_name, sizeof(opt->current_name), "%s", s);
        } else if (strcmp(key, "current_folder") == 0 || strcmp(key, "current_file") == 0) {
            /* current_file (SaveFile) is a full path; current_folder wins if
             * both are somehow present, since it's the one we actually use */
            if (opt->current_folder[0] == '\0')
                liz_portal_read_byte_path(&val, opt->current_folder, sizeof(opt->current_folder));
        } else if (strcmp(key, "filters") == 0) {
            DBusMessageIter group;
            dbus_message_iter_recurse(&val, &group);
            while (dbus_message_iter_get_arg_type(&group) == DBUS_TYPE_STRUCT
                   && opt->filter_count < LIZ_PORTAL_FILTERS_MAX) {
                if (liz_portal_build_filter_spec(&group, opt->filter_specs[opt->filter_count],
                                                sizeof(opt->filter_specs[0])))
                    opt->filter_count++;
                dbus_message_iter_next(&group);
            }
        } else if (strcmp(key, "current_filter") == 0) {
            liz_portal_build_filter_spec(&val, current_filter_spec, sizeof(current_filter_spec));
        }

        dbus_message_iter_next(&it);
    }

    if (current_filter_spec[0]) {
        for (int i = 0; i < opt->filter_count; i++) {
            if (strcmp(opt->filter_specs[i], current_filter_spec) == 0) {
                opt->current_filter_index = i;
                break;
            }
        }
    }
}

/* Percent-encodes a path for the "file://" URIs the portal wants back. */
static void liz_portal_uri_encode(const char* path, char* out, size_t outsz)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (const unsigned char* p = (const unsigned char*)path; *p && n + 4 < outsz; p++) {
        unsigned char c = *p;
        bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                 || (c >= '0' && c <= '9')
                 || c == '/' || c == '-' || c == '_' || c == '.' || c == '~';
        if (safe) {
            out[n++] = (char)c;
        } else {
            out[n++] = '%';
            out[n++] = hex[(c >> 4) & 0xF];
            out[n++] = hex[c & 0xF];
        }
    }
    out[n] = '\0';
}

/* Runs `lizaveta --filechooser ...` built from the parsed request and waits
 * for it to exit. On success (the user confirmed a choice) returns true and
 * fills `paths`/`*n` (each entry heap-owned, caller frees); on cancel or
 * error returns false and leaves them untouched. */
static bool liz_portal_run_picker(const char* lizaveta_exe, const char* title,
                                 bool is_save, const liz_portal_options* opt,
                                 char*** out_paths, int* out_n)
{
    (void)title; /* lizaveta's chooser mode has no window-title bar to set;
                  * kept as a parameter for when/if that changes */

    char out_path[PATH_MAX];
    const char* rundir = getenv("XDG_RUNTIME_DIR");
    snprintf(out_path, sizeof(out_path), "%s/lizaveta-portal-XXXXXX",
             (rundir && rundir[0]) ? rundir : "/tmp");
    int fd = mkstemp(out_path);
    if (fd < 0) {
        fprintf(stderr, "lizaveta-portal: mkstemp failed: %s\n", strerror(errno));
        return false;
    }
    close(fd);

    /* argv: exe --filechooser --out FILE [--multiple] [--directory]
     *       [--filter N:P ...]* [--filter-index N] [--save NAME] [FOLDER] */
    const char* argv_buf[8 + LIZ_PORTAL_FILTERS_MAX * 2 + 8];
    int ac = 0;
    char filter_index_str[16];

    argv_buf[ac++] = lizaveta_exe;
    argv_buf[ac++] = "--filechooser";
    argv_buf[ac++] = "--out";
    argv_buf[ac++] = out_path;
    if (opt->multiple && !is_save && !opt->directory)
        argv_buf[ac++] = "--multiple";
    if (opt->directory)
        argv_buf[ac++] = "--directory";
    for (int i = 0; i < opt->filter_count; i++) {
        argv_buf[ac++] = "--filter";
        argv_buf[ac++] = opt->filter_specs[i];
    }
    if (opt->current_filter_index >= 0) {
        snprintf(filter_index_str, sizeof(filter_index_str), "%d", opt->current_filter_index);
        argv_buf[ac++] = "--filter-index";
        argv_buf[ac++] = filter_index_str;
    }
    if (is_save) {
        argv_buf[ac++] = "--save";
        argv_buf[ac++] = opt->current_name[0] ? opt->current_name : "Untitled";
    }
    if (opt->current_folder[0])
        argv_buf[ac++] = opt->current_folder;
    argv_buf[ac] = NULL;

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "lizaveta-portal: fork failed: %s\n", strerror(errno));
        unlink(out_path);
        return false;
    }
    if (pid == 0) {
        execv(lizaveta_exe, (char* const*)argv_buf);
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }

    bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (ok) {
        FILE* f = fopen(out_path, "r");
        if (!f) {
            ok = false;
        } else {
            char** paths = NULL;
            int n = 0, cap = 0;
            char line[PATH_MAX];
            while (fgets(line, sizeof(line), f)) {
                size_t len = strlen(line);
                while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
                    line[--len] = '\0';
                if (len == 0)
                    continue;
                if (n == cap) {
                    cap = cap ? cap * 2 : 4;
                    paths = (char**)realloc(paths, sizeof(char*) * (size_t)cap);
                }
                paths[n++] = strdup(line);
            }
            fclose(f);
            if (n > 0) {
                *out_paths = paths;
                *out_n = n;
            } else {
                free(paths);
                ok = false; /* user cancelled: empty output is the contract */
            }
        }
    }

    unlink(out_path);
    return ok;
}

/* Sends (u response, a{sv} results) with results = {"uris": <as>} for a
 * successful pick, or an empty dict for a cancellation/error. */
static void liz_portal_reply(DBusConnection* conn, DBusMessage* call,
                            dbus_uint32_t response, char** paths, int n)
{
    DBusMessage* reply = dbus_message_new_method_return(call);
    DBusMessageIter it;
    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &response);

    DBusMessageIter results;
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &results);
    if (n > 0) {
        DBusMessageIter entry, variant, uris;
        dbus_message_iter_open_container(&results, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        const char* key = "uris";
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &variant);
        dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &uris);
        for (int i = 0; i < n; i++) {
            char enc[PATH_MAX * 3];
            liz_portal_uri_encode(paths[i], enc, sizeof(enc));
            char uri[sizeof(enc) + 8];
            snprintf(uri, sizeof(uri), "file://%s", enc);
            const char* uri_p = uri;
            dbus_message_iter_append_basic(&uris, DBUS_TYPE_STRING, &uri_p);
        }
        dbus_message_iter_close_container(&variant, &uris);
        dbus_message_iter_close_container(&entry, &variant);
        dbus_message_iter_close_container(&results, &entry);
    }
    dbus_message_iter_close_container(&it, &results);

    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
}

/* Shared OpenFile/SaveFile/SaveFiles handler; `is_save` picks --save vs. a
 * plain/--directory pick, matching each method's semantics. */
static void liz_portal_handle_pick(DBusConnection* conn, DBusMessage* msg,
                                  const char* lizaveta_exe, bool is_save)
{
    DBusMessageIter args;
    dbus_message_iter_init(msg, &args);

    dbus_message_iter_next(&args); /* handle (o) - not needed: we reply synchronously */
    dbus_message_iter_next(&args); /* app_id (s) */
    dbus_message_iter_next(&args); /* parent_window (s) */

    const char* title = NULL;
    dbus_message_iter_get_basic(&args, &title);
    dbus_message_iter_next(&args);

    liz_portal_options opt;
    liz_portal_parse_options(&args, &opt);

    char** paths = NULL;
    int n = 0;
    bool ok = liz_portal_run_picker(lizaveta_exe, title, is_save, &opt, &paths, &n);

    /* response: 0 success, 1 user cancelled the operation (spec's "the user
     * dismissed the dialog") -- we treat any failure the same way, since
     * lizaveta's own chooser mode never distinguishes "cancelled" from
     * "crashed" beyond writing nothing */
    liz_portal_reply(conn, msg, ok ? 0 : 1, paths, n);

    for (int i = 0; i < n; i++)
        free(paths[i]);
    free(paths);
}

static void liz_portal_reply_empty(DBusConnection* conn, DBusMessage* msg, const char* sig)
{
    DBusMessage* reply = dbus_message_new_method_return(msg);
    (void)sig;
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
}

/* Minimal introspection XML: just enough for `busctl`/`d-feet` to show the
 * interface exists. Not a substitute for the real portal XML, but the
 * portal daemon itself doesn't introspect its backends -- it calls methods
 * directly -- so this is purely a debugging nicety. */
static const char* liz_portal_introspect_xml =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>\n"
    "  <interface name=\"" LIZ_PORTAL_IFACE "\">\n"
    "    <method name=\"OpenFile\">\n"
    "      <arg type=\"o\" name=\"handle\" direction=\"in\"/>\n"
    "      <arg type=\"s\" name=\"app_id\" direction=\"in\"/>\n"
    "      <arg type=\"s\" name=\"parent_window\" direction=\"in\"/>\n"
    "      <arg type=\"s\" name=\"title\" direction=\"in\"/>\n"
    "      <arg type=\"a{sv}\" name=\"options\" direction=\"in\"/>\n"
    "      <arg type=\"u\" name=\"response\" direction=\"out\"/>\n"
    "      <arg type=\"a{sv}\" name=\"results\" direction=\"out\"/>\n"
    "    </method>\n"
    "    <method name=\"SaveFile\">\n"
    "      <arg type=\"o\" name=\"handle\" direction=\"in\"/>\n"
    "      <arg type=\"s\" name=\"app_id\" direction=\"in\"/>\n"
    "      <arg type=\"s\" name=\"parent_window\" direction=\"in\"/>\n"
    "      <arg type=\"s\" name=\"title\" direction=\"in\"/>\n"
    "      <arg type=\"a{sv}\" name=\"options\" direction=\"in\"/>\n"
    "      <arg type=\"u\" name=\"response\" direction=\"out\"/>\n"
    "      <arg type=\"a{sv}\" name=\"results\" direction=\"out\"/>\n"
    "    </method>\n"
    "    <method name=\"SaveFiles\">\n"
    "      <arg type=\"o\" name=\"handle\" direction=\"in\"/>\n"
    "      <arg type=\"s\" name=\"app_id\" direction=\"in\"/>\n"
    "      <arg type=\"s\" name=\"parent_window\" direction=\"in\"/>\n"
    "      <arg type=\"s\" name=\"title\" direction=\"in\"/>\n"
    "      <arg type=\"a{sv}\" name=\"options\" direction=\"in\"/>\n"
    "      <arg type=\"u\" name=\"response\" direction=\"out\"/>\n"
    "      <arg type=\"a{sv}\" name=\"results\" direction=\"out\"/>\n"
    "    </method>\n"
    "    <property name=\"version\" type=\"u\" access=\"read\"/>\n"
    "  </interface>\n"
    "</node>\n";

static DBusHandlerResult liz_portal_message_handler(DBusConnection* conn, DBusMessage* msg,
                                                    void* user_data)
{
    const char* lizaveta_exe = (const char*)user_data;

    if (dbus_message_is_method_call(msg, LIZ_PORTAL_IFACE, "OpenFile")) {
        liz_portal_handle_pick(conn, msg, lizaveta_exe, false);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(msg, LIZ_PORTAL_IFACE, "SaveFile")) {
        liz_portal_handle_pick(conn, msg, lizaveta_exe, true);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(msg, LIZ_PORTAL_IFACE, "SaveFiles")) {
        /* SaveFiles picks a destination *folder* for a batch of already-
         * named files -- there is no single filename to edit, so this maps
         * onto lizaveta's directory-picker mode rather than --save. */
        DBusMessageIter args;
        dbus_message_iter_init(msg, &args);
        dbus_message_iter_next(&args);
        dbus_message_iter_next(&args);
        dbus_message_iter_next(&args);
        const char* title = NULL;
        dbus_message_iter_get_basic(&args, &title);
        dbus_message_iter_next(&args);

        liz_portal_options opt;
        liz_portal_parse_options(&args, &opt);
        opt.directory = true;

        char** paths = NULL;
        int n = 0;
        bool ok = liz_portal_run_picker(lizaveta_exe, title, false, &opt, &paths, &n);
        liz_portal_reply(conn, msg, ok ? 0 : 1, paths, n);
        for (int i = 0; i < n; i++)
            free(paths[i]);
        free(paths);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        DBusMessage* reply = dbus_message_new_method_return(msg);
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &liz_portal_introspect_xml,
                                 DBUS_TYPE_INVALID);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "Get")) {
        DBusMessage* reply = dbus_message_new_method_return(msg);
        DBusMessageIter it, variant;
        dbus_message_iter_init_append(reply, &it);
        dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "u", &variant);
        dbus_uint32_t version = 1;
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &version);
        dbus_message_iter_close_container(&it, &variant);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "GetAll")) {
        liz_portal_reply_empty(conn, msg, "a{sv}");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (!dbus_message_get_no_reply(msg)) {
        DBusMessage* err = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_METHOD,
                                                   "lizaveta: no such method");
        dbus_connection_send(conn, err, NULL);
        dbus_message_unref(err);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

int liz_portal_service_run(const char* lizaveta_exe)
{
    /* picker children are reaped by the blocking waitpid() in
     * liz_portal_run_picker(); SIGCHLD's default disposition is fine (it
     * does not need to interrupt anything here) */
    signal(SIGPIPE, SIG_IGN);

    DBusError err;
    dbus_error_init(&err);

    DBusConnection* conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!conn) {
        fprintf(stderr, "lizaveta-portal: cannot connect to session bus: %s\n", err.message);
        dbus_error_free(&err);
        return 1;
    }

    int rc = dbus_bus_request_name(conn, LIZ_PORTAL_BUS_NAME,
                                   DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "lizaveta-portal: cannot claim %s: %s\n", LIZ_PORTAL_BUS_NAME, err.message);
        dbus_error_free(&err);
        return 1;
    }
    if (rc != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr, "lizaveta-portal: %s is already owned (another lizaveta --portal-service"
                        " running?)\n", LIZ_PORTAL_BUS_NAME);
        return 1;
    }

    DBusObjectPathVTable vtable;
    memset(&vtable, 0, sizeof(vtable));
    vtable.message_function = liz_portal_message_handler;

    if (!dbus_connection_register_object_path(conn, LIZ_PORTAL_OBJECT_PATH, &vtable,
                                              (void*)lizaveta_exe)) {
        fprintf(stderr, "lizaveta-portal: failed to register %s\n", LIZ_PORTAL_OBJECT_PATH);
        return 1;
    }

    fprintf(stderr, "lizaveta-portal: serving %s as %s\n", LIZ_PORTAL_IFACE, LIZ_PORTAL_BUS_NAME);

    /* org.freedesktop.FileManager1 ("Show in folder") is a second, unrelated
     * D-Bus interface, but there's no reason to run a second long-lived
     * process for it: one connection can own several bus names and route
     * each to its own object path, so it rides along on the same service
     * and the same dispatch loop below. Its failure is logged but doesn't
     * stop the FileChooser portal from working. */
    liz_filemanager1_register(conn, lizaveta_exe);

    while (dbus_connection_read_write_dispatch(conn, -1)) { }

    fprintf(stderr, "lizaveta-portal: session bus connection closed, exiting\n");
    return 0;
}
