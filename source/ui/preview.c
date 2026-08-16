/* preview.c - embedded preview pane for the selected entry. */

#include "ui/preview.h"

#include "ui/theme.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xutil.h>

/* bounded poll right after spawn: how long to wait in total, and the poll
 * period. If the window still has not appeared, the slave stays pending and
 * later syncs keep looking for it. */
#define LIZ_PREVIEW_POLL_TOTAL_MS 800
#define LIZ_PREVIEW_POLL_MS 10

/* Uncomment to debug: */
/* #define LIZ_PREVIEW_DBG(fmt, ...) fprintf(stderr, "[preview] " fmt "\n", ##__VA_ARGS__) */
#define LIZ_PREVIEW_DBG(fmt, ...) ;

static const char* const liz_img_exts[] = {
    "avif", "bmp", "gif", "heic", "ico", "jpeg", "jpg",
    "png", "svg", "tif", "tiff", "webp", "xpm", NULL
};

static bool liz_slave_image_matches(const liz_fs_entry* e)
{
    if (e->type != LIZ_FS_FILE)
        return false;
    const char* dot = strrchr(e->name, '.');
    if (!dot || dot == e->name)
        return false;
    for (int i = 0; liz_img_exts[i]; i++) {
        size_t n = strlen(liz_img_exts[i]);
        if (strlen(dot + 1) == n && strncasecmp(dot + 1, liz_img_exts[i], n) == 0)
            return true;
    }
    return false;
}

/* Window title the image slave is told to use; the manager matches this and
 * the image basename. */
#define LIZ_PREVIEW_WINDOW_TITLE "lzaveta-preview"

static struct {
    Display* dpy;         /* set on the first sync */
    Window main_win;

    bool enabled;         /* preview pane armed by the user */
    char active_cwd[PATH_MAX];
    int active_row;       /* selection the preview is anchored to, -1 */

    pid_t pid;            /* slave process, 0 when none */
    const liz_preview_slave* slave_type;
    char path[PATH_MAX];  /* the file currently being previewed */

    Window slave;         /* embedded window, None when pending/absent */
    int x, y, w, h;       /* pane geometry in main-window coordinates */
    char geo[64];         /* geometry string handed to the image/text slave */
} g_pv;

static int liz_slave_image_command(const char* path, const liz_preview_geom* geom,
                                  char** argv, int cap)
{
    if (cap < 12)
        return 0;
    snprintf(g_pv.geo, sizeof(g_pv.geo), "%dx%d+%d+%d",
             geom->w, geom->h, geom->sx, geom->sy);
    argv[0] = "feh";
    argv[1] = "-x";            /* borderless */
    argv[2] = "-g";            /* initial geometry: avoids the launch flicker */
    argv[3] = g_pv.geo;
    argv[4] = "-Z";            /* auto-zoom to fit the window */
    argv[5] = "--no-fehbg";
    argv[6] = "--image-bg";
    argv[7] = "#212128";       /* liz_theme_bg */
    argv[8] = "--title";
    argv[9] = LIZ_PREVIEW_WINDOW_TITLE;
    argv[10] = (char*)path;
    return 11;
}

/* Any regular file that no earlier slave claimed (i.e. not an image) is
 * treated as text and previewed with st running vim. */
static bool liz_slave_text_matches(const liz_fs_entry* e)
{
    return e->type == LIZ_FS_FILE;
}

/* st -e vim runs vim inside the embedded terminal. -G sets the initial
 * geometry in raw pixels (the new st accepts -G instead of the cols/rows
 * -g); with -w it is relative to lizaveta's window. -w embeds the terminal
 * as a child of our window, so the WM never manages it and can never tile
 * it. -n/-c pin the WM_CLASS so the manager can still find the window even
 * after vim renames the title. */
