/* ini.c - the desktop-file / index.theme flavour of INI. */

#include "fs/ini.h"

#include <string.h>

bool liz_ini_get(const char* text, const char* section, const char* key,
                 char* out, size_t outsz)
{
    if (!text || !key || outsz == 0)
        return false;

    const char* p = text;
    bool in_section = (section == NULL);
    size_t keylen = strlen(key);

    while (*p) {
        const char* eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);

        if (len > 0 && p[0] == '[') {
            if (section) {
                size_t seclen = strlen(section);
                in_section = (len == seclen + 2 && p[len - 1] == ']'
                              && strncmp(p + 1, section, seclen) == 0);
            }
        } else if (in_section && len > keylen && strncmp(p, key, keylen) == 0
                   && p[keylen] == '=') {
            size_t vlen = len - keylen - 1;
            if (vlen >= outsz)
                vlen = outsz - 1;
            memcpy(out, p + keylen + 1, vlen);
            out[vlen] = '\0';
            return true;
        }

        if (!eol)
            break;
        p = eol + 1;
    }
    return false;
}
