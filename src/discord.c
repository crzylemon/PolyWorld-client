/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: discord.c                                                                           |
|   Purpose: Discord RPC                                                                      |
\*-------------------------------------------------------------------------------------------*/

#ifndef __EMSCRIPTEN__
#include "discord.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef _WIN32
#include "discord_rpc.h"
#endif

static int64_t start_time = 0;
static int g_pending_join_game_id = 0;

#ifndef _WIN32
static void handleDiscordJoin(const char* secret) {
    g_pending_join_game_id = atoi(secret);
}

static void handleDiscordJoinRequest(const DiscordUser* request) {
    Discord_Respond(request->userId, DISCORD_REPLY_YES);
}
#endif

void discord_init(const char* client_id) {
#ifndef _WIN32
    DiscordEventHandlers handlers;
    memset(&handlers, 0, sizeof(handlers));

    handlers.joinGame = handleDiscordJoin;
    handlers.joinRequest = handleDiscordJoinRequest;

    Discord_Initialize(client_id, &handlers, 1, NULL);
#else
    (void)client_id;
#endif

    start_time = (int64_t)time(NULL);
}

void discord_update_presence(const char* details, const char* state,
                        int current_players, int max_players,
                        const char* join_secret, int game_id, bool enable_thumbnail) {

    #ifndef _WIN32
    DiscordRichPresence presence;
    memset(&presence, 0, sizeof(presence));

    presence.details = details;
    presence.state = state;
    presence.startTimestamp = start_time;

    presence.largeImageKey = "logo";
    presence.largeImageText = "PolyWorld!";

    char small_image_url[256];
    if (enable_thumbnail) {
        snprintf(small_image_url, sizeof(small_image_url),
        "https://polyworld.games/uploads/epic.php?id=%d", game_id);

        presence.smallImageKey = small_image_url;
        presence.smallImageText = state;
    }

    if (max_players > 0) {
    presence.partyId = "polyworld";
    presence.partySize = current_players;
    presence.partyMax = max_players;
    }

    if (join_secret != NULL && max_players > 0) {
    presence.joinSecret = join_secret;
    }

    Discord_UpdatePresence(&presence);
    #else
    (void)details;
    (void)state;
    (void)current_players;
    (void)max_players;
    (void)join_secret;
    (void)game_id;
    #endif
}

void discord_update(void) {
#ifndef _WIN32
    Discord_RunCallbacks();
#endif
}

void discord_shutdown(void) {
#ifndef _WIN32
    Discord_Shutdown();
#endif
}

int discord_get_pending_join(void) {
    int id = g_pending_join_game_id;
    g_pending_join_game_id = 0;
    return id;
}
#endif