static int liz_slave_text_command(const char* path, const liz_preview_geom* geom,
                                 char** argv, int cap)
{
    if (cap < 15)
        return 0;
    snprintf(g_pv.geo, sizeof(g_pv.geo), "%dx%d+%d+%d",
             geom->w, geom->h, geom->x, geom->y);
    static char winid[32];
    snprintf(winid, sizeof(winid), "0x%lx", (unsigned long)g_pv.main_win);
    argv[0] = "st";
    argv[1] = "-G";            /* raw pixel geometry: no launch flicker */
    argv[2] = g_pv.geo;
    argv[3] = "-n";            /* WM_CLASS instance */
    argv[4] = LIZ_PREVIEW_WINDOW_TITLE;
    argv[5] = "-c";            /* WM_CLASS class */
    argv[6] = LIZ_PREVIEW_WINDOW_TITLE;
    argv[7] = "-T";            /* window title */
    argv[8] = LIZ_PREVIEW_WINDOW_TITLE;
    argv[9] = "-w";            /* embed into lizaveta's window */
    argv[10] = winid;
    argv[11] = "-e";           /* run vim on the file */
    argv[12] = "vim";
    argv[13] = (char*)path;
    return 14;
}

static const liz_preview_slave liz_preview_slaves[] = {
    { "image", liz_slave_image_matches, liz_slave_image_command,
      LIZ_PREVIEW_WINDOW_TITLE },
    { "text", liz_slave_text_matches, liz_slave_text_command,
      LIZ_PREVIEW_WINDOW_TITLE },
};

#define LIZ_PREVIEW_SLAVE_COUNT \
    (sizeof(liz_preview_slaves) / sizeof(liz_preview_slaves[0]))

static const liz_preview_slave* liz_preview_find_slave(const liz_fs_entry* e)
{
    for (size_t i = 0; i < LIZ_PREVIEW_SLAVE_COUNT; i++) {
        if (liz_preview_slaves[i].matches(e))
            return &liz_preview_slaves[i];
    }
    return NULL;
}

/* Geometry of the preview pane: right of the sidebar, the bottom half of
 * the list view. Returns false when the pane would be smaller than
 * LIZ_PREVIEW_MIN_H. */
static bool liz_preview_geometry(liz_app* app, int* x, int* y, int* w, int* h)
{
    xwindow* win = app->win;
    int left = app->sidebar_visible ? LIZ_UI_SIDEBAR_W : 0;
    int top = LIZ_UI_NAV_H;
    int bottom = win->height - LIZ_UI_STATUS_H;
    int pane_h = (bottom - top) / 2;
    if (pane_h < LIZ_PREVIEW_MIN_H)
        return false;
    *x = left;
    *y = bottom - pane_h;
    *w = win->width - left;
    if (*w < 1)
        return false;
    *h = pane_h;
    return true;
}

/* Reads a window title: _NET_WM_NAME first, falling back to WM_NAME.
 * Returns false when the window has no title. */
static bool liz_preview_window_title(Window win, char* out, size_t outsz)
{
    Atom net_name = XInternAtom(g_pv.dpy, "_NET_WM_NAME", False);
    Atom type;
    int fmt;
    unsigned long nitems, after;
    unsigned char* data = NULL;
    if (XGetWindowProperty(g_pv.dpy, win, net_name, 0, 1024, False,
                           AnyPropertyType, &type, &fmt, &nitems, &after,
                           &data) == Success && data && nitems > 0) {
        size_t n = nitems * (fmt == 32 ? 4 : fmt == 16 ? 2 : 1);
        if (n >= outsz)
            n = outsz - 1;
        memcpy(out, data, n);
        out[n] = '\0';
        XFree(data);
        return true;
    }
    if (data)
        XFree(data);

    char* name = NULL;
    if (XFetchName(g_pv.dpy, win, &name)) {
        if (name) {
            snprintf(out, outsz, "%s", name);
            XFree(name);
            return true;
        }
        XFree(name);
    }
    return false;
}

/* Matches a candidate window: the marker title, the image basename, or the
 * marker as WM_CLASS instance/class. The class match matters for the text
 * slave: vim renames the terminal title, but the WM_CLASS set with st -n/-c
 * stays stable. */
static bool liz_preview_matches(Window win, const char* marker, const char* basename)
{
    char title[256];
    if (liz_preview_window_title(win, title, sizeof(title))) {
        if ((marker && strcmp(title, marker) == 0)
            || (basename && strcmp(title, basename) == 0))
            return true;
    }
    if (marker) {
        XClassHint ch = { NULL, NULL };
        if (XGetClassHint(g_pv.dpy, win, &ch)) {
            bool m = (ch.res_name && strcmp(ch.res_name, marker) == 0)
                     || (ch.res_class && strcmp(ch.res_class, marker) == 0);
            if (ch.res_name)
                XFree(ch.res_name);
            if (ch.res_class)
                XFree(ch.res_class);
            if (m)
                return true;
        }
    }
    return false;
}

