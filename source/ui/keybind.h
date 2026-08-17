#ifndef LIZ_KEYBIND_H
#define LIZ_KEYBIND_H

#include <stdbool.h>

enum liz_action {
    LIZ_ACTION_NONE = -1,
    LIZ_ACTION_QUIT,
    LIZ_ACTION_NAV_EDIT,
    LIZ_ACTION_TOGGLE_HIDDEN,
    LIZ_ACTION_TOGGLE_SIDEBAR,
    LIZ_ACTION_HALF_DOWN,
    LIZ_ACTION_HALF_UP,
    LIZ_ACTION_HISTORY_BACK,
    LIZ_ACTION_HISTORY_FORWARD,
    LIZ_ACTION_COPY,
    LIZ_ACTION_CUT,
    LIZ_ACTION_PASTE,
    LIZ_ACTION_PREVIEW,
    LIZ_ACTION_NEW_FOLDER,
    LIZ_ACTION_OPEN_TERMINAL,
    LIZ_ACTION_OPEN_TERMINAL_DIR,
    LIZ_ACTION_RENAME,
    LIZ_ACTION_GO_HOME,
    LIZ_ACTION_CLOSE_PREVIEW,
};

#define LIZ_MOD_SHIFT 1   /* X11: ShiftMask   = 1 << 0 */
#define LIZ_MOD_CTRL  4   /* X11: ControlMask  = 1 << 2 */
#define LIZ_MOD_ALT   8   /* X11: Mod1Mask     = 1 << 3 */
#define LIZ_MOD_SUPER 64  /* X11: Mod4Mask     = 1 << 6 */

#define LIZ_MOD_MASK (LIZ_MOD_SHIFT | LIZ_MOD_CTRL | LIZ_MOD_ALT | LIZ_MOD_SUPER)

#define LIZ_BIND(action, key, mod) { key, mod, LIZ_ACTION_##action }

struct liz_keybind {
    int key;
    int mods;
    enum liz_action action;
};

static inline bool liz_is_alphabetic_keysym(int key)
{
    return (key >= 0x41 && key <= 0x5a) || (key >= 0x61 && key <= 0x7a);
}

static inline enum liz_action
liz_keybind_resolve(int ev_key, int ev_state, const struct liz_keybind* binds,
                    int count)
{
    /* For alphabetic keysyms the case is already encoded in the keysym
     * itself (XK_h vs XK_H), so ShiftMask is redundant — strip it so
     * bindings with mods=0 match uppercase letters produced with Shift. */
    int state = ev_state;
    if (liz_is_alphabetic_keysym(ev_key))
        state &= ~LIZ_MOD_SHIFT;

    for (int i = 0; i < count; i++) {
        if (binds[i].key == ev_key
            && (state & LIZ_MOD_MASK) == (binds[i].mods & LIZ_MOD_MASK))
            return binds[i].action;
    }
    return LIZ_ACTION_NONE;
}

#endif
