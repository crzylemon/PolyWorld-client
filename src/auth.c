/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: auth.c                                                                              |
|   Purpose: talk to the web API for login                                                    |
\*-------------------------------------------------------------------------------------------*/

#include "auth.h"
#include "log.h"
#include "platform.h"
#include "client_version.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef __EMSCRIPTEN__
AuthResult auth_login(const char* username, const char* password) {
    (void)username; (void)password;
    return (AuthResult){0};
}
AuthResult auth_login_2fa(const char* challenge, const char* code) {
    (void)challenge; (void)code;
    return (AuthResult){0};
}
AuthResult auth_validate_token(const char* token) {
    (void)token;
    return (AuthResult){0};
}
void auth_save_session(const char* token) { (void)token; }
void auth_clear_session(void) {}
bool auth_load_session(char* out_token, size_t out_size) {
    (void)out_token; (void)out_size;
    return false;
}
void auth_set_session_filename(const char* filename) { (void)filename; }
void pw_set_site_origin(const char* origin) { (void)origin; }
const char* pw_site_origin(void) { return PW_SITE_ORIGIN; }
void pw_set_tcp_endpoint(const char* host, int port) { (void)host; (void)port; }
const char* pw_tcp_host(void) { return PW_TCP_HOST; }
int pw_tcp_port(void) { return PW_TCP_PORT; }
bool pw_site_is_production(void) { return true; }
void pw_use_local_site(void) {}
JoinTicket auth_get_join_ticket(const char* session_token, int game_id, bool guest, int server_id, bool shadowed) {
    (void)session_token; (void)game_id; (void)guest; (void)server_id; (void)shadowed;
    return (JoinTicket){0};
}
#else

static char g_session_filename[96] = "polyworld_session.dat";

void auth_set_session_filename(const char* filename) {
    if (!filename || !filename[0]) {
        snprintf(g_session_filename, sizeof(g_session_filename), "polyworld_session.dat");
        return;
    }
    snprintf(g_session_filename, sizeof(g_session_filename), "%s", filename);
}

static char g_site_origin[192] = PW_SITE_ORIGIN;
static char g_tcp_host[128] = PW_TCP_HOST;
static int g_tcp_port = PW_TCP_PORT;

void pw_set_site_origin(const char* origin) {
    if (!origin || !origin[0]) {
        snprintf(g_site_origin, sizeof(g_site_origin), "%s", PW_SITE_ORIGIN);
        return;
    }
    size_t n = strlen(origin);
    while (n > 0 && origin[n - 1] == '/') n--;
    if (n >= sizeof(g_site_origin)) n = sizeof(g_site_origin) - 1;
    memcpy(g_site_origin, origin, n);
    g_site_origin[n] = '\0';
}

const char* pw_site_origin(void) {
    return g_site_origin;
}

void pw_set_tcp_endpoint(const char* host, int port) {
    if (host && host[0])
        snprintf(g_tcp_host, sizeof(g_tcp_host), "%s", host);
    if (port > 0)
        g_tcp_port = port;
}

const char* pw_tcp_host(void) {
    return g_tcp_host;
}

int pw_tcp_port(void) {
    return g_tcp_port;
}

bool pw_site_is_production(void) {
    return strcmp(g_site_origin, PW_SITE_ORIGIN) == 0;
}

void pw_use_local_site(void) {
    const char* origin = getenv("PW_LOCAL_SITE");
    if (!origin || !origin[0]) origin = "https://polyworld.localhost";
    pw_set_site_origin(origin);
    const char* tcp = getenv("PW_LOCAL_TCP");
    pw_set_tcp_endpoint((tcp && tcp[0]) ? tcp : "127.0.0.1", PW_TCP_PORT);
    auth_set_session_filename("polyworld_session_local.dat");
}

static void pw_fmt_api(char* buf, size_t n, const char* file) {
    snprintf(buf, n, "%s/api/%s", g_site_origin, file ? file : "");
}

#include <stdio.h>

static bool json_get_string(const char* json, const char* key, char* out, size_t out_size) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* pos = strstr(json, search);
    if (!pos) return false;
    pos += strlen(search);
    while (*pos && (*pos == ':' || *pos == ' ' || *pos == '\t')) pos++;
    if (*pos != '"') return false;
    pos++;
    size_t i = 0;
    while (*pos && *pos != '"' && i < out_size - 1) {
        out[i++] = *pos++;
    }
    out[i] = '\0';
    return i > 0;
}

