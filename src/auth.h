/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: auth.h                                                                              |
|   Purpose: login / session (native)                                                         |
\*-------------------------------------------------------------------------------------------*/

#ifndef AUTH_H
#define AUTH_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "accessory.h"
#include "emote.h"
#include "prod_urls.h"

#define AUTH_API_URL PW_AUTH_API_URL
#define TICKET_API_URL PW_TICKET_API_URL

void pw_set_site_origin(const char* origin);
const char* pw_site_origin(void);
void pw_set_tcp_endpoint(const char* host, int port);
const char* pw_tcp_host(void);
int pw_tcp_port(void);
bool pw_site_is_production(void);
void pw_use_local_site(void);

static inline bool pw_error_is_client_outdated(const char* err) {
    return err && err[0] && strstr(err, "no longer supported") != NULL;
}

typedef struct {
    bool authenticated;
    bool needs_2fa;
    bool challenge_expired;
    char username[32];
    char token[65];
    char challenge[65];
    char error[128];
    int user_id;
} AuthResult;

typedef struct {
    bool valid;
    char ticket[33];
    char username[32];
    char avatar_color[8];
    char skin_color[8];
    char error[96];
    int equipped_shirt;
    int equipped_pants;
    int equipped_head;
    int equipped_package;
    int equipped_accessories[PW_MAX_EQUIPPED_ACCESSORIES];
    int equipped_accessory;
    int equipped_emotes[PW_MAX_EQUIPPED_EMOTES];
    int emote_anims[PW_MAX_EQUIPPED_EMOTES];
    char emote_names[PW_MAX_EQUIPPED_EMOTES][PW_EMOTE_NAME_LEN];
    int user_id;
} JoinTicket;

AuthResult auth_login(const char* username, const char* password);
AuthResult auth_login_2fa(const char* challenge, const char* code);

AuthResult auth_validate_token(const char* token);
void auth_save_session(const char* token);
void auth_clear_session(void);
bool auth_load_session(char* out_token, size_t out_size);
void auth_set_session_filename(const char* filename);
JoinTicket auth_get_join_ticket(const char* session_token, int game_id, bool guest, int server_id, bool shadowed);

bool auth_rate_game(const char* session_token, int game_id, int rating,
                    int* out_likes, int* out_dislikes, int* out_user_rating);

#define AVATAR_API_URL PW_AVATAR_API_URL

bool auth_avatar_get(const char* session_token, char* skin_out, size_t skin_sz,
                     int* shirt, int* pants, int* head, int* accessory,
                     int accessories_out[PW_MAX_EQUIPPED_ACCESSORIES]);
typedef struct {
    int id;
    char name[48];
    char type[16];
    char image_path[192];
} AuthAvatarItem;
bool auth_avatar_inventory(const char* session_token, void* items_out, int max_items, int* out_count);
bool auth_avatar_save(const char* session_token, const char* skin_color,
                      int shirt, int pants, int head,
                      const int accessories[PW_MAX_EQUIPPED_ACCESSORIES],
                      int* out_package);
bool auth_avatar_equip_emotes(const char* session_token,
                              const int emotes[PW_MAX_EQUIPPED_EMOTES]);

#endif
