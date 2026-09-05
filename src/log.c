/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: log.c                                                                               |
|   Purpose: logging                                                                          |
\*-------------------------------------------------------------------------------------------*/

#include "log.h"
#include <string.h>

int g_pw_verbose = 0;

void pw_log_set_verbose(int enabled) {
    g_pw_verbose = enabled ? 1 : 0;
}

int pw_log_parse_args(int argc, char** argv) {
    int found = 0;
    for (int i = 1; i < argc; i++) {
        if (!argv[i]) continue;
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_pw_verbose = 1;
            found = 1;
        }
    }
    return found;
}
