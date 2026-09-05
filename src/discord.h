/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: discord.h                                                                           |
|   Purpose: Discord RPC                                                                      |
\*-------------------------------------------------------------------------------------------*/

#ifndef POLYWORLD_DISCORD_H
#define POLYWORLD_DISCORD_H

#include <stdbool.h>

void discord_init(const char* client_id);
void discord_update_presence(const char* details, const char* state,
    int current_players, int max_players,
    const char* join_secret, int game_id, bool enable_thumbnail);
void discord_update(void);
void discord_shutdown(void);
int discord_get_pending_join(void);

#endif