/* Recursively searches the tree under `root` for the slave's window. A
 * window matches when its title or WM_CLASS equals `marker` (the class/title
 * the slave was told to use), or the image basename. */
static Window liz_preview_find_title(Window root, const char* marker, const char* basename)
{
    Window unused1, unused2;
    Window* children = NULL;
    unsigned int n = 0;
    if (XQueryTree(g_pv.dpy, root, &unused1, &unused2, &children, &n) == 0)
        return None;
    Window found = None;
    for (unsigned int i = 0; i < n && found == None; i++) {
        if (liz_preview_matches(children[i], marker, basename))
            found = children[i];
        if (found == None)
            found = liz_preview_find_title(children[i], marker, basename);
    }
    if (children)
        XFree(children);
    return found;
}

/* Silences X errors while embedding or tearing down a slave window. By the
 * time a preview is closed the slave may already have destroyed its window,
 * so X operations fail with BadWindow; the default Xlib handler would abort
 * the app over these benign races. */
static int liz_preview_ignore_xerror(Display* d, XErrorEvent* e)
{
    (void)d;
    (void)e;
    return 0;
}

static void liz_preview_quiet(void)
{
    XSync(g_pv.dpy, False);
    XSetErrorHandler(liz_preview_ignore_xerror);
}

static void liz_preview_loud(void)
{
    XSync(g_pv.dpy, False);
    XSetErrorHandler(NULL);
}

/* Some window managers (notably dwm) leave a freshly reparented window
 * undrawn: the client mapped it as a toplevel and, since the size is
 * already correct, never gets a ConfigureNotify for its new parent. feh
 * redraws on ConfigureNotify, so nudge the size to force one, then also
 * clear and deliver a synthetic Expose for good measure. */
static void liz_preview_refresh(void)
{
    XResizeWindow(g_pv.dpy, g_pv.slave, g_pv.w + 1, g_pv.h + 1);
    XResizeWindow(g_pv.dpy, g_pv.slave, g_pv.w, g_pv.h);
    XClearWindow(g_pv.dpy, g_pv.slave);
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xexpose.type = Expose;
    ev.xexpose.window = g_pv.slave;
    ev.xexpose.x = 0;
    ev.xexpose.y = 0;
    ev.xexpose.width = g_pv.w;
    ev.xexpose.height = g_pv.h;
    ev.xexpose.count = 0;
    XSendEvent(g_pv.dpy, g_pv.slave, False, ExposureMask, &ev);
    XFlush(g_pv.dpy);
}

/* True when `w` currently appears in the WM's _NET_CLIENT_LIST, i.e. the WM
 * is managing it as a toplevel client. */
static bool liz_preview_in_client_list(Window w)
{
    Atom prop = XInternAtom(g_pv.dpy, "_NET_CLIENT_LIST", False);
    Atom type;
    int fmt;
    unsigned long nitems, after;
    unsigned char* data = NULL;
    bool found = false;
    if (XGetWindowProperty(g_pv.dpy, DefaultRootWindow(g_pv.dpy), prop, 0, 1024,
                           False, AnyPropertyType, &type, &fmt, &nitems, &after,
                           &data) == Success && data && fmt == 32) {
        const Window* wins = (const Window*)data;
        for (unsigned long i = 0; i < nitems; i++)
            if (wins[i] == w) {
                found = true;
                break;
            }
        XFree(data);
    }
    return found;
}

/* If the WM is (still) managing the embedded window, force it to drop the
 * client: unmap (the WM processes the UnmapNotify and unmanages), wait until
 * the window leaves _NET_CLIENT_LIST, then map it again. Safety net for the
 * race where the slave mapped as a toplevel before we could embed it. */
static void liz_preview_kick_client(Window w)
{
    XUnmapWindow(g_pv.dpy, w);
    for (int i = 0; i < 100 && liz_preview_in_client_list(w); i++) {
        XSync(g_pv.dpy, False);
        usleep(2 * 1000);
    }
    XMapWindow(g_pv.dpy, w);
}