static int json_get_int(const char* json, const char* key, int default_val) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* pos = strstr(json, search);
    if (!pos) return default_val;
    pos += strlen(search);
    while (*pos && (*pos == ':' || *pos == ' ' || *pos == '\t')) pos++;
    if (*pos == '"') { pos++; }
    char* end;
    long val = strtol(pos, &end, 10);
    if (end == pos) return default_val;
    return (int)val;
}

static bool json_get_bool(const char* json, const char* key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* pos = strstr(json, search);
    if (!pos) return false;
    pos += strlen(search);
    while (*pos && (*pos == ':' || *pos == ' ' || *pos == '\t')) pos++;
    return (*pos == 't' || *pos == 'T' || *pos == '1');
}

static void auth_urlenc(const char* in, char* out, size_t out_size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    if (!in || !out || out_size == 0) return;
    for (; *in && o + 4 < out_size; in++) {
        unsigned char c = (unsigned char)*in;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '-' || c == '_' || c == '.') {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = '\0';
}

static void auth_parse_login_response(AuthResult* result, const char* resp) {
    if (!result || !resp) return;
    if (json_get_bool(resp, "requires_2fa") &&
        json_get_string(resp, "challenge", result->challenge, sizeof(result->challenge))) {
        result->needs_2fa = true;
        json_get_string(resp, "username", result->username, sizeof(result->username));
        result->user_id = json_get_int(resp, "user_id", 0);
        return;
    }
    if (json_get_string(resp, "token", result->token, sizeof(result->token)) &&
        json_get_string(resp, "username", result->username, sizeof(result->username))) {
        result->authenticated = true;
        result->user_id = json_get_int(resp, "user_id", 0);
        return;
    }
    json_get_string(resp, "error", result->error, sizeof(result->error));
    result->challenge_expired = json_get_bool(resp, "expired");
}

static void json_get_int_array(const char* json, const char* key, int* out, int max_n) {
    if (!out || max_n <= 0) return;
    for (int i = 0; i < max_n; i++) out[i] = 0;
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* pos = strstr(json, search);
    if (!pos) return;
    pos = strchr(pos, '[');
    if (!pos) return;
    pos++;
    int idx = 0;
    while (*pos && *pos != ']' && idx < max_n) {
        while (*pos == ' ' || *pos == ',') pos++;
        if (*pos == ']') break;
        out[idx++] = (int)strtol(pos, (char**)&pos, 10);
        while (*pos && *pos != ',' && *pos != ']') pos++;
    }
}

static void json_get_string_array(const char* json, const char* key,
                                  char out[][PW_EMOTE_NAME_LEN], int max_n) {
    if (!out || max_n <= 0) return;
    for (int i = 0; i < max_n; i++) out[i][0] = '\0';
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* pos = strstr(json, search);
    if (!pos) return;
    pos = strchr(pos, '[');
    if (!pos) return;
    pos++;
    int idx = 0;
    while (*pos && *pos != ']' && idx < max_n) {
        while (*pos == ' ' || *pos == ',') pos++;
        if (*pos == ']') break;
        if (*pos != '"') {
            while (*pos && *pos != ',' && *pos != ']') pos++;
            continue;
        }
        pos++;
        size_t n = 0;
        while (*pos && *pos != '"' && n + 1 < PW_EMOTE_NAME_LEN) {
            if (*pos == '\\' && pos[1]) pos++;
            out[idx][n++] = *pos++;
        }
        out[idx][n] = '\0';
        idx++;
        if (*pos == '"') pos++;
        while (*pos && *pos != ',' && *pos != ']') pos++;
    }
}

static void sync_ticket_accessory_mirror(JoinTicket* ticket) {
    if (!ticket) return;
    ticket->equipped_accessory = ticket->equipped_accessories[0];
}

static char* do_http_post(const char* url, const char* postdata) {
    size_t len = 0;
    return (char*)platform_http_post(url, postdata, &len);
}

AuthResult auth_login(const char* username, const char* password) {
    AuthResult result = {0};
    char postdata[512];
    snprintf(postdata, sizeof(postdata), "action=login&username=%s&password=%s", username, password);
    char url[256];
    pw_fmt_api(url, sizeof(url), "auth.php");
    char* resp = do_http_post(url, postdata);
    if (!resp) {
        fprintf(stderr, "Error: login request failed\n");
        snprintf(result.error, sizeof(result.error), "Network error");
        return result;
    }
    auth_parse_login_response(&result, resp);
    if (!result.authenticated && !result.needs_2fa) {
        fprintf(stderr, "[Auth] Login failed: %s\n", result.error[0] ? result.error : "unknown error");
    }
    free(resp);
    return result;
}

AuthResult auth_login_2fa(const char* challenge, const char* code) {
    AuthResult result = {0};
    char enc_code[96];
    auth_urlenc(code ? code : "", enc_code, sizeof(enc_code));
    char postdata[512];
    snprintf(postdata, sizeof(postdata), "action=login_2fa&challenge=%s&code=%s",
             challenge ? challenge : "", enc_code);
    char url[256];
    pw_fmt_api(url, sizeof(url), "auth.php");
    char* resp = do_http_post(url, postdata);
    if (!resp) {
        fprintf(stderr, "Error: 2FA login request failed\n");
        snprintf(result.error, sizeof(result.error), "Network error");
        return result;
    }
    auth_parse_login_response(&result, resp);
    if (!result.authenticated) {
        fprintf(stderr, "[Auth] 2FA failed: %s\n", result.error[0] ? result.error : "unknown error");
    }
    free(resp);
    return result;
}

AuthResult auth_validate_token(const char* token) {
    AuthResult result = {0};
    char postdata[256];
    snprintf(postdata, sizeof(postdata), "action=validate&token=%s", token);
    char url[256];
    pw_fmt_api(url, sizeof(url), "auth.php");
    char* resp = do_http_post(url, postdata);
    if (!resp) { fprintf(stderr, "Error: validate request\n"); return result; }
    if (json_get_string(resp, "username", result.username, sizeof(result.username))) {
        strncpy(result.token, token, sizeof(result.token) - 1);
        result.authenticated = true;
        result.user_id = json_get_int(resp, "user_id", 0);
    } else {
        fprintf(stderr, "Error: token invalid\n");
    }
    free(resp);
    return result;
}

void auth_save_session(const char* token) {
    if (!token || !token[0]) return;
    char path[512];
    if (!platform_userdata_path(g_session_filename, path, sizeof(path))) return;
    FILE* f = fopen(path, "w");
    if (!f) {
        return;
    }
    fputs(token, f);
    fclose(f);
}

void auth_clear_session(void) {
    char path[512];
    if (platform_userdata_path(g_session_filename, path, sizeof(path))) {
        remove(path);
    }
}

bool auth_load_session(char* out_token, size_t out_size) {
    if (!out_token || out_size < 2) return false;
    out_token[0] = '\0';
    char path[512];
    if (!platform_userdata_path(g_session_filename, path, sizeof(path))) return false;
    FILE* f = fopen(path, "r");
    if (!f) return false;
    if (!fgets(out_token, (int)out_size, f)) {
        fclose(f);
        return false;
    }
    fclose(f);
    out_token[strcspn(out_token, "\r\n")] = '\0';
    return out_token[0] != '\0';
}

JoinTicket auth_get_join_ticket(const char* session_token, int game_id, bool guest, int server_id, bool shadowed) {
    JoinTicket ticket = {0};
    char postdata[384];
    int sh = shadowed ? 1 : 0;
    if (guest) {
        if (server_id > 0)
            snprintf(postdata, sizeof(postdata), "action=create&game_id=%d&guest=1&server_id=%d&ver=%s&shadowed=%d",
                     game_id, server_id, CLIENT_VERSION, sh);
        else
            snprintf(postdata, sizeof(postdata), "action=create&game_id=%d&guest=1&ver=%s&shadowed=%d",
                     game_id, CLIENT_VERSION, sh);
    } else {
        if (server_id > 0)
            snprintf(postdata, sizeof(postdata), "action=create&game_id=%d&session_token=%s&server_id=%d&ver=%s&shadowed=%d",
                     game_id, session_token, server_id, CLIENT_VERSION, sh);
        else
            snprintf(postdata, sizeof(postdata), "action=create&game_id=%d&session_token=%s&ver=%s&shadowed=%d",
                     game_id, session_token, CLIENT_VERSION, sh);
    }
    char url[256];
    pw_fmt_api(url, sizeof(url), "join_ticket.php");
    char* resp = do_http_post(url, postdata);
    if (!resp) { fprintf(stderr, "[Auth] Join ticket request failed\n");
        snprintf(ticket.error, sizeof(ticket.error), "Network error");
        return ticket; }
    if (json_get_string(resp, "ticket", ticket.ticket, sizeof(ticket.ticket)) &&
        json_get_string(resp, "username", ticket.username, sizeof(ticket.username))) {
        ticket.valid = true;
        if (!json_get_string(resp, "avatar_color", ticket.avatar_color, sizeof(ticket.avatar_color)))
            strcpy(ticket.avatar_color, "#4da6cb");
        if (!json_get_string(resp, "skin_color", ticket.skin_color, sizeof(ticket.skin_color)))
            strcpy(ticket.skin_color, "#eaeaea");
        ticket.equipped_shirt = json_get_int(resp, "equipped_shirt", 1);
        ticket.equipped_pants = json_get_int(resp, "equipped_pants", 10);
        ticket.equipped_head = json_get_int(resp, "equipped_head", 19);
        ticket.equipped_package = json_get_int(resp, "equipped_package", 0);
        json_get_int_array(resp, "equipped_accessories", ticket.equipped_accessories,
                           PW_MAX_EQUIPPED_ACCESSORIES);
        if (ticket.equipped_accessories[0] == 0 && ticket.equipped_accessories[1] == 0 &&
            ticket.equipped_accessories[2] == 0) {
            ticket.equipped_accessories[0] = json_get_int(resp, "equipped_accessory", 0);
        }
        sync_ticket_accessory_mirror(&ticket);
        {
            uint32_t def[PW_MAX_EQUIPPED_EMOTES];
            emote_default_loadout(def);
            for (int i = 0; i < PW_MAX_EQUIPPED_EMOTES; i++) {
                ticket.equipped_emotes[i] = (int)def[i];
                ticket.emote_anims[i] = 0;
                ticket.emote_names[i][0] = '\0';
            }
            json_get_int_array(resp, "equipped_emotes", ticket.equipped_emotes,
                               PW_MAX_EQUIPPED_EMOTES);
            json_get_int_array(resp, "emote_anims", ticket.emote_anims,
                               PW_MAX_EQUIPPED_EMOTES);
            json_get_string_array(resp, "emote_names", ticket.emote_names,
                                 PW_MAX_EQUIPPED_EMOTES);
            int any = 0;
            for (int i = 0; i < PW_MAX_EQUIPPED_EMOTES; i++) {
                if (ticket.equipped_emotes[i] > 0) { any = 1; break; }
            }
            if (!any) {
                for (int i = 0; i < PW_MAX_EQUIPPED_EMOTES; i++)
                    ticket.equipped_emotes[i] = (int)def[i];
            }
            for (int i = 0; i < PW_MAX_EQUIPPED_EMOTES; i++) {
                uint32_t id = (uint32_t)ticket.equipped_emotes[i];
                if (ticket.emote_anims[i] <= 0)
                    ticket.emote_anims[i] = (int)emote_id_to_base(id);
                if (!ticket.emote_names[i][0]) {
                    if (id == PW_EMOTE_DANCE1_ID)
                        snprintf(ticket.emote_names[i], PW_EMOTE_NAME_LEN, "Dance 1");
                    else if (id == PW_EMOTE_DANCE2_ID)
                        snprintf(ticket.emote_names[i], PW_EMOTE_NAME_LEN, "Dance 2");
                    else if (id == PW_EMOTE_DANCE3_ID)
                        snprintf(ticket.emote_names[i], PW_EMOTE_NAME_LEN, "Dance 3");
                    else if (id > 0)
                        snprintf(ticket.emote_names[i], PW_EMOTE_NAME_LEN, "#%u", id);
                }
            }
        }
        ticket.user_id = json_get_int(resp, "user_id", 0);
    } else {
        char err[128] = {0};
        json_get_string(resp, "error", err, sizeof(err));
        fprintf(stderr, "[Auth] Failed to get join ticket: %s\n", err[0] ? err : "unknown");
        snprintf(ticket.error, sizeof(ticket.error), "%s", err[0] ? err : "Could not get join ticket.");
    }
    free(resp);
    return ticket;
}

#define GAMES_API_URL PW_GAMES_API_URL

bool auth_rate_game(const char* session_token, int game_id, int rating,
                    int* out_likes, int* out_dislikes, int* out_user_rating) {
    if (!session_token || !session_token[0] || game_id <= 0 || (rating != 1 && rating != -1))
        return false;
    char postdata[384];
    snprintf(postdata, sizeof(postdata),
             "action=rate&game_id=%d&rating=%d&session_token=%s",
             game_id, rating, session_token);
    char url[256];
    pw_fmt_api(url, sizeof(url), "games.php");
    char* resp = do_http_post(url, postdata);
    if (!resp) return false;
    bool ok = strstr(resp, "\"ok\"") != NULL;
    if (ok) {
        if (out_likes) *out_likes = json_get_int(resp, "likes", 0);
        if (out_dislikes) *out_dislikes = json_get_int(resp, "dislikes", 0);
        if (out_user_rating) *out_user_rating = json_get_int(resp, "user_rating", 0);
    }
    free(resp);
    return ok;
}

bool auth_avatar_get(const char* session_token, char* skin_out, size_t skin_sz,
                     int* shirt, int* pants, int* head, int* accessory,
                     int accessories_out[PW_MAX_EQUIPPED_ACCESSORIES]) {
    if (!session_token || !session_token[0]) return false;
    char api[256];
    char url[512];
    pw_fmt_api(api, sizeof(api), "avatar.php");
    snprintf(url, sizeof(url), "%s?action=get&session_token=%s", api, session_token);
    size_t len = 0;
    char* resp = (char*)platform_http_get(url, &len);
    if (!resp) return false;
    bool ok = strstr(resp, "skin_color") != NULL;
    if (ok) {
        if (skin_out && skin_sz > 0)
            json_get_string(resp, "skin_color", skin_out, (int)skin_sz);
        if (shirt) *shirt = json_get_int(resp, "equipped_shirt", 0);
        if (pants) *pants = json_get_int(resp, "equipped_pants", 0);
        if (head) *head = json_get_int(resp, "equipped_head", 0);
        int accs[PW_MAX_EQUIPPED_ACCESSORIES] = {0};
        json_get_int_array(resp, "equipped_accessories", accs, PW_MAX_EQUIPPED_ACCESSORIES);
        if (accs[0] == 0 && accs[1] == 0 && accs[2] == 0)
            accs[0] = json_get_int(resp, "equipped_accessory", 0);
        if (accessory) *accessory = accs[0];
        if (accessories_out)
            memcpy(accessories_out, accs, sizeof(accs[0]) * PW_MAX_EQUIPPED_ACCESSORIES);
    }
    free(resp);
    return ok;
}

bool auth_avatar_inventory(const char* session_token, void* items_out, int max_items, int* out_count) {
    if (out_count) *out_count = 0;
    if (!session_token || !session_token[0] || !items_out || max_items <= 0) return false;
    char api[256];
    char url[512];
    pw_fmt_api(api, sizeof(api), "avatar.php");
    snprintf(url, sizeof(url), "%s?action=inventory&session_token=%s", api, session_token);
    size_t len = 0;
    char* resp = (char*)platform_http_get(url, &len);
    if (!resp) return false;

    typedef struct {
        int id;
        char name[48];
        char type[16];
        char image_path[192];
        char mesh_style[16];
        unsigned int thumb_tex;
        bool thumb_loaded;
    } Item;
    Item* items = (Item*)items_out;
    int count = 0;
    const char* cur = resp;
    while (count < max_items) {
        const char* id_key = strstr(cur, "\"id\"");
        if (!id_key) break;
        const char* colon = strchr(id_key + 4, ':');
        if (!colon) break;
        int id = atoi(colon + 1);
        Item* it = &items[count];
        memset(it, 0, sizeof(*it));
        it->id = id;
        strncpy(it->mesh_style, "legacy", sizeof(it->mesh_style) - 1);
        const char* name_key = strstr(id_key, "\"name\"");
        if (name_key) {
            const char* c = strchr(name_key + 6, ':');
            if (c) { c++; while (*c == ' ') c++; if (*c == '"') { c++;
                int n = 0; while (*c && *c != '"' && n < 47) it->name[n++] = *c++; it->name[n] = '\0';
            }}
        }
        const char* type_key = strstr(id_key, "\"type\"");
        if (type_key) {
            const char* c = strchr(type_key + 6, ':');
            if (c) { c++; while (*c == ' ') c++; if (*c == '"') { c++;
                int n = 0; while (*c && *c != '"' && n < 15) it->type[n++] = *c++; it->type[n] = '\0';
            }}
        }
        const char* path_key = strstr(id_key, "\"image_path\"");
        if (path_key) {
            const char* c = strchr(path_key + 12, ':');
            if (c) { c++; while (*c == ' ') c++; if (*c == '"') { c++;
                int n = 0;
                while (*c && *c != '"' && n < 191) {
                    if (*c == '\\' && c[1] == '/') { c++; continue; }
                    it->image_path[n++] = *c++;
                }
                it->image_path[n] = '\0';
            }}
        }
        const char* style_key = strstr(id_key, "\"mesh_style\"");
        if (style_key) {
            const char* c = strchr(style_key + 12, ':');
            if (c) { c++; while (*c == ' ') c++; if (*c == '"') { c++;
                int n = 0; while (*c && *c != '"' && n < 15) it->mesh_style[n++] = *c++; it->mesh_style[n] = '\0';
            }}
        }
        count++;
        cur = strchr(colon, '}');
        if (!cur) break;
        cur++;
    }
    if (out_count) *out_count = count;
    free(resp);
    return true;
}

bool auth_avatar_save(const char* session_token, const char* skin_color,
                      int shirt, int pants, int head,
                      const int accessories[PW_MAX_EQUIPPED_ACCESSORIES],
                      int* out_package) {
    if (!session_token || !session_token[0] || !skin_color) return false;
    char acc_csv[64] = {0};
    int n = 0;
    for (int i = 0; i < PW_MAX_EQUIPPED_ACCESSORIES; i++) {
        if (!accessories || accessories[i] <= 0) continue;
        if (n > 0) acc_csv[n++] = ',';
        n += snprintf(acc_csv + n, sizeof(acc_csv) - (size_t)n, "%d", accessories[i]);
        if (n >= (int)sizeof(acc_csv) - 1) break;
    }
    char postdata[640];
    snprintf(postdata, sizeof(postdata),
             "action=save&session_token=%s&color=%s&shirt=%d&pants=%d&head=%d&accessories=%s",
             session_token, skin_color, shirt, pants, head, acc_csv);
    char url[256];
    pw_fmt_api(url, sizeof(url), "avatar.php");
    char* resp = do_http_post(url, postdata);
    if (!resp) return false;
    bool ok = strstr(resp, "\"ok\"") != NULL;
    if (ok && out_package)
        *out_package = json_get_int(resp, "equipped_package", 0);
    free(resp);
    return ok;
}

bool auth_avatar_equip_emotes(const char* session_token,
                              const int emotes[PW_MAX_EQUIPPED_EMOTES]) {
    if (!session_token || !session_token[0] || !emotes) return false;
    char csv[96];
    int n = 0;
    csv[0] = '\0';
    for (int i = 0; i < PW_MAX_EQUIPPED_EMOTES; i++) {
        if (i > 0) csv[n++] = ',';
        n += snprintf(csv + n, sizeof(csv) - (size_t)n, "%d", emotes[i]);
        if (n >= (int)sizeof(csv) - 1) break;
    }
    char postdata[384];
    snprintf(postdata, sizeof(postdata),
             "action=equip&slot=emote&emotes=%s&session_token=%s",
             csv, session_token);
    char url[256];
    pw_fmt_api(url, sizeof(url), "avatar.php");
    char* resp = do_http_post(url, postdata);
    if (!resp) return false;
    bool ok = strstr(resp, "\"ok\"") != NULL;
    free(resp);
    return ok;
}

#endif
