/* xc.h - minimal X11/Xft primitive rendering library for lizaveta. */

#ifndef XC_H
#define XC_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <sys/select.h>
#include <sys/time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xrender.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} xc_color;

#define XC_RGB(r, g, b) ((xc_color) { (r), (g), (b), 255 })

typedef enum {
    XC_EVENT_NONE = 0,
    XC_EVENT_KEY,    /* ev.key, ev.state */
    XC_EVENT_BUTTON, /* ev.button, ev.x, ev.y, ev.state */
    XC_EVENT_BUTTON_RELEASE, /* ev.button, ev.x, ev.y, ev.state */
    XC_EVENT_MOTION, /* ev.x, ev.y, ev.state */
    XC_EVENT_EXPOSE,
    XC_EVENT_RESIZE, /* ev.width, ev.height */
    XC_EVENT_CLOSE,  /* window manager asked us to close */
} xc_event_type;

typedef struct {
    xc_event_type type;
    KeySym key;          /* XC_EVENT_KEY  */
    char chars[8];        /* XC_EVENT_KEY: composed text (XLookupString), NUL-terminated */
    int nchars;           /* XC_EVENT_KEY: bytes in chars, 0 if the key produced no text */
    unsigned int button; /* XC_EVENT_BUTTON: 1-3 buttons, 4/5 wheel */
    int x;
    int y;
    unsigned int width;
    unsigned int height;
    unsigned int state;  /* X modifier/button mask */
} xc_event;

typedef struct xwindow xwindow;

typedef struct xwindow {
    Display* display;
    Window window;
    GC gc;
    int screen;
    Visual* visual;
    Colormap colormap;
    int depth;
    int width;
    int height;

    bool running;
    xc_color bg;

    Pixmap buffer;   /* backing store, size == window */
    XftDraw* xftdraw;

    Atom wm_delete;

    /* CLIPBOARD selection ownership */
    Atom clip_sel;      /* CLIPBOARD */
    Atom clip_utf8;     /* UTF8_STRING */
    Atom clip_text;     /* TEXT */
    Atom clip_targets;  /* TARGETS */
    Atom clip_prop;     /* property used to transfer pasted data */
    char* clip_buf;     /* owned selection payload */
    int clip_len;
    bool clip_owns;     /* this window is the CLIPBOARD owner */
    int clip_req;       /* 0 none, 1 UTF8 request pending, 2 STRING fallback */
    void (*on_clipboard)(xwindow* w, const char* text, int len, void* userdata);

    /* XDND (drag source) atoms, interned once at window creation */
    Atom xdnd_aware;
    Atom xdnd_selection;
    Atom xdnd_enter;
    Atom xdnd_position;
    Atom xdnd_status;
    Atom xdnd_leave;
    Atom xdnd_drop;
    Atom xdnd_finished;
    Atom xdnd_action_copy;
    Atom xdnd_type_list;
    Atom xdnd_uri_list;   /* text/uri-list */
    Atom xdnd_plain_text; /* text/plain, offered as a fallback */
    Atom xdnd_prop;       /* our property for receiving a dropped uri-list */

    /* payload for the drag currently being served via SelectionRequest;
     * only valid while we are the XdndSelection owner (during xc_dnd_begin) */
    char* dnd_uri_payload;
    int dnd_uri_len;

    /* XDND drop target state (in-flight drag that targets this window) */
    Window dnd_source;        /* source window, None when idle */
    int dnd_version;          /* source's XDND protocol version */
    bool dnd_has_uri_list;    /* the source offers text/uri-list */
    bool dnd_over;            /* an accepted drag is currently over us */
    int dnd_last_x, dnd_last_y; /* last XdndPosition root coordinates */

    /* drop-target callbacks; userdata is the same pointer as `userdata`.
     * on_dnd_position sets *accept to decide whether the drop is allowed at
     * the given root coordinates. on_dnd_drop hands over the absolute paths
     * that were dropped. */
    void (*on_dnd_enter)(void* data, int x_root, int y_root);
    void (*on_dnd_position)(void* data, int x_root, int y_root, bool* accept);
    void (*on_dnd_leave)(void* data);
    void (*on_dnd_drop)(void* data, char* const* paths, int count,
                        int x_root, int y_root);

    /* custom "drag a file" cursor shown while an XDND drag is in flight */
    Cursor dnd_cursor;

    /* optional hook the drag loop calls each time the pointer moves so the
     * app can repaint its buffer before the drag badge is drawn on top
     * (this is what keeps the badge from leaving trails). userdata is the
     * same pointer as `userdata` below. */
    void (*dnd_repaint)(void* userdata);

    void* userdata; /* passed as the second argument of the events callback */
    void (*events)(xc_event, void*);
} xwindow;

typedef struct {
    XftFont* xft;
    XftColor color;
} xc_font;

static inline xwindow* xc_window_create(int x, int y, int width, int height, xc_color bg, const char* title);
static inline void xc_window_destroy(xwindow* w);
static inline void xc_set_managed(xwindow* w, bool managed);
static inline void xc_run(xwindow* w);

/* ---- clipboard API ----

 * Owns the CLIPBOARD selection with `text`/`len` as the payload (UTF-8).
 * The app then serves SelectionRequest events from the xc_run loop until it
 * loses ownership. */
static inline void xc_clipboard_set(xwindow* w, const char* text, int len);

/* Requests the CLIPBOARD selection content asynchronously; when it arrives,
 * w->on_clipboard is called with the text. Applications that only offer
 * XA_STRING are retried automatically. */
static inline void xc_clipboard_request(xwindow* w);

/* ---- drag-and-drop (XDND source) API ----
 *
 * Runs a small nested event loop that drives the whole drag: grabs the
 * pointer, tracks which window is under the cursor, negotiates with it via
 * the XDND client-message protocol, and -- if dropped on a window that
 * accepts it -- serves the file list as "text/uri-list" when the target
 * asks for the XdndSelection. Returns once the drag ends (dropped,
 * rejected, or cancelled with Escape). `paths` must be absolute paths;
 * `count` must be > 0. */
