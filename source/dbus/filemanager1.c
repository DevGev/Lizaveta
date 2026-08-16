/* filemanager1.c - org.freedesktop.FileManager1 implementation. */

#include "dbus/filemanager1.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/wait.h>

#define LIZ_FM1_BUS_NAME "org.freedesktop.FileManager1"
#define LIZ_FM1_OBJECT_PATH "/org/freedesktop/FileManager1"
#define LIZ_FM1_IFACE "org.freedesktop.FileManager1"

/* "file:///a/b%20c" -> "/a/b c". Any URI without a "file://" prefix (a
 * remote/trash/etc location we can't open as a local path) yields an empty
 * string, which the caller treats as "skip this one". */
static void liz_fm1_uri_to_path(const char* uri, char* out, size_t outsz)
{
    out[0] = '\0';
    if (strncmp(uri, "file://", 7) != 0)
        return;
    const char* p = uri + 7;

    size_t n = 0;
    while (*p && n + 1 < outsz) {
        if (p[0] == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
            char hex[3] = { p[1], p[2], '\0' };
            out[n++] = (char)strtol(hex, NULL, 16);
            p += 3;
        } else {
            out[n++] = *p++;
        }
    }
    out[n] = '\0';
}

/* Same double-fork detach as liz_app_detach() in app/app.c (duplicated
 * rather than shared, to keep this file free of app/ headers): the
 * intermediate child is reaped immediately so the D-Bus service never
 * accumulates zombies, and the actual lizaveta window is reparented to
 * init, fully independent of the service process. */
static void liz_fm1_launch(const char* lizaveta_exe, const char* path)
{
    pid_t pid = fork();
    if (pid < 0)
        return;
    if (pid == 0) {
        setsid();
        pid_t pid2 = fork();
        if (pid2 == 0) {
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                if (devnull > 2)
                    close(devnull);
            }
            if (path && path[0])
                execl(lizaveta_exe, lizaveta_exe, path, (char*)NULL);
            else
                execl(lizaveta_exe, lizaveta_exe, (char*)NULL);
            _exit(127);
        }
        _exit(0);
    }
    int status;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
}

/* Shared ShowFolders/ShowItems/ShowItemProperties handler: all three take
 * (as uris, s startup_id) and return nothing. Only the first URI is acted
 * on -- lizaveta opens one window either way, and a single item is what
 * every real caller (Firefox's downloads panel included) actually sends. */
static void liz_fm1_handle(DBusConnection* conn, DBusMessage* msg, const char* lizaveta_exe)
{
    DBusMessageIter args, uris;
    dbus_message_iter_init(msg, &args);
    dbus_message_iter_recurse(&args, &uris);

    char path[PATH_MAX];
    path[0] = '\0';
    if (dbus_message_iter_get_arg_type(&uris) == DBUS_TYPE_STRING) {
        const char* uri = NULL;
        dbus_message_iter_get_basic(&uris, &uri);
        liz_fm1_uri_to_path(uri, path, sizeof(path));
    }

    if (path[0])
        liz_fm1_launch(lizaveta_exe, path);

    DBusMessage* reply = dbus_message_new_method_return(msg);
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
}

static const char* liz_fm1_introspect_xml =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>\n"
    "  <interface name=\"" LIZ_FM1_IFACE "\">\n"
    "    <method name=\"ShowFolders\">\n"
    "      <arg type=\"as\" name=\"uris\" direction=\"in\"/>\n"
    "      <arg type=\"s\" name=\"startup_id\" direction=\"in\"/>\n"
    "    </method>\n"
    "    <method name=\"ShowItems\">\n"
    "      <arg type=\"as\" name=\"uris\" direction=\"in\"/>\n"
    "      <arg type=\"s\" name=\"startup_id\" direction=\"in\"/>\n"
    "    </method>\n"
    "    <method name=\"ShowItemProperties\">\n"
    "      <arg type=\"as\" name=\"uris\" direction=\"in\"/>\n"
    "      <arg type=\"s\" name=\"startup_id\" direction=\"in\"/>\n"
    "    </method>\n"
    "  </interface>\n"
    "</node>\n";

static DBusHandlerResult liz_fm1_message_handler(DBusConnection* conn, DBusMessage* msg,
                                                void* user_data)
{
    const char* lizaveta_exe = (const char*)user_data;

    if (dbus_message_is_method_call(msg, LIZ_FM1_IFACE, "ShowFolders")
        || dbus_message_is_method_call(msg, LIZ_FM1_IFACE, "ShowItems")
        /* no properties dialog to show -- falling back to "reveal the item
         * in its folder", same as ShowItems, is more useful than a no-op */
        || dbus_message_is_method_call(msg, LIZ_FM1_IFACE, "ShowItemProperties")) {
        liz_fm1_handle(conn, msg, lizaveta_exe);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        DBusMessage* reply = dbus_message_new_method_return(msg);
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &liz_fm1_introspect_xml,
                                 DBUS_TYPE_INVALID);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
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

bool liz_filemanager1_register(DBusConnection* conn, const char* lizaveta_exe)
{
    DBusError err;
    dbus_error_init(&err);

    int rc = dbus_bus_request_name(conn, LIZ_FM1_BUS_NAME, DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "lizaveta-filemanager1: cannot claim %s: %s\n",
                LIZ_FM1_BUS_NAME, err.message);
        dbus_error_free(&err);
        return false;
    }
    if (rc != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr, "lizaveta-filemanager1: %s is already owned by another running file"
                        " manager -- \"Show in folder\" will keep opening that one until it"
                        " exits (it isn't just D-Bus-activatable, it's actively running and"
                        " holding the name)\n", LIZ_FM1_BUS_NAME);
        return false;
    }

    DBusObjectPathVTable vtable;
    memset(&vtable, 0, sizeof(vtable));
    vtable.message_function = liz_fm1_message_handler;

    if (!dbus_connection_register_object_path(conn, LIZ_FM1_OBJECT_PATH, &vtable,
                                              (void*)lizaveta_exe)) {
        fprintf(stderr, "lizaveta-filemanager1: failed to register %s\n", LIZ_FM1_OBJECT_PATH);
        dbus_bus_release_name(conn, LIZ_FM1_BUS_NAME, NULL);
        return false;
    }

    fprintf(stderr, "lizaveta-filemanager1: serving %s as %s\n", LIZ_FM1_IFACE, LIZ_FM1_BUS_NAME);
    return true;
}
