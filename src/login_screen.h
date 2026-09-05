/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: login_screen.h                                                                      |
|   Purpose: (originally for login) Almost all pre-ingame UI                                  |
\*-------------------------------------------------------------------------------------------*/

#ifndef LOGIN_SCREEN_H
#define LOGIN_SCREEN_H

#include <stdbool.h>
#include "accessory.h"
#include "emote.h"

#define LOGIN_MAX_GAMES 100

typedef struct {
    char username[64];
    char password[64];
    int username_len;
    int password_len;
    int active_field;
    bool submitted;
    bool play_as_guest; // PAG clicked?
    bool logged_in;
    bool awaiting_2fa;
    char totp_challenge[65];
    char error[128];
    char session_token[128];
    int game_id;
    // Login result
    char ticket[64];
    char ticket_username[32];
    char skin_color[8];
    int equipped_shirt;
    int equipped_pants;
    int equipped_head;
    int equipped_package;
    int equipped_accessories[PW_MAX_EQUIPPED_ACCESSORIES];
    int equipped_accessory; // mirror of equipped_accessories[0]
    int equipped_emotes[PW_MAX_EQUIPPED_EMOTES];
    int emote_anims[PW_MAX_EQUIPPED_EMOTES];
    char emote_names[PW_MAX_EQUIPPED_EMOTES][PW_EMOTE_NAME_LEN];
    int user_id; // 0 = guest
    // 0=login, 1=games list, 2=game detail
    int phase;
    // Games
    struct {
        int id;
        char title[64];
        char creator[32];
        char thumbnail[256]; // Thumbnail URL
        char description[512];
        char local_path[128]; // non empty means this is offline
        int likes;
        int dislikes;
        int plays;
        int playing_now;
        int user_rating; // -1 = disliked, 0 = not rated, 1 = liked
        char created_at[32];
        unsigned int thumb_tex; // GL tex
        int thumb_w, thumb_h; // Source size for clip
        bool thumb_loaded;
        bool thumb_loading;
    } games[LOGIN_MAX_GAMES];
    int game_count;
    int selected_game; // index
    float games_scroll_y; // VERTICAL scroll
    float games_scroll_target; // scroll dest
    float sel_draw_x, sel_draw_y; // selector pos/size
    float sel_draw_w, sel_draw_h;
    bool sel_draw_valid;
    bool games_fetched;
    // 0=Recommended, 1=Top Playing, 2=Popular, 3=Newest,
    // 4=My Games (logged-in) / Offline (guest), 5=Offline (logged-in)
    int games_tab;
    bool ready_to_play; // Play confirmed, try
    bool detail_fetched;
    bool want_avatar_editor;
    bool want_catalog_ui;
    bool logout_confirm;
    bool offline_play; 
    bool games_loading;
    bool detail_loading;
    float skeleton_t;
    bool games_drag_active;
    bool games_drag_moved;
    float games_drag_last_x;
    float games_drag_last_y;
    float games_drag_start_x;
    float games_drag_start_y;
    float games_rail_scroll_x;
    float games_rail_scroll_target;
    int games_drag_mode;
    bool ignore_pointer_until_up;
    bool home_fetched;
    bool home_loading;
    // Misc structs for home
    struct {
        int id;
        char username[32];
        char status[16];
        char avatar_color[8];
        int current_game_id;
        int current_server_id;
        char playing_title[64];
        unsigned int avatar_tex;
        int avatar_w, avatar_h;
        bool avatar_loaded;
        bool avatar_loading;
    } friends[16];
    int friend_count;
    struct {
        int id;
        char title[64];
        char creator[32];
        char thumbnail[256];
        int plays;
        int playing_now;
        unsigned int thumb_tex;
        int thumb_w, thumb_h;
        bool thumb_loaded;
        bool thumb_loading;
    } continue_games[8];
    int continue_count;
    struct {
        int id;
        char title[64];
        char creator[32];
        char thumbnail[256];
        int plays;
        int playing_now;
        unsigned int thumb_tex;
        int thumb_w, thumb_h;
        bool thumb_loaded;
        bool thumb_loading;
    } gotw_games[8];
    int gotw_count;
    bool skip_benchmark;
    bool skip_benchmark_dirty;
    // Site announcement (same banner as the website)
    bool banner_enabled;
    bool banner_fetched;
    bool banner_loading;
    char banner_message[256];
    char banner_link[512];
    char banner_tone[16];
    char banner_icon_url[512];
    unsigned int banner_icon_tex;
    int banner_icon_w, banner_icon_h;
    bool banner_icon_loaded;
    bool banner_icon_loading;
    bool update_required;
} LoginScreen;

void login_screen_init(LoginScreen* ls);
void login_screen_require_update(LoginScreen* ls);
bool login_screen_update(LoginScreen* ls, float dt);
void login_screen_render(LoginScreen* ls, int width, int height);
void login_screen_render_to(LoginScreen* ls, int width, int height, unsigned int fbo);
void login_screen_on_key(LoginScreen* ls, int keycode, bool shift, bool ctrl);
void login_screen_on_char(LoginScreen* ls, unsigned int codepoint);
bool login_screen_copy(LoginScreen* ls);
bool login_screen_cut(LoginScreen* ls);
bool login_screen_paste(LoginScreen* ls);
bool login_screen_select_all(LoginScreen* ls);
void login_screen_on_mousedown(LoginScreen* ls, int x, int y);
void login_screen_on_mouseup(LoginScreen* ls);
bool login_screen_on_scroll(LoginScreen* ls, float delta);
void login_screen_invalidate_gl(LoginScreen* ls, bool context_alive);

#endif
