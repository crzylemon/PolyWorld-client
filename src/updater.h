/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: updater.h                                                                           |
|   Purpose: auto-update (native + studio)                                                    |
\*-------------------------------------------------------------------------------------------*/

#ifndef UPDATER_H
#define UPDATER_H

#include <stdbool.h>

void updater_check(void);
void updater_set_force(bool force);

void updater_set_studio_channel(bool studio);

const char* updater_get_version(void);
bool updater_server_unreachable(void);

#endif
