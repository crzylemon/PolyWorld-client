/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: social.h                                                                            |
|   Purpose: friend requests + player card                                                    |
\*-------------------------------------------------------------------------------------------*/

#ifndef SOCIAL_H
#define SOCIAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define SOCIAL_MAX_TOASTS 4
#define SOCIAL_NAME_MAX 32

typedef enum {
    SOCIAL_REL_UNKNOWN = -1,
    SOCIAL_REL_NONE = 0,
    SOCIAL_REL_FRIENDS = 1,
    SOCIAL_REL_OUTGOING = 2,
    SOCIAL_REL_INCOMING = 3
} SocialFriendRel;

typedef struct {
    bool active;
    uint32_t from_id;
    char from_name[SOCIAL_NAME_MAX];
    unsigned int headshot_tex;
    bool headshot_loaded;
    float age;
} SocialToast;

typedef struct {
    bool card_open;
    uint32_t target_pid;
    uint32_t target_uid;
    char target_name[SOCIAL_NAME_MAX];
    unsigned int headshot_tex;
    bool headshot_loaded;
    float card_x, card_y;
    bool send_busy;
    SocialFriendRel friend_rel;
    char status[96];

    SocialToast toasts[SOCIAL_MAX_TOASTS];
    int toast_count;

    unsigned int nineslice_tex;
    unsigned int quad_shader;
    int quad_u_projection;
    int quad_u_tex;
    int quad_u_alpha;
    int quad_u_tint;
    unsigned int quad_vao;
    unsigned int quad_vbo;
    float ui_scale;
} SocialUI;

void social_init(SocialUI* s);
void social_set_nineslice(SocialUI* s, unsigned int tex);
void social_set_shaders(SocialUI* s, unsigned int quad_prog, int u_proj, int u_tex,
                        int u_alpha, int u_tint, unsigned int vao, unsigned int vbo);
void social_update(SocialUI* s, float dt);
void social_render(SocialUI* s, int sw, int sh);

void social_open_card(SocialUI* s, const char* name, uint32_t pid, uint32_t account_id,
                      float screen_x, float screen_y);
void social_close_card(SocialUI* s);
void social_set_friend_rel(SocialUI* s, uint32_t account_id, SocialFriendRel rel);

void social_push_toast(SocialUI* s, uint32_t from_id, const char* from_name);

bool social_on_click(SocialUI* s, float x, float y, int sw, int sh,
                     int* out_action, uint32_t* out_uid, char* out_name, size_t name_cap);

void social_open_profile(uint32_t user_id, const char* username);

#endif