static inline void xc_dnd_begin(xwindow* w, char* const* paths, int count);

/* Creates the custom "file being dragged" cursor (used by xc_window_create). */
static inline Cursor xc_dnd_make_cursor(xwindow* w);

static inline void xc_clear(xwindow* w);
static inline void xc_rect(xwindow* w, int x, int y, int width, int height, xc_color c);
static inline void xc_line(xwindow* w, int x1, int y1, int x2, int y2, xc_color c);
static inline int xc_text(xwindow* w, int x, int y, const char* text, int len, xc_font* f);
static inline void xc_text_measure(xwindow* w, const char* text, int len, xc_font* f, int* out_w, int* out_h);
static inline void xc_flip(xwindow* w);

static inline xc_font* xc_font_load(xwindow* w, const char* family, double px, xc_color color);
static inline xc_font* xc_font_load_style(xwindow* w, const char* family, double px, const char* style, xc_color color);
static inline void xc_font_free(xwindow* w, xc_font* f);
static inline void xc_font_metrics(xc_font* f, int* ascent, int* descent);

static inline int xc_color_shift(unsigned long mask)
{
    int shift = 0;
    while (mask && !(mask & 1)) {
        mask >>= 1;
        shift++;
    }
    return shift;
}

static inline int xc_color_bits(unsigned long mask)
{
    int bits = 0;
    while (mask) {
        bits += (int)(mask & 1);
        mask >>= 1;
    }
    return bits;
}

/* Converts an 8-bit-per-channel color to a pixel value. Uses direct color
 * math on TrueColor visuals and falls back to colormap allocation elsewhere,
 * so we never need to keep XColor/colormap slots alive. */
static inline unsigned long xc_pixel(xwindow* w, xc_color c)
{
    if (w->visual->class == TrueColor) {
        unsigned long r = (((unsigned long)c.r << 8) | c.r) >> (16 - xc_color_bits(w->visual->red_mask));
        unsigned long g = (((unsigned long)c.g << 8) | c.g) >> (16 - xc_color_bits(w->visual->green_mask));
        unsigned long b = (((unsigned long)c.b << 8) | c.b) >> (16 - xc_color_bits(w->visual->blue_mask));
        return (r << xc_color_shift(w->visual->red_mask))
             | (g << xc_color_shift(w->visual->green_mask))
             | (b << xc_color_shift(w->visual->blue_mask));
    }
    XColor xc2;
    xc2.red = ((unsigned short)c.r << 8) | c.r;
    xc2.green = ((unsigned short)c.g << 8) | c.g;
    xc2.blue = ((unsigned short)c.b << 8) | c.b;
    xc2.flags = DoRed | DoGreen | DoBlue;
    if (XAllocColor(w->display, w->colormap, &xc2) != 0)
        return xc2.pixel;
    return BlackPixel(w->display, w->screen);
}

static inline void xc_resize_buffer(xwindow* w)
{
    if (w->width < 1 || w->height < 1)
        return;
    Pixmap next = XCreatePixmap(w->display, w->window, w->width, w->height, w->depth);
    XftDrawChange(w->xftdraw, next);
    if (w->buffer != None)
        XFreePixmap(w->display, w->buffer);
    w->buffer = next;
}

