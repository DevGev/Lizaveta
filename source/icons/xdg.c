/* xdg.c - XDG base directory lookup. */

#include "icons/xdg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int liz_xdg_push(char out[][LIZ_XDG_PATH_MAX], int max, int n, const char* path)
{
    if (n >= max || !path || path[0] != '/')
        return n;
    for (int i = 0; i < n; i++) {
        if (strcmp(out[i], path) == 0)
            return n;
    }
    snprintf(out[n], LIZ_XDG_PATH_MAX, "%s", path);
    return n + 1;
}

int liz_xdg_data_dirs(char out[][LIZ_XDG_PATH_MAX], int max)
{
    int n = 0;
    char buf[LIZ_XDG_PATH_MAX];

    const char* data_home = getenv("XDG_DATA_HOME");
    if (data_home && data_home[0]) {
        n = liz_xdg_push(out, max, n, data_home);
    } else {
        const char* home = getenv("HOME");
        if (home && home[0]) {
            snprintf(buf, sizeof(buf), "%s/.local/share", home);
            n = liz_xdg_push(out, max, n, buf);
        }
    }

    const char* dirs = getenv("XDG_DATA_DIRS");
    if (!dirs || !dirs[0])
        dirs = "/usr/local/share:/usr/share";

    while (*dirs && n < max) {
        const char* sep = strchr(dirs, ':');
        size_t len = sep ? (size_t)(sep - dirs) : strlen(dirs);
        if (len > 0 && len < sizeof(buf)) {
            memcpy(buf, dirs, len);
            buf[len] = '\0';
            n = liz_xdg_push(out, max, n, buf);
        }
        if (!sep)
            break;
        dirs = sep + 1;
    }
    return n;
}

int liz_xdg_config_dirs(char out[][LIZ_XDG_PATH_MAX], int max)
{
    int n = 0;
    char buf[LIZ_XDG_PATH_MAX];

    const char* config_home = getenv("XDG_CONFIG_HOME");
    if (config_home && config_home[0]) {
        n = liz_xdg_push(out, max, n, config_home);
    } else {
        const char* home = getenv("HOME");
        if (home && home[0]) {
            snprintf(buf, sizeof(buf), "%s/.config", home);
            n = liz_xdg_push(out, max, n, buf);
        }
    }

    const char* dirs = getenv("XDG_CONFIG_DIRS");
    if (!dirs || !dirs[0])
        dirs = "/etc/xdg";

    while (*dirs && n < max) {
        const char* sep = strchr(dirs, ':');
        size_t len = sep ? (size_t)(sep - dirs) : strlen(dirs);
        if (len > 0 && len < sizeof(buf)) {
            memcpy(buf, dirs, len);
            buf[len] = '\0';
            n = liz_xdg_push(out, max, n, buf);
        }
        if (!sep)
            break;
        dirs = sep + 1;
    }
    return n;
}