/* Embeds the slave window into our window at the pane position and maps it.
 *
 * The text slave (st) is spawned with -w <our window>, so its window is born
 * as our child and the WM never manages it. The image slave (feh) has no
 * such option: it maps as a normal toplevel, and the WM tiles it -- that is
 * what makes the pane flicker between "embedded" and "tiled" under dwm.
 *
 * To make the WM release a window it already manages, the window is unmapped
 * first (the WM gets an UnmapNotify and drops its client), and we wait until
 * it actually leaves _NET_CLIENT_LIST so we never reparent while the WM still
 * has a live client for it. override_redirect is set while the window is
 * unmapped, so the mapping that follows generates no MapRequest and no WM can
 * ever manage it again. */
static void liz_preview_embed(liz_app* app)
{
    if (g_pv.slave == None)
        return;
    liz_preview_quiet();

    Window root_ret, parent_ret;
    Window* children = NULL;
    unsigned int n = 0;
    bool ours = XQueryTree(g_pv.dpy, g_pv.slave, &root_ret, &parent_ret,
                           &children, &n) && parent_ret == g_pv.main_win;
    if (children)
        XFree(children);

    if (!ours) {
        /* The WM is managing the window as a toplevel and keeps tiling it.
         * Unmap it so the WM drops the client, and wait (bounded) until it
         * is really gone, or the WM may fight us over the geometry while we
         * reparent. */
        XUnmapWindow(g_pv.dpy, g_pv.slave);
        for (int i = 0; i < 100 && liz_preview_in_client_list(g_pv.slave); i++) {
            XSync(g_pv.dpy, False);
            usleep(2 * 1000);
        }

        /* override_redirect while the window is unmapped: a window mapped
         * with override_redirect generates no MapRequest, so no WM can ever
         * manage it again. */
        XSetWindowAttributes attrs;
        memset(&attrs, 0, sizeof(attrs));
        attrs.override_redirect = True;
        XChangeWindowAttributes(g_pv.dpy, g_pv.slave, CWOverrideRedirect, &attrs);
        XSetWindowBackground(g_pv.dpy, g_pv.slave, xc_pixel(app->win, liz_theme_bg));
    }

    XReparentWindow(g_pv.dpy, g_pv.slave, g_pv.main_win, g_pv.x, g_pv.y);
    XResizeWindow(g_pv.dpy, g_pv.slave, g_pv.w, g_pv.h);
    XMapWindow(g_pv.dpy, g_pv.slave);
    XRaiseWindow(g_pv.dpy, g_pv.slave);
    XFlush(g_pv.dpy);
    liz_preview_refresh();
    liz_preview_loud();
    LIZ_PREVIEW_DBG("embedded window 0x%lx", (unsigned long)g_pv.slave);
}

/* Looks for the pending slave's window and embeds it. Returns true when it
 * was found (whether or not it could be embedded). */
static bool liz_preview_embed_pending(liz_app* app)
{
    if (g_pv.slave != None || g_pv.pid <= 0 || !g_pv.slave_type)
        return false;
    const char* basename = strrchr(g_pv.path, '/');
    basename = basename ? basename + 1 : g_pv.path;
    Window found = liz_preview_find_title(DefaultRootWindow(g_pv.dpy),
                                         g_pv.slave_type->window_title,
                                         basename);
    if (found == None)
        return false;
    LIZ_PREVIEW_DBG("found slave window 0x%lx", (unsigned long)found);
    g_pv.slave = found;
    liz_preview_embed(app);
    return true;
}

/* Stops the slave: unmap its window, terminate the process (SIGTERM, then
 * SIGKILL after a short grace period), and reap it. */
static void liz_preview_kill(void)
{
    if (g_pv.slave != None) {
        liz_preview_quiet();
        XUnmapWindow(g_pv.dpy, g_pv.slave);
        liz_preview_loud();
        g_pv.slave = None;
    }
    if (g_pv.pid > 0) {
        LIZ_PREVIEW_DBG("killing slave pid %d", g_pv.pid);
        kill(g_pv.pid, SIGTERM);
        pid_t r = 0;
        int tries = 0;
        while ((r = waitpid(g_pv.pid, NULL, WNOHANG)) == 0 && tries++ < 10)
            usleep(10 * 1000);
        if (r == 0) {
            kill(g_pv.pid, SIGKILL);
            waitpid(g_pv.pid, NULL, 0);
        }
        g_pv.pid = 0;
    }
    g_pv.slave_type = NULL;
    XFlush(g_pv.dpy);
}

