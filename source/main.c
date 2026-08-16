#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app/app.h"
#include "dbus/portal.h"
#include "rendering/x11/xc.h"
#include "ui/chooser.h"

static void liz_usage(FILE* stream)
{
    fprintf(stream,
        "Usage: lizaveta [PATH]\n"
        "       lizaveta [--filechooser [options] [PATH]]\n"
        "       lizaveta --portal-service\n"
        "\n"
        "    (no options)          run as a file manager, optionally starting at\n"
        "                          PATH (a directory to open, or a file to open\n"
        "                          its folder with that file selected)\n"
        "\n"
        "    --filechooser         file-picker mode: confirm a choice with\n"
        "                          Enter (l/q in directory mode), Ctrl+Enter to\n"
        "                          commit explicitly regardless of what's\n"
        "                          focused, cancel with Escape, write the chosen\n"
        "                          absolute paths one per line to --out or\n"
        "                          stdout, then quit.\n"
        "      --out FILE          write the chosen paths to FILE\n"
        "      --multiple          allow selecting several files\n"
        "      --directory         pick a directory (not a file)\n"
        "      --save NAME         pick a save location; NAME is the suggested\n"
        "                          filename (editable in-app; also see --out)\n"
        "      --filter NAME:GLOB1;GLOB2\n"
        "                          only list files matching one of the globs\n"
        "                          under this label; repeat for more filter\n"
        "                          groups, cycle between them with Tab\n"
        "      --filter-index N    which --filter (0-based, in the order given)\n"
        "                          is active on start; default 0\n"
        "      PATH                start directory, or a file-selector:// URI\n"
        "\n"
        "    --portal-service      run as the D-Bus service backing both\n"
        "                          org.freedesktop.impl.portal.FileChooser (Open/\n"
        "                          Save dialogs) and org.freedesktop.FileManager1\n"
        "                          (\"Show in folder\") instead of a GUI. See\n"
        "                          install/install-portal.sh to register it.\n");
}

int main(int argc, char* argv[])
{
    liz_app app;

    bool chooser = false;
    bool portal_service = false;
    const char* out_path = NULL;
    const char* save_name = NULL;
    bool multiple = false;
    bool directory = false;
    const char* start = NULL;
    const char* filter_specs[LIZ_CHOOSER_FILTERS_MAX];
    int filter_count = 0;
    int filter_index = 0;

    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (strcmp(a, "--filechooser") == 0) {
            chooser = true;
        } else if (strcmp(a, "--portal-service") == 0) {
            portal_service = true;
        } else if (strcmp(a, "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(a, "--save") == 0 && i + 1 < argc) {
            save_name = argv[++i];
        } else if (strcmp(a, "--multiple") == 0) {
            multiple = true;
        } else if (strcmp(a, "--directory") == 0) {
            directory = true;
        } else if (strcmp(a, "--filter") == 0 && i + 1 < argc) {
            if (filter_count < LIZ_CHOOSER_FILTERS_MAX)
                filter_specs[filter_count++] = argv[++i];
            else
                i++;
        } else if (strcmp(a, "--filter-index") == 0 && i + 1 < argc) {
            filter_index = atoi(argv[++i]);
        } else if (strcmp(a, "--help") == 0) {
            liz_usage(stdout);
            return 0;
        } else if (a[0] == '-') {
            fprintf(stderr, "lizaveta: unknown option %s\n", a);
            liz_usage(stderr);
            return 1;
        } else {
            start = a;
        }
    }

    if (chooser && directory && save_name) {
        fprintf(stderr, "lizaveta: --directory and --save are mutually exclusive\n");
        return 1;
    }

    if (portal_service) {
        char exe[PATH_MAX];
        ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (n <= 0) {
            fprintf(stderr, "lizaveta: cannot resolve own executable path\n");
            return 1;
        }
        exe[n] = '\0';
        return liz_portal_service_run(exe);
    }

    if (liz_app_init(&app) != 0) {
        fprintf(stderr, "lizaveta: failed to initialize\n");
        return 1;
    }

    if (chooser) {
        liz_chooser* c = &app.chooser;
        c->mode = save_name ? LIZ_CHOOSER_SAVE
                            : (directory ? LIZ_CHOOSER_DIRECTORY : LIZ_CHOOSER_OPEN);
        c->multiple = multiple && c->mode == LIZ_CHOOSER_OPEN;
        if (out_path)
            snprintf(c->out_path, sizeof(c->out_path), "%s", out_path);
        if (save_name)
            snprintf(c->save_name, sizeof(c->save_name), "%s", save_name);
        for (int i = 0; i < filter_count; i++)
            liz_chooser_add_filter(&app, filter_specs[i]);
        if (c->filter_count > 0) {
            if (filter_index < 0 || filter_index >= c->filter_count)
                filter_index = 0;
            c->current_filter = filter_index;
        }
        if (start) {
            /* a file-selector:// URI carries the path after the scheme */
            if (strncmp(start, "file-selector://", 16) == 0)
                start += 16;
            snprintf(c->start_dir, sizeof(c->start_dir), "%s", start);
        }
        liz_chooser_start(&app);
    } else if (start) {
        liz_app_navigate_and_select(&app, start);
    }

    liz_app_render(&app);
    xc_run(app.win);
    liz_app_quit(&app);

    return chooser ? app.chooser.exit_code : 0;
}