static inline xwindow* xc_window_create(int x, int y, int width, int height, xc_color bg, const char* title)
{
    xwindow* w = (xwindow*)calloc(1, sizeof(xwindow));
    if (!w)
        return NULL;

    w->display = XOpenDisplay(NULL);
    if (!w->display) {
        fprintf(stderr, "xc: cannot open X display\n");
        free(w);
        return NULL;
    }
    w->screen = DefaultScreen(w->display);
    w->visual = DefaultVisual(w->display, w->screen);
    w->depth = DefaultDepth(w->display, w->screen);
    w->colormap = DefaultColormap(w->display, w->screen);
    w->width = width;
    w->height = height;
    w->bg = bg;
    w->running = false;
    w->events = NULL;
    w->buffer = None;

    XSetWindowAttributes attr;
    memset(&attr, 0, sizeof(attr));
    attr.background_pixel = BlackPixel(w->display, w->screen);
    attr.border_pixel = BlackPixel(w->display, w->screen);
    attr.colormap = w->colormap;
    attr.event_mask = ButtonPressMask | ButtonReleaseMask | KeyPressMask
                    | PointerMotionMask | ExposureMask | StructureNotifyMask;

    w->window = XCreateWindow(w->display, DefaultRootWindow(w->display),
                              x, y, width, height, 1, w->depth, InputOutput,
                              w->visual, CWBackPixel | CWColormap | CWEventMask, &attr);

    w->gc = XCreateGC(w->display, w->window, 0, NULL);
    XSetLineAttributes(w->display, w->gc, 1, LineSolid, CapButt, JoinMiter);

    /* WM_DELETE_WINDOW so clicking the close button generates XC_EVENT_CLOSE */
    w->wm_delete = XInternAtom(w->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(w->display, w->window, &w->wm_delete, 1);

    /* CLIPBOARD selection atoms */
    w->clip_sel = XInternAtom(w->display, "CLIPBOARD", False);
    w->clip_utf8 = XInternAtom(w->display, "UTF8_STRING", False);
    w->clip_text = XInternAtom(w->display, "TEXT", False);
    w->clip_targets = XInternAtom(w->display, "TARGETS", False);
    w->clip_prop = XInternAtom(w->display, "LIZAVETA_CLIPBOARD", False);

    /* XDND atoms */
    w->xdnd_aware = XInternAtom(w->display, "XdndAware", False);
    w->xdnd_selection = XInternAtom(w->display, "XdndSelection", False);
    w->xdnd_enter = XInternAtom(w->display, "XdndEnter", False);
    w->xdnd_position = XInternAtom(w->display, "XdndPosition", False);
    w->xdnd_status = XInternAtom(w->display, "XdndStatus", False);
    w->xdnd_leave = XInternAtom(w->display, "XdndLeave", False);
    w->xdnd_drop = XInternAtom(w->display, "XdndDrop", False);
    w->xdnd_finished = XInternAtom(w->display, "XdndFinished", False);
    w->xdnd_action_copy = XInternAtom(w->display, "XdndActionCopy", False);
    w->xdnd_type_list = XInternAtom(w->display, "XdndTypeList", False);
    w->xdnd_uri_list = XInternAtom(w->display, "text/uri-list", False);
    w->xdnd_plain_text = XInternAtom(w->display, "text/plain", False);
    w->xdnd_prop = XInternAtom(w->display, "LIZAVETA_DND", False);

    /* advertise ourselves as an XDND drop target (protocol version 5) */
    unsigned long dnd_aware_ver = 5;
    XChangeProperty(w->display, w->window, w->xdnd_aware, XA_ATOM, 32,
                    PropModeReplace, (unsigned char*)&dnd_aware_ver, 1);
    w->dnd_source = None;
    w->dnd_uri_payload = NULL;
    w->dnd_uri_len = 0;

    XSizeHints size;
    memset(&size, 0, sizeof(size));
    size.flags = PPosition | PSize;
    size.x = x;
    size.y = y;
    size.width = width;
    size.height = height;
    XSetStandardProperties(w->display, w->window, "", "", None, 0, 0, &size);
    XStoreName(w->display, w->window, title);

    w->xftdraw = XftDrawCreate(w->display, w->window, w->visual, w->colormap);
    xc_resize_buffer(w);

    w->dnd_cursor = xc_dnd_make_cursor(w);

    return w;
}

static inline void xc_grab_focus(xwindow* w)
{
    Window focus;
    int revert;
    XGetInputFocus(w->display, &focus, &revert);
    if (focus == None || focus == PointerRoot || focus == w->window)
        XSetInputFocus(w->display, w->window, RevertToParent, CurrentTime);
}

static inline void xc_set_managed(xwindow* w, bool managed)
{
    XSetWindowAttributes attr;
    memset(&attr, 0, sizeof(attr));
    attr.override_redirect = !managed;
    XChangeWindowAttributes(w->display, w->window, CWOverrideRedirect, &attr);
    XMapWindow(w->display, w->window);
}

static inline void xc_clipboard_set(xwindow* w, const char* text, int len)
{
    free(w->clip_buf);
    w->clip_buf = NULL;
    if (len > 0) {
        w->clip_buf = (char*)malloc((size_t)len + 1);
        if (!w->clip_buf)
            return;
        memcpy(w->clip_buf, text, (size_t)len);
        w->clip_buf[len] = '\0';
    }
    w->clip_len = len;
    XSetSelectionOwner(w->display, w->clip_sel, w->window, CurrentTime);
    w->clip_owns = true;
    XFlush(w->display);
}

static inline void xc_clipboard_request(xwindow* w)
{
    /* the X server does not deliver a selection to its own owner, so a
     * copy-then-paste within this window is served directly */
    if (w->clip_owns && w->clip_buf && w->on_clipboard) {
        w->on_clipboard(w, w->clip_buf, w->clip_len, w->userdata);
        return;
    }
    w->clip_req = 1;
    XConvertSelection(w->display, w->clip_sel, w->clip_utf8, w->clip_prop,
                      w->window, CurrentTime);
    XFlush(w->display);
}

/* 16x16 "file being dragged" cursor: a small page with the arrow pointer
 * from its upper-left corner (the hotspot). Rows are MSB-first. */
static inline Cursor xc_dnd_make_cursor(xwindow* w)
{
    static const unsigned char shape[16][2] = {
        { 0x01, 0x00 },
        { 0x07, 0x00 },
        { 0xDF, 0xFF },
        { 0xFF, 0xA0 },
        { 0xFF, 0xC3 },
        { 0x7F, 0x80 },
        { 0x77, 0x80 },
        { 0xF3, 0x80 },
        { 0xE1, 0x81 },
        { 0xE0, 0x80 },
        { 0x40, 0x80 },
        { 0x40, 0x80 },
        { 0x40, 0x80 },
        { 0x40, 0x80 },
        { 0x40, 0x80 },
        { 0xC0, 0xFF },
    };

    static const unsigned char mask[16][2] = {
        { 0x01, 0x00 },
        { 0x07, 0x00 },
        { 0xDF, 0x1F },
        { 0xFF, 0x3F },
        { 0xFF, 0x7F },
        { 0xFF, 0xFF },
        { 0xF7, 0xFF },
        { 0xF3, 0xFF },
        { 0xE1, 0xFF },
        { 0xE0, 0xFF },
        { 0xC0, 0xFF },
        { 0xC0, 0xFF },
        { 0xC0, 0xFF },
        { 0xC0, 0xFF },
        { 0xC0, 0xFF },
        { 0xC0, 0xFF },
    };

    Pixmap src = XCreateBitmapFromData(w->display, w->window,
                                       (const char*)shape, 16, 16);
    Pixmap msk = XCreateBitmapFromData(w->display, w->window,
                                       (const char*)mask, 16, 16);
    if (src == None || msk == None) {
        if (src != None)
            XFreePixmap(w->display, src);
        if (msk != None)
            XFreePixmap(w->display, msk);
        return None;
    }

    XColor fg, bg;
    fg.red = fg.green = fg.blue = 0x0000;
    fg.flags = DoRed | DoGreen | DoBlue;
    bg.red = bg.green = bg.blue = 0xFFFF;
    bg.flags = DoRed | DoGreen | DoBlue;
    Cursor c = XCreatePixmapCursor(w->display, src, msk, &fg, &bg, 0, 0);
    XFreePixmap(w->display, src);
    XFreePixmap(w->display, msk);
    return c;
}

/* Draws the "you are dragging" badge next to the cursor and flips. Requires
 * w->dnd_repaint: the app buffer is repainted first so the badge never
 * leaves trails, then the badge is drawn on top. Only draws when the pointer
 * is actually over our own window. `f` is the badge font, cached for the
 * whole drag. */
static inline void xc_dnd_draw_badge(xwindow* w, int x_root, int y_root,
                                     char* const* paths, int count, xc_font* f)
{
    if (!w->dnd_repaint || !f || !paths || count <= 0)
        return;

    int lx, ly;
    Window child;
    if (!XTranslateCoordinates(w->display, DefaultRootWindow(w->display),
                               w->window, x_root, y_root, &lx, &ly, &child))
        return;
    if (child != None)
        return; /* another window covers us at this point */
    if (lx < 0 || ly < 0 || lx >= w->width || ly >= w->height)
        return;

    char label[512];
    int lablen;
    if (count == 1) {
        const char* base = strrchr(paths[0], '/');
        base = base ? base + 1 : paths[0];
        lablen = snprintf(label, sizeof(label), "%s", base);
    } else {
        lablen = snprintf(label, sizeof(label), "%d items", count);
    }
    if (lablen <= 0 || lablen >= (int)sizeof(label))
        return;

    w->dnd_repaint(w->userdata);

    int tw = 0;
    xc_text_measure(w, label, lablen, f, &tw, NULL);
    int asc = 0, desc = 0;
    xc_font_metrics(f, &asc, &desc);

    const int pad = 6;
    const int bh = 22;
    int bw = tw + pad * 2;
    int bx = lx + 14;
    int by = ly + 18;
    if (bx + bw > w->width)
        bx = lx - bw - 8;
    if (by + bh > w->height)
        by = ly - bh - 8;
    if (bx < 0)
        bx = 0;
    if (by < 0)
        by = 0;

    xc_rect(w, bx - 1, by - 1, bw + 2, bh + 2, XC_RGB(120, 160, 255));
    xc_rect(w, bx, by, bw, bh, XC_RGB(24, 28, 36));
    int baseline = by + (bh - (asc + desc)) / 2 + asc;
    xc_text(w, bx + pad, baseline, label, lablen, f);
    xc_flip(w);
}

/* Percent-encodes everything except unreserved URI characters, as required
 * for the path component of a file:// URI. */
static inline int xc_dnd_url_encode(const char* path, char* out, int outcap)
{
    static const char hex[] = "0123456789ABCDEF";
    int n = 0;
    for (const unsigned char* p = (const unsigned char*)path; *p; p++) {
        unsigned char c = *p;
        bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                 || (c >= '0' && c <= '9')
                 || c == '/' || c == '-' || c == '_' || c == '.' || c == '~';
        if (safe) {
            if (n + 1 >= outcap)
                break;
            out[n++] = (char)c;
        } else {
            if (n + 3 >= outcap)
                break;
            out[n++] = '%';
            out[n++] = hex[(c >> 4) & 0xF];
            out[n++] = hex[c & 0xF];
        }
    }
    out[n] = '\0';
    return n;
}

/* Builds a CRLF-separated text/uri-list payload ("file://<encoded path>"
 * per line, each followed by \r\n, per the XDND/freedesktop convention).
 * Caller owns the returned buffer (malloc'd). */
static inline char* xc_dnd_build_uri_list(char* const* paths, int count, int* out_len)
{
    size_t cap = 16;
    for (int i = 0; i < count; i++)
        cap += strlen(paths[i]) * 3 + 16;

    char* buf = (char*)malloc(cap);
    if (!buf) {
        *out_len = 0;
        return NULL;
    }

    size_t off = 0;
    char enc[4096];
    for (int i = 0; i < count; i++) {
        xc_dnd_url_encode(paths[i], enc, (int)sizeof(enc));
        int n = snprintf(buf + off, cap - off, "file://%s\r\n", enc);
        if (n < 0)
            break;
        off += (size_t)n;
        if (off >= cap)
            break;
    }
    *out_len = (int)off;
    return buf;
}

/* Translates root-window coordinates into this window's local coordinates.
 * Returns false when the point is not over the window (or translation
 * fails). */
static inline bool xc_translate(xwindow* w, int x_root, int y_root, int* x, int* y)
{
    Window child;
    return XTranslateCoordinates(w->display, DefaultRootWindow(w->display),
                                 w->window, x_root, y_root, x, y, &child) != 0;
}

static inline int xc_dnd_hex_digit(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* Parses a text/uri-list payload into absolute paths: lines separated by
 * \r\n (or \n), each either a file:// URI (percent-decoded) or a plain
 * path. Caller frees each entry and the array itself. */
static inline char** xc_dnd_parse_uri_list(const char* data, int len, int* out_count)
{
    char** paths = NULL;
    int n = 0, cap = 0;

    int i = 0;
    while (i < len) {
        int j = i;
        while (j < len && data[j] != '\r' && data[j] != '\n')
            j++;
        if (j > i) {
            const char* p = data + i;
            int linelen = j - i;
            if (linelen >= 7 && strncmp(p, "file://", 7) == 0) {
                p += 7;
                linelen -= 7;
            }

            char path[PATH_MAX];
            int k = 0;
            for (int m = 0; m < linelen && k < (int)sizeof(path) - 1; m++) {
                char c = p[m];
                if (c == '%' && m + 2 < linelen) {
                    int hi = xc_dnd_hex_digit((unsigned char)p[m + 1]);
                    int lo = xc_dnd_hex_digit((unsigned char)p[m + 2]);
                    if (hi >= 0 && lo >= 0) {
                        path[k++] = (char)((hi << 4) | lo);
                        m += 2;
                        continue;
                    }
                }
                path[k++] = c;
            }
            path[k] = '\0';

            if (path[0] != '\0') {
                if (n == cap) {
                    cap = cap ? cap * 2 : 8;
                    char** np = (char**)realloc(paths, sizeof(char*) * (size_t)cap);
                    if (!np) {
                        for (int q = 0; q < n; q++)
                            free(paths[q]);
                        free(paths);
                        *out_count = 0;
                        return NULL;
                    }
                    paths = np;
                }
                paths[n++] = strdup(path);
            }
        }
        while (j < len && (data[j] == '\r' || data[j] == '\n'))
            j++;
        i = j;
    }

    *out_count = n;
    return paths;
}

/* Finds the innermost window under the given root-relative point, then
 * walks up its ancestor chain looking for one that advertises XdndAware.
 * Returns None if the pointer isn't over any drop-aware window (including
 * over our own). */
static inline Window xc_dnd_find_target(xwindow* w, int x_root, int y_root)
{
    Display* dpy = w->display;
    Window root = DefaultRootWindow(dpy);

    Window cur = root;
    for (;;) {
        Window child = None;
        int cx, cy;
        if (!XTranslateCoordinates(dpy, root, cur, x_root, y_root, &cx, &cy, &child))
            break;
        if (child == None)
            break;
        cur = child;
    }

    Window node = cur;
    while (node != None) {
        if (node != w->window) {
            Atom type;
            int format;
            unsigned long nitems, after;
            unsigned char* data = NULL;
            int r = XGetWindowProperty(dpy, node, w->xdnd_aware, 0, 1, False,
                                       AnyPropertyType, &type, &format,
                                       &nitems, &after, &data);
            bool has = (r == Success && type != None);
            if (data)
                XFree(data);
            if (has)
                return node;
        }
        if (node == root)
            break;
        Window root_ret, parent_ret;
        Window* children = NULL;
        unsigned int nchildren = 0;
        if (!XQueryTree(dpy, node, &root_ret, &parent_ret, &children, &nchildren))
            break;
        if (children)
            XFree(children);
        node = parent_ret;
    }
    return None;
}

static inline void xc_dnd_send(xwindow* w, Window target, Atom message, long l1, long l2, long l3, long l4)
{
    XClientMessageEvent m;
    memset(&m, 0, sizeof(m));
    m.type = ClientMessage;
    m.window = target;
    m.message_type = message;
    m.format = 32;
    m.data.l[0] = (long)w->window;
    m.data.l[1] = l1;
    m.data.l[2] = l2;
    m.data.l[3] = l3;
    m.data.l[4] = l4;
    XSendEvent(w->display, target, False, NoEventMask, (XEvent*)&m);
}

/* Serves one SelectionRequest for the XDND payload, the same shape as the
 * CLIPBOARD SelectionRequest handling in xc_run. Used both while the drag
 * loop is still tracking the pointer and while it's waiting for
 * XdndFinished after the drop. */
static inline void xc_dnd_serve_selection(xwindow* w, XSelectionRequestEvent* r)
{
    bool ok = false;
    if (r->selection == w->xdnd_selection && w->dnd_uri_payload
        && (r->target == w->xdnd_uri_list || r->target == w->xdnd_plain_text)) {
        Atom prop = r->property != None ? r->property : r->target;
        XChangeProperty(w->display, r->requestor, prop, r->target, 8,
                        PropModeReplace, (unsigned char*)w->dnd_uri_payload,
                        w->dnd_uri_len);
        ok = true;
    }
    XSelectionEvent se;
    memset(&se, 0, sizeof(se));
    se.type = SelectionNotify;
    se.display = w->display;
    se.requestor = r->requestor;
    se.selection = r->selection;
    se.target = r->target;
    se.time = r->time;
    se.property = ok ? r->property : None;
    XSendEvent(w->display, r->requestor, False, 0, (XEvent*)&se);
}

static inline void xc_dnd_begin(xwindow* w, char* const* paths, int count)
{
    if (count <= 0)
        return;
    Display* dpy = w->display;

    int uri_len = 0;
    char* uri = xc_dnd_build_uri_list(paths, count, &uri_len);
    if (!uri)
        return;
    free(w->dnd_uri_payload);
    w->dnd_uri_payload = uri;
    w->dnd_uri_len = uri_len;

    XSetSelectionOwner(dpy, w->xdnd_selection, w->window, CurrentTime);
    if (XGetSelectionOwner(dpy, w->xdnd_selection) != w->window) {
        free(w->dnd_uri_payload);
        w->dnd_uri_payload = NULL;
        w->dnd_uri_len = 0;
        return;
    }

    if (XGrabPointer(dpy, DefaultRootWindow(dpy), False,
                     ButtonReleaseMask | PointerMotionMask,
                     GrabModeAsync, GrabModeAsync, None, w->dnd_cursor,
                     CurrentTime) != GrabSuccess) {
        XSetSelectionOwner(dpy, w->xdnd_selection, None, CurrentTime);
        free(w->dnd_uri_payload);
        w->dnd_uri_payload = NULL;
        w->dnd_uri_len = 0;
        return;
    }

    /* show the badge immediately, before the first motion event */
    xc_font* badge_font = xc_font_load(w, "monospace", 13.0, XC_RGB(255, 255, 255));
    {
        Window root_ret, child_ret;
        int rrx, rry, rwx, rwy;
        unsigned int mask_state;
        if (XQueryPointer(dpy, DefaultRootWindow(dpy), &root_ret, &child_ret,
                          &rrx, &rry, &rwx, &rwy, &mask_state))
            xc_dnd_draw_badge(w, rrx, rry, paths, count, badge_font);
    }

    Window target = None;
    bool will_accept = false;
    bool dropped = false;

    for (;;) {
        XEvent xe;
        XNextEvent(dpy, &xe);

        if (xe.type == MotionNotify) {
            /* coalesce a burst of queued motion into the latest position */
            while (XCheckTypedEvent(dpy, MotionNotify, &xe)) { }

            int xr = xe.xmotion.x_root;
            int yr = xe.xmotion.y_root;
            Window hit = xc_dnd_find_target(w, xr, yr);

            if (hit != target) {
                if (target != None)
                    xc_dnd_send(w, target, w->xdnd_leave, 0, 0, 0, 0);
                target = hit;
                will_accept = false;
                if (target != None) {
                    /* version 5 in the top byte of data.l[1]; no extra
                     * type-list window property since we only ever offer
                     * two types, both in the Enter message itself */
                    xc_dnd_send(w, target, w->xdnd_enter, (5L << 24),
                               (long)w->xdnd_uri_list, (long)w->xdnd_plain_text, 0);
                }
            }

            if (target != None) {
                xc_dnd_send(w, target, w->xdnd_position, 0,
                           (long)(((xr & 0xFFFF) << 16) | (yr & 0xFFFF)),
                           (long)CurrentTime, (long)w->xdnd_action_copy);
            }
            xc_dnd_draw_badge(w, xr, yr, paths, count, badge_font);
            continue;
        }

        if (xe.type == ClientMessage && (Atom)xe.xclient.message_type == w->xdnd_status) {
            will_accept = (xe.xclient.data.l[1] & 1) != 0;
            continue;
        }

        if (xe.type == SelectionRequest) {
            xc_dnd_serve_selection(w, &xe.xselectionrequest);
            continue;
        }

        if (xe.type == ButtonRelease) {
            if (target != None && will_accept) {
                xc_dnd_send(w, target, w->xdnd_drop, 0, (long)CurrentTime, 0, 0);
                dropped = true;
            } else if (target != None) {
                xc_dnd_send(w, target, w->xdnd_leave, 0, 0, 0, 0);
            }
            break;
        }

        if (xe.type == KeyPress) {
            KeySym ks = XLookupKeysym(&xe.xkey, 0);
            if (ks == XK_Escape) {
                if (target != None)
                    xc_dnd_send(w, target, w->xdnd_leave, 0, 0, 0, 0);
                break;
            }
            continue;
        }

        /* anything else (Expose, etc.) is ignored for the duration of the drag */
    }

    if (dropped) {
        /* wait briefly for the target to read the selection and confirm;
         * a non-compliant or slow target can't hang the app past this */
        int fd = ConnectionNumber(dpy);
        struct timespec deadline;
        clock_gettime(CLOCK_MONOTONIC, &deadline);
        deadline.tv_sec += 3;

        for (;;) {
            while (XPending(dpy) == 0) {
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                double remaining = (double)(deadline.tv_sec - now.tv_sec)
                                  + (double)(deadline.tv_nsec - now.tv_nsec) / 1e9;
                if (remaining <= 0)
                    goto dnd_wait_done;
                struct timeval tv;
                tv.tv_sec = (long)remaining;
                tv.tv_usec = (long)((remaining - (double)tv.tv_sec) * 1e6);
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(fd, &fds);
                if (select(fd + 1, &fds, NULL, NULL, &tv) <= 0)
                    goto dnd_wait_done;
            }

            XEvent xe;
            XNextEvent(dpy, &xe);

            if (xe.type == SelectionRequest) {
                xc_dnd_serve_selection(w, &xe.xselectionrequest);
                continue;
            }
            if (xe.type == ClientMessage && (Atom)xe.xclient.message_type == w->xdnd_finished)
                break;
            /* ignore anything else while waiting */
        }
    dnd_wait_done:;
    }

    if (badge_font)
        xc_font_free(w, badge_font);
    XUngrabPointer(dpy, CurrentTime);
    XSetSelectionOwner(dpy, w->xdnd_selection, None, CurrentTime);
    free(w->dnd_uri_payload);
    w->dnd_uri_payload = NULL;
    w->dnd_uri_len = 0;
    XFlush(dpy);
}

/* XdndEnter: remember the source and check whether it offers uri-lists. */
static inline void xc_dnd_handle_enter(xwindow* w, XClientMessageEvent* m)
{
    w->dnd_source = (Window)m->data.l[0];
    w->dnd_version = (int)((m->data.l[1] >> 24) & 0xFF);
    w->dnd_has_uri_list = false;

    if (m->data.l[1] & 1) {
        /* offered types live in the source's XdndTypeList property */
        Atom type;
        int fmt;
        unsigned long nitems, after;
        unsigned char* data = NULL;
        if (XGetWindowProperty(w->display, w->dnd_source, w->xdnd_type_list, 0, 16,
                               False, XA_ATOM, &type, &fmt, &nitems, &after, &data)
                == Success
            && data) {
            Atom* atoms = (Atom*)data;
            for (unsigned long i = 0; i < nitems; i++)
                if (atoms[i] == w->xdnd_uri_list)
                    w->dnd_has_uri_list = true;
            XFree(data);
        }
    } else {
        w->dnd_has_uri_list = ((Atom)m->data.l[2] == w->xdnd_uri_list)
                           || ((Atom)m->data.l[3] == w->xdnd_uri_list);
    }
}

/* XdndPosition: decide whether we accept the drop here, reply XdndStatus,
 * and let the app update its drop-target highlight. */
static inline void xc_dnd_handle_position(xwindow* w, XClientMessageEvent* m)
{
    int xr = (int)((m->data.l[2] >> 16) & 0xFFFF);
    int yr = (int)(m->data.l[2] & 0xFFFF);
    w->dnd_last_x = xr;
    w->dnd_last_y = yr;

    bool accept = w->dnd_has_uri_list && w->dnd_source != None;
    if (w->on_dnd_position)
        w->on_dnd_position(w->userdata, xr, yr, &accept);
    w->dnd_over = accept;

    if (w->dnd_source != None) {
        /* bit 0: accept drop; bit 1: want an action list; no action rect;
         * offered action: copy */
        xc_dnd_send(w, w->dnd_source, w->xdnd_status,
                    (long)(accept ? 3 : 0), 0, (long)w->xdnd_action_copy, 0);
    }
}

static inline void xc_dnd_handle_leave(xwindow* w, XClientMessageEvent* m)
{
    (void)m;
    w->dnd_source = None;
    w->dnd_over = false;
    if (w->on_dnd_leave)
        w->on_dnd_leave(w->userdata);
}

/* XdndDrop: fetch the dropped uri-list via the XdndSelection; the actual
 * parsing and app handoff happen in the SelectionNotify handler. */
static inline void xc_dnd_handle_drop(xwindow* w, XClientMessageEvent* m)
{
    if (w->dnd_source == None)
        return;
    if (!w->dnd_has_uri_list) {
        xc_dnd_send(w, w->dnd_source, w->xdnd_finished, 0, 0, 0, 0);
        w->dnd_source = None;
        w->dnd_over = false;
        if (w->on_dnd_leave)
            w->on_dnd_leave(w->userdata);
        return;
    }
    XConvertSelection(w->display, w->xdnd_selection, w->xdnd_uri_list,
                      w->xdnd_prop, w->window, (Time)m->data.l[2]);
}

static inline void xc_run(xwindow* w)
{
    XMapRaised(w->display, w->window);
    XFlush(w->display);
    xc_grab_focus(w);

    w->running = true;
    XEvent xe;

    while (w->running) {
        XNextEvent(w->display, &xe);

        xc_event ev;
        memset(&ev, 0, sizeof(ev));

        switch (xe.type) {
        case KeyPress: {
            ev.type = XC_EVENT_KEY;
            char buf[8];
            int n = XLookupString(&xe.xkey, buf, (int)sizeof(buf) - 1, &ev.key, NULL);
            if (n < 0)
                n = 0;
            if (n > (int)sizeof(ev.chars) - 1)
                n = (int)sizeof(ev.chars) - 1;
            memcpy(ev.chars, buf, (size_t)n);
            ev.chars[n] = '\0';
            ev.nchars = n;
            ev.x = xe.xkey.x;
            ev.y = xe.xkey.y;
            ev.state = xe.xkey.state;
            break;
        }
        case ButtonPress:
            ev.type = XC_EVENT_BUTTON;
            ev.button = xe.xbutton.button;
            ev.x = xe.xbutton.x;
            ev.y = xe.xbutton.y;
            ev.state = xe.xbutton.state;
            break;
        case ButtonRelease:
            ev.type = XC_EVENT_BUTTON_RELEASE;
            ev.button = xe.xbutton.button;
            ev.x = xe.xbutton.x;
            ev.y = xe.xbutton.y;
            ev.state = xe.xbutton.state;
            break;
        case MotionNotify:
            ev.type = XC_EVENT_MOTION;
            ev.x = xe.xmotion.x;
            ev.y = xe.xmotion.y;
            ev.state = xe.xmotion.state;
            break;
        case Expose:
            if (xe.xexpose.count == 0)
                ev.type = XC_EVENT_EXPOSE;
            break;
        case ConfigureNotify:
            if (xe.xconfigure.width != w->width || xe.xconfigure.height != w->height) {
                w->width = xe.xconfigure.width;
                w->height = xe.xconfigure.height;
                xc_resize_buffer(w);
            }
            ev.type = XC_EVENT_RESIZE;
            ev.width = (unsigned int)w->width;
            ev.height = (unsigned int)w->height;
            break;
        case ClientMessage: {
            Atom mt = (Atom)xe.xclient.message_type;
            if (mt == w->wm_delete) {
                ev.type = XC_EVENT_CLOSE;
            } else if (mt == w->xdnd_enter) {
                xc_dnd_handle_enter(w, &xe.xclient);
            } else if (mt == w->xdnd_position) {
                xc_dnd_handle_position(w, &xe.xclient);
            } else if (mt == w->xdnd_leave) {
                xc_dnd_handle_leave(w, &xe.xclient);
            } else if (mt == w->xdnd_drop) {
                xc_dnd_handle_drop(w, &xe.xclient);
            }
            break;
        }

        /* ---- CLIPBOARD selection events (always delivered, no event
         * mask needed) ---- */

        case SelectionRequest: {
            XSelectionRequestEvent* r = &xe.xselectionrequest;
            bool ok = false;
            if (r->selection == w->clip_sel && w->clip_owns && w->clip_buf) {
                if (r->target == w->clip_targets) {
                    Atom targets[4];
                    int tn = 0;
                    targets[tn++] = w->clip_utf8;
                    targets[tn++] = XA_STRING;
                    targets[tn++] = w->clip_text;
                    targets[tn++] = w->clip_targets;
                    XChangeProperty(w->display, r->requestor, r->property, XA_ATOM,
                                    32, PropModeReplace, (unsigned char*)targets, tn);
                    ok = true;
                } else if (r->target == w->clip_utf8 || r->target == XA_STRING
                           || r->target == w->clip_text) {
                    Atom prop = r->property != None ? r->property : r->target;
                    XChangeProperty(w->display, r->requestor, prop, r->target, 8,
                                    PropModeReplace, (unsigned char*)w->clip_buf,
                                    w->clip_len);
                    ok = true;
                }
            }
            XSelectionEvent se;
            memset(&se, 0, sizeof(se));
            se.type = SelectionNotify;
            se.display = w->display;
            se.requestor = r->requestor;
            se.selection = r->selection;
            se.target = r->target;
            se.time = r->time;
            se.property = ok ? r->property : None;
            XSendEvent(w->display, r->requestor, False, 0, (XEvent*)&se);
            XFlush(w->display);
            continue;
        }
        case SelectionClear:
            w->clip_owns = false;
            free(w->clip_buf);
            w->clip_buf = NULL;
            w->clip_len = 0;
            continue;
        case SelectionNotify: {
            XSelectionEvent* s = &xe.xselection;
            if (s->selection == w->clip_sel && s->property != None) {
                w->clip_req = 0;
                Atom type;
                int fmt;
                unsigned long nitems, after;
                unsigned char* data = NULL;
                if (XGetWindowProperty(w->display, w->window, s->property, 0,
                                       (long)(1 << 20), True, AnyPropertyType,
                                       &type, &fmt, &nitems, &after, &data) == Success
                    && data) {
                    int bytes = (int)(nitems * (fmt == 32 ? 4 : fmt == 16 ? 2 : 1));
                    if (w->on_clipboard)
                        w->on_clipboard(w, (const char*)data, bytes, w->userdata);
                    XFree(data);
                } else if (data) {
                    XFree(data);
                }
            } else if (s->selection == w->clip_sel && w->clip_req == 1) {
                /* owner refused UTF8_STRING: retry with XA_STRING */
                w->clip_req = 2;
                XConvertSelection(w->display, w->clip_sel, XA_STRING, w->clip_prop,
                                  w->window, CurrentTime);
            } else if (s->selection == w->xdnd_selection && s->property != None) {
                /* a dropped uri-list arrived: parse it, hand it to the app,
                 * and tell the source we're done */
                Atom type;
                int fmt;
                unsigned long nitems, after;
                unsigned char* data = NULL;
                if (XGetWindowProperty(w->display, w->window, s->property, 0,
                                       (long)(1 << 20), True, AnyPropertyType,
                                       &type, &fmt, &nitems, &after, &data)
                        == Success
                    && data) {
                    int bytes = (int)(nitems * (fmt == 32 ? 4 : fmt == 16 ? 2 : 1));
                    int n = 0;
                    char** paths = xc_dnd_parse_uri_list((const char*)data, bytes, &n);
                    if (n > 0 && w->on_dnd_drop)
                        w->on_dnd_drop(w->userdata, paths, n,
                                       w->dnd_last_x, w->dnd_last_y);
                    for (int i = 0; i < n; i++)
                        free(paths[i]);
                    free(paths);
                    XFree(data);
                }
                if (w->dnd_source != None) {
                    xc_dnd_send(w, w->dnd_source, w->xdnd_finished, 0,
                                (long)w->xdnd_action_copy, 0, 0);
                    w->dnd_source = None;
                }
                w->dnd_over = false;
                if (w->on_dnd_leave)
                    w->on_dnd_leave(w->userdata);
            }
            continue;
        }

        default:
            continue;
        }

        if (ev.type != XC_EVENT_NONE && w->events != NULL)
            w->events(ev, w->userdata);
    }
}

static inline void xc_window_destroy(xwindow* w)
{
    if (!w)
        return;
    if (w->xftdraw)
        XftDrawDestroy(w->xftdraw);
    if (w->buffer != None)
        XFreePixmap(w->display, w->buffer);
    if (w->gc)
        XFreeGC(w->display, w->gc);
    if (w->clip_buf)
        free(w->clip_buf);
    if (w->dnd_cursor != None)
        XFreeCursor(w->display, w->dnd_cursor);
    if (w->window != None)
        XDestroyWindow(w->display, w->window);
    if (w->display)
        XCloseDisplay(w->display);
    free(w);
}

static inline void xc_clear(xwindow* w)
{
    XSetForeground(w->display, w->gc, xc_pixel(w, w->bg));
    XFillRectangle(w->display, w->buffer, w->gc, 0, 0, w->width, w->height);
}

static inline void xc_rect(xwindow* w, int x, int y, int width, int height, xc_color c)
{
    XSetForeground(w->display, w->gc, xc_pixel(w, c));
    XFillRectangle(w->display, w->buffer, w->gc, x, y, width, height);
}

static inline void xc_line(xwindow* w, int x1, int y1, int x2, int y2, xc_color c)
{
    XSetForeground(w->display, w->gc, xc_pixel(w, c));
    XDrawLine(w->display, w->buffer, w->gc, x1, y1, x2, y2);
}

static inline void xc_text_measure(xwindow* w, const char* text, int len, xc_font* f, int* out_w, int* out_h)
{
    XGlyphInfo ext;
    XftTextExtentsUtf8(w->display, f->xft, (const FcChar8*)text, len, &ext);
    if (out_w)
        *out_w = ext.xOff;
    if (out_h)
        *out_h = f->xft->ascent + f->xft->descent;
}

static inline int xc_text(xwindow* w, int x, int y, const char* text, int len, xc_font* f)
{
    XftDrawStringUtf8(w->xftdraw, &f->color, f->xft, x, y, (const FcChar8*)text, len);
    XGlyphInfo ext;
    XftTextExtentsUtf8(w->display, f->xft, (const FcChar8*)text, len, &ext);
    return ext.xOff;
}

static inline void xc_flip(xwindow* w)
{
    XCopyArea(w->display, w->buffer, w->window, w->gc, 0, 0, w->width, w->height, 0, 0);
    XFlush(w->display);
}

/* Font sizes are given in pixels, not points: `pixelsize` makes rendering
 * independent of the display's DPI, which X11 servers often report wrongly
 * (Xft's default point-size handling can otherwise produce enormous glyphs
 * on HiDPI or fractionally scaled setups). */
static inline xc_font* xc_font_load(xwindow* w, const char* family, double px, xc_color color)
{
    return xc_font_load_style(w, family, px, NULL, color);
}

static inline xc_font* xc_font_load_style(xwindow* w, const char* family, double px, const char* style, xc_color color)
{
    char pattern[256];
    if (style && style[0])
        snprintf(pattern, sizeof(pattern), "%s:pixelsize=%g:%s", family, px, style);
    else
        snprintf(pattern, sizeof(pattern), "%s:pixelsize=%g", family, px);

    XftFont* xft = XftFontOpenName(w->display, w->screen, pattern);
    if (!xft) {
        fprintf(stderr, "xc: cannot load font '%s'\n", pattern);
        return NULL;
    }

    xc_font* f = (xc_font*)calloc(1, sizeof(xc_font));
    if (!f) {
        XftFontClose(w->display, xft);
        return NULL;
    }
    f->xft = xft;

    XRenderColor rc;
    rc.red = ((unsigned short)color.r << 8) | color.r;
    rc.green = ((unsigned short)color.g << 8) | color.g;
    rc.blue = ((unsigned short)color.b << 8) | color.b;
    rc.alpha = ((unsigned short)color.a << 8) | color.a;
    XftColorAllocValue(w->display, w->visual, w->colormap, &rc, &f->color);
    return f;
}

static inline void xc_font_free(xwindow* w, xc_font* f)
{
    if (!f)
        return;
    XftColorFree(w->display, w->visual, w->colormap, &f->color);
    XftFontClose(w->display, f->xft);
    free(f);
}

static inline void xc_font_metrics(xc_font* f, int* ascent, int* descent)
{
    if (ascent)
        *ascent = f->xft->ascent;
    if (descent)
        *descent = f->xft->descent;
}

#ifdef __cplusplus
}
#endif

#endif /* XC_H */