/* Spawns the slave program for `path`, detached from our stdio. The argv is
 * built from string literals plus g_pv.path and g_pv.geo, so there is
 * nothing to free. */
static void liz_preview_spawn(liz_app* app, const liz_preview_slave* sl, const char* path)
{
    (void)app;
    snprintf(g_pv.path, sizeof(g_pv.path), "%s", path);

    /* screen-space position of the pane, for the slave's initial placement */
    Window root_ret, child_ret;
    int abs_x = 0, abs_y = 0;
    if (XTranslateCoordinates(g_pv.dpy, g_pv.main_win,
                              DefaultRootWindow(g_pv.dpy), 0, 0,
                              &abs_x, &abs_y, &child_ret)) {
        (void)root_ret;
        abs_x += g_pv.x;
        abs_y += g_pv.y;
    } else {
        abs_x = g_pv.x;
        abs_y = g_pv.y;
    }

    liz_preview_geom geom;
    geom.x = g_pv.x;
    geom.y = g_pv.y;
    geom.w = g_pv.w;
    geom.h = g_pv.h;
    geom.sx = abs_x;
    geom.sy = abs_y;
    LIZ_PREVIEW_DBG("geometry pane=%dx%d+%d+%d screen=%d+%d (app window at %d,%d)",
                   geom.w, geom.h, geom.x, geom.y, geom.sx, geom.sy, abs_x - g_pv.x,
                   abs_y - g_pv.y);

    char* argv[LIZ_PREVIEW_ARGV_MAX];
    int n = sl->command(g_pv.path, &geom, argv, LIZ_PREVIEW_ARGV_MAX);
    if (n <= 0 || n >= LIZ_PREVIEW_ARGV_MAX) {
        LIZ_PREVIEW_DBG("slave %s: empty command", sl->name);
        return;
    }
    argv[n] = NULL;   /* execvp requires a NULL-terminated argv */

    char cmdline[1024] = "";
    for (int i = 0; argv[i] && i < LIZ_PREVIEW_ARGV_MAX; i++) {
        size_t len = strlen(argv[i]);
        if (strlen(cmdline) + len + 2 >= sizeof(cmdline))
            break;
        if (i)
            strcat(cmdline, " ");
        strcat(cmdline, argv[i]);
    }
    LIZ_PREVIEW_DBG("argv: %s", cmdline);

    pid_t pid = fork();
    if (pid < 0) {
        LIZ_PREVIEW_DBG("fork failed");
        return;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            if (devnull > 2)
                close(devnull);
        }
        /* debug: keep the slave's stderr in a log file */
        int logfd = open("/tmp/lzaveta-preview-feh.log",
                         O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (logfd >= 0) {
            dup2(logfd, STDERR_FILENO);
            if (logfd > 2)
                close(logfd);
        }
        execvp(argv[0], argv);
        fprintf(stderr, "execvp %s failed: errno=%d (%s)\n", argv[0], errno,
                strerror(errno));
        fprintf(stderr, "PATH=%s\n", getenv("PATH") ? getenv("PATH") : "(unset)");
        _exit(127);
    }

    g_pv.pid = pid;
    g_pv.slave_type = sl;
    g_pv.slave = None;
    LIZ_PREVIEW_DBG("spawned pid %d: %s", pid, g_pv.path);
}

void liz_preview_toggle(liz_app* app)
{
    if (g_pv.enabled) {
        LIZ_PREVIEW_DBG("toggle off (was armed)");
        g_pv.enabled = false;
        liz_preview_kill();
        return;
    }
    g_pv.enabled = true;
    g_pv.active_row = app->selected;
    snprintf(g_pv.active_cwd, sizeof(g_pv.active_cwd), "%s", app->cwd);
    LIZ_PREVIEW_DBG("toggle on (row %d, cwd %s)", g_pv.active_row, g_pv.active_cwd);
}

void liz_preview_close(liz_app* app)
{
    (void)app;
    if (!g_pv.enabled && g_pv.pid <= 0)
        return;
    LIZ_PREVIEW_DBG("close");
    g_pv.enabled = false;
    liz_preview_kill();
}

int liz_preview_pane_top(liz_app* app)
{
    if (!g_pv.enabled || g_pv.pid <= 0)
        return -1;
    int x, y, w, h;
    if (!liz_preview_geometry(app, &x, &y, &w, &h))
        return -1;
    return y;
}

