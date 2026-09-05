/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: android_stubs.c                                                                     |
|   Purpose: Discord/updater stubs so Android actually builds properly                        |
\*-------------------------------------------------------------------------------------------*/

#ifdef __ANDROID__

#include "discord.h"
#include "updater.h"

#include <stdbool.h>

void discord_init(const char* client_id) { (void)client_id; }
void discord_update_presence(const char* details, const char* state,
                             int current_players, int max_players,
                             const char* join_secret, int game_id, bool enable_thumbnail) {
    (void)details; (void)state; (void)current_players; (void)max_players;
    (void)join_secret; (void)game_id; (void)enable_thumbnail;
}
void discord_update(void) {}
void discord_shutdown(void) {}
int discord_get_pending_join(void) { return 0; }

#ifndef PW_MOBILE_STORE
void updater_check(void) {}
void updater_set_force(bool force) { (void)force; }
const char* updater_get_version(void) { return "android"; }
bool updater_server_unreachable(void) { return false; }
#endif

#endif
