/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: log.h                                                                               |
|   Purpose: logging                                                                          |
\*-------------------------------------------------------------------------------------------*/

#ifndef PW_LOG_H
#define PW_LOG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_pw_verbose;

void pw_log_set_verbose(int enabled);
int pw_log_parse_args(int argc, char** argv);

#define PW_LOG(...) do { if (g_pw_verbose) printf(__VA_ARGS__); } while (0)

#define PW_ERR(code, ...) do { (void)(code); fprintf(stderr, __VA_ARGS__); } while (0)

#define PW_WARN(...) do { fprintf(stderr, __VA_ARGS__); } while (0)

#define ERR_GENERIC 0
#define ERR_FILE 1
#define ERR_CONN 2
#define ERR_HTTP_CONN 3
#define ERR_HTTP_CON 3
#define ERR_SERVER_REJECT 4
#define ERR_AUDIO 5
#define ERR_SHADER 6

#ifdef __cplusplus
}
#endif

#endif