void liz_preview_sync(liz_app* app)
{
    xwindow* win = app->win;
    if (!win)
        return;

    if (g_pv.dpy != win->display) {
        g_pv.dpy = win->display;
        g_pv.main_win = win->window;
        g_pv.active_row = -1;
        g_pv.enabled = false;
        g_pv.pid = 0;
        g_pv.slave = None;
    }

    /* a slave that has since died leaves nothing to show */
    if (g_pv.pid > 0 && waitpid(g_pv.pid, NULL, WNOHANG) == g_pv.pid) {
        LIZ_PREVIEW_DBG("slave pid %d exited", g_pv.pid);
        g_pv.pid = 0;
        g_pv.slave_type = NULL;
        g_pv.slave = None;
        g_pv.enabled = false;
        return;
    }

    int row = app->selected;
    if (!g_pv.enabled) {
        if (g_pv.pid > 0)
            liz_preview_kill();
        return;
    }

    /* an armed preview closes when the selection or directory moves */
    if (row != g_pv.active_row || strcmp(app->cwd, g_pv.active_cwd) != 0) {
        LIZ_PREVIEW_DBG("selection moved, closing preview");
        g_pv.enabled = false;
        liz_preview_kill();
        return;
    }

    const liz_fs_entry* e = NULL;
    if (row >= 0 && (size_t)row < app->entry_count)
        e = &app->entries[row];

    int x, y, w, h;
    bool has_geom = liz_preview_geometry(app, &x, &y, &w, &h);
    const liz_preview_slave* sl = e ? liz_preview_find_slave(e) : NULL;

    char path[PATH_MAX];
    bool path_ok = e != NULL && liz_fs_join(path, sizeof(path), app->cwd, e->name) == 0;

    if (!e || !sl || !has_geom || !path_ok) {
        if (g_pv.pid > 0)
            liz_preview_kill();
        return;
    }

    /* already running for the armed file: position or embed the window */
    if (g_pv.pid > 0 && g_pv.slave_type == sl && strcmp(g_pv.path, path) == 0) {
        g_pv.x = x;
        g_pv.y = y;
        g_pv.w = w;
        g_pv.h = h;
        if (g_pv.slave != None) {
            liz_preview_quiet();
            /* if the WM still holds a client for the preview (it managed
             * the window before we could embed it) pull the window back
             * into ours and force the WM to drop the client, then
             * reposition */
            Window root_ret, parent_ret;
            Window* children = NULL;
            unsigned int n = 0;
            if (XQueryTree(g_pv.dpy, g_pv.slave, &root_ret, &parent_ret, &children, &n)) {
                if (children)
                    XFree(children);
                if (parent_ret != g_pv.main_win)
                    XReparentWindow(g_pv.dpy, g_pv.slave, g_pv.main_win, x, y);
            }
            if (liz_preview_in_client_list(g_pv.slave))
                liz_preview_kick_client(g_pv.slave);
            XMoveResizeWindow(g_pv.dpy, g_pv.slave, x, y, w, h);
            XMapWindow(g_pv.dpy, g_pv.slave);
            liz_preview_loud();
        } else {
            liz_preview_embed_pending(app);
        }
        return;
    }

    /* spawn the slave for the armed file, then poll briefly for its window */
    g_pv.x = x;
    g_pv.y = y;
    g_pv.w = w;
    g_pv.h = h;
    liz_preview_spawn(app, sl, path);
    if (g_pv.pid == 0)
        return;

    double deadline = liz_app_now() + LIZ_PREVIEW_POLL_TOTAL_MS / 1000.0;
    while (liz_app_now() < deadline) {
        if (waitpid(g_pv.pid, NULL, WNOHANG) == g_pv.pid) {
            LIZ_PREVIEW_DBG("slave pid %d exited during poll", g_pv.pid);
            g_pv.pid = 0;
            g_pv.enabled = false;
            return;
        }
        if (liz_preview_embed_pending(app))
            return;
        usleep(LIZ_PREVIEW_POLL_MS * 1000);
    }
    LIZ_PREVIEW_DBG("window not found yet, will retry on next sync");
}

void liz_preview_shutdown(liz_app* app)
{
    (void)app;
    if (g_pv.dpy != NULL) {
        liz_preview_kill();
        g_pv.dpy = NULL;
        g_pv.enabled = false;
        g_pv.active_row = -1;
    }
}
