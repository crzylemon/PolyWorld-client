/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   License: See LICENSE file (Custom)                                                        |
|   Credits:                                                                                  |
|       - Me (obviously)                                                                      |
|       - Github Contributors                                                                 |
|       - All the bug-catchers and moderators, even Creators or people who suggested features |
|       - Everyone who donated! :D                                                            |________________
|   Notes:                                                                                                    |
|       - You are not allowed to modify this code to do the following: (quick: new features yes but hacks no) |
|         Create exploits or hacks to get an unfair advantage or to bypass security,                          |
|         Distribute an unmodified build of the game outside PolyWorld.games without permission,              |
|         Change the license of the game,                                                     ________________|
|         Distribute a version of the game for profit, or to purposefully hack someone,       |
|                                                                                             |
|       - PolyWorld is free to modify (eg. studying netcode or adding something like mod api) |
|         but PLEASE! Don't add new features to the game that doesn't let others play!        |
|         It's really unfair to the other players to have to download a mod to play a game... |
\*-------------------------------------------------------------------------------------------*/

#include "platform.h"
#include "log.h"
#include "renderer.h"
#include "brick_batch.h"
#include "input.h"
#include "physics.h"
#include "scene.h"
#include "part_material.h"
#include "camera.h"
#include "avatar.h"
#include "mesh_loader.h"
#include "mesh_primitives.h"
#include "math_types.h"
#include "skybox.h"
#include "world_loader.h"
#include "texture.h"
#include "net_client.h"
#include "protocol.h"
#include "vr_ik.h"
#include "vr_session.h"
#include "vr_hub.h"
#include "vr_openxr.h"
#include "client_version.h"
#include "chat.h"
#include "auth.h"
#include "updater.h"
#include "login_screen.h"
#include "avatar_editor.h"
#include "catalog_ui.h"
#include "scripting.h"
#include "avatar_anim.h"
#include "accessory.h"
#include "emote.h"
#include "emote_wheel.h"
#include "emote_clip.h"
#include "audio.h"
#include "font.h"
#include "game_menu.h"
#include "gfx_benchmark.h"
#include "social.h"
#include "touch_controls.h"
#include "vidactor.h"
#ifndef __EMSCRIPTEN__
#include "discord.h"
#include "studio_embed.h"
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !defined(PW_IOS)
#include "pw_studio_play.h"
#endif
#endif

#include "pw_gles.h"
#if PW_USE_GLES
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#ifndef __EMSCRIPTEN__
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#define pw_getpid() _getpid()
#else
#include <unistd.h>
#include <sys/stat.h>
#define pw_getpid() getpid()
#endif
#endif

int g_fps_limit_setting = 0;
int g_fps_limit_override = 0;

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !defined(PW_IOS)
static int g_host_canvas_w, g_host_canvas_h;
static bool g_studio_host_play;
static bool g_studio_host_stop;
static bool g_studio_game_inited;
static bool join_from_polyworld_url(const char* arg);
#endif

#define PHYSICS_FIXED_DT (1.0f / 60.0f)
#if PW_MOBILE
#define MAX_PHYSICS_STEPS_PER_FRAME 2
#else
#define MAX_PHYSICS_STEPS_PER_FRAME 4
#endif
#define AVATAR_ROOT_HALF_Y 2.625f
#define AVATAR_FEET_OFFSET AVATAR_ROOT_HALF_Y
#define AVATAR_CAMERA_ORBIT_Y 4.625f

#define BASEPLATE_HALF_X 25.0f
#define BASEPLATE_HALF_Y 0.5f
#define BASEPLATE_HALF_Z 25.0f

#define CUBE_HALF_SIZE 0.5f

typedef struct {
    Renderer renderer;
    Scene scene;
    PhysicsWorld* physics;
    Camera camera;
    Avatar avatar;
    float accumulator;
    bool initialized;

    GPUMesh avatar_gpu_mesh;
    GPUMesh cube_gpu_mesh;

    uint32_t avatar_texture;
    uint32_t guest_avatar_texture;
    uint32_t local_tex_shirt, local_tex_pants, local_tex_head;

    Vec3 skin_color;
    AvatarAnim avatar_anim;
    AvatarAnim avatar_body_legacy;
    AvatarAnim avatar_body_new;
    bool avatar_body_legacy_ready;
    bool avatar_body_new_ready;
    bool avatar_body_legacy_loading;
    bool avatar_body_new_loading;
    bool avatar_anim_parts_owned;
    Accessory local_accessory[PW_MAX_EQUIPPED_ACCESSORIES];
    uint32_t local_accessory_tex[PW_MAX_EQUIPPED_ACCESSORIES];

    NetClient net;
    bool multiplayer;
    int game_id;
    bool auth_sent;
    char username[32];
    uint32_t account_id;
    char session_token[65];
    char join_ticket[33];
    char avatar_color[8];

    bool allow_freecam;

    bool reset_enabled;
    uint8_t camera_mode;
    Vec3 freecam_pos;
    float freecam_yaw, freecam_pitch;
    uint8_t local_badges;
    float local_transparency;
    bool local_has_name_color;
    float local_name_color_r, local_name_color_g, local_name_color_b;

    struct {
        bool active;
        int team_count;
        struct {
            char name[32];
            float r, g, b;
        } teams[CHAT_PL_TEAM_MAX];
        int stat_count;
        char stat_names[CHAT_PL_STAT_MAX][24];
        int entry_count;
        struct {
            uint32_t player_id;
            int8_t team_idx;
            float stats[CHAT_PL_STAT_MAX];
        } entries[CHAT_PL_MAX];
    } scoreboard;

    #define MAX_REMOTE_PLAYERS 96
    struct {
        uint32_t id;
        EntityID entity;
        bool active;
        bool dead;
        char name[32];
        uint8_t badges;
        uint32_t account_id;
        float transparency;
        bool has_name_color;
        float name_color_r, name_color_g, name_color_b;
        AvatarAnim anim;
        int mesh_flags;
        uint32_t tex_shirt, tex_pants, tex_head, tex_package;
        Vec3 skin_color;
        Accessory accessory[PW_MAX_EQUIPPED_ACCESSORIES];
        uint32_t accessory_tex[PW_MAX_EQUIPPED_ACCESSORIES];
        uint32_t equipped_emotes[PW_MAX_EQUIPPED_EMOTES];

        Vec3 target_pos;
        float target_yaw;
        Vec3 lerp_start_pos;
        float lerp_start_yaw;
        float lerp_t;
        float lerp_duration;
        float lerp_interval_ema;
        double last_update_time;
        bool has_target;
        float anim_dt_accum;
        Vec3 last_vel;
        char held_tool[32];

        bool is_vr;
        PwVrPose vr_pose;
        VrIkCalib vr_calib;

        struct {
            bool active;
            float timer;
            PhysicsBodyID bodies[AVATAR_PART_COUNT];
            Mat4 mesh_from_body[AVATAR_PART_COUNT];
            PhysicsBodyID acc_bodies[PW_MAX_EQUIPPED_ACCESSORIES][ACCESSORY_MAX_PARTS];
            Mat4 acc_mesh_from_body[PW_MAX_EQUIPPED_ACCESSORIES][ACCESSORY_MAX_PARTS];
        } ragdoll;
    } remote_players[MAX_REMOTE_PLAYERS];
    int remote_player_count;

    EntityID cube_entities[4];
    bool debug_draw;
    bool show_fps;
    bool show_chunk_borders;
    bool hide_hud;
    bool trail_recording;
    int trail_count;
    Vec3 trail_points[2048];
    Vec3 trail_last_sample;
    float fps_timer;
    int fps_frames;
    int fps_display;
    float ui_scale;
    Skybox skybox;
#ifdef VIDACTOR
    struct {
        bool active;
        EntityID entity;
        AvatarAnim anim;
        Vec3 skin_color;
        uint32_t tex_shirt, tex_pants, tex_head;
        float move_speed;
        uint8_t anim_state;
    } vid_puppets[VIDACTOR_MAX_ACTORS];
#endif
    bool loading_world;
    bool world_ready;
    bool await_batch_ready;

    VrSessionState vr;

    uint8_t* world_init_buf;
    size_t world_init_len;
    size_t world_init_off;
    uint32_t world_init_total;
    uint32_t world_init_done;
    bool world_init_streaming;
    bool physics_streaming;
    int physics_stream_i;
    uint8_t* pending_connectors;
    size_t pending_connectors_len;
    bool connectors_streaming;
    int connectors_phase;
    int connectors_stream_i;
    bool pending_spawn;
    float pending_spawn_pos[3];
    bool spawn_received;
    bool world_colliders_ready;

    #define MAX_PENDING_SCRIPTS 64
    struct {
        uint32_t parent_obj_id;
        char* source;
    } pending_scripts[MAX_PENDING_SCRIPTS];
    int pending_script_count;

    Chat chat;

    #define MAX_DYNAMIC_OBJECTS 64
    struct {
        uint32_t net_id;
        EntityID entity;
        PhysicsBodyID body;
        bool active;
        bool owned_locally;
        float own_timer;
        Vec3 last_pos;
    } dynamic_objects[MAX_DYNAMIC_OBJECTS];
    int dynamic_object_count;

    #define MAX_NET_OBJECTS 8192
    struct {
        uint32_t net_id;
        EntityID entity;
        bool anchored;
        bool connector_static;
        uint8_t obj_type;
        uint32_t mesh_id;
        uint8_t mesh_collider;
        float size[3];

        Vec3 target_pos;
        Vec3 target_rot;
        Vec3 lerp_start_pos;
        Vec3 lerp_start_rot;
        float lerp_t;
        float lerp_duration;
        float lerp_interval_ema;
        double last_update_time;
        bool has_target;

        bool collide_wanted;
        bool clickable;
        bool collision_lod_active;
        bool net_owned;
        bool never_netown;
    } net_objects[MAX_NET_OBJECTS];
    int net_object_count;
    uint16_t net_proto;
    uint32_t local_player_id;
    #define MAX_NET_CONSTRAINTS 2048
    struct {
        uint32_t id_a, id_b;
        ConstraintDesc desc;
        uint32_t local_conn;
        bool active;
    } net_constraints[MAX_NET_CONSTRAINTS];
    int net_constraint_count;
    #define MAX_PENDING_NOCOLLIDE 4096
    uint32_t pending_nocollide_ids[MAX_PENDING_NOCOLLIDE];
    int pending_nocollide_count;
    #define MAX_PENDING_CLICKABLE 256
    uint32_t pending_clickable_ids[MAX_PENDING_CLICKABLE];
    int pending_clickable_count;
    #define MAX_PENDING_MATERIAL 4096
    uint32_t pending_material_ids[MAX_PENDING_MATERIAL];
    uint8_t pending_material_vals[MAX_PENDING_MATERIAL];
    int pending_material_count;
    int collision_lod_scan_i;
    int collision_lod_idle_frames;
    int collision_lod_focus_cx;
    int collision_lod_focus_cz;

    ClientScriptEngine* scripts;

    struct {
        bool active;
        float timer;
        PhysicsBodyID bodies[AVATAR_PART_COUNT];
        Mat4 mesh_from_body[AVATAR_PART_COUNT];
        PhysicsBodyID acc_bodies[PW_MAX_EQUIPPED_ACCESSORIES][ACCESSORY_MAX_PARTS];
        Mat4 acc_mesh_from_body[PW_MAX_EQUIPPED_ACCESSORIES][ACCESSORY_MAX_PARTS];
    } ragdoll;

    struct {
        char toast[201];
        float toast_timer;
        float toast_alpha;
        float toast_yoff;
        bool toast_out;
        char hud[201];
        float hud_timer;
        float hud_alpha;
        bool hud_out;
    } script_ui;

    struct {
        char title[96];
        char author[96];
        char pending_title[96];
        char pending_author[96];
        float alpha;
        float yoff;
        bool rise;
        int phase;
        unsigned last_gen;
    } music_cred;

    unsigned int load_bg_tex;
    unsigned int logo_tex;
    int load_bg_w, load_bg_h;
    int logo_w, logo_h;
    float loading_time;
    bool show_kick;
    char kick_reason[128];
    float overlay_spin;

    #define MAX_EXPLOSIONS 16
    #define EXPLOSION_DEBRIS 8
    #define EXPLOSION_PARTS  36
    struct {
        bool active;
        Vec3 position;
        float radius;
        float age;
        float life;
        int part_count;
        struct {
            uint8_t kind;
            uint8_t tile;
            Vec3 pos, vel;
            float size0, size1;
            float roll, rollvel;
            float delay, dur;
        } parts[EXPLOSION_PARTS];
        struct {
            Vec3 pos, vel, rot, rotvel;
            float size;
            Vec3 color;
        } debris[EXPLOSION_DEBRIS];
    } explosions[MAX_EXPLOSIONS];
    GPUMesh explosion_mesh;
    bool explosion_mesh_ready;
    GPUMesh explosion_quad;
    bool explosion_quad_ready;
    uint32_t explosion_tex;
    float exp_flash;

    int equipped_tool;
    float tool_cooldown;
    float tool_cooldown_max;
    #define MAX_TOOLS 9
    struct {
        char name[32];
        bool available;
        uint32_t icon_tex;
        int icon_w, icon_h;
    } tools[MAX_TOOLS];
    int tool_count;

    #define MAX_ROCKETS 8
    struct {
        bool active;
        Vec3 position;
        Vec3 direction;
        float speed;
        float timer;
    } rockets[MAX_ROCKETS];

    bool show_login;
    bool studio_playtest;
#ifndef __EMSCRIPTEN__
    AvatarEditor avatar_editor;
    CatalogUi catalog_ui;
#endif
    int local_equipped_shirt;
    int local_equipped_pants;
    int local_equipped_head;
    int local_equipped_package;
    int local_equipped_accessories[PW_MAX_EQUIPPED_ACCESSORIES];
    int local_equipped_accessory;
    uint32_t local_equipped_emotes[PW_MAX_EQUIPPED_EMOTES];
    uint32_t active_emote_id;
    uint8_t local_emote_anims[PW_MAX_EQUIPPED_EMOTES];
    char local_emote_names[PW_MAX_EQUIPPED_EMOTES][PW_EMOTE_NAME_LEN];
    EmoteWheel emote_wheel;
    char local_skin_hex[8];
    LoginScreen login_screen;

    bool show_disconnect;
    char disconnect_reason[160];
    unsigned int disconnect_tex;
    bool avatar_ready;
    char* host;

    GameMenu menu;

    SocialUI social;

    Mat4 last_view;
    Mat4 last_projection;
    bool last_view_valid;

    float move_lock_timer;

    bool collision_chunk_loading;
    Vec3 collision_pin_pos;
    int collision_load_attempts;
    double collision_load_attempt_time;
    bool collision_spawn_gate;
} GameState;

static GameState g_game;

static int s_netown_lite_ack = -1;

static bool client_netown_lite(void) {
    if (!g_game.multiplayer) return false;
    GfxQuality q = game_menu_get_effective_quality(&g_game.menu);
    return q <= GFX_QUALITY_MEDIUM;
}

static void client_send_protocol_ack(void);
static void netown_drop_all_local(void);
static int net_find_ni(uint32_t id);

static void vr_calibrate_on_world_ready(void);
static void vr_send_local_pose(void);
static void vr_pose_remote_avatar(int rp);

static bool hud_hidden(void) {
    return g_game.hide_hud || vidactor_ui_hidden() || vr_hub_active();
}

static int scoreboard_find_entry(uint32_t pid, const uint32_t* remote_pids, int remote_n,
                                 const int* used_flags) {
    for (int i = 0; i < g_game.scoreboard.entry_count; i++) {
        if (used_flags[i]) continue;
        if (pid != 0 && g_game.scoreboard.entries[i].player_id == pid)
            return i;
    }
    if (pid == 0) {
        for (int i = 0; i < g_game.scoreboard.entry_count; i++) {
            if (used_flags[i]) continue;
            uint32_t sp = g_game.scoreboard.entries[i].player_id;
            bool is_remote = false;
            for (int r = 0; r < remote_n; r++) {
                if (remote_pids[r] == sp) { is_remote = true; break; }
            }
            if (!is_remote) return i;
        }
    }
    return -1;
}

#define NET_MESH_CACHE_MAX 4096
static struct {
    float a, b, c;
    uint8_t type;
    GPUMesh mesh;
} g_net_mesh_cache[NET_MESH_CACHE_MAX];
static int g_net_mesh_cache_count = 0;
static uint8_t g_net_msg_buf[65536];

static GPUMesh g_unit_box_mesh;
static GPUMesh g_unit_sphere_mesh;
static GPUMesh g_unit_cylinder_mesh;
static GPUMesh g_unit_wedge_mesh;
static bool g_unit_meshes_ready = false;

static void fill_wedge_hull_points(float hx, float hy, float hz, Vec3 out[6]) {
    if (hx < 0.01f) hx = 0.01f;
    if (hy < 0.01f) hy = 0.01f;
    if (hz < 0.01f) hz = 0.01f;
    out[0] = (Vec3){ -hx, -hy, -hz };
    out[1] = (Vec3){  hx, -hy, -hz };
    out[2] = (Vec3){ -hx, -hy,  hz };
    out[3] = (Vec3){  hx, -hy,  hz };
    out[4] = (Vec3){ -hx,  hy,  hz };
    out[5] = (Vec3){  hx,  hy,  hz };
}

static void body_desc_apply_wedge(BodyDesc* desc, float hx, float hy, float hz) {
    desc->collider = COLLIDER_HULL;
    desc->half_extents = (Vec3){ hx, hy, hz };
    desc->hull_point_count = 6;
    fill_wedge_hull_points(hx, hy, hz, desc->hull_points);
}

static Vec3 g_mesh_hull_scratch[256];

static int sample_catalog_mesh_hull(const GPUMesh* mesh, float sx, float sy, float sz,
                                    int max_n, Vec3* out) {
    if (!mesh || !mesh->cpu_positions || mesh->cpu_vertex_count == 0 || !out || max_n < 8)
        return 0;
    size_t vc = mesh->cpu_vertex_count;
    float mn[3] = { 1e30f, 1e30f, 1e30f };
    float mx[3] = { -1e30f, -1e30f, -1e30f };
    for (size_t i = 0; i < vc; i++) {
        float x = mesh->cpu_positions[i * 3 + 0] * sx;
        float y = mesh->cpu_positions[i * 3 + 1] * sy;
        float z = mesh->cpu_positions[i * 3 + 2] * sz;
        if (x < mn[0]) mn[0] = x; if (x > mx[0]) mx[0] = x;
        if (y < mn[1]) mn[1] = y; if (y > mx[1]) mx[1] = y;
        if (z < mn[2]) mn[2] = z; if (z > mx[2]) mx[2] = z;
    }

    if (max_n <= 8) {
        int n = 0;
        for (int ix = 0; ix < 2; ix++)
        for (int iy = 0; iy < 2; iy++)
        for (int iz = 0; iz < 2; iz++)
            out[n++] = (Vec3){ ix ? mx[0] : mn[0], iy ? mx[1] : mn[1], iz ? mx[2] : mn[2] };
        return n;
    }

    int ext_i[6] = { -1, -1, -1, -1, -1, -1 };
    float ext_v[6] = { -1e30f, 1e30f, -1e30f, 1e30f, -1e30f, 1e30f };
    for (size_t i = 0; i < vc; i++) {
        float x = mesh->cpu_positions[i * 3 + 0] * sx;
        float y = mesh->cpu_positions[i * 3 + 1] * sy;
        float z = mesh->cpu_positions[i * 3 + 2] * sz;
        if (x > ext_v[0]) { ext_v[0] = x; ext_i[0] = (int)i; }
        if (x < ext_v[1]) { ext_v[1] = x; ext_i[1] = (int)i; }
        if (y > ext_v[2]) { ext_v[2] = y; ext_i[2] = (int)i; }
        if (y < ext_v[3]) { ext_v[3] = y; ext_i[3] = (int)i; }
        if (z > ext_v[4]) { ext_v[4] = z; ext_i[4] = (int)i; }
        if (z < ext_v[5]) { ext_v[5] = z; ext_i[5] = (int)i; }
    }
    int n = 0;
    for (int e = 0; e < 6; e++) {
        if (ext_i[e] < 0) continue;
        Vec3 p = {
            mesh->cpu_positions[ext_i[e] * 3 + 0] * sx,
            mesh->cpu_positions[ext_i[e] * 3 + 1] * sy,
            mesh->cpu_positions[ext_i[e] * 3 + 2] * sz
        };
        bool dup = false;
        for (int k = 0; k < n; k++) {
            float dx = p.x - out[k].x, dy = p.y - out[k].y, dz = p.z - out[k].z;
            if (dx * dx + dy * dy + dz * dz < 1e-8f) { dup = true; break; }
        }
        if (!dup) out[n++] = p;
    }
    if (n < 4) {
        n = 0;
        for (int ix = 0; ix < 2; ix++)
        for (int iy = 0; iy < 2; iy++)
        for (int iz = 0; iz < 2; iz++)
            out[n++] = (Vec3){ ix ? mx[0] : mn[0], iy ? mx[1] : mn[1], iz ? mx[2] : mn[2] };
        return n;
    }

    size_t stride = 1;
    if (vc > 2048) stride = vc / 2048;
    for (int round = n; round < max_n; round++) {
        int best = -1;
        float best_d = -1.0f;
        for (size_t i = 0; i < vc; i += stride) {
            float x = mesh->cpu_positions[i * 3 + 0] * sx;
            float y = mesh->cpu_positions[i * 3 + 1] * sy;
            float z = mesh->cpu_positions[i * 3 + 2] * sz;
            float dmin = 1e30f;
            for (int k = 0; k < n; k++) {
                float dx = x - out[k].x, dy = y - out[k].y, dz = z - out[k].z;
                float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 < dmin) dmin = d2;
            }
            if (dmin > best_d) { best_d = dmin; best = (int)i; }
        }
        if (best < 0 || best_d < 1e-8f) break;
        out[n++] = (Vec3){
            mesh->cpu_positions[best * 3 + 0] * sx,
            mesh->cpu_positions[best * 3 + 1] * sy,
            mesh->cpu_positions[best * 3 + 2] * sz
        };
    }
    return n;
}

static void net_apply_part_collider(BodyDesc* desc, int ni, float hx, float hy, float hz);
static ColliderType collider_for_obj_type(uint8_t obj_type);

static void clear_net_mesh_cache(void) {
    for (int mi = 0; mi < g_net_mesh_cache_count; mi++)
        mesh_gpu_free(&g_net_mesh_cache[mi].mesh);
    g_net_mesh_cache_count = 0;
}

static bool ensure_unit_net_meshes(void) {
    if (g_unit_meshes_ready) return true;
    MeshData md;
    memset(&g_unit_box_mesh, 0, sizeof(g_unit_box_mesh));
    memset(&g_unit_sphere_mesh, 0, sizeof(g_unit_sphere_mesh));
    memset(&g_unit_cylinder_mesh, 0, sizeof(g_unit_cylinder_mesh));
    memset(&g_unit_wedge_mesh, 0, sizeof(g_unit_wedge_mesh));
    if (!create_box_mesh(&md, 0.5f, 0.5f, 0.5f)) return false;
    if (!mesh_upload(&md, &g_unit_box_mesh)) { mesh_data_free(&md); return false; }
    mesh_data_free(&md);
    if (!create_sphere_mesh(&md, 0.5f, 32, 24)) return false;
    if (!mesh_upload(&md, &g_unit_sphere_mesh)) { mesh_data_free(&md); return false; }
    mesh_data_free(&md);
    g_unit_sphere_mesh.prim_kind = 1;
    if (!create_cylinder_mesh(&md, 0.5f, 1.0f, 20)) return false;
    if (!mesh_upload(&md, &g_unit_cylinder_mesh)) { mesh_data_free(&md); return false; }
    mesh_data_free(&md);
    g_unit_cylinder_mesh.prim_kind = 2;
    if (!create_wedge_mesh(&md, 0.5f, 0.5f, 0.5f)) return false;
    if (!mesh_upload(&md, &g_unit_wedge_mesh)) { mesh_data_free(&md); return false; }
    mesh_data_free(&md);
    g_unit_wedge_mesh.prim_kind = 3;
    g_unit_meshes_ready = true;
    return true;
}

static GPUMesh* net_unit_mesh_for_type(uint8_t obj_type) {
    if (!ensure_unit_net_meshes()) return NULL;
    if (obj_type == 1) return &g_unit_sphere_mesh;
    if (obj_type == 2) return &g_unit_cylinder_mesh;
    if (obj_type == 3) return &g_unit_wedge_mesh;
    return &g_unit_box_mesh;
}

#define PW_CATALOG_MESH_CACHE 256
#define PW_CATALOG_TEX_CACHE 256
#define PW_MAX_NET_DECALS 2048
#define PW_MAX_PENDING_MESH 4096
#define PW_MAX_PENDING_DECAL 4096

static struct {
    int id;
    GPUMesh mesh;
    unsigned int tex;
    bool ready, failed, loading;
    bool tex_ready, tex_failed, tex_loading;
} g_cat_mesh[PW_CATALOG_MESH_CACHE];
static int g_cat_mesh_n;

static struct {
    int id;
    unsigned int tex;
    bool ready, failed, loading;
} g_cat_tex[PW_CATALOG_TEX_CACHE];
static int g_cat_tex_n;

static GPUMesh g_decal_quad;
static bool g_decal_quad_ready = false;
static struct { float u, v; GPUMesh m; bool ready; } g_decal_uv[16];

static struct {
    uint32_t parent_id;
    EntityID entity;
    int tex_id;
    uint8_t mode, face;
    bool active;
} g_net_decals[PW_MAX_NET_DECALS];
static int g_net_decal_n;

static uint32_t g_pend_mesh_oid[PW_MAX_PENDING_MESH];
static uint32_t g_pend_mesh_val[PW_MAX_PENDING_MESH];
static uint8_t g_pend_mesh_col[PW_MAX_PENDING_MESH];
static int g_pend_mesh_n;
static uint32_t g_pend_dec_parent[PW_MAX_PENDING_DECAL];
static uint32_t g_pend_dec_tex[PW_MAX_PENDING_DECAL];
static uint8_t g_pend_dec_mode[PW_MAX_PENDING_DECAL];
static uint8_t g_pend_dec_face[PW_MAX_PENDING_DECAL];
static float g_pend_dec_tx[PW_MAX_PENDING_DECAL];
static float g_pend_dec_ty[PW_MAX_PENDING_DECAL];
static int g_pend_dec_n;

static void catalog_mesh_fit_unit(MeshData* md) {
    if (!md || md->vertex_count == 0 || !md->positions) return;
    float mn[3] = { 1e9f, 1e9f, 1e9f };
    float mx[3] = { -1e9f, -1e9f, -1e9f };
    for (size_t i = 0; i < md->vertex_count; i++) {
        for (int c = 0; c < 3; c++) {
            float v = md->positions[i * 3 + c];
            if (v < mn[c]) mn[c] = v;
            if (v > mx[c]) mx[c] = v;
        }
    }
    float ext = mx[0] - mn[0];
    if (mx[1] - mn[1] > ext) ext = mx[1] - mn[1];
    if (mx[2] - mn[2] > ext) ext = mx[2] - mn[2];
    if (ext < 1e-6f) ext = 1.0f;
    float cx = 0.5f * (mn[0] + mx[0]);
    float cy = 0.5f * (mn[1] + mx[1]);
    float cz = 0.5f * (mn[2] + mx[2]);
    for (size_t i = 0; i < md->vertex_count; i++) {
        md->positions[i * 3 + 0] = (md->positions[i * 3 + 0] - cx) / ext;
        md->positions[i * 3 + 1] = (md->positions[i * 3 + 1] - cy) / ext;
        md->positions[i * 3 + 2] = (md->positions[i * 3 + 2] - cz) / ext;
    }
}

static GPUMesh* client_decal_quad(void) {
    if (g_decal_quad_ready) return &g_decal_quad;
    MeshData md;
    if (!create_quad_mesh(&md)) return NULL;
    if (!mesh_upload(&md, &g_decal_quad)) { mesh_data_free(&md); return NULL; }
    mesh_data_free(&md);
    g_decal_quad_ready = true;
    return &g_decal_quad;
}

static GPUMesh* client_decal_quad_uv(float u, float v) {
    if (u < 0.05f) u = 0.05f;
    if (v < 0.05f) v = 0.05f;
    if (fabsf(u - 1.0f) < 0.05f && fabsf(v - 1.0f) < 0.05f)
        return client_decal_quad();
    for (int i = 0; i < 16; i++) {
        if (g_decal_uv[i].ready && fabsf(g_decal_uv[i].u - u) < 0.08f &&
            fabsf(g_decal_uv[i].v - v) < 0.08f)
            return &g_decal_uv[i].m;
    }
    int slot = 0;
    for (int i = 0; i < 16; i++) if (!g_decal_uv[i].ready) { slot = i; break; }
    MeshData md;
    if (!create_quad_mesh(&md)) return client_decal_quad();
    for (int i = 0; i < 4; i++) {
        md.texcoords[i * 2 + 0] *= u;
        md.texcoords[i * 2 + 1] *= v;
    }
    if (g_decal_uv[slot].ready) mesh_gpu_free(&g_decal_uv[slot].m);
    if (!mesh_upload(&md, &g_decal_uv[slot].m)) {
        mesh_data_free(&md);
        return client_decal_quad();
    }
    mesh_data_free(&md);
    g_decal_uv[slot].u = u;
    g_decal_uv[slot].v = v;
    g_decal_uv[slot].ready = true;
    return &g_decal_uv[slot].m;
}

static void net_brick_rebuild_collision(int ni);

static void apply_catalog_mesh_to_net(uint32_t oid, uint32_t mesh_id, GPUMesh* mesh) {
    for (int ni = 0; ni < g_game.net_object_count; ni++) {
        if (g_game.net_objects[ni].net_id != oid) continue;
        g_game.net_objects[ni].mesh_id = mesh_id;
        Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
        if (ent && mesh) {
            ent->mesh = mesh;
            ent->static_batch = false;
            ent->render_batched = false;
            for (int i = 0; i < g_cat_mesh_n; i++) {
                if (g_cat_mesh[i].id == (int)mesh_id && g_cat_mesh[i].tex_ready && g_cat_mesh[i].tex) {
                    ent->material.texture_id = g_cat_mesh[i].tex;
                    break;
                }
            }
        }
        if (g_game.net_objects[ni].mesh_collider >= MESH_COLLIDER_LOW)
            net_brick_rebuild_collision(ni);
        return;
    }
}

static int catalog_mesh_url(char* url, size_t n, int id, int stage) {
    const char* host = g_game.host[0] ? g_game.host : "https://polyworld.games";
    const char* fmt = NULL;

    if (stage == 0) fmt = "%s/uploads/meshes/%d.obj";
    else if (stage == 1) fmt = "%s/uploads/accessories/%d.obj";
    else return 0;
    if (url && n) snprintf(url, n, fmt, host, id);
    return 1;
}

static bool catalog_looks_like_obj(const uint8_t* data, size_t len) {
    if (!data || len < 2) return false;
    size_t i = 0;
    if (len >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf) i = 3;
    while (i < len && (data[i] == ' ' || data[i] == '\t' || data[i] == '\r' || data[i] == '\n'))
        i++;
    if (i < len && data[i] == '<') return false;
    return true;
}

static void catalog_mesh_request(int id, int stage);
static void catalog_mesh_tex_request(int id);

static void on_catalog_mesh_tex_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    int id = (int)(intptr_t)user;
    int slot = -1;
    for (int i = 0; i < g_cat_mesh_n; i++) {
        if (g_cat_mesh[i].id == id) { slot = i; break; }
    }
    if (slot < 0) return;
    g_cat_mesh[slot].tex_loading = false;
    (void)path;
    if (!data || len < 8 || (unsigned char)data[0] != 0x89 || data[1] != 'P') {
        g_cat_mesh[slot].tex_failed = true;
        return;
    }
    extern unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
    extern void stbi_image_free(void*);
    int w = 0, h = 0, c = 0;
    unsigned char* px = stbi_load_from_memory(data, (int)len, &w, &h, &c, 4);
    if (!px) { g_cat_mesh[slot].tex_failed = true; return; }
    g_cat_mesh[slot].tex = texture_load_from_memory(px, w, h, 4);
    stbi_image_free(px);
    if (!g_cat_mesh[slot].tex) { g_cat_mesh[slot].tex_failed = true; return; }
    texture_set_overlay_sampling(g_cat_mesh[slot].tex);
    g_cat_mesh[slot].tex_ready = true;
    if (g_cat_mesh[slot].ready) {
        for (int ni = 0; ni < g_game.net_object_count; ni++) {
            if (g_game.net_objects[ni].mesh_id == (uint32_t)id)
                apply_catalog_mesh_to_net(g_game.net_objects[ni].net_id, (uint32_t)id,
                                         &g_cat_mesh[slot].mesh);
        }
    }
}

static void catalog_mesh_tex_request(int id) {
    int slot = -1;
    for (int i = 0; i < g_cat_mesh_n; i++) {
        if (g_cat_mesh[i].id == id) { slot = i; break; }
    }
    if (slot < 0) return;
    if (g_cat_mesh[slot].tex_loading || g_cat_mesh[slot].tex_ready || g_cat_mesh[slot].tex_failed)
        return;
    g_cat_mesh[slot].tex_loading = true;
    const char* host = g_game.host[0] ? g_game.host : "https://polyworld.games";
    char url[256];
    snprintf(url, sizeof(url), "%s/uploads/meshes/%d.png", host, id);
    platform_load_file(url, on_catalog_mesh_tex_loaded, (void*)(intptr_t)id);
}

static void on_catalog_mesh_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    intptr_t packed = (intptr_t)user;
    int id = (int)(packed & 0x3fffffff);
    int stage = (int)((packed >> 30) & 3);
    int slot = -1;
    for (int i = 0; i < g_cat_mesh_n; i++) {
        if (g_cat_mesh[i].id == id) { slot = i; break; }
    }
    if (slot < 0) return;
    g_cat_mesh[slot].loading = false;

    bool usable = data && len > 0 && catalog_looks_like_obj(data, len);
    MeshData md;
    memset(&md, 0, sizeof(md));
    if (usable && mesh_parse_obj((const char*)data, len, &md)) {
        catalog_mesh_fit_unit(&md);
        if (mesh_upload(&md, &g_cat_mesh[slot].mesh)) {
            mesh_data_free(&md);
            g_cat_mesh[slot].ready = true;
            catalog_mesh_tex_request(id);
            for (int ni = 0; ni < g_game.net_object_count; ni++) {
                if (g_game.net_objects[ni].mesh_id == (uint32_t)id)
                    apply_catalog_mesh_to_net(g_game.net_objects[ni].net_id, (uint32_t)id,
                                             &g_cat_mesh[slot].mesh);
            }
            (void)path;
            return;
        }
        mesh_data_free(&md);
    }

    char next_url[256];
    if (catalog_mesh_url(next_url, sizeof(next_url), id, stage + 1)) {
        g_cat_mesh[slot].loading = true;
        intptr_t next = ((intptr_t)id & 0x3fffffff) | ((intptr_t)(stage + 1) << 30);
        platform_load_file(next_url, on_catalog_mesh_loaded, (void*)next);
        return;
    }
    g_cat_mesh[slot].failed = true;
    (void)path;
}

static void catalog_mesh_request(int id, int stage) {
    char url[256];
    if (!catalog_mesh_url(url, sizeof(url), id, stage)) return;
    intptr_t packed = ((intptr_t)id & 0x3fffffff) | ((intptr_t)stage << 30);
    platform_load_file(url, on_catalog_mesh_loaded, (void*)packed);
}

static GPUMesh* catalog_mesh_get(int id) {
    if (id <= 0) return NULL;
    for (int i = 0; i < g_cat_mesh_n; i++) {
        if (g_cat_mesh[i].id != id) continue;
        if (g_cat_mesh[i].ready) return &g_cat_mesh[i].mesh;
        return NULL;
    }
    if (g_cat_mesh_n >= PW_CATALOG_MESH_CACHE) return NULL;
    int slot = g_cat_mesh_n++;
    memset(&g_cat_mesh[slot], 0, sizeof(g_cat_mesh[slot]));
    g_cat_mesh[slot].id = id;
    g_cat_mesh[slot].loading = true;
    catalog_mesh_request(id, 0);
    return NULL;
}

static void net_apply_part_collider(BodyDesc* desc, int ni, float hx, float hy, float hz) {
    if (!desc || ni < 0 || ni >= g_game.net_object_count) return;
    uint8_t ot = g_game.net_objects[ni].obj_type;
    desc->half_extents = (Vec3){ hx, hy, hz };
    desc->radius = hx;
    desc->hull_point_count = 0;
    desc->hull_points_ext = NULL;
    if (ot == 3) {
        body_desc_apply_wedge(desc, hx, hy, hz);
        return;
    }
    desc->collider = collider_for_obj_type(ot);
    uint32_t mid = g_game.net_objects[ni].mesh_id;
    uint8_t mc = g_game.net_objects[ni].mesh_collider;
    if (!mid) return;
    if (mc == MESH_COLLIDER_SPHERE) {
        float r = hx;
        if (hy > r) r = hy;
        if (hz > r) r = hz;
        desc->collider = COLLIDER_SPHERE;
        desc->radius = r;
        return;
    }
    int budget = 0;
    if (mc == MESH_COLLIDER_LOW) budget = 8;
    else if (mc == MESH_COLLIDER_MED) budget = 32;
    else if (mc == MESH_COLLIDER_HIGH) budget = 128;
    if (budget < 8) return;
    GPUMesh* mesh = catalog_mesh_get((int)mid);
    if (!mesh) return;
    int n = sample_catalog_mesh_hull(mesh, g_game.net_objects[ni].size[0],
                                     g_game.net_objects[ni].size[1],
                                     g_game.net_objects[ni].size[2],
                                     budget, g_mesh_hull_scratch);
    if (n < 4) return;
    desc->collider = COLLIDER_HULL;
    desc->hull_point_count = n;
    desc->hull_points_ext = g_mesh_hull_scratch;
}

static void net_brick_rebuild_collision(int ni);

static void pending_mesh_add(uint32_t oid, uint32_t mesh_id, uint8_t collider) {
    if (!mesh_id) return;
    if (collider >= MESH_COLLIDER_COUNT) collider = MESH_COLLIDER_CUBE;
    for (int i = 0; i < g_pend_mesh_n; i++) {
        if (g_pend_mesh_oid[i] == oid) {
            g_pend_mesh_val[i] = mesh_id;
            g_pend_mesh_col[i] = collider;
            return;
        }
    }
    if (g_pend_mesh_n >= PW_MAX_PENDING_MESH) return;
    int n = g_pend_mesh_n++;
    g_pend_mesh_oid[n] = oid;
    g_pend_mesh_val[n] = mesh_id;
    g_pend_mesh_col[n] = collider;
}

static uint32_t pending_mesh_take(uint32_t oid, uint8_t* out_col) {
    for (int i = 0; i < g_pend_mesh_n; i++) {
        if (g_pend_mesh_oid[i] != oid) continue;
        uint32_t v = g_pend_mesh_val[i];
        if (out_col) *out_col = g_pend_mesh_col[i];
        int last = g_pend_mesh_n - 1;
        g_pend_mesh_oid[i] = g_pend_mesh_oid[last];
        g_pend_mesh_val[i] = g_pend_mesh_val[last];
        g_pend_mesh_col[i] = g_pend_mesh_col[last];
        g_pend_mesh_n--;
        return v;
    }
    return 0;
}

static void apply_net_part_mesh(uint32_t oid, uint32_t mesh_id, uint8_t collider) {
    if (!mesh_id) return;
    if (collider >= MESH_COLLIDER_COUNT) collider = MESH_COLLIDER_CUBE;
    int ni = -1;
    for (int i = 0; i < g_game.net_object_count; i++) {
        if (g_game.net_objects[i].net_id == oid) { ni = i; break; }
    }
    if (ni < 0) {
        pending_mesh_add(oid, mesh_id, collider);
        catalog_mesh_get((int)mesh_id);
        return;
    }
    g_game.net_objects[ni].mesh_id = mesh_id;
    g_game.net_objects[ni].mesh_collider = collider;
    GPUMesh* m = catalog_mesh_get((int)mesh_id);
    if (m) apply_catalog_mesh_to_net(oid, mesh_id, m);
    else {
        Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
        if (ent) {
            ent->static_batch = false;
            ent->render_batched = false;
        }
    }
    if (collider == MESH_COLLIDER_SPHERE)
        net_brick_rebuild_collision(ni);
}

static void apply_decal_tex_to_waiting(int tex_id, unsigned int tex) {
    for (int i = 0; i < g_net_decal_n; i++) {
        if (!g_net_decals[i].active || g_net_decals[i].tex_id != tex_id) continue;
        Entity* ent = scene_get_entity(&g_game.scene, g_net_decals[i].entity);
        if (ent) {
            ent->material.texture_id = tex;
            ent->material.texture_mode = 5;
        }
    }
}

static void on_catalog_decal_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    intptr_t packed = (intptr_t)user;
    int id = (int)(packed & 0x3fffffff);
    int stage = (int)((packed >> 30) & 3);
    int slot = -1;
    for (int i = 0; i < g_cat_tex_n; i++) {
        if (g_cat_tex[i].id == id) { slot = i; break; }
    }
    if (slot < 0) return;
    g_cat_tex[slot].loading = false;
    if (!data || len == 0) {
        if (stage < 2) {
            const char* host = g_game.host[0] ? g_game.host : "https://polyworld.games";
            const char* fmt = (stage == 0) ? "%s/uploads/shirts/%d.png"
                                           : "%s/uploads/catalog_previews/%d.png";
            char url[256];
            snprintf(url, sizeof(url), fmt, host, id);
            g_cat_tex[slot].loading = true;
            platform_load_file(url, on_catalog_decal_loaded,
                               (void*)(intptr_t)(id | ((stage + 1) << 30)));
            return;
        }
        g_cat_tex[slot].failed = true;
        return;
    }
    extern unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
    extern void stbi_image_free(void*);
    int w = 0, h = 0, c = 0;
    unsigned char* px = stbi_load_from_memory(data, (int)len, &w, &h, &c, 4);
    if (!px) { g_cat_tex[slot].failed = true; return; }
    g_cat_tex[slot].tex = texture_load_from_memory(px, w, h, 4);
    stbi_image_free(px);
    texture_set_overlay_sampling(g_cat_tex[slot].tex);
    g_cat_tex[slot].ready = g_cat_tex[slot].tex != 0;
    if (g_cat_tex[slot].ready)
        apply_decal_tex_to_waiting(id, g_cat_tex[slot].tex);
    (void)path;
}

static unsigned int catalog_decal_tex(int id) {
    if (id <= 0) return 0;
    for (int i = 0; i < g_cat_tex_n; i++) {
        if (g_cat_tex[i].id != id) continue;
        return g_cat_tex[i].ready ? g_cat_tex[i].tex : 0;
    }
    if (g_cat_tex_n >= PW_CATALOG_TEX_CACHE) return 0;
    int slot = g_cat_tex_n++;
    memset(&g_cat_tex[slot], 0, sizeof(g_cat_tex[slot]));
    g_cat_tex[slot].id = id;
    g_cat_tex[slot].loading = true;
    const char* host = g_game.host[0] ? g_game.host : "https://polyworld.games";
    char url[256];
    snprintf(url, sizeof(url), "%s/uploads/decals/%d.png", host, id);
    platform_load_file(url, on_catalog_decal_loaded, (void*)(intptr_t)id);
    return 0;
}

static void spawn_net_decal(uint32_t parent_id, uint32_t tex_id, uint8_t mode, uint8_t face,
                            float tile_x, float tile_y) {
    int pni = -1;
    for (int i = 0; i < g_game.net_object_count; i++) {
        if (g_game.net_objects[i].net_id == parent_id) { pni = i; break; }
    }
    if (pni < 0) {
        if (g_pend_dec_n >= PW_MAX_PENDING_DECAL) return;
        int n = g_pend_dec_n++;
        g_pend_dec_parent[n] = parent_id;
        g_pend_dec_tex[n] = tex_id;
        g_pend_dec_mode[n] = mode;
        g_pend_dec_face[n] = face;
        g_pend_dec_tx[n] = tile_x;
        g_pend_dec_ty[n] = tile_y;
        catalog_decal_tex((int)tex_id);
        return;
    }
    if (g_net_decal_n >= PW_MAX_NET_DECALS) return;
    EntityID eid = scene_create_entity(&g_game.scene);
    Entity* ent = scene_get_entity(&g_game.scene, eid);
    if (!ent) return;
    float sx = g_game.net_objects[pni].size[0];
    float sy = g_game.net_objects[pni].size[1];
    float sz = g_game.net_objects[pni].size[2];
    if (sx < 0.1f) sx = 0.1f;
    if (sy < 0.1f) sy = 0.1f;
    if (sz < 0.1f) sz = 0.1f;
    float epsx = 0.02f / sx, epsy = 0.02f / sy, epsz = 0.02f / sz;
    Vec3 lp = {0, 0, 0.5f + epsz};
    Vec3 lr = {0, 0, 0};
    float fu = sx, fv = sy;
    switch (face) {
        case 1: lp = (Vec3){0, 0, -0.5f - epsz}; lr = (Vec3){0, 180, 0}; fu = sx; fv = sy; break;
        case 5: lp = (Vec3){0.5f + epsx, 0, 0}; lr = (Vec3){0, 90, 0}; fu = sz; fv = sy; break;
        case 4: lp = (Vec3){-0.5f - epsx, 0, 0}; lr = (Vec3){0, -90, 0}; fu = sz; fv = sy; break;
        case 2: lp = (Vec3){0, 0.5f + epsy, 0}; lr = (Vec3){-90, 0, 0}; fu = sx; fv = sz; break;
        case 3: lp = (Vec3){0, -0.5f - epsy, 0}; lr = (Vec3){90, 0, 0}; fu = sx; fv = sz; break;
        default: break;
    }
    if (tile_x < 0.05f) tile_x = 1.0f;
    if (tile_y < 0.05f) tile_y = 1.0f;
    if (tile_x > 64.0f) tile_x = 64.0f;
    if (tile_y > 64.0f) tile_y = 64.0f;
    GPUMesh* q = (mode == 1) ? client_decal_quad_uv(fu * tile_x, fv * tile_y) : client_decal_quad();
    ent->mesh = q;
    ent->transform.position = lp;
    ent->transform.rotation = lr;
    ent->transform.scale = (Vec3){1, 1, 1};
    ent->material.color = (Vec3){1, 1, 1};
    ent->material.alpha = 1.0f;
    ent->material.texture_id = catalog_decal_tex((int)tex_id);
    ent->material.texture_mode = 5;
    ent->parent = g_game.net_objects[pni].entity;
    ent->new_object = true;
    ent->physics_body = 0;
    ent->static_batch = false;
    int di = g_net_decal_n++;
    g_net_decals[di].parent_id = parent_id;
    g_net_decals[di].entity = eid;
    g_net_decals[di].tex_id = (int)tex_id;
    g_net_decals[di].mode = mode;
    g_net_decals[di].face = face;
    g_net_decals[di].active = true;
}

static void flush_pending_decals_for(uint32_t parent_id) {
    for (int i = 0; i < g_pend_dec_n; ) {
        if (g_pend_dec_parent[i] != parent_id) { i++; continue; }
        uint32_t tex = g_pend_dec_tex[i];
        uint8_t mode = g_pend_dec_mode[i];
        uint8_t face = g_pend_dec_face[i];
        float tx = g_pend_dec_tx[i];
        float ty = g_pend_dec_ty[i];
        int last = g_pend_dec_n - 1;
        g_pend_dec_parent[i] = g_pend_dec_parent[last];
        g_pend_dec_tex[i] = g_pend_dec_tex[last];
        g_pend_dec_mode[i] = g_pend_dec_mode[last];
        g_pend_dec_face[i] = g_pend_dec_face[last];
        g_pend_dec_tx[i] = g_pend_dec_tx[last];
        g_pend_dec_ty[i] = g_pend_dec_ty[last];
        g_pend_dec_n--;
        spawn_net_decal(parent_id, tex, mode, face, tx, ty);
    }
}

static void destroy_net_decals_for(uint32_t parent_id) {
    for (int i = 0; i < g_net_decal_n; ) {
        if (!g_net_decals[i].active || g_net_decals[i].parent_id != parent_id) { i++; continue; }
        scene_destroy_entity(&g_game.scene, g_net_decals[i].entity);
        g_net_decals[i] = g_net_decals[g_net_decal_n - 1];
        g_net_decal_n--;
    }
}

static void clear_catalog_visual_caches(void) {
    for (int i = 0; i < g_cat_mesh_n; i++) {
        if (g_cat_mesh[i].ready) mesh_gpu_free(&g_cat_mesh[i].mesh);
    }
    g_cat_mesh_n = 0;
    g_cat_tex_n = 0;
    g_net_decal_n = 0;
    g_pend_mesh_n = 0;
    g_pend_dec_n = 0;
}

static ColliderType collider_for_obj_type(uint8_t obj_type) {
    if (obj_type == 1) return COLLIDER_SPHERE;
    if (obj_type == 2) return COLLIDER_CYLINDER;
    return COLLIDER_BOX;
}

#define PW_MAX_CLIENT_PARTS 256
typedef struct {
    int used;
    EntityID entity;
    char name[64];
    uint8_t shape;
    uint8_t can_collide;
} ClientLocalPartRec;
static ClientLocalPartRec g_client_local_parts[PW_MAX_CLIENT_PARTS];

static void apply_local_avatar(void);
static void refresh_local_avatar_meshes(void);
static void on_avatar_texture_loaded(const char* path, const uint8_t* data, size_t len, void* user);
static void ensure_avatar_bodies_loaded(void);
static void on_guest_avatar_texture_loaded(const char* path, const uint8_t* data, size_t len, void* user);
static void clear_game_world(void);
#ifndef __EMSCRIPTEN__
#ifdef VR
static void enter_vr_hub(LoginScreen* ls);
#endif
static void consume_login_play(LoginScreen* ls);
static void return_to_games_menu(void) {
    if (g_game.login_screen.game_id == 0)
        g_game.login_screen.game_id = 6;
    login_screen_init(&g_game.login_screen);
    g_game.show_login = true;
    g_game.login_screen.phase = 1;
    g_game.login_screen.ready_to_play = false;
    g_game.login_screen.detail_fetched = false;
    g_game.login_screen.detail_loading = false;
    g_game.login_screen.games_fetched = false;
    g_game.login_screen.games_loading = false;
    g_game.login_screen.games_scroll_y = 0.0f;
    g_game.login_screen.games_scroll_target = 0.0f;
    g_game.login_screen.sel_draw_valid = false;
    g_game.login_screen.selected_game = 0;
    if (g_game.session_token[0]) {
        strncpy(g_game.login_screen.session_token, g_game.session_token,
                sizeof(g_game.login_screen.session_token) - 1);
        g_game.login_screen.logged_in = true;
        if (g_game.username[0]) {
            strncpy(g_game.login_screen.username, g_game.username,
                    sizeof(g_game.login_screen.username) - 1);
            g_game.login_screen.username_len = (int)strlen(g_game.login_screen.username);
        }
    } else {
        g_game.login_screen.session_token[0] = '\0';
        g_game.login_screen.logged_in = true;
    }
#ifdef VR
    if (g_game.vr.active && g_game.login_screen.phase >= 1) {
        enter_vr_hub(&g_game.login_screen);
        return;
    }
#endif
    if (!platform_was_resized_by_user())
        platform_set_window_size(1091, 711);
}

static void leave_game_ui(void) {
    clear_game_world();
    if (g_game.studio_playtest) {
        platform_request_close();
        return;
    }
    return_to_games_menu();
}
static void open_avatar_editor_ui(void);
static void open_catalog_ui(void);
static void poll_avatar_editor_save(void);
void avatar_editor_mouseup_bridge(void);
bool avatar_editor_mouse_bridge(int x, int y, int button, int pressed);
bool avatar_editor_scroll_bridge(float x, float y, float delta);
#endif
void spawn_explosion(float x, float y, float z, float radius);
static void explosion_update(float dt);
static void explosion_push_lights(void);
static void explosion_draw(const Mat4* view, const Mat4* projection);
static void explosion_draw_flash(void);
static void on_explosion_sheet_loaded(const char* path, const uint8_t* data, size_t len, void* user);
void player_respawn(void);
static void on_tool_hold_obj_loaded(const char* path, const uint8_t* data, size_t len, void* user);
static void on_tool_hold_tex_loaded(const char* path, const uint8_t* data, size_t len, void* user);
static void on_tool_icon_loaded(const char* path, const uint8_t* data, size_t len, void* user);
static void on_world_loaded(const char* path, const uint8_t* data, size_t len, void* user);
static void freecam_init_from_camera(void);
static void ensure_avatar_bodies_loaded(void);
static void on_guest_avatar_texture_loaded(const char* path, const uint8_t* data, size_t len, void* user);
static void free_world_init_stream(void);

typedef struct {
    unsigned int* tex;
    int* out_w;
    int* out_h;
    int flip_y;
} PwPngLoadCtx;

static void on_pw_png_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    PwPngLoadCtx* ctx = (PwPngLoadCtx*)user;
    (void)path;
    if (!ctx || !ctx->tex || !data || len == 0) return;
    extern unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
    extern void stbi_image_free(void*);
    extern void stbi_set_flip_vertically_on_load(int);
    int w = 0, h = 0, c = 0;
    stbi_set_flip_vertically_on_load(ctx->flip_y);
    unsigned char* px = stbi_load_from_memory(data, (int)len, &w, &h, &c, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!px) return;
    *ctx->tex = texture_load_from_memory(px, w, h, 4);
    if (ctx->out_w) *ctx->out_w = w;
    if (ctx->out_h) *ctx->out_h = h;
    stbi_image_free(px);
}

static void pw_load_png(const char* path, unsigned int* tex, int flip_y) {
    if (!path || !tex) return;
    PwPngLoadCtx ctx = { tex, NULL, NULL, flip_y };
    platform_load_file(path, on_pw_png_loaded, &ctx);
}

static void pw_load_png_sized(const char* path, unsigned int* tex, int* out_w, int* out_h, int flip_y) {
    if (!path || !tex) return;
    PwPngLoadCtx ctx = { tex, out_w, out_h, flip_y };
    platform_load_file(path, on_pw_png_loaded, &ctx);
}

static void on_explosion_sheet_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    (void)path;
    (void)user;
    if (!data || len == 0) return;
    extern unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
    extern void stbi_image_free(void*);
    extern void stbi_set_flip_vertically_on_load(int);
    int w = 0, h = 0, c = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* px = stbi_load_from_memory(data, (int)len, &w, &h, &c, 4);
    if (!px) return;
    uint32_t tex = texture_load_atlas_from_memory(px, w, h, 4);
    stbi_image_free(px);
    if (!tex) return;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    g_game.explosion_tex = tex;
}

typedef struct {
    bool active;
    float timer;
    PhysicsBodyID bodies[AVATAR_PART_COUNT];
    Mat4 mesh_from_body[AVATAR_PART_COUNT];
    PhysicsBodyID acc_bodies[PW_MAX_EQUIPPED_ACCESSORIES][ACCESSORY_MAX_PARTS];
    Mat4 acc_mesh_from_body[PW_MAX_EQUIPPED_ACCESSORIES][ACCESSORY_MAX_PARTS];
} RagdollState;

static void ragdoll_destroy(RagdollState* rd) {
    if (!rd) return;
    for (int i = 0; i < AVATAR_PART_COUNT; i++) {
        if (rd->bodies[i]) {
            physics_destroy_body(g_game.physics, rd->bodies[i]);
            rd->bodies[i] = 0;
        }
    }
    for (int ai = 0; ai < PW_MAX_EQUIPPED_ACCESSORIES; ai++) {
        for (int i = 0; i < ACCESSORY_MAX_PARTS; i++) {
            if (rd->acc_bodies[ai][i]) {
                physics_destroy_body(g_game.physics, rd->acc_bodies[ai][i]);
                rd->acc_bodies[ai][i] = 0;
            }
        }
    }
    rd->active = false;
}

static Mat4 ragdoll_part_matrix(const RagdollState* rd, int part_index) {
    if (!rd || !rd->bodies[part_index]) return mat4_identity();
    Mat4 body = physics_get_transform_mat4(g_game.physics, rd->bodies[part_index]);
    return mat4_multiply(body, rd->mesh_from_body[part_index]);
}

static Vec3 mesh_local_centroid(const GPUMesh* mesh) {
    if (mesh && mesh->cpu_positions && mesh->cpu_vertex_count > 0) {
        double sx = 0.0, sy = 0.0, sz = 0.0;
        for (size_t v = 0; v < mesh->cpu_vertex_count; v++) {
            sx += mesh->cpu_positions[v * 3 + 0];
            sy += mesh->cpu_positions[v * 3 + 1];
            sz += mesh->cpu_positions[v * 3 + 2];
        }
        float n = (float)mesh->cpu_vertex_count;
        return (Vec3){ (float)(sx / n), (float)(sy / n), (float)(sz / n) };
    }
    if (mesh) {
        return (Vec3){
            0.5f * (mesh->aabb_min[0] + mesh->aabb_max[0]),
            0.5f * (mesh->aabb_min[1] + mesh->aabb_max[1]),
            0.5f * (mesh->aabb_min[2] + mesh->aabb_max[2]),
        };
    }
    return (Vec3){0, 0, 0};
}

static Mat4 mat4_rotation_only(Mat4 m) {
    Vec3 x = vec3_normalize((Vec3){ m.m[0], m.m[1], m.m[2] });
    Vec3 y = vec3_normalize((Vec3){ m.m[4], m.m[5], m.m[6] });
    Vec3 z = vec3_normalize(vec3_cross(x, y));
    y = vec3_normalize(vec3_cross(z, x));
    Mat4 r = mat4_identity();
    r.m[0] = x.x; r.m[1] = x.y; r.m[2] = x.z;
    r.m[4] = y.x; r.m[5] = y.y; r.m[6] = y.z;
    r.m[8] = z.x; r.m[9] = z.y; r.m[10] = z.z;
    return r;
}

static int fill_hull_from_mesh(const GPUMesh* mesh, Mat4 mesh_from_body,
                               Vec3* out, int max_out) {
    if (!out || max_out < 3) return 0;
    int n = 0;
    if (mesh && mesh->cpu_positions && mesh->cpu_vertex_count > 0) {
        size_t vc = mesh->cpu_vertex_count;
        size_t step = vc / (size_t)max_out;
        if (step < 1) step = 1;
        for (size_t v = 0; v < vc && n < max_out; v += step) {
            Vec4 p = mat4_mul_vec4(mesh_from_body, (Vec4){
                mesh->cpu_positions[v * 3 + 0],
                mesh->cpu_positions[v * 3 + 1],
                mesh->cpu_positions[v * 3 + 2],
                1.0f
            });
            out[n++] = (Vec3){ p.x, p.y, p.z };
        }
        if (n < max_out && vc > 1) {
            size_t v = vc - 1;
            Vec4 p = mat4_mul_vec4(mesh_from_body, (Vec4){
                mesh->cpu_positions[v * 3 + 0],
                mesh->cpu_positions[v * 3 + 1],
                mesh->cpu_positions[v * 3 + 2],
                1.0f
            });
            out[n++] = (Vec3){ p.x, p.y, p.z };
        }
    }
    if (n < 3 && mesh) {
        float xs[2] = { mesh->aabb_min[0], mesh->aabb_max[0] };
        float ys[2] = { mesh->aabb_min[1], mesh->aabb_max[1] };
        float zs[2] = { mesh->aabb_min[2], mesh->aabb_max[2] };
        n = 0;
        for (int ix = 0; ix < 2 && n < max_out; ix++)
        for (int iy = 0; iy < 2 && n < max_out; iy++)
        for (int iz = 0; iz < 2 && n < max_out; iz++) {
            Vec4 p = mat4_mul_vec4(mesh_from_body, (Vec4){ xs[ix], ys[iy], zs[iz], 1.0f });
            out[n++] = (Vec3){ p.x, p.y, p.z };
        }
    }
    return n;
}

static void ragdoll_spawn(RagdollState* rd, AvatarAnim* anim, Accessory* accs, Vec3 pos, float yaw,
                          Vec3 inherit_vel) {
    if (!rd || rd->active) return;
    rd->active = true;
    rd->timer = 4.0f;
    float s = AVATAR_SCALE;

    float part_sizes[][3] = {
        {1.25f*s, 1.25f*s, 1.25f*s},
        {2.0f*s*0.5f, 4.0f*s*0.5f, 1.0f*s*0.5f},
        {1.0f*s*0.5f, 4.0f*s*0.5f, 1.0f*s*0.5f},
        {1.0f*s*0.5f, 4.0f*s*0.5f, 1.0f*s*0.5f},
        {1.0f*s*0.5f, 4.0f*s*0.5f, 1.0f*s*0.5f},
        {1.0f*s*0.5f, 4.0f*s*0.5f, 1.0f*s*0.5f},
    };
    const float limb_inflate = 0.04f;

    for (int i = 0; i < AVATAR_PART_COUNT; i++) {
        Mat4 part_world = mat4_identity();
        if (anim) {
            part_world = avatar_anim_get_part_matrix(anim, i, pos, yaw, AVATAR_SCALE);
        } else {
            part_world = mat4_multiply(mat4_translate(pos),
                         mat4_multiply(mat4_rotate_y(yaw),
                         mat4_scale((Vec3){s, s, s})));
        }

        Vec3 center = avatar_anim_get_part_center(i);
        Vec4 cw = mat4_mul_vec4(part_world, (Vec4){center.x, center.y, center.z, 1.0f});
        Vec3 spawn = (Vec3){cw.x, cw.y, cw.z};

        if (i == ANIM_PART_HEAD) {
            spawn.y += 0.6f;
        }

        BodyDesc desc = {0};
        desc.type = BODY_DYNAMIC;
        desc.position = (Vec3){spawn.x, spawn.y, spawn.z};
        desc.mass = (i == ANIM_PART_HEAD) ? 1.0f : 2.0f;
        desc.restitution = (i == ANIM_PART_HEAD) ? 0.55f : 0.3f;
        desc.friction = (i == ANIM_PART_HEAD) ? 0.4f : 0.8f;

        Mat4 body_rot = (i == ANIM_PART_HEAD)
            ? mat4_rotate_y(yaw)
            : mat4_rotation_only(part_world);

        if (i == ANIM_PART_HEAD) {
            desc.collider = COLLIDER_SPHERE;
            desc.radius = 1.25f * s;
        } else {
            desc.collider = COLLIDER_BOX;
            desc.half_extents = (Vec3){
                part_sizes[i][0] + limb_inflate,
                part_sizes[i][1] + limb_inflate,
                part_sizes[i][2] + limb_inflate
            };
        }

        rd->bodies[i] = physics_create_body(g_game.physics, &desc);
        if (i == ANIM_PART_HEAD)
            physics_set_rotation_euler(g_game.physics, rd->bodies[i], (Vec3){0, yaw, 0});
        else
            physics_set_rotation_mat4(g_game.physics, rd->bodies[i], body_rot);
        physics_set_never_disable(g_game.physics, rd->bodies[i]);
        physics_unlock_rotation(g_game.physics, rd->bodies[i]);

        physics_set_geom_bits(g_game.physics, rd->bodies[i], 0x2, ~0x1UL);

        Mat4 body_world = mat4_multiply(mat4_translate(spawn), body_rot);
        rd->mesh_from_body[i] = mat4_multiply(mat4_inverse(body_world), part_world);

        Vec3 angular = {
            (float)(rand() % 80 - 40) * 0.08f,
            (float)(rand() % 80 - 40) * 0.08f,
            (float)(rand() % 80 - 40) * 0.08f
        };
        physics_set_velocity(g_game.physics, rd->bodies[i], inherit_vel);
        physics_set_angular_velocity(g_game.physics, rd->bodies[i], angular);
    }

    for (int ai = 0; ai < PW_MAX_EQUIPPED_ACCESSORIES; ai++) {
        Accessory* acc = accs ? &accs[ai] : NULL;
        for (int i = 0; i < ACCESSORY_MAX_PARTS; i++) {
            rd->acc_bodies[ai][i] = 0;
            if (!acc || !acc->loaded || !acc->parts[i].valid) continue;
            int attach = acc->parts[i].attach_part;
            if (attach < 0 || attach >= AVATAR_PART_COUNT) continue;

            Mat4 part_world = anim
                ? avatar_anim_get_part_matrix(anim, attach, pos, yaw, AVATAR_SCALE)
                : mat4_multiply(mat4_translate(pos),
                    mat4_multiply(mat4_rotate_y(yaw), mat4_scale((Vec3){s, s, s})));

            Vec3 local_com = mesh_local_centroid(&acc->parts[i].mesh);
            Vec4 cw = mat4_mul_vec4(part_world,
                (Vec4){ local_com.x, local_com.y, local_com.z, 1.0f });
            Vec3 spawn = (Vec3){ cw.x, cw.y, cw.z };

            Mat4 body_rot = mat4_rotation_only(part_world);
            Mat4 body_world = mat4_multiply(mat4_translate(spawn), body_rot);
            Mat4 mesh_from_body = mat4_multiply(mat4_inverse(body_world), part_world);

            BodyDesc desc = {0};
            desc.type = BODY_DYNAMIC;
            desc.position = spawn;
            desc.mass = 0.3f;
            desc.restitution = 0.5f;
            desc.friction = 0.35f;
            Vec3 hull_pts[64];
            desc.hull_point_count = fill_hull_from_mesh(
                &acc->parts[i].mesh, mesh_from_body,
                hull_pts, 64);
            if (desc.hull_point_count >= 3) {
                desc.collider = COLLIDER_HULL;
                desc.hull_points_ext = hull_pts;
            } else {
                desc.collider = COLLIDER_SPHERE;
                desc.radius = 0.2f;
            }

            rd->acc_bodies[ai][i] = physics_create_body(g_game.physics, &desc);
            if (!rd->acc_bodies[ai][i]) continue;
            physics_set_rotation_mat4(g_game.physics, rd->acc_bodies[ai][i], body_rot);
            physics_set_never_disable(g_game.physics, rd->acc_bodies[ai][i]);
            physics_unlock_rotation(g_game.physics, rd->acc_bodies[ai][i]);
            physics_set_geom_bits(g_game.physics, rd->acc_bodies[ai][i], 0x4, ~(0x1UL | 0x2UL));
            rd->acc_mesh_from_body[ai][i] = mesh_from_body;

            Vec3 angular = {
                (float)(rand() % 200 - 100) * 0.12f,
                (float)(rand() % 200 - 100) * 0.12f,
                (float)(rand() % 200 - 100) * 0.12f
            };
            physics_set_velocity(g_game.physics, rd->acc_bodies[ai][i], inherit_vel);
            physics_set_angular_velocity(g_game.physics, rd->acc_bodies[ai][i], angular);
        }
    }
}

static void ragdoll_update(RagdollState* rd, float dt, bool keep_alive) {
    if (!rd || !rd->active) return;
    rd->timer -= dt;
    if (rd->timer <= 0.0f || !keep_alive) {
        ragdoll_destroy(rd);
        return;
    }

    const float world_g = -39.24f;
    const float debris_g = -190.0f;
    const float extra = debris_g - world_g;
    for (int i = 0; i < AVATAR_PART_COUNT; i++) {
        if (!rd->bodies[i]) continue;
        Vec3 v = physics_get_velocity(g_game.physics, rd->bodies[i]);
        v.y += extra * dt;
        physics_set_velocity(g_game.physics, rd->bodies[i], v);
    }
    for (int ai = 0; ai < PW_MAX_EQUIPPED_ACCESSORIES; ai++) {
        for (int i = 0; i < ACCESSORY_MAX_PARTS; i++) {
            if (!rd->acc_bodies[ai][i]) continue;
            Vec3 v = physics_get_velocity(g_game.physics, rd->acc_bodies[ai][i]);
            v.y += extra * dt;
            physics_set_velocity(g_game.physics, rd->acc_bodies[ai][i], v);
        }
    }
}

#ifndef __EMSCRIPTEN__
static void paint_loading_now(const char* title);
static void game_busy_redraw(void);
#endif

static bool game_init(void) {
#ifdef VR
    vr_session_shutdown(&g_game.vr);
#endif
    bool was_loading = g_game.loading_world;
    bool was_multiplayer = g_game.multiplayer;
    bool was_show_login = g_game.show_login;
    bool was_studio_playtest = g_game.studio_playtest;
    char was_session[sizeof(g_game.session_token)];
    char was_user[sizeof(g_game.username)];
    memcpy(was_session, g_game.session_token, sizeof(was_session));
    memcpy(was_user, g_game.username, sizeof(was_user));
    memset(&g_game, 0, sizeof(GameState));
    g_game.loading_world = was_loading;
    g_game.multiplayer = was_multiplayer;
    g_game.show_login = was_show_login;
    memcpy(g_game.session_token, was_session, sizeof(g_game.session_token));
    memcpy(g_game.username, was_user, sizeof(g_game.username));
    g_game.studio_playtest = was_studio_playtest;
    g_game.skin_color = (Vec3){0.918f, 0.918f, 0.918f};
#ifdef __ANDROID__
    g_game.ui_scale = 2.0f;
#else
    g_game.ui_scale = 1.0f;
#endif
    g_game.host = pw_site_origin();
    emote_clip_set_host(g_game.host);
    g_game.reset_enabled = true;
    g_game.allow_freecam = true;

    input_init();

    int canvas_w = 1280, canvas_h = 720;
#ifdef __EMSCRIPTEN__
    {
        double css_w, css_h;
        emscripten_get_element_css_size("#canvas", &css_w, &css_h);
        double dpr = emscripten_get_device_pixel_ratio();
        canvas_w = (int)(css_w * dpr);
        canvas_h = (int)(css_h * dpr);
    }
#elif defined(__ANDROID__)
    platform_get_window_size(&canvas_w, &canvas_h);
    if (canvas_w < 1) canvas_w = 1280;
    if (canvas_h < 1) canvas_h = 720;
#else
    if (g_host_canvas_w > 0 && g_host_canvas_h > 0) {
        canvas_w = g_host_canvas_w;
        canvas_h = g_host_canvas_h;
    } else if (g_game.show_login) { canvas_w = 1091; canvas_h = 711; }
#endif

#ifdef PW_STUDIO_HOST
    pw_studio_host_busy_redraw();
#endif
    if (!renderer_init(&g_game.renderer, canvas_w, canvas_h, false)) {
        PW_ERR(ERR_GENERIC, "Renderer init failed\n");
        return false;
    }
#ifdef PW_STUDIO_HOST
    pw_studio_host_busy_redraw();
#endif

    g_game.physics = physics_create((Vec3){0.0f, -39.24f, 0.0f});
    if (!g_game.physics) {
        PW_ERR(ERR_GENERIC, "Physics init failed\n");
        return false;
    }

    camera_init(&g_game.camera);

    memset(&g_game.scene, 0, sizeof(Scene));

    if (!g_game.loading_world && !g_game.show_login) {

    EntityID baseplate_id = scene_create_entity(&g_game.scene);
    Entity* baseplate = scene_get_entity(&g_game.scene, baseplate_id);
    if (baseplate) {
        baseplate->transform.position = (Vec3){0.0f, -BASEPLATE_HALF_Y, 0.0f};
        baseplate->transform.scale = (Vec3){1.0f, 1.0f, 1.0f};
        baseplate->material.color = (Vec3){0.3f, 0.7f, 0.3f};
        baseplate->material.surfaces[0] = SURFACE_STUD;
        baseplate->material.surfaces[1] = SURFACE_INLET;
        baseplate->material.surfaces[2] = SURFACE_SMOOTH;
        baseplate->material.surfaces[3] = SURFACE_SMOOTH;
        baseplate->material.surfaces[4] = SURFACE_SMOOTH;
        baseplate->material.surfaces[5] = SURFACE_SMOOTH;

        MeshData baseplate_mesh;
        if (create_box_mesh(&baseplate_mesh, BASEPLATE_HALF_X, BASEPLATE_HALF_Y, BASEPLATE_HALF_Z)) {
            static GPUMesh baseplate_gpu;
            if (mesh_upload(&baseplate_mesh, &baseplate_gpu)) {
                baseplate->mesh = &baseplate_gpu;
            }
            mesh_data_free(&baseplate_mesh);
        }

        BodyDesc bp_desc = {
            .type = BODY_STATIC,
            .collider = COLLIDER_BOX,
            .position = {0.0f, -BASEPLATE_HALF_Y, 0.0f},
            .half_extents = {BASEPLATE_HALF_X, BASEPLATE_HALF_Y, BASEPLATE_HALF_Z},
            .mass = 0.0f,
            .restitution = 0.5f,
            .friction = 1.0f
        };
        baseplate->physics_body = physics_create_body(g_game.physics, &bp_desc);
    }

    MeshData cube_mesh;
    if (create_box_mesh(&cube_mesh, CUBE_HALF_SIZE, CUBE_HALF_SIZE, CUBE_HALF_SIZE)) {
        if (mesh_upload(&cube_mesh, &g_game.cube_gpu_mesh)) {
            Vec3 cube_colors[] = {
                {0.9f, 0.2f, 0.2f},
                {0.2f, 0.4f, 0.9f},
                {0.9f, 0.8f, 0.1f},
                {0.8f, 0.3f, 0.8f},
            };
            Vec3 cube_positions[] = {
                {3.0f, CUBE_HALF_SIZE + 0.5f + 0.01f, 2.0f},
                {-2.0f, CUBE_HALF_SIZE + 0.5f + 0.01f, -3.0f},
                {5.0f, CUBE_HALF_SIZE + 0.5f + 0.01f, -1.0f},
                {-4.0f, CUBE_HALF_SIZE + 0.5f + 0.01f, 4.0f},
            };

            for (int i = 0; i < 4; i++) {
                EntityID cid = scene_create_entity(&g_game.scene);
                g_game.cube_entities[i] = cid;
                Entity* cube = scene_get_entity(&g_game.scene, cid);
                if (cube) {
                    cube->transform.position = cube_positions[i];
                    cube->transform.scale = (Vec3){1.0f, 1.0f, 1.0f};
                    cube->material.color = cube_colors[i];
                    cube->mesh = &g_game.cube_gpu_mesh;

                    cube->material.surfaces[0] = SURFACE_STUD;
                    cube->material.surfaces[1] = SURFACE_INLET;
                    cube->material.surfaces[2] = SURFACE_SMOOTH;
                    cube->material.surfaces[3] = SURFACE_SMOOTH;
                    cube->material.surfaces[4] = SURFACE_SMOOTH;
                    cube->material.surfaces[5] = SURFACE_SMOOTH;

                    BodyDesc cube_desc = {
                        .type = BODY_DYNAMIC,
                        .collider = COLLIDER_BOX,
                        .position = cube_positions[i],
                        .half_extents = {CUBE_HALF_SIZE, CUBE_HALF_SIZE, CUBE_HALF_SIZE},
                        .mass = 5.0f,
                        .restitution = 0.3f,
                        .friction = 1.0f
                    };
                    cube->physics_body = physics_create_body(g_game.physics, &cube_desc);
                }
            }
        }
        mesh_data_free(&cube_mesh);
    }

    }
    if (!g_game.loading_world && !g_game.show_login) {
        g_game.world_ready = true;
        g_game.allow_freecam = true;
        vr_calibrate_on_world_ready();
    }

    avatar_init(&g_game.avatar, &g_game.scene, g_game.physics);

    MeshData avatar_box;
    if (create_box_mesh(&avatar_box, 0.4f, 0.9f, 0.4f)) {
        if (mesh_upload(&avatar_box, &g_game.avatar_gpu_mesh)) {
            Entity* av_ent = scene_get_entity(&g_game.scene, g_game.avatar.entity);
            if (av_ent) {
                av_ent->mesh = &g_game.avatar_gpu_mesh;
                av_ent->material.color = (Vec3){0.2f, 0.6f, 1.0f};
            }
        }
        mesh_data_free(&avatar_box);
    }

    g_game.accumulator = 0.0f;
    g_game.initialized = true;
#ifndef __EMSCRIPTEN__
    platform_set_busy_redraw(game_busy_redraw);
#endif

    audio_init();
    font_init();

    skybox_init(&g_game.skybox);
    {
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
        Vec3 ld = vec3_normalize(g_game.renderer.light_dir);
        Vec3 sun = {-ld.x, -ld.y, -ld.z};
        float sun_yaw = atan2f(sun.x, sun.z);
        float cubemap_sun_yaw = (float)M_PI * 0.5f;
        skybox_set_yaw(&g_game.skybox, (sun_yaw - cubemap_sun_yaw) * (180.0f / (float)M_PI));
    }

    chat_init(&g_game.chat);
    social_init(&g_game.social);

#ifdef __EMSCRIPTEN__
    {
        extern unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
        extern void stbi_image_free(void*);
        extern void stbi_set_flip_vertically_on_load(int);
        FILE* bf = fopen("assets/bubble_nineslice.png", "rb");
        if (bf) {
            fseek(bf, 0, SEEK_END); long bsz = ftell(bf); fseek(bf, 0, SEEK_SET);
            uint8_t* bd = (uint8_t*)malloc(bsz);
            fread(bd, 1, bsz, bf); fclose(bf);
            int bw, bh, bc;
            stbi_set_flip_vertically_on_load(1);
            unsigned char* px = stbi_load_from_memory(bd, (int)bsz, &bw, &bh, &bc, 4);
            stbi_set_flip_vertically_on_load(0);
            free(bd);
            if (px) { g_game.chat.bubble_texture = texture_load_from_memory(px, bw, bh, 4); stbi_image_free(px); }
        }
        bf = fopen("assets/bubble_bottom.png", "rb");
        if (bf) {
            fseek(bf, 0, SEEK_END); long bsz = ftell(bf); fseek(bf, 0, SEEK_SET);
            uint8_t* bd = (uint8_t*)malloc(bsz);
            fread(bd, 1, bsz, bf); fclose(bf);
            int bw, bh, bc;
            stbi_set_flip_vertically_on_load(1);
            unsigned char* px = stbi_load_from_memory(bd, (int)bsz, &bw, &bh, &bc, 4);
            stbi_set_flip_vertically_on_load(0);
            free(bd);
            if (px) { g_game.chat.bubble_bottom_tex = texture_load_from_memory(px, bw, bh, 4); stbi_image_free(px); }
        }
        bf = fopen("assets/dark_nineslice.png", "rb");
        if (bf) {
            fseek(bf, 0, SEEK_END); long bsz = ftell(bf); fseek(bf, 0, SEEK_SET);
            uint8_t* bd = (uint8_t*)malloc(bsz);
            fread(bd, 1, bsz, bf); fclose(bf);
            int bw, bh, bc;
            stbi_set_flip_vertically_on_load(1);
            unsigned char* px = stbi_load_from_memory(bd, (int)bsz, &bw, &bh, &bc, 4);
            stbi_set_flip_vertically_on_load(0);
            free(bd);
            if (px) { g_game.chat.nineslice_tex = texture_load_from_memory(px, bw, bh, 4); stbi_image_free(px); }
        }
        bf = fopen("assets/menu.png", "rb");
        if (bf) {
            fseek(bf, 0, SEEK_END); long bsz = ftell(bf); fseek(bf, 0, SEEK_SET);
            uint8_t* bd = (uint8_t*)malloc(bsz);
            fread(bd, 1, bsz, bf); fclose(bf);
            int bw, bh, bc;
            stbi_set_flip_vertically_on_load(1);
            unsigned char* px = stbi_load_from_memory(bd, (int)bsz, &bw, &bh, &bc, 4);
            stbi_set_flip_vertically_on_load(0);
            free(bd);
            if (px) { g_game.chat.menu_tex = texture_load_from_memory(px, bw, bh, 4); stbi_image_free(px); }
        }
        bf = fopen("assets/chat_unread.png", "rb");
        if (bf) {
            fseek(bf, 0, SEEK_END); long bsz = ftell(bf); fseek(bf, 0, SEEK_SET);
            uint8_t* bd = (uint8_t*)malloc(bsz);
            fread(bd, 1, bsz, bf); fclose(bf);
            int bw, bh, bc;
            stbi_set_flip_vertically_on_load(1);
            unsigned char* px = stbi_load_from_memory(bd, (int)bsz, &bw, &bh, &bc, 4);
            stbi_set_flip_vertically_on_load(0);
            free(bd);
            if (px) { g_game.chat.chat_unread_tex = texture_load_from_memory(px, bw, bh, 4); stbi_image_free(px); }
        }
        bf = fopen("assets/chat_open.png", "rb");
        if (bf) {
            fseek(bf, 0, SEEK_END); long bsz = ftell(bf); fseek(bf, 0, SEEK_SET);
            uint8_t* bd = (uint8_t*)malloc(bsz);
            fread(bd, 1, bsz, bf); fclose(bf);
            int bw, bh, bc;
            stbi_set_flip_vertically_on_load(1);
            unsigned char* px = stbi_load_from_memory(bd, (int)bsz, &bw, &bh, &bc, 4);
            stbi_set_flip_vertically_on_load(0);
            free(bd);
            if (px) { g_game.chat.chat_open_tex = texture_load_from_memory(px, bw, bh, 4); stbi_image_free(px); }
        }
        bf = fopen("assets/chat_closed.png", "rb");
        if (bf) {
            fseek(bf, 0, SEEK_END); long bsz = ftell(bf); fseek(bf, 0, SEEK_SET);
            uint8_t* bd = (uint8_t*)malloc(bsz);
            fread(bd, 1, bsz, bf); fclose(bf);
            int bw, bh, bc;
            stbi_set_flip_vertically_on_load(1);
            unsigned char* px = stbi_load_from_memory(bd, (int)bsz, &bw, &bh, &bc, 4);
            stbi_set_flip_vertically_on_load(0);
            free(bd);
            if (px) { g_game.chat.chat_closed_tex = texture_load_from_memory(px, bw, bh, 4); stbi_image_free(px); }
        }

    }
#else
    {
        pw_load_png("assets/bubble_nineslice.png", &g_game.chat.bubble_texture, 1);
        pw_load_png("assets/bubble_bottom.png", &g_game.chat.bubble_bottom_tex, 1);
        pw_load_png("assets/dark_nineslice.png", &g_game.chat.nineslice_tex, 1);
        pw_load_png("assets/chat_closed.png", &g_game.chat.chat_closed_tex, 1);
        pw_load_png("assets/chat_open.png", &g_game.chat.chat_open_tex, 1);
        pw_load_png("assets/chat_unread.png", &g_game.chat.chat_unread_tex, 1);
        pw_load_png("assets/menu.png", &g_game.chat.menu_tex, 1);
    }
#endif

    {
        pw_load_png("assets/badges/creator.png", &g_game.chat.badge_creator, 1);
        pw_load_png("assets/badges/verified.png", &g_game.chat.badge_verified, 1);
        pw_load_png("assets/badges/shield.png", &g_game.chat.badge_shield, 1);
        pw_load_png("assets/badges/tester.png", &g_game.chat.badge_tester, 1);
        pw_load_png("assets/music.png", &g_game.chat.music_icon_tex, 1);
        pw_load_png_sized("assets/load_bg.png", &g_game.load_bg_tex, &g_game.load_bg_w, &g_game.load_bg_h, 0);
        pw_load_png_sized("assets/polyworld.png", &g_game.logo_tex, &g_game.logo_w, &g_game.logo_h, 0);
        emote_wheel_init(&g_game.emote_wheel);
        pw_load_png("assets/emote_wheel.png", &g_game.emote_wheel.wheel_tex, 1);
        pw_load_png("assets/emote_wheel_select.png", &g_game.emote_wheel.select_tex, 1);
    }

    {
        unsigned int ui_textures[] = {
            g_game.chat.nineslice_tex, g_game.chat.menu_tex,
            g_game.chat.chat_closed_tex, g_game.chat.chat_open_tex,
            g_game.chat.chat_unread_tex, g_game.chat.bubble_texture,
            g_game.chat.bubble_bottom_tex,
            g_game.chat.badge_creator, g_game.chat.badge_verified,
            g_game.chat.badge_shield, g_game.chat.badge_tester,
            g_game.chat.music_icon_tex,
            g_game.load_bg_tex, g_game.logo_tex,
            g_game.emote_wheel.wheel_tex, g_game.emote_wheel.select_tex
        };
        for (int i = 0; i < (int)(sizeof(ui_textures)/sizeof(ui_textures[0])); i++) {
            if (ui_textures[i]) {
                glBindTexture(GL_TEXTURE_2D, ui_textures[i]);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    game_menu_init(&g_game.menu, g_game.ui_scale);
    if (g_game.studio_playtest)
        g_game.menu.studio_playtest = true;
    g_game.menu.nineslice_tex = g_game.chat.nineslice_tex;
    social_set_nineslice(&g_game.social, g_game.chat.nineslice_tex);
    social_set_shaders(&g_game.social,
                       g_game.chat.quad_shader, g_game.chat.quad_u_projection,
                       g_game.chat.quad_u_tex, g_game.chat.quad_u_alpha, g_game.chat.quad_u_tint,
                       g_game.chat.text_vao, g_game.chat.text_vbo);
    g_game.social.ui_scale = g_game.ui_scale;
    g_game.menu.menu_tex = g_game.chat.menu_tex;
    game_menu_set_shaders(g_game.chat.quad_shader, g_game.chat.quad_u_projection,
                          g_game.chat.quad_u_tex, g_game.chat.quad_u_alpha, g_game.chat.quad_u_tint,
                          g_game.chat.text_shader, g_game.chat.u_projection,
                          g_game.chat.u_tex, g_game.chat.u_color,
                          g_game.chat.font_texture, g_game.chat.text_vao, g_game.chat.text_vbo);
    game_menu_load_settings(&g_game.menu);
#if defined(VR) && !defined(__EMSCRIPTEN__)
    vr_openxr_set_comfort(g_game.menu.vr_turn, false);
#endif
    g_fps_limit_setting = g_game.menu.fps_limit;
    g_game.ui_scale = g_game.menu.ui_scale;
    g_game.chat.ui_scale = g_game.ui_scale;
    g_game.social.ui_scale = g_game.ui_scale;
    touch_controls_init();
    brick_batch_init();
    vidactor_init();

    g_game.scripts = client_script_create(&g_game.scene);
    client_script_set_player(g_game.scripts, &g_game.avatar);
    client_script_set_playtest(g_game.scripts, g_game.studio_playtest);
    if (g_game.username[0])
        client_script_set_local_name(g_game.scripts, g_game.username);

    {
        MeshData md;
        if (create_sphere_mesh(&md, 1.0f, 24, 16)) {
            mesh_upload(&md, &g_game.explosion_mesh);
            mesh_data_free(&md);
            g_game.explosion_mesh_ready = true;
        }
        if (create_quad_mesh(&md)) {
            mesh_upload(&md, &g_game.explosion_quad);
            mesh_data_free(&md);
            g_game.explosion_quad_ready = true;
        }
        platform_load_file("assets/fx/explosion.png", on_explosion_sheet_loaded, NULL);
    }

#ifdef VR
    vr_session_init(&g_game.vr);
#endif

    return true;
}

static void on_avatar_texture_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    (void)path;
    if (!data || len == 0) return;

    extern unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
    extern void stbi_image_free(void*);
    extern void stbi_set_flip_vertically_on_load(int);

    int w, h, channels;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* pixels = stbi_load_from_memory(data, (int)len, &w, &h, &channels, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!pixels) return;

    uint32_t tex = texture_load_atlas_from_memory(pixels, w, h, 4);
    stbi_image_free(pixels);

    intptr_t slot = (intptr_t)user;
    if (slot >= 100) {
        int rp_idx = (int)((slot - 100) / 4);
        int part = (int)((slot - 100) % 4);
        if (rp_idx >= 0 && rp_idx < MAX_REMOTE_PLAYERS && g_game.remote_players[rp_idx].active) {
            switch (part) {
                case 0: g_game.remote_players[rp_idx].tex_shirt = tex; break;
                case 1: g_game.remote_players[rp_idx].tex_pants = tex; break;
                case 2: g_game.remote_players[rp_idx].tex_head = tex; break;
                case 3: g_game.remote_players[rp_idx].tex_package = tex; break;
            }
        }
    } else {
        switch (slot) {
            case 0: g_game.local_tex_shirt = tex; break;
            case 1: g_game.local_tex_pants = tex; break;
            case 2: g_game.local_tex_head = tex; break;
            default: g_game.avatar_texture = tex; break;
        }
    }

}

static void on_guest_avatar_texture_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    (void)user; (void)path;
    if (!data || len == 0) return;

    extern unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
    extern void stbi_image_free(void*);
    extern void stbi_set_flip_vertically_on_load(int);

    int w, h, channels;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* pixels = stbi_load_from_memory(data, (int)len, &w, &h, &channels, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!pixels) return;

    g_game.guest_avatar_texture = texture_load_atlas_from_memory(pixels, w, h, 4);
    stbi_image_free(pixels);

    apply_local_avatar();
}

static void on_accessory_obj_loaded(const char* path, const uint8_t* data, size_t len, void* user);
static void on_accessory_tex_loaded(const char* path, const uint8_t* data, size_t len, void* user);

static void* pw_acc_load_user(int player_slot, int acc_index) {
    return (void*)(intptr_t)((player_slot << 8) | (acc_index & 0xFF));
}

static void pw_decode_acc_load_user(void* user, int* player_slot, int* acc_index) {
    intptr_t v = (intptr_t)user;
    if (player_slot) *player_slot = (int)(v >> 8);
    if (acc_index) *acc_index = (int)(v & 0xFF);
}

static void parse_appearance_accessory_ids(const uint8_t* buf, size_t msg_len,
                                           uint32_t out[PW_MAX_EQUIPPED_ACCESSORIES]) {
    memset(out, 0, PW_MAX_EQUIPPED_ACCESSORIES * sizeof(uint32_t));
    int nacc = 3;
    if (msg_len >= (size_t)(4 + 7 + 16 + PW_MAX_EQUIPPED_ACCESSORIES * 4 + PW_MAX_EQUIPPED_EMOTES * 4))
        nacc = PW_MAX_EQUIPPED_ACCESSORIES;
    for (int i = 0; i < nacc; i++) {
        size_t off = 27 + (size_t)i * 4;
        if (msg_len < off + 4) break;
        out[i] = ((uint32_t)buf[off] << 24) | ((uint32_t)buf[off + 1] << 16) |
                 ((uint32_t)buf[off + 2] << 8) | (uint32_t)buf[off + 3];
    }
}

#define PW_APPEARANCE_HDR_LEGACY_3ACC 39
#define PW_APPEARANCE_HDR_EMOTES_3ACC 71
#define PW_APPEARANCE_HDR (4 + 7 + 16 + (PW_MAX_EQUIPPED_ACCESSORIES * 4) + (PW_MAX_EQUIPPED_EMOTES * 4))

static void parse_appearance_emote_ids(const uint8_t* buf, size_t msg_len,
                                       uint32_t out[PW_MAX_EQUIPPED_EMOTES]) {
    emote_default_loadout(out);
    size_t emote_off = 39;
    if (msg_len >= PW_APPEARANCE_HDR)
        emote_off = 27 + (size_t)PW_MAX_EQUIPPED_ACCESSORIES * 4;
    else if (msg_len < PW_APPEARANCE_HDR_EMOTES_3ACC)
        return;
    for (int i = 0; i < PW_MAX_EQUIPPED_EMOTES; i++) {
        size_t off = emote_off + (size_t)i * 4;
        if (msg_len < off + 4) break;
        out[i] = ((uint32_t)buf[off] << 24) | ((uint32_t)buf[off + 1] << 16) |
                 ((uint32_t)buf[off + 2] << 8) | (uint32_t)buf[off + 3];
    }
}

static void local_emote_fill_meta_gaps(void) {
    for (int i = 0; i < PW_MAX_EQUIPPED_EMOTES; i++) {
        uint32_t id = g_game.local_equipped_emotes[i];
        if (!g_game.local_emote_anims[i])
            g_game.local_emote_anims[i] = emote_id_to_base(id);
        if (!g_game.local_emote_names[i][0]) {
            if (id == PW_EMOTE_DANCE1_ID)
                snprintf(g_game.local_emote_names[i], PW_EMOTE_NAME_LEN, "Dance 1");
            else if (id == PW_EMOTE_DANCE2_ID)
                snprintf(g_game.local_emote_names[i], PW_EMOTE_NAME_LEN, "Dance 2");
            else if (id == PW_EMOTE_DANCE3_ID)
                snprintf(g_game.local_emote_names[i], PW_EMOTE_NAME_LEN, "Dance 3");
            else if (id > 0)
                snprintf(g_game.local_emote_names[i], PW_EMOTE_NAME_LEN, "#%u", id);
            else
                g_game.local_emote_names[i][0] = '\0';
        }
    }
}

static void apply_local_emotes_from_ticket(const JoinTicket* jt) {
    if (!jt) return;
    for (int ei = 0; ei < PW_MAX_EQUIPPED_EMOTES; ei++) {
        g_game.local_equipped_emotes[ei] = (uint32_t)jt->equipped_emotes[ei];
        g_game.local_emote_anims[ei] = (uint8_t)(jt->emote_anims[ei] > 0 ? jt->emote_anims[ei] : 0);
        snprintf(g_game.local_emote_names[ei], PW_EMOTE_NAME_LEN, "%s", jt->emote_names[ei]);
    }
    {
        int any = 0;
        for (int ei = 0; ei < PW_MAX_EQUIPPED_EMOTES; ei++)
            if (g_game.local_equipped_emotes[ei]) { any = 1; break; }
        if (!any) {
            emote_default_loadout(g_game.local_equipped_emotes);
            emote_default_anim_bases(g_game.local_emote_anims);
            emote_default_names(g_game.local_emote_names);
        }
    }
    local_emote_fill_meta_gaps();
}

static void parse_appearance_update_accessory_ids(const uint8_t* buf, size_t msg_len,
                                                  uint32_t out[PW_MAX_EQUIPPED_ACCESSORIES]) {
    memset(out, 0, PW_MAX_EQUIPPED_ACCESSORIES * sizeof(uint32_t));
    int nacc = 3;
    size_t full = 7 + 16 + (size_t)PW_MAX_EQUIPPED_ACCESSORIES * 4 + (size_t)PW_MAX_EQUIPPED_EMOTES * 4;
    if (msg_len >= full) nacc = PW_MAX_EQUIPPED_ACCESSORIES;
    for (int i = 0; i < nacc; i++) {
        size_t off = 23 + (size_t)i * 4;
        if (msg_len < off + 4) break;
        out[i] = ((uint32_t)buf[off] << 24) | ((uint32_t)buf[off + 1] << 16) |
                 ((uint32_t)buf[off + 2] << 8) | (uint32_t)buf[off + 3];
    }
}

static void parse_appearance_update_emote_ids(const uint8_t* buf, size_t msg_len,
                                              uint32_t out[PW_MAX_EQUIPPED_EMOTES]) {
    emote_default_loadout(out);
    size_t emote_off = 35;
    size_t full = 7 + 16 + (size_t)PW_MAX_EQUIPPED_ACCESSORIES * 4 + (size_t)PW_MAX_EQUIPPED_EMOTES * 4;
    if (msg_len >= full)
        emote_off = 23 + (size_t)PW_MAX_EQUIPPED_ACCESSORIES * 4;
    else if (msg_len < 67)
        return;
    for (int i = 0; i < PW_MAX_EQUIPPED_EMOTES; i++) {
        size_t off = emote_off + (size_t)i * 4;
        if (msg_len < off + 4) break;
        out[i] = ((uint32_t)buf[off] << 24) | ((uint32_t)buf[off + 1] << 16) |
                 ((uint32_t)buf[off + 2] << 8) | (uint32_t)buf[off + 3];
    }
}

static void unload_player_accessories(int player_slot) {
    if (player_slot == 0) {
        for (int i = 0; i < PW_MAX_EQUIPPED_ACCESSORIES; i++) {
            accessory_unload(&g_game.local_accessory[i]);
            g_game.local_accessory_tex[i] = 0;
        }
        return;
    }
    int rp = player_slot - 1;
    if (rp < 0 || rp >= MAX_REMOTE_PLAYERS) return;
    for (int i = 0; i < PW_MAX_EQUIPPED_ACCESSORIES; i++) {
        accessory_unload(&g_game.remote_players[rp].accessory[i]);
        g_game.remote_players[rp].accessory_tex[i] = 0;
    }
}

static void remove_remote_player_by_id(uint32_t pid) {
    if (pid == 0) return;
    for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
        if (!g_game.remote_players[rp].active || g_game.remote_players[rp].id != pid)
            continue;
        chat_set_name_color_override(&g_game.chat,
            g_game.remote_players[rp].name, false, 0, 0, 0);
        unload_player_accessories(1 + rp);
        ragdoll_destroy((RagdollState*)&g_game.remote_players[rp].ragdoll);
        scene_destroy_entity(&g_game.scene, g_game.remote_players[rp].entity);
        g_game.remote_players[rp].active = false;
        g_game.remote_players[rp].dead = false;
        g_game.remote_players[rp].is_vr = false;
        g_game.remote_players[rp].id = 0;
        if (g_game.remote_player_count > 0)
            g_game.remote_player_count--;
    }
}

static void vr_pose_remote_avatar(int rp) {
    if (rp < 0 || rp >= MAX_REMOTE_PLAYERS) return;
    if (!g_game.remote_players[rp].active || !g_game.remote_players[rp].is_vr) return;
    if (g_game.remote_players[rp].dead) return;
    Entity* ent = scene_get_entity(&g_game.scene, g_game.remote_players[rp].entity);
    if (!ent) return;
    vr_ik_apply(&g_game.remote_players[rp].anim,
                &g_game.remote_players[rp].vr_pose,
                &g_game.remote_players[rp].vr_calib,
                ent->transform.position,
                ent->transform.rotation.y);
}

static void vr_send_local_pose(void) {
    if (!g_game.vr.active || g_game.net.state != NET_STATE_CONNECTED) return;
    PwVrPose pose = g_game.vr.local;
    pose.flags |= PW_VR_FLAG_ACTIVE;
    if (g_game.vr.calib_done) {
        pose.flags |= PW_VR_FLAG_IK | PW_VR_FLAG_CALIB;
        pose.height_m = g_game.vr.calib.height_m;
        pose.arm_span_m = g_game.vr.calib.arm_span_m;
    }
    uint8_t buf[PW_VR_BODY_MAX];
    size_t n = pw_vr_pack_pose(buf, sizeof(buf), &pose);
    if (n)
        net_client_send(&g_game.net, MSG_VR, buf, n);
}

static void vr_calibrate_on_world_ready(void) {
    if (g_game.vr.calib.ready)
        return;
    if (!g_game.vr.active) {
        vr_ik_calib_defaults(&g_game.vr.calib);
        g_game.vr.calib_done = true;
        return;
    }
    if (!(g_game.vr.local.flags & PW_VR_FLAG_HEAD))
        return;
    Vec3 feet = {
        g_game.avatar.pos.x,
        g_game.avatar.pos.y - AVATAR_FEET_OFFSET,
        g_game.avatar.pos.z
    };

    vr_ik_calibrate(&g_game.vr.calib, &g_game.vr.local, feet,
                    g_game.avatar.current_yaw, false);
    g_game.vr.calib_done = true;
}

static void load_player_accessories(int player_slot, const uint32_t ids[PW_MAX_EQUIPPED_ACCESSORIES]) {
    const char* host = g_game.host[0] ? g_game.host : "https://polyworld.games";
    const char* base = "/uploads";
    char tpath[256];
    unload_player_accessories(player_slot);
    for (int i = 0; i < PW_MAX_EQUIPPED_ACCESSORIES; i++) {
        if (!ids[i]) continue;
        snprintf(tpath, sizeof(tpath), "%s%s/accessories/%u.obj", host, base, ids[i]);
        platform_load_file(tpath, on_accessory_obj_loaded, pw_acc_load_user(player_slot, i));
        snprintf(tpath, sizeof(tpath), "%s%s/accessories/%u.png", host, base, ids[i]);
        platform_load_file(tpath, on_accessory_tex_loaded, pw_acc_load_user(player_slot, i));
    }
}

static void on_accessory_obj_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    (void)path;
    if (!data || len == 0) return;
    int player_slot = 0, acc_index = 0;
    pw_decode_acc_load_user(user, &player_slot, &acc_index);
    if (acc_index < 0 || acc_index >= PW_MAX_EQUIPPED_ACCESSORIES) return;

    if (player_slot == 0) {
        accessory_load(&g_game.local_accessory[acc_index], (const char*)data, len);
    } else {
        int rp_idx = player_slot - 1;
        if (rp_idx >= 0 && rp_idx < MAX_REMOTE_PLAYERS && g_game.remote_players[rp_idx].active) {
            accessory_load(&g_game.remote_players[rp_idx].accessory[acc_index], (const char*)data, len);
        }
    }
}

static void on_accessory_tex_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    (void)path;
    if (!data || len == 0) return;

    extern unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
    extern void stbi_image_free(void*);
    extern void stbi_set_flip_vertically_on_load(int);

    int w, h, channels;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* pixels = stbi_load_from_memory(data, (int)len, &w, &h, &channels, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!pixels) return;

    uint32_t tex = texture_load_atlas_from_memory(pixels, w, h, 4);

    int player_slot = 0, acc_index = 0;
    pw_decode_acc_load_user(user, &player_slot, &acc_index);
    if (acc_index >= 0 && acc_index < PW_MAX_EQUIPPED_ACCESSORIES) {
        if (player_slot == 0) {
            accessory_set_atlas(&g_game.local_accessory[acc_index], pixels, w, h);
            if (tex) g_game.local_accessory_tex[acc_index] = tex;
        } else {
            int rp_idx = player_slot - 1;
            if (rp_idx >= 0 && rp_idx < MAX_REMOTE_PLAYERS) {
                accessory_set_atlas(&g_game.remote_players[rp_idx].accessory[acc_index], pixels, w, h);
                if (tex) g_game.remote_players[rp_idx].accessory_tex[acc_index] = tex;
            }
        }
    }
    stbi_image_free(pixels);
}

static void render_accessory(Accessory* acc, uint32_t tex, AvatarAnim* anim,
                             Vec3 pos, float yaw, float scale,
                             Vec3 skin_color, const Mat4* view, const Mat4* projection,
                             int player_slot, float alpha) {
    if (!acc->loaded || alpha <= 0.001f) return;
    for (int i = 0; i < ACCESSORY_MAX_PARTS; i++) {
        if (!acc->parts[i].valid) continue;
        int attach = acc->parts[i].attach_part;
        if (attach < 0 || attach >= AVATAR_PART_COUNT) continue;
        Mat4 part_mat = avatar_anim_get_part_matrix(anim, attach, pos, yaw, scale);
        Vec3 acc_color = tex ? (Vec3){1.0f, 1.0f, 1.0f} : skin_color;
        renderer_set_shadow_id(&g_game.renderer, renderer_shadow_id_accessory(player_slot, i));
        renderer_draw_mesh_alpha(&g_game.renderer, &acc->parts[i].mesh,
                                 &part_mat, acc_color, tex, tex ? 5 : 0, view, projection, alpha);
    }
}

static void render_accessory_ragdoll(Accessory* acc, uint32_t tex, RagdollState* rd, int acc_index,
                                     Vec3 skin_color, const Mat4* view, const Mat4* projection,
                                     int player_slot) {
    if (!acc || !acc->loaded || !rd || acc_index < 0 || acc_index >= PW_MAX_EQUIPPED_ACCESSORIES) return;
    for (int i = 0; i < ACCESSORY_MAX_PARTS; i++) {
        if (!acc->parts[i].valid || !rd->acc_bodies[acc_index][i]) continue;
        Mat4 body = physics_get_transform_mat4(g_game.physics, rd->acc_bodies[acc_index][i]);
        Mat4 part_mat = mat4_multiply(body, rd->acc_mesh_from_body[acc_index][i]);
        renderer_set_shadow_id(&g_game.renderer, renderer_shadow_id_accessory(player_slot, i));
        Vec3 acc_color = tex ? (Vec3){1.0f, 1.0f, 1.0f} : skin_color;
        renderer_draw_mesh(&g_game.renderer, &acc->parts[i].mesh,
                           &part_mat, acc_color, tex, tex ? 5 : 0, view, projection);
    }
}

static void render_all_accessories_ragdoll(Accessory* accs, uint32_t* texs, RagdollState* rd,
                                          Vec3 skin_color, const Mat4* view, const Mat4* projection,
                                          int player_slot) {
    if (!accs || !texs || !rd) return;
    for (int ai = 0; ai < PW_MAX_EQUIPPED_ACCESSORIES; ai++) {
        render_accessory_ragdoll(&accs[ai], texs[ai], rd, ai, skin_color, view, projection, player_slot);
    }
}

static void render_all_accessories(Accessory* accs, uint32_t* texs, AvatarAnim* anim,
                                   Vec3 pos, float yaw, float scale, Vec3 skin_color,
                                   const Mat4* view, const Mat4* projection,
                                   int player_slot, float alpha) {
    if (!accs || !texs) return;
    for (int ai = 0; ai < PW_MAX_EQUIPPED_ACCESSORIES; ai++) {
        render_accessory(&accs[ai], texs[ai], anim, pos, yaw, scale, skin_color,
                         view, projection, player_slot, alpha);
    }
}

#ifdef VR

static Vec3 vr_local_mesh_head_world(void) {
    Entity* av_ent = scene_get_entity(&g_game.scene, g_game.avatar.entity);
    Vec3 feet = av_ent ? av_ent->transform.position : (Vec3){
        g_game.avatar.pos.x,
        g_game.avatar.pos.y - AVATAR_FEET_OFFSET,
        g_game.avatar.pos.z
    };
    float yaw = av_ent ? av_ent->transform.rotation.y : g_game.avatar.current_yaw;
    Mat4 m = avatar_anim_get_part_matrix(&g_game.avatar_anim, ANIM_PART_HEAD,
                                         feet, yaw, AVATAR_SCALE);
    Vec3 c = avatar_anim_get_part_center(ANIM_PART_HEAD);
    Vec4 w = mat4_mul_vec4(m, (Vec4){c.x, c.y, c.z, 1.0f});
    return (Vec3){w.x, w.y, w.z};
}

static Vec3 vr_quat_look(const PwVrTracker* t) {
    Vec3 v = {0.0f, 0.0f, -1.0f};
    Vec3 u = {t->qx, t->qy, t->qz};
    Vec3 uv = vec3_cross(u, v);
    Vec3 uuv = vec3_cross(u, uv);
    uv = vec3_scale(uv, 2.0f * t->qw);
    uuv = vec3_scale(uuv, 2.0f);
    return vec3_add(v, vec3_add(uv, uuv));
}

static void draw_vr_recalibrate_ui(const Mat4* view, const Mat4* projection) {
    if (!g_game.vr.recal_ui || !view || !projection) return;

    Vec3 look = {0.0f, 0.0f, -1.0f};
    if (g_game.vr.local.flags & PW_VR_FLAG_HEAD)
        look = vr_quat_look(&g_game.vr.local.head);
    look.y = 0.0f;
    float llen = sqrtf(look.x * look.x + look.z * look.z);
    if (llen < 1e-4f) {
        look.x = 0.0f;
        look.z = -1.0f;
        llen = 1.0f;
    }
    look.x /= llen;
    look.z /= llen;

    Vec3 eye = g_game.vr.hmd_eye;
    Vec3 feet = {
        g_game.avatar.pos.x,
        g_game.avatar.pos.y - AVATAR_FEET_OFFSET,
        g_game.avatar.pos.z
    };
    Vec3 pos = { eye.x + look.x * 4.2f, feet.y, eye.z + look.z * 4.2f };
    float yaw = atan2f(eye.x - pos.x, eye.z - pos.z) * (180.0f / 3.14159265f) + 270.0f;

    if (g_unit_box_mesh.vao) {
        Vec3 panel_p = { pos.x - look.x * 0.55f, pos.y + 2.35f, pos.z - look.z * 0.55f };
        Mat4 panel = mat4_multiply(mat4_translate(panel_p),
                     mat4_multiply(mat4_rotate_y(yaw),
                                   mat4_scale((Vec3){2.6f, 3.4f, 0.06f})));
        renderer_draw_mesh_alpha(&g_game.renderer, &g_unit_box_mesh, &panel,
                                 (Vec3){0.07f, 0.08f, 0.10f}, 0, 0, view, projection, 0.82f);
    }

    Vec3 color = g_game.skin_color;
    for (int p = 0; p < AVATAR_PART_COUNT; p++) {
        if (!g_game.avatar_anim.parts[p].valid) continue;
        Mat4 part_mat = avatar_anim_get_part_matrix(&g_game.avatar_anim, p, pos, yaw, AVATAR_SCALE);
        uint32_t tex = g_game.local_tex_shirt;
        if (p == ANIM_PART_HEAD) tex = g_game.local_tex_head;
        else if (p == ANIM_PART_RIGHT_LEG || p == ANIM_PART_LEFT_LEG) tex = g_game.local_tex_pants;
        int tex_mode = tex ? 3 : 0;
        renderer_draw_mesh(&g_game.renderer, &g_game.avatar_anim.parts[p].mesh,
                           &part_mat, color, tex, tex_mode, view, projection);
    }
    render_all_accessories(g_game.local_accessory, g_game.local_accessory_tex,
                           &g_game.avatar_anim, pos, yaw, AVATAR_SCALE,
                           color, view, projection, 0, 1.0f);

    if (g_game.chat.initialized) {
        int sw = g_game.renderer.canvas_width;
        int sh = g_game.renderer.canvas_height;
        float us = g_game.ui_scale > 0.1f ? g_game.ui_scale : 1.0f;
        const char* msg =
            "Recalibrate\n"
            "Put your arms downward. Make sure your controllers also point down.\n"
            "Trigger or Menu to confirm  |  B to cancel";
        chat_render_banner(&g_game.chat, msg, (float)sw * 0.5f, (float)sh * 0.14f,
                           2.15f * us, 1.0f, 0.0f, sw, sh);
    }
}

static Vec3 vr_ik_model_to_world(Vec3 model, Vec3 feet, float yaw_deg) {
    float s = AVATAR_SCALE;
    float mx = model.x * s, my = model.y * s, mz = model.z * s;
    float rad = yaw_deg * (3.14159265f / 180.0f);
    float c = cosf(rad), sn = sinf(rad);
    return (Vec3){
        feet.x + mx * c + mz * sn,
        feet.y + my,
        feet.z - mx * sn + mz * c
    };
}

static Vec3 vr_ik_world_to_model(Vec3 world, Vec3 feet, float yaw_deg) {
    Vec3 d = { world.x - feet.x, world.y - feet.y, world.z - feet.z };
    float rad = -yaw_deg * (3.14159265f / 180.0f);
    float c = cosf(rad), sn = sinf(rad);
    return (Vec3){
        (d.x * c + d.z * sn) / AVATAR_SCALE,
        d.y / AVATAR_SCALE,
        (-d.x * sn + d.z * c) / AVATAR_SCALE
    };
}

static void vr_ik_dbg_box(Vec3 p, Vec3 color, float sz,
                          const Mat4* view, const Mat4* projection) {
    if (!g_unit_box_mesh.vao) return;
    Mat4 m = mat4_multiply(mat4_translate(p), mat4_scale((Vec3){sz, sz, sz}));
    renderer_draw_mesh_alpha(&g_game.renderer, &g_unit_box_mesh, &m,
                             color, 0, 0, view, projection, 0.9f);
}

static void draw_vr_ik_debug(const Mat4* view, const Mat4* projection) {
    if (!vr_openxr_ik_debug() || !view || !projection || !g_game.vr.active)
        return;

    Vec3 feet = {
        g_game.avatar.pos.x,
        g_game.avatar.pos.y - AVATAR_FEET_OFFSET,
        g_game.avatar.pos.z
    };
    float yaw = g_game.avatar.current_yaw;
    const PwVrPose* pose = &g_game.vr.local;
    Vec3 sh_l = vr_ik_model_to_world((Vec3){0.0f, 7.5f, -3.0f}, feet, yaw);
    Vec3 sh_r = vr_ik_model_to_world((Vec3){0.0f, 7.5f,  3.0f}, feet, yaw);
    Vec3 head = vr_ik_model_to_world((Vec3){0.0f, 9.25f, 0.0f}, feet, yaw);

    vr_ik_dbg_box(feet, (Vec3){1.0f, 1.0f, 1.0f}, 0.18f, view, projection);
    vr_ik_dbg_box(head, (Vec3){1.0f, 0.9f, 0.2f}, 0.22f, view, projection);
    vr_ik_dbg_box(sh_l, (Vec3){0.2f, 1.0f, 0.3f}, 0.20f, view, projection);
    vr_ik_dbg_box(sh_r, (Vec3){1.0f, 0.2f, 0.2f}, 0.20f, view, projection);

    int hang_l = 0, hang_r = 0;
    Vec3 lm = {0}, rm = {0};
    if (pose->flags & PW_VR_FLAG_LHAND) {
        Vec3 h = { pose->lhand.x, pose->lhand.y, pose->lhand.z };
        vr_ik_dbg_box(h, (Vec3){0.1f, 0.85f, 1.0f}, 0.16f, view, projection);
        renderer_debug_line(&g_game.renderer, sh_l, h,
                            (Vec3){0.1f, 0.85f, 1.0f}, view, projection);
        lm = vr_ik_world_to_model(h, feet, yaw);
        float dx = lm.x, dy = lm.y - 7.5f, dz = lm.z + 3.0f;
        float len = sqrtf(dx * dx + dy * dy + dz * dz);
        float align = (len > 1e-4f) ? (-dy / len) : 1.0f;
        hang_l = (align > 0.70f) ? 1 : 0;
    }
    if (pose->flags & PW_VR_FLAG_RHAND) {
        Vec3 h = { pose->rhand.x, pose->rhand.y, pose->rhand.z };
        vr_ik_dbg_box(h, (Vec3){1.0f, 0.2f, 0.9f}, 0.16f, view, projection);
        renderer_debug_line(&g_game.renderer, sh_r, h,
                            (Vec3){1.0f, 0.2f, 0.9f}, view, projection);
        rm = vr_ik_world_to_model(h, feet, yaw);
        float dx = rm.x, dy = rm.y - 7.5f, dz = rm.z - 3.0f;
        float len = sqrtf(dx * dx + dy * dy + dz * dz);
        float align = (len > 1e-4f) ? (-dy / len) : 1.0f;
        hang_r = (align > 0.70f) ? 1 : 0;
    }

    if (g_game.chat.initialized) {
        int sw = g_game.renderer.canvas_width;
        int sh = g_game.renderer.canvas_height;
        float us = g_game.ui_scale > 0.1f ? g_game.ui_scale : 1.0f;
        char msg[384];
        snprintf(msg, sizeof(msg),
                 "IK debug  squeeze/grip\n"
                 "cyan=OpenXR left  magenta=right\n"
                 "green=L shoulder  red=R  yellow=head  white=feet\n"
                 "L model %.1f %.1f %.1f hang %s\n"
                 "R model %.1f %.1f %.1f hang %s\n"
                 "mesh_yaw %.0f  H%d L%d R%d",
                 lm.x, lm.y, lm.z, hang_l ? "Y" : "n",
                 rm.x, rm.y, rm.z, hang_r ? "Y" : "n",
                 yaw,
                 (pose->flags & PW_VR_FLAG_HEAD) ? 1 : 0,
                 (pose->flags & PW_VR_FLAG_LHAND) ? 1 : 0,
                 (pose->flags & PW_VR_FLAG_RHAND) ? 1 : 0);
        chat_render_banner(&g_game.chat, msg, (float)sw * 0.5f, (float)sh * 0.16f,
                           1.85f * us, 1.0f, 0.0f, sw, sh);
    }
}
#endif

static void cast_accessory_shadows(Accessory* acc, AvatarAnim* anim, Vec3 pos, float yaw, float scale) {
    if (!acc || !acc->loaded || !anim) return;
    for (int i = 0; i < ACCESSORY_MAX_PARTS; i++) {
        if (!acc->parts[i].valid) continue;
        int attach = acc->parts[i].attach_part;
        if (attach < 0 || attach >= AVATAR_PART_COUNT) continue;
        Mat4 part_mat = avatar_anim_get_part_matrix(anim, attach, pos, yaw, scale);
        renderer_shadow_cast_mesh(&g_game.renderer, &acc->parts[i].mesh, &part_mat);
    }
}

static void cast_all_accessory_shadows(Accessory* accs, AvatarAnim* anim, Vec3 pos, float yaw, float scale) {
    if (!accs) return;
    for (int ai = 0; ai < PW_MAX_EQUIPPED_ACCESSORIES; ai++)
        cast_accessory_shadows(&accs[ai], anim, pos, yaw, scale);
}

static void cast_accessory_ragdoll_shadows(Accessory* acc, RagdollState* rd, int acc_index) {
    if (!acc || !acc->loaded || !rd || acc_index < 0 || acc_index >= PW_MAX_EQUIPPED_ACCESSORIES) return;
    for (int i = 0; i < ACCESSORY_MAX_PARTS; i++) {
        if (!acc->parts[i].valid || !rd->acc_bodies[acc_index][i]) continue;
        Mat4 body = physics_get_transform_mat4(g_game.physics, rd->acc_bodies[acc_index][i]);
        Mat4 part_mat = mat4_multiply(body, rd->acc_mesh_from_body[acc_index][i]);
        renderer_shadow_cast_mesh(&g_game.renderer, &acc->parts[i].mesh, &part_mat);
    }
}

static void cast_all_accessory_ragdoll_shadows(Accessory* accs, RagdollState* rd) {
    if (!accs || !rd) return;
    for (int ai = 0; ai < PW_MAX_EQUIPPED_ACCESSORIES; ai++)
        cast_accessory_ragdoll_shadows(&accs[ai], rd, ai);
}

#define TOOL_HOLD_MAX 16
typedef struct {
    char name[32];
    Accessory acc;
    uint32_t tex;
} ToolHoldEntry;
static ToolHoldEntry g_tool_holds[TOOL_HOLD_MAX];

static ToolHoldEntry* tool_hold_find(const char* name) {
    if (!name || !name[0]) return NULL;
    for (int i = 0; i < TOOL_HOLD_MAX; i++) {
        if (g_tool_holds[i].name[0] && strcmp(g_tool_holds[i].name, name) == 0)
            return &g_tool_holds[i];
    }
    return NULL;
}

static void tool_hold_ensure(const char* name) {
    if (!name || !name[0]) return;
    if (tool_hold_find(name)) return;
    int slot = -1;
    for (int i = 0; i < TOOL_HOLD_MAX; i++) {
        if (!g_tool_holds[i].name[0]) { slot = i; break; }
    }
    if (slot < 0) return;
    snprintf(g_tool_holds[slot].name, sizeof(g_tool_holds[slot].name), "%s", name);
    char path[160];
    snprintf(path, sizeof(path), "assets/tools/%s.obj", name);
    platform_load_file(path, on_tool_hold_obj_loaded, (void*)(intptr_t)slot);
    snprintf(path, sizeof(path), "assets/tools/%s_Tex.png", name);
    platform_load_file(path, on_tool_hold_tex_loaded, (void*)(intptr_t)slot);
}

static const char* local_held_tool_name(void) {
    if (g_game.avatar.dead || g_game.ragdoll.active) return NULL;
    if (g_game.equipped_tool <= 0 || g_game.equipped_tool > g_game.tool_count) return NULL;
    if (!g_game.tools[g_game.equipped_tool - 1].available) return NULL;
    const char* n = g_game.tools[g_game.equipped_tool - 1].name;
    return (n && n[0]) ? n : NULL;
}

static void render_held_tool(const char* name, AvatarAnim* anim, Vec3 pos, float yaw,
                             const Mat4* view, const Mat4* projection,
                             int player_slot, float alpha) {
    ToolHoldEntry* e = tool_hold_find(name);
    if (!e || !e->acc.loaded || !anim) return;
    Vec3 white = {1.0f, 1.0f, 1.0f};
    render_accessory(&e->acc, e->tex, anim, pos, yaw, AVATAR_SCALE,
                     white, view, projection, player_slot, alpha);
}

static void cast_held_tool_shadows(const char* name, AvatarAnim* anim, Vec3 pos, float yaw) {
    ToolHoldEntry* e = tool_hold_find(name);
    if (!e || !e->acc.loaded || !anim) return;
    cast_accessory_shadows(&e->acc, anim, pos, yaw, AVATAR_SCALE);
}

static void fill_player_shadow_skip(void) {
    renderer_shadow_skip_reset(&g_game.renderer);
    renderer_shadow_skip_add(&g_game.renderer, g_game.avatar.entity);
    for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
        if (!g_game.remote_players[rp].active) continue;
        renderer_shadow_skip_add(&g_game.renderer, g_game.remote_players[rp].entity);
    }
#ifdef VIDACTOR
    for (int vi = 0; vi < VIDACTOR_MAX_ACTORS; vi++) {
        if (!g_game.vid_puppets[vi].active) continue;
        renderer_shadow_skip_add(&g_game.renderer, g_game.vid_puppets[vi].entity);
    }
#endif
}

static void cast_player_shadows_meshes(void) {
    renderer_set_shadow_id(&g_game.renderer, renderer_shadow_id_avatar(0, 0));
    if (g_game.ragdoll.active) {
        for (int p = 0; p < AVATAR_PART_COUNT; p++) {
            if (!g_game.avatar_anim.parts[p].valid) continue;
            Mat4 part_mat = ragdoll_part_matrix((RagdollState*)&g_game.ragdoll, p);
            renderer_shadow_cast_mesh(&g_game.renderer, &g_game.avatar_anim.parts[p].mesh, &part_mat);
        }
        cast_all_accessory_ragdoll_shadows(g_game.local_accessory, (RagdollState*)&g_game.ragdoll);
    } else {
        Entity* av_ent = scene_get_entity(&g_game.scene, g_game.avatar.entity);
        if (av_ent && (1.0f - g_game.local_transparency) > 0.01f) {
            float yaw = av_ent->transform.rotation.y;
            Vec3 pos = av_ent->transform.position;
            for (int p = 0; p < AVATAR_PART_COUNT; p++) {
                if (!g_game.avatar_anim.parts[p].valid) continue;
                Mat4 part_mat = avatar_anim_get_part_matrix(&g_game.avatar_anim, p, pos, yaw, AVATAR_SCALE);
                renderer_shadow_cast_mesh(&g_game.renderer, &g_game.avatar_anim.parts[p].mesh, &part_mat);
            }
            cast_all_accessory_shadows(g_game.local_accessory, &g_game.avatar_anim, pos, yaw, AVATAR_SCALE);
            cast_held_tool_shadows(local_held_tool_name(), &g_game.avatar_anim, pos, yaw);
        }
    }

    for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
        if (!g_game.remote_players[rp].active) continue;
        Entity* rent = scene_get_entity(&g_game.scene, g_game.remote_players[rp].entity);
        if (!rent) continue;
        if ((1.0f - g_game.remote_players[rp].transparency) <= 0.01f) continue;
        Vec3 d = vec3_sub(rent->transform.position, g_game.avatar.pos);
        float dist2 = d.x * d.x + d.y * d.y + d.z * d.z;
        if (dist2 > (220.0f * 220.0f)) continue;

        renderer_set_shadow_id(&g_game.renderer, renderer_shadow_id_avatar(rp + 1, 0));
        if (g_game.remote_players[rp].ragdoll.active) {
            for (int p = 0; p < AVATAR_PART_COUNT; p++) {
                if (!g_game.remote_players[rp].anim.parts[p].valid) continue;
                Mat4 part_mat = ragdoll_part_matrix((RagdollState*)&g_game.remote_players[rp].ragdoll, p);
                renderer_shadow_cast_mesh(&g_game.renderer, &g_game.remote_players[rp].anim.parts[p].mesh, &part_mat);
            }
            cast_all_accessory_ragdoll_shadows(g_game.remote_players[rp].accessory,
                                               (RagdollState*)&g_game.remote_players[rp].ragdoll);
        } else {
            vr_pose_remote_avatar(rp);
            float yaw = rent->transform.rotation.y;
            Vec3 pos = rent->transform.position;
            for (int p = 0; p < AVATAR_PART_COUNT; p++) {
                if (!g_game.remote_players[rp].anim.parts[p].valid) continue;
                Mat4 part_mat = avatar_anim_get_part_matrix(&g_game.remote_players[rp].anim, p, pos, yaw, AVATAR_SCALE);
                renderer_shadow_cast_mesh(&g_game.renderer, &g_game.remote_players[rp].anim.parts[p].mesh, &part_mat);
            }
            if (dist2 < (100.0f * 100.0f)) {
                cast_all_accessory_shadows(g_game.remote_players[rp].accessory,
                                           &g_game.remote_players[rp].anim, pos, yaw, AVATAR_SCALE);
                if (g_game.remote_players[rp].held_tool[0])
                    cast_held_tool_shadows(g_game.remote_players[rp].held_tool,
                                           &g_game.remote_players[rp].anim, pos, yaw);
            }
        }
    }

#ifdef VIDACTOR
    for (int vi = 0; vi < VIDACTOR_MAX_ACTORS; vi++) {
        if (!g_game.vid_puppets[vi].active) continue;
        if (!g_game.vid_puppets[vi].anim.parts[0].valid) continue;
        Entity* vent = scene_get_entity(&g_game.scene, g_game.vid_puppets[vi].entity);
        if (!vent) continue;
        renderer_set_shadow_id(&g_game.renderer, renderer_shadow_id_avatar(80 + vi, 0));
        float yaw = vent->transform.rotation.y;
        Vec3 pos = vent->transform.position;
        for (int p = 0; p < AVATAR_PART_COUNT; p++) {
            if (!g_game.vid_puppets[vi].anim.parts[p].valid) continue;
            Mat4 part_mat = avatar_anim_get_part_matrix(&g_game.vid_puppets[vi].anim, p, pos, yaw, AVATAR_SCALE);
            renderer_shadow_cast_mesh(&g_game.renderer, &g_game.vid_puppets[vi].anim.parts[p].mesh, &part_mat);
        }
    }
#endif
}

typedef struct {
    bool first_person;
    const Mat4* view;
    const Mat4* projection;
    double dt;
} WorldMidDrawCtx;

static void avatar_sort_parts_far_first(int* order, int n, AvatarAnim* anim,
                                        Vec3 pos, float yaw, float scale, const Mat4* view) {
    if (n < 2 || !anim || !view) return;
    Mat4 inv = mat4_inverse(*view);
    Vec3 cam = { inv.m[12], inv.m[13], inv.m[14] };
    float dist[AVATAR_PART_COUNT];
    for (int i = 0; i < n; i++) {
        Mat4 m = avatar_anim_get_part_matrix(anim, order[i], pos, yaw, scale);
        float dx = m.m[12] - cam.x;
        float dy = m.m[13] - cam.y;
        float dz = m.m[14] - cam.z;
        dist[i] = dx * dx + dy * dy + dz * dz;
    }
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (dist[j] > dist[i]) {
                int t = order[i];
                order[i] = order[j];
                order[j] = t;
                float td = dist[i];
                dist[i] = dist[j];
                dist[j] = td;
            }
        }
    }
}

static void draw_world_avatars_and_rockets(void* user) {
    WorldMidDrawCtx* ctx = (WorldMidDrawCtx*)user;
    bool first_person = ctx->first_person;
    const Mat4* view = ctx->view;
    const Mat4* projection = ctx->projection;
    double dt = ctx->dt;

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);

    if (g_game.avatar_anim.parts[0].valid) {
        if (g_game.ragdoll.active) {
            Vec3 color = g_game.skin_color;
            for (int p = 0; p < AVATAR_PART_COUNT; p++) {
                if (!g_game.avatar_anim.parts[p].valid) continue;
                Mat4 part_mat = ragdoll_part_matrix((RagdollState*)&g_game.ragdoll, p);
                uint32_t tex = g_game.local_tex_shirt;
                if (p == ANIM_PART_HEAD) tex = g_game.local_tex_head;
                else if (p == ANIM_PART_RIGHT_LEG || p == ANIM_PART_LEFT_LEG) tex = g_game.local_tex_pants;
                int tex_mode = tex ? 3 : 0;
                renderer_set_shadow_id(&g_game.renderer, renderer_shadow_id_avatar(0, p));
                renderer_draw_mesh(&g_game.renderer, &g_game.avatar_anim.parts[p].mesh,
                                   &part_mat, color, tex, tex_mode, view, projection);
            }
            render_all_accessories_ragdoll(g_game.local_accessory, g_game.local_accessory_tex,
                                           (RagdollState*)&g_game.ragdoll, color, view, projection, 0);
        } else {
            float body_a = camera_body_alpha(&g_game.camera);
            if (first_person) body_a = 0.0f;
#ifdef VR

            bool vr_fp = g_game.vr.active && first_person && g_game.world_ready
                         && !g_game.avatar.dead && !g_game.vr.inspect;
            if (vr_hub_active() && g_game.vr.active && !g_game.avatar.dead &&
                !g_game.vr.inspect)
                vr_fp = true;
            if (vr_fp) body_a = 1.0f;
#endif
            body_a *= (1.0f - g_game.local_transparency);
            Entity* av_ent = scene_get_entity(&g_game.scene, g_game.avatar.entity);
            if (body_a > 0.01f && (av_ent || g_game.avatar_anim.parts[0].valid)) {
                float yaw = av_ent ? av_ent->transform.rotation.y : g_game.avatar.current_yaw;
                Vec3 pos = av_ent ? av_ent->transform.position : (Vec3){
                    g_game.avatar.pos.x,
                    g_game.avatar.pos.y - AVATAR_FEET_OFFSET + g_game.avatar.step_offset,
                    g_game.avatar.pos.z
                };
                Vec3 color = g_game.skin_color;

                int order[AVATAR_PART_COUNT];
                int nparts = 0;
                for (int p = 0; p < AVATAR_PART_COUNT; p++) {
                    if (!g_game.avatar_anim.parts[p].valid) continue;
#ifdef VR
                    if (vr_fp && (p == ANIM_PART_HEAD || p == ANIM_PART_TORSO))
                        continue;
#endif
                    order[nparts++] = p;
                }
                if (body_a < 0.999f)
                    avatar_sort_parts_far_first(order, nparts, &g_game.avatar_anim,
                                                pos, yaw, AVATAR_SCALE, view);

                for (int i = 0; i < nparts; i++) {
                    int p = order[i];
                    Mat4 part_mat = avatar_anim_get_part_matrix(&g_game.avatar_anim, p, pos, yaw, AVATAR_SCALE);
                    uint32_t tex = g_game.local_tex_shirt;
                    if (p == ANIM_PART_HEAD) tex = g_game.local_tex_head;
                    else if (p == ANIM_PART_RIGHT_LEG || p == ANIM_PART_LEFT_LEG) tex = g_game.local_tex_pants;
                    int tex_mode = tex ? 3 : 0;
                    renderer_set_shadow_id(&g_game.renderer, renderer_shadow_id_avatar(0, p));
                    if (body_a >= 0.999f) {
                        renderer_draw_mesh(&g_game.renderer, &g_game.avatar_anim.parts[p].mesh,
                                           &part_mat, color, tex, tex_mode, view, projection);
                    } else {
                        renderer_draw_mesh_alpha(&g_game.renderer, &g_game.avatar_anim.parts[p].mesh,
                                                 &part_mat, color, tex, tex_mode, view, projection, body_a);
                    }
                }
#ifdef VR
                if (!vr_fp)
#endif
                render_all_accessories(g_game.local_accessory, g_game.local_accessory_tex,
                                       &g_game.avatar_anim, pos, yaw, AVATAR_SCALE,
                                       color, view, projection, 0, body_a);
#ifdef VR
                if (!vr_fp)
#endif
                render_held_tool(local_held_tool_name(), &g_game.avatar_anim, pos, yaw,
                                 view, projection, 0, body_a);
            }
        }

        for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
            if (!g_game.remote_players[rp].active) continue;
            Entity* rent = scene_get_entity(&g_game.scene, g_game.remote_players[rp].entity);
            if (!rent) continue;

            float rp_alpha = 1.0f - g_game.remote_players[rp].transparency;
            if (rp_alpha <= 0.01f) continue;

            Vec3 d = vec3_sub(rent->transform.position, g_game.avatar.pos);
            float dist2 = d.x * d.x + d.y * d.y + d.z * d.z;
            if (dist2 > (220.0f * 220.0f)) continue;

            bool draw_acc = dist2 < (100.0f * 100.0f);
            Vec3 color = g_game.remote_players[rp].skin_color;

            if (g_game.remote_players[rp].ragdoll.active) {
                for (int p = 0; p < AVATAR_PART_COUNT; p++) {
                    if (!g_game.remote_players[rp].anim.parts[p].valid) continue;
                    Mat4 part_mat = ragdoll_part_matrix(
                        (RagdollState*)&g_game.remote_players[rp].ragdoll, p);
                    uint32_t tex = g_game.remote_players[rp].tex_shirt;
                    if (p == ANIM_PART_HEAD) tex = g_game.remote_players[rp].tex_head;
                    else if (p == ANIM_PART_RIGHT_LEG || p == ANIM_PART_LEFT_LEG)
                        tex = g_game.remote_players[rp].tex_pants;
                    int tex_mode = tex ? 3 : 0;
                    renderer_set_shadow_id(&g_game.renderer, renderer_shadow_id_avatar(rp + 1, p));
                    if (rp_alpha >= 0.999f) {
                        renderer_draw_mesh(&g_game.renderer, &g_game.remote_players[rp].anim.parts[p].mesh,
                                           &part_mat, color, tex, tex_mode, view, projection);
                    } else {
                        renderer_draw_mesh_alpha(&g_game.renderer, &g_game.remote_players[rp].anim.parts[p].mesh,
                                                 &part_mat, color, tex, tex_mode, view, projection, rp_alpha);
                    }
                }
                if (draw_acc) {
                    render_all_accessories_ragdoll(g_game.remote_players[rp].accessory,
                                                   g_game.remote_players[rp].accessory_tex,
                                                   (RagdollState*)&g_game.remote_players[rp].ragdoll,
                                                   color, view, projection, rp + 1);
                }
                continue;
            }

            float anim_dt = (float)dt;
            if (dist2 > (50.0f * 50.0f)) {
                g_game.remote_players[rp].anim_dt_accum += (float)dt;
                float step = (dist2 > (120.0f * 120.0f)) ? 0.20f : 0.10f;
                if (g_game.remote_players[rp].anim_dt_accum < step) {
                    anim_dt = 0.0f;
                } else {
                    anim_dt = g_game.remote_players[rp].anim_dt_accum;
                    g_game.remote_players[rp].anim_dt_accum = 0.0f;
                }
            } else {
                g_game.remote_players[rp].anim_dt_accum = 0.0f;
            }

            if (g_game.remote_players[rp].is_vr) {
                vr_pose_remote_avatar(rp);
            } else if (anim_dt > 0.0f) {
                float rp_speed = 0.0f;
                if (g_game.remote_players[rp].anim.state == ANIM_STATE_WALKING)
                    rp_speed = 16.0f;
                else if (g_game.remote_players[rp].anim.state == ANIM_STATE_CLIMBING)
                    rp_speed = 12.0f;
                if (g_game.remote_players[rp].anim.state == ANIM_STATE_EMOTE &&
                    g_game.remote_players[rp].anim.emote_id &&
                    !g_game.remote_players[rp].anim.emote_clip) {
                    g_game.remote_players[rp].anim.emote_clip =
                        emote_clip_get(g_game.remote_players[rp].anim.emote_id);
                }
                g_game.remote_players[rp].anim.tool_hold =
                    g_game.remote_players[rp].held_tool[0] != '\0';
                avatar_anim_update(&g_game.remote_players[rp].anim,
                                   g_game.remote_players[rp].anim.state, rp_speed, anim_dt);
            } else {
                g_game.remote_players[rp].anim.tool_hold =
                    g_game.remote_players[rp].held_tool[0] != '\0';
            }

            float yaw = rent->transform.rotation.y;
            Vec3 pos = rent->transform.position;

            for (int p = 0; p < AVATAR_PART_COUNT; p++) {
                if (!g_game.remote_players[rp].anim.parts[p].valid) continue;
                Mat4 part_mat = avatar_anim_get_part_matrix(&g_game.remote_players[rp].anim, p, pos, yaw, AVATAR_SCALE);
                uint32_t tex = g_game.remote_players[rp].tex_shirt;
                if (p == ANIM_PART_HEAD) tex = g_game.remote_players[rp].tex_head;
                else if (p == ANIM_PART_RIGHT_LEG || p == ANIM_PART_LEFT_LEG)
                    tex = g_game.remote_players[rp].tex_pants;
                int tex_mode = tex ? 3 : 0;
                renderer_set_shadow_id(&g_game.renderer, renderer_shadow_id_avatar(rp + 1, p));
                if (rp_alpha >= 0.999f) {
                    renderer_draw_mesh(&g_game.renderer, &g_game.remote_players[rp].anim.parts[p].mesh,
                                       &part_mat, color, tex, tex_mode, view, projection);
                } else {
                    renderer_draw_mesh_alpha(&g_game.renderer, &g_game.remote_players[rp].anim.parts[p].mesh,
                                             &part_mat, color, tex, tex_mode, view, projection, rp_alpha);
                }
            }
            if (draw_acc) {
                render_all_accessories(g_game.remote_players[rp].accessory,
                                       g_game.remote_players[rp].accessory_tex,
                                       &g_game.remote_players[rp].anim, pos, yaw, AVATAR_SCALE,
                                       color, view, projection, rp + 1, rp_alpha);
                if (g_game.remote_players[rp].held_tool[0])
                    render_held_tool(g_game.remote_players[rp].held_tool,
                                     &g_game.remote_players[rp].anim, pos, yaw,
                                     view, projection, rp + 1, rp_alpha);
            }
        }

#ifdef VIDACTOR
        for (int vi = 0; vi < VIDACTOR_MAX_ACTORS; vi++) {
            if (!g_game.vid_puppets[vi].active) continue;
            if (!g_game.vid_puppets[vi].anim.parts[0].valid) continue;
            Entity* vent = scene_get_entity(&g_game.scene, g_game.vid_puppets[vi].entity);
            if (!vent) continue;
            float yaw = vent->transform.rotation.y;
            Vec3 pos = vent->transform.position;
            Vec3 color = g_game.vid_puppets[vi].skin_color;
            for (int p = 0; p < AVATAR_PART_COUNT; p++) {
                if (!g_game.vid_puppets[vi].anim.parts[p].valid) continue;
                Mat4 part_mat = avatar_anim_get_part_matrix(&g_game.vid_puppets[vi].anim, p, pos, yaw, AVATAR_SCALE);
                uint32_t tex = g_game.vid_puppets[vi].tex_shirt;
                if (p == ANIM_PART_HEAD) tex = g_game.vid_puppets[vi].tex_head;
                else if (p == ANIM_PART_RIGHT_LEG || p == ANIM_PART_LEFT_LEG)
                    tex = g_game.vid_puppets[vi].tex_pants;
                int tex_mode = tex ? 3 : 0;
                renderer_set_shadow_id(&g_game.renderer, renderer_shadow_id_avatar(80 + vi, p));
                renderer_draw_mesh(&g_game.renderer, &g_game.vid_puppets[vi].anim.parts[p].mesh,
                                   &part_mat, color, tex, tex_mode, view, projection);
            }
        }
#endif
    }

    if (g_game.explosion_mesh_ready) {
        for (int ri = 0; ri < MAX_ROCKETS; ri++) {
            if (!g_game.rockets[ri].active) continue;
            Vec3 rpos = g_game.rockets[ri].position;
            float rs = 0.3f;
            Mat4 m = mat4_multiply(mat4_translate(rpos), mat4_scale((Vec3){rs, rs, rs}));
            Vec3 color = {1.0f, 0.9f, 0.2f};
            renderer_set_shadow_id(&g_game.renderer, 0);
            renderer_draw_mesh(&g_game.renderer, &g_game.explosion_mesh, &m, color, 0, 0, view, projection);
        }
    }
}

static int normalize_mesh_flags(int package_id) {
    if (package_id < 0 || package_id > 7) return 0;
    return package_id;
}

static void avatar_body_url(char* out, size_t out_sz, const char* host,
                            const char* uploads_base, bool want_new) {
    (void)uploads_base;

    if (want_new)
        snprintf(out, out_sz, "%s/assets/wasm/new.obj", host);
    else
        snprintf(out, out_sz, "%s/assets/wasm/avatar.obj", host);
}

static void apply_mesh_flags_to_anim(AvatarAnim* dst, int mesh_flags, bool* parts_owned) {
    if (!dst) return;
    mesh_flags = normalize_mesh_flags(mesh_flags);
    const AvatarAnim* leg = g_game.avatar_body_legacy_ready ? &g_game.avatar_body_legacy : NULL;
    const AvatarAnim* neu = g_game.avatar_body_new_ready ? &g_game.avatar_body_new : NULL;
    if (!leg && !neu) return;
    if (parts_owned && *parts_owned) {
        avatar_anim_clear(dst);
        *parts_owned = false;
    } else {
        avatar_anim_detach(dst);
    }
    avatar_anim_apply_mesh_flags(dst, leg, neu, mesh_flags);
}

static void refresh_local_avatar_meshes(void) {
    apply_mesh_flags_to_anim(&g_game.avatar_anim, g_game.local_equipped_package,
                             &g_game.avatar_anim_parts_owned);
    Entity* av_ent = scene_get_entity(&g_game.scene, g_game.avatar.entity);
    if (av_ent && g_game.avatar_anim.parts[0].valid)
        av_ent->mesh = NULL;
}

static void refresh_remote_avatar_meshes(int rp) {
    if (rp < 0 || rp >= MAX_REMOTE_PLAYERS) return;
    apply_mesh_flags_to_anim(&g_game.remote_players[rp].anim,
                             g_game.remote_players[rp].mesh_flags, NULL);
}

static void refresh_all_avatar_meshes(void) {
    refresh_local_avatar_meshes();
    for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
        if (g_game.remote_players[rp].active)
            refresh_remote_avatar_meshes(rp);
    }
}

static void on_avatar_body_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    (void)path;
    intptr_t which = (intptr_t)user;
    if (which == 0) g_game.avatar_body_legacy_loading = false;
    else g_game.avatar_body_new_loading = false;

    if (!data || len == 0) {
        PW_ERR(ERR_FILE, "Failed to load avatar body template\n");
        return;
    }
    AvatarAnim* target = (which == 0) ? &g_game.avatar_body_legacy : &g_game.avatar_body_new;
    avatar_anim_clear(target);
    if (!avatar_anim_load(target, (const char*)data, len)) {
        PW_ERR(ERR_FILE, "Failed to parse avatar body template\n");
        return;
    }
    if (which == 0) g_game.avatar_body_legacy_ready = true;
    else g_game.avatar_body_new_ready = true;
    {
        int n = 0;
        for (int p = 0; p < AVATAR_PART_COUNT; p++)
            if (target->parts[p].valid) n++;
        PW_LOG("[Avatar] %s body ready (%d/6 parts)\n",
               which == 0 ? "legacy" : "new", n);
    }
    refresh_all_avatar_meshes();
}

static void ensure_avatar_bodies_loaded(void) {
    const char* host = g_game.host[0] ? g_game.host : "https://polyworld.games";
    char path[512];
    int flags = normalize_mesh_flags(g_game.local_equipped_package);

    bool need_legacy = (flags & 7) != 7;
#if defined(PW_STUDIO_HOST)
    if (g_game.studio_playtest) need_legacy = true;
#endif
    if (need_legacy && !g_game.avatar_body_legacy_ready && !g_game.avatar_body_legacy_loading) {
        g_game.avatar_body_legacy_loading = true;
#if defined(__ANDROID__)
        snprintf(path, sizeof(path), "assets/avatar.obj");
#else
        avatar_body_url(path, sizeof(path), host, "/uploads", false);
#endif
        platform_load_file(path, on_avatar_body_loaded, (void*)(intptr_t)0);
    }
    if (!g_game.avatar_body_new_ready && !g_game.avatar_body_new_loading) {
        g_game.avatar_body_new_loading = true;
#if defined(__ANDROID__)
        snprintf(path, sizeof(path), "assets/new.obj");
#else
        avatar_body_url(path, sizeof(path), host, "/uploads", true);
#endif
        platform_load_file(path, on_avatar_body_loaded, (void*)(intptr_t)1);
    }
}

void on_avatar_mesh_loaded(const char* path, const uint8_t* data, size_t len, void* user_data) {
    (void)path; (void)data; (void)len; (void)user_data;

}

static float lerp_angle_deg(float from, float to, float t) {
    float d = to - from;
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return from + d * t;
}

static float wrap_deg360(float a) {
    while (a >= 360.0f) a -= 360.0f;
    while (a < 0.0f) a += 360.0f;
    return a;
}

static float angle_follow_deg(float prev, float now) {
    float d = now - prev;
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return prev + d;
}

static Vec3 euler_follow(Vec3 prev, Vec3 now) {
    return (Vec3){
        angle_follow_deg(prev.x, now.x),
        angle_follow_deg(prev.y, now.y),
        angle_follow_deg(prev.z, now.z)
    };
}

static Vec3 netown_body_local_to_world(PhysicsBodyID body, Vec3 loc) {
    Mat4 m = physics_get_transform_mat4(g_game.physics, body);
    Vec4 r = mat4_mul_vec4(m, (Vec4){ loc.x, loc.y, loc.z, 1.0f });
    return (Vec3){ r.x, r.y, r.z };
}

static Vec3 netown_body_local_dir_to_world(PhysicsBodyID body, Vec3 d) {
    Mat4 m = physics_get_transform_mat4(g_game.physics, body);
    Vec4 r = mat4_mul_vec4(m, (Vec4){ d.x, d.y, d.z, 0.0f });
    return (Vec3){ r.x, r.y, r.z };
}

static void sync_physics_to_scene(void) {

    if (g_game.multiplayer) {
        if (g_game.net_proto < PW_PROTO_NETOWN) return;
        for (int ni = 0; ni < g_game.net_object_count; ni++) {
            if (!g_game.net_objects[ni].net_owned) continue;
            Entity* e = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
            if (!e || !e->physics_body) continue;
            Vec3 p = physics_get_position(g_game.physics, e->physics_body);
            if (!isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z) ||
                fabsf(p.x) > 80000.0f || fabsf(p.y) > 80000.0f || fabsf(p.z) > 80000.0f)
                continue;
            e->transform.position = p;
            {
                Vec3 nr;
                physics_get_rotation_euler(g_game.physics, e->physics_body, &nr);
                e->transform.rotation = euler_follow(e->transform.rotation, nr);
            }
            {
                Mat4 m = physics_get_transform_mat4(g_game.physics, e->physics_body);
                e->use_phys_model = true;
                e->phys_model = mat4_multiply(m, mat4_scale(e->transform.scale));
            }
        }
        return;
    }
    for (uint32_t i = 0; i < g_game.scene.count; i++) {
        Entity* e = &g_game.scene.entities[i];
        if (!e->active || e->physics_body == 0) {
            continue;
        }

        if (e->id == g_game.avatar.entity) {
            continue;
        }
        Vec3 p = physics_get_position(g_game.physics, e->physics_body);
        if (!isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z) ||
            fabsf(p.x) > 80000.0f || fabsf(p.y) > 80000.0f || fabsf(p.z) > 80000.0f)
            continue;
        e->transform.position = p;

        for (int ni = 0; ni < g_game.net_object_count; ni++) {
            if (g_game.net_objects[ni].entity == e->id && g_game.net_objects[ni].net_owned) {
                physics_get_rotation_euler(g_game.physics, e->physics_body, &e->transform.rotation);
                break;
            }
        }

    }
}

static float net_lerp_duration(float* ema, double now, double last_t, bool have_last) {
    float measured = 0.075f;
    if (have_last && last_t > 0.0) {
        measured = (float)(now - last_t);
        if (measured < 0.016f) measured = 0.016f;
        if (measured > 1.0f) measured = 1.0f;
    }
    if (ema) {
        if (*ema < 0.001f) *ema = measured;
        else *ema += 0.35f * (measured - *ema);
        measured = *ema;
    }
    float dur = measured * 0.9f;
    if (dur < 0.016f) dur = 0.016f;
    return dur;
}

static float net_player_lerp_duration(float* ema, double now, double last_t, bool have_last) {
    float measured = 0.05f;
    if (have_last && last_t > 0.0) {
        measured = (float)(now - last_t);
        if (measured < 0.020f) measured = 0.020f;
        if (measured > 0.25f) measured = 0.25f;
    }
    if (ema) {
        if (*ema < 0.001f) *ema = measured;
        else *ema += 0.2f * (measured - *ema);
        measured = *ema;
    }
    float dur = measured * 1.05f;
    if (dur < 0.028f) dur = 0.028f;
    if (dur > 0.20f) dur = 0.20f;
    return dur;
}

static void net_apply_replicated_pose(Entity* target, int ni, Vec3 tgt, Vec3 nr,
                                      bool should_tween, bool zero_vel) {
    if (!target || ni < 0 || ni >= g_game.net_object_count) return;
    double now = platform_get_time();
    float dx = tgt.x - target->transform.position.x;
    float dy = tgt.y - target->transform.position.y;
    float dz = tgt.z - target->transform.position.z;
    float d2 = dx * dx + dy * dy + dz * dz;
    const float snap_dist = 16.0f;
    bool tween = should_tween &&
                 g_game.net_objects[ni].has_target &&
                 !target->new_object &&
                 d2 <= snap_dist * snap_dist;
    if (tween) {
        g_game.net_objects[ni].lerp_start_pos = target->transform.position;
        g_game.net_objects[ni].lerp_start_rot = target->transform.rotation;
        g_game.net_objects[ni].target_pos = tgt;
        g_game.net_objects[ni].target_rot = nr;
        g_game.net_objects[ni].lerp_duration = net_lerp_duration(
            &g_game.net_objects[ni].lerp_interval_ema,
            now,
            g_game.net_objects[ni].last_update_time,
            true);
        g_game.net_objects[ni].lerp_t = 0.0f;
        g_game.net_objects[ni].last_update_time = now;
        g_game.net_objects[ni].has_target = true;
    } else {
        g_game.net_objects[ni].last_update_time = now;
        target->transform.position = tgt;
        target->transform.rotation = nr;
        g_game.net_objects[ni].lerp_t = 1.0f;
        g_game.net_objects[ni].target_pos = tgt;
        g_game.net_objects[ni].target_rot = nr;
        g_game.net_objects[ni].has_target = true;
        if (target->physics_body) {
            physics_set_position(g_game.physics, target->physics_body, tgt);
            physics_set_rotation_euler(g_game.physics, target->physics_body, nr);
            if (zero_vel) {
                Vec3 zv = {0, 0, 0};
                physics_set_velocity(g_game.physics, target->physics_body, zv);
                physics_set_angular_velocity(g_game.physics, target->physics_body, zv);
            }
            if (g_game.avatar.has_ground_platform &&
                g_game.avatar.ground_body == target->physics_body) {
                g_game.avatar.ground_prev_pos = tgt;
            }
        }
    }
    target->new_object = false;
}

static void game_camera_screen_ray(float mx, float my, Vec3* origin, Vec3* dir) {
    int sw = g_game.renderer.canvas_width;
    int sh = g_game.renderer.canvas_height;
    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;
    if (mx <= 0.0f && my <= 0.0f) {
        mx = (float)sw * 0.5f;
        my = (float)sh * 0.5f;
    }
    Mat4 view, proj;
    if (g_game.last_view_valid) {
        view = g_game.last_view;
        proj = g_game.last_projection;
    } else {
        float yaw_rad = g_game.camera.yaw * (3.14159265f / 180.0f);
        float pitch_rad = g_game.camera.pitch * (3.14159265f / 180.0f);
        float cos_p = cosf(pitch_rad), sin_p = sinf(pitch_rad);
        float cos_y = cosf(yaw_rad), sin_y = sinf(yaw_rad);
        Vec3 cam_offset = {
            g_game.camera.distance * cos_p * sin_y,
            g_game.camera.distance * sin_p,
            g_game.camera.distance * cos_p * cos_y
        };
        Vec3 cam_eye = vec3_add(g_game.camera.target, cam_offset);
        Vec3 cam_up = {0, 1, 0};
        if (g_game.camera.distance < 0.5f) {
            cam_eye = g_game.camera.target;
            Vec3 look_dir = { cos_p * sin_y, sin_p, cos_p * cos_y };
            Vec3 look_at = { cam_eye.x - look_dir.x, cam_eye.y - look_dir.y, cam_eye.z - look_dir.z };
            view = mat4_look_at(cam_eye, look_at, cam_up);
        } else {
            view = mat4_look_at(cam_eye, g_game.camera.target, cam_up);
        }
        float aspect = (float)sw / (float)sh;
        float fov = g_game.scripts ? client_script_camera_fov(g_game.scripts) : 60.0f;
        proj = mat4_perspective(fov, aspect, PW_CAMERA_NEAR, PW_CAMERA_FAR);
    }
    Mat4 inv_proj = mat4_inverse(proj);
    Mat4 inv_view = mat4_inverse(view);
    Vec3 cam_eye = { inv_view.m[12], inv_view.m[13], inv_view.m[14] };
    float ndc_x = (2.0f * mx / (float)sw) - 1.0f;
    float ndc_y = 1.0f - (2.0f * my / (float)sh);
    Vec4 clip_near = {ndc_x, ndc_y, -1.0f, 1.0f};
    Vec4 eye_near = mat4_mul_vec4(inv_proj, clip_near);
    if (fabsf(eye_near.w) > 1e-8f) {
        eye_near.x /= eye_near.w; eye_near.y /= eye_near.w; eye_near.z /= eye_near.w;
    }
    eye_near.w = 1.0f;
    Vec4 world_near = mat4_mul_vec4(inv_view, eye_near);
    Vec4 clip_far = {ndc_x, ndc_y, 1.0f, 1.0f};
    Vec4 eye_far = mat4_mul_vec4(inv_proj, clip_far);
    if (fabsf(eye_far.w) > 1e-8f) {
        eye_far.x /= eye_far.w; eye_far.y /= eye_far.w; eye_far.z /= eye_far.w;
    }
    eye_far.w = 1.0f;
    Vec4 world_far = mat4_mul_vec4(inv_view, eye_far);
    Vec3 ray_dir = { world_far.x - world_near.x, world_far.y - world_near.y, world_far.z - world_near.z };
    float rlen = vec3_length(ray_dir);
    if (rlen > 0.001f) ray_dir = vec3_scale(ray_dir, 1.0f / rlen);
    if (origin) *origin = cam_eye;
    if (dir) *dir = ray_dir;
}

static bool ray_aabb(Vec3 orig, Vec3 dir, Vec3 c, Vec3 half, float max_dist, float* t_hit) {
    float tmin = 0.0f, tmax = max_dist;
    float o[3] = { orig.x - c.x, orig.y - c.y, orig.z - c.z };
    float d[3] = { dir.x, dir.y, dir.z };
    float h[3] = { half.x, half.y, half.z };
    for (int i = 0; i < 3; i++) {
        if (fabsf(d[i]) < 1e-8f) {
            if (o[i] < -h[i] || o[i] > h[i]) return false;
            continue;
        }
        float inv = 1.0f / d[i];
        float t1 = (-h[i] - o[i]) * inv;
        float t2 = ( h[i] - o[i]) * inv;
        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
        if (t1 > tmin) tmin = t1;
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return false;
    }
    if (t_hit) *t_hit = tmin;
    return tmin <= max_dist && tmax >= 0.0f;
}

static int client_local_part_slot(EntityID entity) {
    if (entity == ENTITY_INVALID) return -1;
    for (int i = 0; i < PW_MAX_CLIENT_PARTS; i++) {
        if (g_client_local_parts[i].used && g_client_local_parts[i].entity == entity)
            return i;
    }
    return -1;
}

static void client_local_part_rebuild_body(int slot) {
    if (slot < 0 || !g_client_local_parts[slot].used) return;
    Entity* ent = scene_get_entity(&g_game.scene, g_client_local_parts[slot].entity);
    if (!ent) return;
    if (ent->physics_body) {
        physics_destroy_body(g_game.physics, ent->physics_body);
        ent->physics_body = 0;
    }
    if (!g_client_local_parts[slot].can_collide || !g_game.physics) return;
    Vec3 sz = ent->transform.scale;
    float hx = sz.x * 0.5f, hy = sz.y * 0.5f, hz = sz.z * 0.5f;
    if (hx < 0.01f) hx = 0.01f;
    if (hy < 0.01f) hy = 0.01f;
    if (hz < 0.01f) hz = 0.01f;
    uint8_t obj_type = g_client_local_parts[slot].shape;
    ColliderType coll_type = collider_for_obj_type(obj_type);
    BodyDesc desc = {
        .type = BODY_STATIC,
        .collider = coll_type,
        .position = ent->transform.position,
        .half_extents = { hx, hy, hz },
        .radius = hx,
        .mass = 0.0f,
        .restitution = 0.3f,
        .friction = 1.0f
    };
    if (obj_type == 3) body_desc_apply_wedge(&desc, hx, hy, hz);
    ent->physics_body = physics_create_body(g_game.physics, &desc);
    Vec3 rot = ent->transform.rotation;
    if (ent->physics_body && (rot.x != 0.0f || rot.y != 0.0f || rot.z != 0.0f))
        physics_set_rotation_euler(g_game.physics, ent->physics_body, rot);
}

EntityID client_script_spawn_local_part(void) {
    int slot = -1;
    for (int i = 0; i < PW_MAX_CLIENT_PARTS; i++) {
        if (!g_client_local_parts[i].used) { slot = i; break; }
    }
    if (slot < 0) return ENTITY_INVALID;
    EntityID eid = scene_create_entity(&g_game.scene);
    Entity* ent = scene_get_entity(&g_game.scene, eid);
    if (!ent) return ENTITY_INVALID;
    ent->transform.position = (Vec3){ 0.0f, 10.0f, 0.0f };
    ent->transform.rotation = (Vec3){ 0.0f, 0.0f, 0.0f };
    ent->transform.scale = (Vec3){ 4.0f, 1.0f, 2.0f };
    ent->material.color = (Vec3){ 0.7f, 0.7f, 0.7f };
    ent->material.alpha = 1.0f;
    ent->material.glow = 0.0f;
    ent->mesh = net_unit_mesh_for_type(0);
    ent->new_object = true;
    ent->static_batch = false;
    ent->render_batched = false;
    g_client_local_parts[slot].used = 1;
    g_client_local_parts[slot].entity = eid;
    g_client_local_parts[slot].shape = 0;
    g_client_local_parts[slot].can_collide = 1;
    strncpy(g_client_local_parts[slot].name, "Part", sizeof(g_client_local_parts[slot].name) - 1);
    g_client_local_parts[slot].name[sizeof(g_client_local_parts[slot].name) - 1] = '\0';
    client_local_part_rebuild_body(slot);
    return eid;
}

void client_script_destroy_local_part(EntityID entity) {
    int slot = client_local_part_slot(entity);
    if (slot < 0) return;
    Entity* ent = scene_get_entity(&g_game.scene, entity);
    if (ent && ent->physics_body && g_game.physics) {
        physics_destroy_body(g_game.physics, ent->physics_body);
        ent->physics_body = 0;
    }
    scene_destroy_entity(&g_game.scene, entity);
    g_client_local_parts[slot].used = 0;
    g_client_local_parts[slot].entity = ENTITY_INVALID;
}

int client_script_is_local_part(EntityID entity) {
    return client_local_part_slot(entity) >= 0;
}

const char* client_script_local_part_name(EntityID entity) {
    int slot = client_local_part_slot(entity);
    if (slot < 0) return NULL;
    return g_client_local_parts[slot].name[0] ? g_client_local_parts[slot].name : "Part";
}

void client_script_local_part_set_name(EntityID entity, const char* name) {
    int slot = client_local_part_slot(entity);
    if (slot < 0) return;
    if (!name) name = "Part";
    strncpy(g_client_local_parts[slot].name, name, sizeof(g_client_local_parts[slot].name) - 1);
    g_client_local_parts[slot].name[sizeof(g_client_local_parts[slot].name) - 1] = '\0';
}

void client_script_local_part_set_collide(EntityID entity, int on) {
    int slot = client_local_part_slot(entity);
    if (slot < 0) return;
    g_client_local_parts[slot].can_collide = on ? 1 : 0;
    client_local_part_rebuild_body(slot);
}

void client_script_local_part_set_shape(EntityID entity, uint8_t obj_type) {
    int slot = client_local_part_slot(entity);
    if (slot < 0) return;
    g_client_local_parts[slot].shape = obj_type;
    Entity* ent = scene_get_entity(&g_game.scene, entity);
    if (ent) ent->mesh = net_unit_mesh_for_type(obj_type);
    client_local_part_rebuild_body(slot);
}

void client_script_local_part_sync_pose(EntityID entity) {
    client_script_part_commit_transform(entity);
}

void client_script_part_commit_transform(EntityID entity) {
    Entity* ent = scene_get_entity(&g_game.scene, entity);
    if (!ent) return;
    if (ent->physics_body && g_game.physics) {
        physics_set_position(g_game.physics, ent->physics_body, ent->transform.position);
        physics_set_rotation_euler(g_game.physics, ent->physics_body, ent->transform.rotation);
        Vec3 zv = {0, 0, 0};
        physics_set_velocity(g_game.physics, ent->physics_body, zv);
        physics_set_angular_velocity(g_game.physics, ent->physics_body, zv);
    }
    for (int ni = 0; ni < g_game.net_object_count; ni++) {
        if (g_game.net_objects[ni].entity != entity) continue;
        g_game.net_objects[ni].target_pos = ent->transform.position;
        g_game.net_objects[ni].target_rot = ent->transform.rotation;
        g_game.net_objects[ni].lerp_start_pos = ent->transform.position;
        g_game.net_objects[ni].lerp_start_rot = ent->transform.rotation;
        g_game.net_objects[ni].lerp_t = 1.0f;
        break;
    }
}

void client_script_part_commit_size(EntityID entity) {
    if (client_script_is_local_part(entity)) {
        client_script_local_part_rebuild(entity);
        return;
    }
    Entity* ent = scene_get_entity(&g_game.scene, entity);
    if (!ent) return;
    for (int ni = 0; ni < g_game.net_object_count; ni++) {
        if (g_game.net_objects[ni].entity != entity) continue;
        g_game.net_objects[ni].size[0] = ent->transform.scale.x;
        g_game.net_objects[ni].size[1] = ent->transform.scale.y;
        g_game.net_objects[ni].size[2] = ent->transform.scale.z;
        break;
    }
}

void client_script_local_part_rebuild(EntityID entity) {
    int slot = client_local_part_slot(entity);
    if (slot < 0) return;
    client_local_part_rebuild_body(slot);
}

int client_script_local_part_aabb_ray(float ox, float oy, float oz,
                                      float dx, float dy, float dz, float max_dist,
                                      float* t_hit, EntityID* entity) {
    Vec3 origin = { ox, oy, oz };
    Vec3 dir = { dx, dy, dz };
    float best = max_dist;
    int best_slot = -1;
    for (int i = 0; i < PW_MAX_CLIENT_PARTS; i++) {
        if (!g_client_local_parts[i].used) continue;
        Entity* ent = scene_get_entity(&g_game.scene, g_client_local_parts[i].entity);
        if (!ent || !ent->active) continue;
        Vec3 half = {
            ent->transform.scale.x * 0.5f,
            ent->transform.scale.y * 0.5f,
            ent->transform.scale.z * 0.5f
        };
        float t = 0;
        if (!ray_aabb(origin, dir, ent->transform.position, half, best, &t))
            continue;
        if (t < 0.0f || t >= best) continue;
        best = t;
        best_slot = i;
    }
    if (best_slot < 0) return 0;
    if (t_hit) *t_hit = best;
    if (entity) *entity = g_client_local_parts[best_slot].entity;
    return 1;
}

static int net_index_for_entity(EntityID entity) {
    for (int ni = 0; ni < g_game.net_object_count; ni++) {
        if (g_game.net_objects[ni].entity == entity)
            return ni;
    }
    return -1;
}

static int net_index_for_body(PhysicsBodyID body) {
    if (!body) return -1;
    for (int ni = 0; ni < g_game.net_object_count; ni++) {
        Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
        if (ent && ent->physics_body == body)
            return ni;
    }
    return -1;
}

static int pick_part_at_cursor(float mx, float my, EntityID* entity_out, bool ignore_avatar) {
    if (entity_out) *entity_out = ENTITY_INVALID;
    if (!g_game.world_ready || g_game.show_login || g_game.menu.open)
        return -1;
    if (g_game.avatar.dead) return -1;
    Vec3 origin, dir;
    game_camera_screen_ray(mx, my, &origin, &dir);
    Vec3 ray_start = vec3_add(origin, vec3_scale(dir, 0.2f));
    if (ignore_avatar && g_game.avatar.body)
        physics_disable_geom(g_game.physics, g_game.avatar.body);
    RaycastHit hit = physics_raycast(g_game.physics, ray_start, dir, 64.0f);
    if (ignore_avatar && g_game.avatar.body && !g_game.avatar.dead)
        physics_enable_geom(g_game.physics, g_game.avatar.body);

    int ni = -1;
    EntityID entity = ENTITY_INVALID;
    Vec3 hit_pt = {0};
    if (hit.hit) {
        ni = net_index_for_body(hit.body);
        if (ni >= 0) {
            entity = g_game.net_objects[ni].entity;
            hit_pt = hit.point;
        }
    }
    if (ni < 0) {
        float best_t = 64.0f;
        for (int i = 0; i < g_game.net_object_count; i++) {
            Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[i].entity);
            if (!ent || !ent->active) continue;
            Vec3 half = {
                g_game.net_objects[i].size[0] * 0.5f,
                g_game.net_objects[i].size[1] * 0.5f,
                g_game.net_objects[i].size[2] * 0.5f
            };
            if (half.x < 0.01f) half.x = ent->transform.scale.x * 0.5f;
            if (half.y < 0.01f) half.y = ent->transform.scale.y * 0.5f;
            if (half.z < 0.01f) half.z = ent->transform.scale.z * 0.5f;
            float t = 0;
            if (!ray_aabb(ray_start, dir, ent->transform.position, half, best_t, &t))
                continue;
            if (t < 0.0f || t >= best_t) continue;
            best_t = t;
            ni = i;
            entity = ent->id;
            hit_pt = vec3_add(ray_start, vec3_scale(dir, t));
        }
    }
    if (ni < 0 || entity == ENTITY_INVALID) return -1;

    Vec3 apos = g_game.avatar.pos;
    Entity* ent = scene_get_entity(&g_game.scene, entity);
    Vec3 c = ent ? ent->transform.position : hit_pt;
    float dx = c.x - apos.x, dy = c.y - apos.y, dz = c.z - apos.z;
    if (dx * dx + dy * dy + dz * dz > 32.0f * 32.0f)
        return -1;
    if (entity_out) *entity_out = entity;
    return ni;
}

static bool try_part_click(float mx, float my) {
    EntityID entity = ENTITY_INVALID;
    int ni = pick_part_at_cursor(mx, my, &entity, true);
    if (ni < 0 || entity == ENTITY_INVALID) return false;

    if (g_game.scripts)
        client_script_fire_clicked(g_game.scripts, entity);
    if (g_game.multiplayer && g_game.net.state == NET_STATE_CONNECTED) {
        uint32_t oid = g_game.net_objects[ni].net_id;
        uint8_t payload[4];
        memcpy(payload, &oid, 4);
        client_script_send_remote("PartClicked", payload, 4);
    }
    return true;
}

static void update_click_cursor(const InputState* input, bool chat_active) {
    bool pointer = false;
    if (!chat_active && g_game.world_ready && !g_game.show_login &&
        !g_game.show_disconnect && !g_game.show_kick &&
        !g_game.avatar.dead && g_game.equipped_tool == 0 &&
        input && !input->mouse_right) {
        EntityID entity = ENTITY_INVALID;
        int ni = pick_part_at_cursor(input->mouse_x, input->mouse_y, &entity, false);
        if (ni >= 0 && entity != ENTITY_INVALID) {
            if (g_game.net_objects[ni].clickable)
                pointer = true;
            else if (g_game.scripts && client_script_part_has_clicked(g_game.scripts, entity))
                pointer = true;
        }
    }
    platform_set_cursor_pointer(pointer);
}

static void interpolate_net_objects(float dt) {
    if (!g_game.multiplayer) return;
    for (int ni = 0; ni < g_game.net_object_count; ni++) {
        if (g_game.net_objects[ni].net_owned) continue;
        if (!g_game.net_objects[ni].has_target) continue;
        if (g_game.net_objects[ni].lerp_t >= 1.0f) continue;
        Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
        if (!ent) continue;

        float dur = g_game.net_objects[ni].lerp_duration;
        if (dur < 0.016f) dur = 0.075f;
        g_game.net_objects[ni].lerp_t += dt / dur;
        if (g_game.net_objects[ni].lerp_t > 1.0f) g_game.net_objects[ni].lerp_t = 1.0f;

        float t = g_game.net_objects[ni].lerp_t;
        Vec3 s = g_game.net_objects[ni].lerp_start_pos;
        Vec3 e = g_game.net_objects[ni].target_pos;
        Vec3 pos = {
            s.x + (e.x - s.x) * t,
            s.y + (e.y - s.y) * t,
            s.z + (e.z - s.z) * t
        };
        ent->transform.position = pos;

        Vec3 sr = g_game.net_objects[ni].lerp_start_rot;
        Vec3 er = g_game.net_objects[ni].target_rot;
        Vec3 rot = {
            lerp_angle_deg(sr.x, er.x, t),
            lerp_angle_deg(sr.y, er.y, t),
            lerp_angle_deg(sr.z, er.z, t)
        };
        ent->transform.rotation = rot;

        if (ent->physics_body) {
            physics_set_position(g_game.physics, ent->physics_body, pos);
            physics_set_rotation_euler(g_game.physics, ent->physics_body, rot);
            Vec3 zv = {0, 0, 0};
            physics_set_velocity(g_game.physics, ent->physics_body, zv);
            physics_set_angular_velocity(g_game.physics, ent->physics_body, zv);
        }
    }
}

static void remote_player_snap_pose(int rp, Entity* ent, Vec3 feet, float yaw) {
    if (ent) {
        ent->transform.position = feet;
        ent->transform.rotation.y = yaw;
    }
    g_game.remote_players[rp].lerp_start_pos = feet;
    g_game.remote_players[rp].target_pos = feet;
    g_game.remote_players[rp].lerp_start_yaw = yaw;
    g_game.remote_players[rp].target_yaw = yaw;
    g_game.remote_players[rp].lerp_t = 1.0f;
    g_game.remote_players[rp].has_target = true;
    g_game.remote_players[rp].last_vel = (Vec3){0, 0, 0};
    g_game.remote_players[rp].last_update_time = platform_get_time();
}

static void interpolate_remote_players(float dt) {
    if (!g_game.multiplayer) return;
    for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
        if (!g_game.remote_players[rp].active) continue;
        if (!g_game.remote_players[rp].has_target) continue;
        if (g_game.remote_players[rp].dead) continue;

        Entity* ent = scene_get_entity(&g_game.scene, g_game.remote_players[rp].entity);
        if (!ent) continue;

        float dur = g_game.remote_players[rp].lerp_duration;
        if (dur < 0.028f) dur = 0.05f;
        g_game.remote_players[rp].lerp_t += dt / dur;
        float tmax = 1.0f + 0.06f / dur;
        if (g_game.remote_players[rp].lerp_t > tmax)
            g_game.remote_players[rp].lerp_t = tmax;

        float t = g_game.remote_players[rp].lerp_t;
        Vec3 s = g_game.remote_players[rp].lerp_start_pos;
        Vec3 e = g_game.remote_players[rp].target_pos;
        float span2 = (e.x - s.x) * (e.x - s.x) + (e.y - s.y) * (e.y - s.y)
                    + (e.z - s.z) * (e.z - s.z);

        if (span2 > 64.0f && t > 1.0f) t = 1.0f;
        Vec3 new_pos = {
            s.x + (e.x - s.x) * t,
            s.y + (e.y - s.y) * t,
            s.z + (e.z - s.z) * t
        };
        ent->transform.position = new_pos;

        float y = g_game.remote_players[rp].lerp_start_yaw +
                  (g_game.remote_players[rp].target_yaw -
                   g_game.remote_players[rp].lerp_start_yaw) * t;
        ent->transform.rotation.y = wrap_deg360(y);
    }
}

static void freecam_init_from_camera(void) {
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
    float yaw_rad = g_game.camera.yaw * (float)M_PI / 180.0f;
    float pitch_rad = g_game.camera.pitch * (float)M_PI / 180.0f;
    float cos_p = cosf(pitch_rad), sin_p = sinf(pitch_rad);
    float cos_y = cosf(yaw_rad), sin_y = sinf(yaw_rad);
    float dist = g_game.camera.distance;
    if (dist < 0.5f) {
        g_game.freecam_pos = g_game.camera.target;
    } else {
        Vec3 dir = { cos_p * sin_y, sin_p, cos_p * cos_y };
        g_game.freecam_pos = (Vec3){
            g_game.camera.target.x + dir.x * dist,
            g_game.camera.target.y + dir.y * dist,
            g_game.camera.target.z + dir.z * dist
        };
    }
    g_game.freecam_yaw = g_game.camera.yaw;
    g_game.freecam_pitch = g_game.camera.pitch;
}

#ifdef VIDACTOR
static void vid_puppets_clear(void) {
    for (int i = 0; i < VIDACTOR_MAX_ACTORS; i++) {
        if (g_game.vid_puppets[i].active) {
            avatar_anim_detach(&g_game.vid_puppets[i].anim);
            if (g_game.vid_puppets[i].entity)
                scene_destroy_entity(&g_game.scene, g_game.vid_puppets[i].entity);
        }
        memset(&g_game.vid_puppets[i], 0, sizeof(g_game.vid_puppets[i]));
    }
}

static void vid_puppet_ensure(int i) {
    if (i < 0 || i >= VIDACTOR_MAX_ACTORS) return;
    if (g_game.vid_puppets[i].active) return;
    ensure_avatar_bodies_loaded();
    EntityID eid = scene_create_entity(&g_game.scene);
    Entity* ent = scene_get_entity(&g_game.scene, eid);
    if (ent) {
        ent->transform.scale = (Vec3){AVATAR_SCALE, AVATAR_SCALE, AVATAR_SCALE};
        ent->mesh = NULL;
    }
    static const Vec3 tint[VIDACTOR_MAX_ACTORS] = {
        {0.92f, 0.75f, 0.55f},
        {0.55f, 0.78f, 0.92f},
        {0.72f, 0.88f, 0.55f},
        {0.90f, 0.60f, 0.75f},
    };
    g_game.vid_puppets[i].active = true;
    g_game.vid_puppets[i].entity = eid;
    g_game.vid_puppets[i].skin_color = tint[i];
    g_game.vid_puppets[i].tex_shirt = g_game.local_tex_shirt;
    g_game.vid_puppets[i].tex_pants = g_game.local_tex_pants;
    g_game.vid_puppets[i].tex_head = g_game.local_tex_head;
    apply_mesh_flags_to_anim(&g_game.vid_puppets[i].anim,
                             g_game.local_equipped_package, NULL);
}

static void vid_stage_update(double dt) {
    static bool was_staging = false;
    bool staging = vidactor_is_staging();

    if (staging && !was_staging) {

        g_game.allow_freecam = true;
        if (g_game.camera_mode != CAM_MODE_FREECAM) {
            freecam_init_from_camera();
            g_game.camera_mode = CAM_MODE_FREECAM;
        }
        g_game.local_transparency = 1.0f;
    }
    if (!staging && was_staging) {
        vid_puppets_clear();
        g_game.local_transparency = 0.0f;

        if (g_game.camera_mode == CAM_MODE_FREECAM)
            g_game.camera_mode = CAM_MODE_NORMAL;
    }
    was_staging = staging;

    if (!staging) return;

    g_game.allow_freecam = true;
    if (g_game.camera_mode != CAM_MODE_FREECAM) {
        freecam_init_from_camera();
        g_game.camera_mode = CAM_MODE_FREECAM;
    }
    g_game.local_transparency = 1.0f;

    VidPose poses[VIDACTOR_MAX_ACTORS];
    int n = vidactor_stage_tick(dt, poses);
    (void)n;
    for (int i = 0; i < VIDACTOR_MAX_ACTORS; i++) {
        if (!poses[i].active) {
            if (g_game.vid_puppets[i].active) {
                avatar_anim_detach(&g_game.vid_puppets[i].anim);
                if (g_game.vid_puppets[i].entity)
                    scene_destroy_entity(&g_game.scene, g_game.vid_puppets[i].entity);
                memset(&g_game.vid_puppets[i], 0, sizeof(g_game.vid_puppets[i]));
            }
            continue;
        }
        vid_puppet_ensure(i);
        Entity* ent = scene_get_entity(&g_game.scene, g_game.vid_puppets[i].entity);
        if (!ent) continue;

        ent->transform.position = (Vec3){
            poses[i].pos.x,
            poses[i].pos.y - AVATAR_FEET_OFFSET,
            poses[i].pos.z
        };
        ent->transform.rotation.y = poses[i].yaw;
        g_game.vid_puppets[i].anim_state = poses[i].anim;
        g_game.vid_puppets[i].move_speed = poses[i].move_speed;
        float spd = poses[i].move_speed;
        AnimState st = (AnimState)poses[i].anim;
        if (st == ANIM_STATE_WALKING && spd < 0.5f) spd = 16.0f;
        if (st != ANIM_STATE_WALKING && st != ANIM_STATE_JUMPING &&
            st != ANIM_STATE_IDLE && st != ANIM_STATE_CLIMBING &&
            !(st >= ANIM_STATE_DANCING && st <= ANIM_STATE_DANCING3))
            st = ANIM_STATE_IDLE;
        avatar_anim_update(&g_game.vid_puppets[i].anim, st, spd, (float)dt);
    }
}
#endif

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
bool chat_handle_key(int keycode, bool shift, bool ctrl) {

    if (keycode == 27 && !g_game.show_login && (g_game.multiplayer || g_game.world_ready)) {
        if (g_game.chat.focused) {
            chat_blur(&g_game.chat);
        } else {
            game_menu_toggle(&g_game.menu);
        }
        return true;
    }
    return chat_on_key(&g_game.chat, keycode, shift, ctrl);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
bool chat_handle_char(unsigned int codepoint) {
    return chat_on_char(&g_game.chat, codepoint);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
bool chat_handle_copy(void) {
    return chat_copy_input(&g_game.chat);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
bool chat_handle_paste(void) {
    return chat_paste_text(&g_game.chat, platform_clipboard_get());
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
bool chat_handle_cut(void) {
    return chat_cut_input(&g_game.chat);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
bool chat_handle_select_all(void) {
    return chat_select_all_input(&g_game.chat);
}

static void apply_menu_action(MenuAction action) {
    g_fps_limit_setting = g_game.menu.fps_limit;
    if (action == MENU_ACTION_RESPAWN) {
        if (!g_game.reset_enabled) {
            strncpy(g_game.script_ui.toast, "Respawn is disabled in this place",
                    sizeof(g_game.script_ui.toast) - 1);
            g_game.script_ui.toast_timer = 2.0f;
            g_game.script_ui.toast_alpha = 1.0f;
            g_game.script_ui.toast_yoff = 0.0f;
        } else if (g_game.multiplayer && g_game.net.state == NET_STATE_CONNECTED) {
            uint8_t evt_buf[32];
            const char* evt_name = "ResetCharacter";
            uint8_t name_len = (uint8_t)strlen(evt_name);
            evt_buf[0] = name_len;
            memcpy(evt_buf + 1, evt_name, name_len);
            net_client_send(&g_game.net, MSG_REMOTE_EVENT, evt_buf, 1 + name_len);
        } else {
            player_respawn();
        }
    } else if (action == MENU_ACTION_AVATAR_EDITOR) {
#ifndef __EMSCRIPTEN__
        open_avatar_editor_ui();
        g_game.menu.open = false;
#else

        g_game.menu.open = false;
        EM_ASM({
            try {
                var url = '/avatar/';
                if (window.parent && window.parent !== window) {
                    window.parent.postMessage({type: 'open_avatar_editor', url: url}, '*');
                } else {
                    window.open(url, '_blank');
                }
            } catch (e) {
                window.location.href = '/avatar/';
            }
        });
#endif
    } else if (action == MENU_ACTION_CATALOG) {
#ifndef __EMSCRIPTEN__
        open_catalog_ui();
        g_game.menu.open = false;
#else
        g_game.menu.open = false;
        EM_ASM({
            try {
                var url = '/catalog/';
                if (window.parent && window.parent !== window) {
                    window.parent.postMessage({type: 'open_catalog', url: url}, '*');
                } else {
                    window.open(url, '_blank');
                }
            } catch (e) {
                window.location.href = '/catalog/';
            }
        });
#endif
    } else if (action == MENU_ACTION_LEAVE_GAME) {
        audio_stop_music();
        net_client_disconnect(&g_game.net);
        g_game.multiplayer = false;
        g_game.loading_world = false;
        g_game.world_ready = false;
        g_game.avatar_ready = false;
#ifdef __EMSCRIPTEN__
        EM_ASM({
            try {
                if (window.parent && window.parent !== window) {
                    window.parent.postMessage({type: 'leave_game'}, '*');
                } else {
                    window.location.href = '/';
                }
            } catch (e) {
                window.location.href = '/';
            }
        });
#else
        leave_game_ui();
#endif
    }
}

static bool pw_player_is_guest(const char* name, uint32_t account_id) {
    if (account_id == 0) return true;
    if (!name || strncmp(name, "Guest", 5) != 0) return false;
    char c = name[5];
    return c == '\0' || (c >= '0' && c <= '9');
}

bool chat_handle_click(float x, float y) {
#ifndef __EMSCRIPTEN__
    if (catalog_ui_blocks_input(&g_game.catalog_ui)) {
        catalog_ui_on_mousedown(&g_game.catalog_ui, x, y);
        return true;
    }
    if (avatar_editor_blocks_input(&g_game.avatar_editor)) {
        avatar_editor_on_mousedown(&g_game.avatar_editor, x, y, 0);
        poll_avatar_editor_save();
        return true;
    }

    if (g_game.show_login) return false;
#endif

    if (g_game.menu.open) {
        g_game.menu.reset_enabled = g_game.reset_enabled;
        game_menu_on_mousedown(&g_game.menu, x, y,
                               g_game.renderer.canvas_width, g_game.renderer.canvas_height);
        return true;
    }

    {
        float uis = g_game.chat.ui_scale > 0.1f ? g_game.chat.ui_scale : 1.0f;
        float margin = 10.0f * uis;
        float btn_size = 40.0f * uis;
        if (x >= margin && x <= margin + btn_size &&
            y >= margin && y <= margin + btn_size) {
            game_menu_toggle(&g_game.menu);
            return true;
        }
    }

    if (chat_on_click(&g_game.chat, x, y,
                      g_game.renderer.canvas_width, g_game.renderer.canvas_height)) {
        return true;
    }

    if (g_game.tool_count > 0) {
        float tool_lift = 0.0f;
        if (g_game.music_cred.title[0] && g_game.music_cred.alpha > 0.01f) {
            float uis = g_game.ui_scale > 0.1f ? g_game.ui_scale : 1.0f;
            float mscale = 0.72f;
            tool_lift = (chat_music_credit_stack_height(&g_game.chat, mscale) + 8.0f * uis)
                        * g_game.music_cred.alpha;
        }
        int slot = chat_toolbar_hit_test(&g_game.chat, x, y, g_game.tool_count,
                                         tool_lift,
                                         g_game.renderer.canvas_width, g_game.renderer.canvas_height);
        if (slot > 0 && slot <= g_game.tool_count && g_game.tools[slot - 1].available) {
            g_game.equipped_tool = (g_game.equipped_tool == slot) ? 0 : slot;
            return true;
        }
    }

    {
        int sw = g_game.renderer.canvas_width;
        int sh = g_game.renderer.canvas_height;
        int action = 0;
        uint32_t uid = 0;
        char sname[32] = {0};
        if (social_on_click(&g_game.social, x, y, sw, sh, &action, &uid, sname, sizeof(sname))) {
            if (action == 1) {
                social_open_profile(uid, sname);
            } else if (action == 2 && uid > 0 && g_game.multiplayer && g_game.net.state == NET_STATE_CONNECTED) {
                uint8_t buf[5];
                buf[0] = 1;
                memcpy(buf + 1, &uid, 4);
                net_client_send(&g_game.net, MSG_FRIEND, buf, 5);
            } else if (action == 2 && uid == 0) {
                snprintf(g_game.social.status, sizeof(g_game.social.status), "Guests can't be friended");
                g_game.social.send_busy = false;
            } else if ((action == 3 || action == 4) && uid > 0 &&
                       g_game.multiplayer && g_game.net.state == NET_STATE_CONNECTED) {
                uint8_t buf[5];
                buf[0] = (uint8_t)(action == 3 ? 2 : 3);
                memcpy(buf + 1, &uid, 4);
                net_client_send(&g_game.net, MSG_FRIEND, buf, 5);
                if (action == 3)
                    social_set_friend_rel(&g_game.social, uid, SOCIAL_REL_FRIENDS);
            } else if (action == 5 && uid > 0 &&
                       g_game.multiplayer && g_game.net.state == NET_STATE_CONNECTED) {
                uint8_t buf[5];
                buf[0] = 5;
                memcpy(buf + 1, &uid, 4);
                net_client_send(&g_game.net, MSG_FRIEND, buf, 5);
            } else if (action == 6 && uid > 0 &&
                       g_game.multiplayer && g_game.net.state == NET_STATE_CONNECTED) {
                uint8_t buf[5];
                buf[0] = 6;
                memcpy(buf + 1, &uid, 4);
                net_client_send(&g_game.net, MSG_FRIEND, buf, 5);
            }
            return true;
        }

        if (g_game.multiplayer) {
            float cx = 0, cy = 0;
            int idx = chat_player_list_hit_test(&g_game.chat, x, y, &cx, &cy);
            if (idx > 0 && idx < g_game.chat.pl_hit_count) {

                const char* pname = g_game.chat.pl_entry_names[idx];
                uint32_t pid = 0, account_id = 0;
                for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
                    if (!g_game.remote_players[rp].active) continue;
                    if (strcmp(g_game.remote_players[rp].name, pname) == 0) {
                        pid = g_game.remote_players[rp].id;
                        account_id = g_game.remote_players[rp].account_id;
                        break;
                    }
                }
                if (!pw_player_is_guest(pname, account_id)) {
                    float card_w = 220.0f * (g_game.ui_scale > 0.1f ? g_game.ui_scale : 1.0f);
                    social_open_card(&g_game.social, pname, pid, account_id,
                                     cx - card_w, cy);
                    if (account_id > 0 && g_game.net.state == NET_STATE_CONNECTED &&
                        g_game.account_id > 0) {
                        uint8_t buf[5];
                        buf[0] = 4;
                        memcpy(buf + 1, &account_id, 4);
                        net_client_send(&g_game.net, MSG_FRIEND, buf, 5);
                    }
                }
                return true;
            }

            char ntag[32] = {0};
            if (chat_nametag_hit_test(&g_game.chat, x, y, ntag, sizeof(ntag)) >= 0) {
                uint32_t pid = 0, account_id = 0;
                float cx = x, cy = y;
                for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
                    if (!g_game.remote_players[rp].active) continue;
                    if (strcmp(g_game.remote_players[rp].name, ntag) == 0) {
                        pid = g_game.remote_players[rp].id;
                        account_id = g_game.remote_players[rp].account_id;
                        break;
                    }
                }
                if (!pw_player_is_guest(ntag, account_id)) {
                    float card_w = 220.0f * (g_game.ui_scale > 0.1f ? g_game.ui_scale : 1.0f);
                    social_open_card(&g_game.social, ntag, pid, account_id,
                                     cx - card_w * 0.5f, cy + 8.0f);
                    if (account_id > 0 && g_game.net.state == NET_STATE_CONNECTED &&
                        g_game.account_id > 0) {
                        uint8_t buf[5];
                        buf[0] = 4;
                        memcpy(buf + 1, &account_id, 4);
                        net_client_send(&g_game.net, MSG_FRIEND, buf, 5);
                    }
                }
                return true;
            }
        }
    }
    return false;
}

bool chat_handle_mouseup(float x, float y) {
#ifndef __EMSCRIPTEN__
    if (catalog_ui_blocks_input(&g_game.catalog_ui)) {
        catalog_ui_on_mouseup(&g_game.catalog_ui, x, y);
        return true;
    }
    if (avatar_editor_blocks_input(&g_game.avatar_editor)) {
        avatar_editor_on_mouseup(&g_game.avatar_editor, x, y, 0);
        poll_avatar_editor_save();
        return true;
    }
#endif
    if (!g_game.menu.open) return false;
    g_game.menu.reset_enabled = g_game.reset_enabled;
    MenuAction action = game_menu_on_mouseup(&g_game.menu, x, y,
                                             g_game.renderer.canvas_width,
                                             g_game.renderer.canvas_height);
    apply_menu_action(action);
    return true;
}

#ifndef __EMSCRIPTEN__
static void pack_u32_be(uint8_t* out, uint32_t v) {
    out[0] = (v >> 24) & 0xFF;
    out[1] = (v >> 16) & 0xFF;
    out[2] = (v >> 8) & 0xFF;
    out[3] = v & 0xFF;
}
#endif

static void apply_skin_hex(const char* hex, Vec3* out) {
    *out = (Vec3){0.918f, 0.918f, 0.918f};
    if (hex && hex[0] == '#') {
        unsigned int h = 0;
        if (sscanf(hex + 1, "%06x", &h) == 1) {
            *out = (Vec3){
                ((h >> 16) & 0xFF) / 255.0f,
                ((h >> 8) & 0xFF) / 255.0f,
                (h & 0xFF) / 255.0f
            };
        }
    }
}

static void reload_local_avatar_from_ids(const char* skin_hex, int shirt, int pants, int head,
                                         const int accessories[PW_MAX_EQUIPPED_ACCESSORIES]) {
    strncpy(g_game.local_skin_hex, skin_hex && skin_hex[0] ? skin_hex : "#eaeaea", sizeof(g_game.local_skin_hex) - 1);
    g_game.local_equipped_shirt = shirt;
    g_game.local_equipped_pants = pants;
    g_game.local_equipped_head = head;
    for (int i = 0; i < PW_MAX_EQUIPPED_ACCESSORIES; i++) {
        g_game.local_equipped_accessories[i] = accessories ? accessories[i] : 0;
    }
    g_game.local_equipped_accessory = g_game.local_equipped_accessories[0];
    apply_skin_hex(g_game.local_skin_hex, &g_game.skin_color);

    char tpath[256];
    const char* host = g_game.host[0] ? g_game.host : "https://polyworld.games";
    if (shirt > 0) snprintf(tpath, sizeof(tpath), "%s/uploads/shirts/%d.png", host, shirt);
    else snprintf(tpath, sizeof(tpath), "%s/uploads/shirts/guest.png", host);
    platform_load_file(tpath, on_avatar_texture_loaded, (void*)(intptr_t)0);
    if (pants > 0) snprintf(tpath, sizeof(tpath), "%s/uploads/pants/%d.png", host, pants);
    else snprintf(tpath, sizeof(tpath), "%s/uploads/pants/guest.png", host);
    platform_load_file(tpath, on_avatar_texture_loaded, (void*)(intptr_t)1);
    if (head > 0) snprintf(tpath, sizeof(tpath), "%s/uploads/heads/%d.png", host, head);
    else snprintf(tpath, sizeof(tpath), "%s/uploads/heads/19.png", host);
    platform_load_file(tpath, on_avatar_texture_loaded, (void*)(intptr_t)2);

    g_game.local_equipped_package = normalize_mesh_flags(g_game.local_equipped_package);
    ensure_avatar_bodies_loaded();
    refresh_local_avatar_meshes();

    uint32_t acc_ids[PW_MAX_EQUIPPED_ACCESSORIES];
    for (int i = 0; i < PW_MAX_EQUIPPED_ACCESSORIES; i++)
        acc_ids[i] = (uint32_t)g_game.local_equipped_accessories[i];
    load_player_accessories(0, acc_ids);
    apply_local_avatar();
}

#ifndef __EMSCRIPTEN__
static bool g_vr_hub_play_pending = false;

static void copy_login_identity(LoginScreen* ls) {
    strncpy(g_game.username, ls->ticket_username[0] ? ls->ticket_username : "Guest",
            sizeof(g_game.username) - 1);
    g_game.account_id = (uint32_t)ls->user_id;
    if (ls->session_token[0]) {
        strncpy(g_game.session_token, ls->session_token, sizeof(g_game.session_token) - 1);
    }

    g_game.local_equipped_shirt = ls->equipped_shirt;
    g_game.local_equipped_pants = ls->equipped_pants;
    g_game.local_equipped_head = ls->equipped_head;
    g_game.local_equipped_package = ls->equipped_package;
    memcpy(g_game.local_equipped_accessories, ls->equipped_accessories,
           sizeof(g_game.local_equipped_accessories));
    g_game.local_equipped_accessory = ls->equipped_accessory;
    for (int ei = 0; ei < PW_MAX_EQUIPPED_EMOTES; ei++) {
        g_game.local_equipped_emotes[ei] = (uint32_t)ls->equipped_emotes[ei];
        g_game.local_emote_anims[ei] = (uint8_t)(ls->emote_anims[ei] > 0 ? ls->emote_anims[ei] : 0);
        snprintf(g_game.local_emote_names[ei], PW_EMOTE_NAME_LEN, "%s", ls->emote_names[ei]);
    }
    {
        int any = 0;
        for (int ei = 0; ei < PW_MAX_EQUIPPED_EMOTES; ei++)
            if (g_game.local_equipped_emotes[ei]) { any = 1; break; }
        if (!any) {
            emote_default_loadout(g_game.local_equipped_emotes);
            emote_default_anim_bases(g_game.local_emote_anims);
            emote_default_names(g_game.local_emote_names);
        }
    }
    local_emote_fill_meta_gaps();
    strncpy(g_game.local_skin_hex, ls->skin_color[0] ? ls->skin_color : "#eaeaea",
            sizeof(g_game.local_skin_hex) - 1);
    if (ls->skin_color[0] == '#') {
        unsigned int hex = 0;
        sscanf(ls->skin_color + 1, "%06x", &hex);
        g_game.skin_color = (Vec3){
            ((hex >> 16) & 0xFF) / 255.0f,
            ((hex >> 8) & 0xFF) / 255.0f,
            (hex & 0xFF) / 255.0f
        };
    }
}

#ifdef VR
static void enter_vr_hub(LoginScreen* ls) {
    if (!ls || !g_game.vr.active) return;
    copy_login_identity(ls);
    vr_hub_shutdown();
    vr_hub_set_active(true);
    g_game.show_login = false;
    if (!platform_was_resized_by_user())
        platform_set_window_size(1280, 720);
    clear_game_world();
    g_game.multiplayer = false;
    g_game.auth_sent = false;
    g_game.allow_freecam = false;
    g_game.reset_enabled = true;
    g_game.avatar.health = 100;
    g_game.avatar.dead = false;
    g_game.tool_count = 0;
    g_game.equipped_tool = 0;
    g_game.loading_world = true;
    g_game.local_equipped_package = normalize_mesh_flags(ls->equipped_package);
    reload_local_avatar_from_ids(
        ls->skin_color[0] ? ls->skin_color :
        (g_game.local_skin_hex[0] ? g_game.local_skin_hex : "#eaeaea"),
        ls->equipped_shirt, ls->equipped_pants,
        ls->equipped_head, ls->equipped_accessories);
    discord_update_presence("VR Hub", "Choosing a game", 1, 1, NULL, 0, false);
    platform_load_file(VR_HUB_PLACE_PATH, on_world_loaded, NULL);
}
#endif

static void consume_login_play(LoginScreen* ls) {
    g_game.show_login = false;
    int game_id = ls->game_id;
    bool offline = ls->offline_play;
    ls->offline_play = false;

    if (!platform_was_resized_by_user()) {
        platform_set_window_size(1280, 720);
    }
    g_game.game_id = game_id;
    g_game.loading_world = true;
    copy_login_identity(ls);

    if (offline) {
        const char* place_path = NULL;
        const char* place_title = "Offline";
        if (ls->selected_game >= 0 && ls->selected_game < ls->game_count) {
            place_path = ls->games[ls->selected_game].local_path;
            if (ls->games[ls->selected_game].title[0])
                place_title = ls->games[ls->selected_game].title;
        }
        if (!place_path || !place_path[0]) {
            g_game.loading_world = false;
            return_to_games_menu();
        } else {
            char details[128];
            snprintf(details, sizeof(details), "Playing %s", place_title);
            discord_update_presence(details, "Offline", 1, 1, NULL, 0, false);

            clear_game_world();
            g_game.multiplayer = false;
            g_game.auth_sent = false;
            g_game.allow_freecam = true;
            g_game.reset_enabled = true;
            g_game.avatar.health = 100;
            g_game.avatar.dead = false;
            g_game.tool_count = 0;
            g_game.equipped_tool = 0;

            g_game.local_equipped_package = normalize_mesh_flags(ls->equipped_package);
            reload_local_avatar_from_ids(
                ls->skin_color[0] ? ls->skin_color :
                (g_game.local_skin_hex[0] ? g_game.local_skin_hex : "#eaeaea"),
                ls->equipped_shirt, ls->equipped_pants,
                ls->equipped_head, ls->equipped_accessories);

            platform_load_file(place_path, on_world_loaded, NULL);
        }
    } else if (net_client_connect(&g_game.net, pw_tcp_host(), pw_tcp_port())) {
        clear_game_world();
        {
            char details[128];
            char join_secret[32];
            snprintf(details, sizeof(details), "Playing %s",
                     ls->games[ls->selected_game].title[0] ? ls->games[ls->selected_game].title : "a game");
            snprintf(join_secret, sizeof(join_secret), "%d", game_id);
            discord_update_presence(details, "In-game", 1, 64, join_secret, game_id, true);
        }
        g_game.multiplayer = true;
        g_game.auth_sent = true;
        net_client_send_auth_ticket(&g_game.net, game_id, ls->ticket);

        g_game.local_equipped_package = normalize_mesh_flags(ls->equipped_package);
        reload_local_avatar_from_ids(
            ls->skin_color[0] ? ls->skin_color :
            (g_game.local_skin_hex[0] ? g_game.local_skin_hex : "#eaeaea"),
            ls->equipped_shirt, ls->equipped_pants,
            ls->equipped_head, ls->equipped_accessories);
    } else {
        g_game.loading_world = false;
        return_to_games_menu();
    }
}

static void send_appearance_to_server(void) {
    if (!g_game.multiplayer || g_game.net.state != NET_STATE_CONNECTED) return;

    uint8_t buf[7 + 16 + PW_MAX_EQUIPPED_ACCESSORIES * 4 + PW_MAX_EQUIPPED_EMOTES * 4];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, g_game.local_skin_hex, 7);
    pack_u32_be(buf + 7, (uint32_t)g_game.local_equipped_shirt);
    pack_u32_be(buf + 11, (uint32_t)g_game.local_equipped_pants);
    pack_u32_be(buf + 15, (uint32_t)g_game.local_equipped_head);
    pack_u32_be(buf + 19, (uint32_t)normalize_mesh_flags(g_game.local_equipped_package));
    for (int i = 0; i < PW_MAX_EQUIPPED_ACCESSORIES; i++)
        pack_u32_be(buf + 23 + i * 4, (uint32_t)g_game.local_equipped_accessories[i]);
    size_t emote_off = 23 + (size_t)PW_MAX_EQUIPPED_ACCESSORIES * 4;
    for (int i = 0; i < PW_MAX_EQUIPPED_EMOTES; i++)
        pack_u32_be(buf + emote_off + (size_t)i * 4, g_game.local_equipped_emotes[i]);
    net_client_send(&g_game.net, MSG_APPEARANCE_UPDATE, buf, sizeof(buf));
}

static void open_avatar_editor_ui(void) {
    const char* host = g_game.host[0] ? g_game.host : "https://polyworld.games";
    const char* tok = g_game.session_token[0] ? g_game.session_token : g_game.login_screen.session_token;
    const char* skin = g_game.local_skin_hex[0] ? g_game.local_skin_hex :
                       (g_game.login_screen.skin_color[0] ? g_game.login_screen.skin_color : "#eaeaea");
    int shirt = g_game.local_equipped_shirt ? g_game.local_equipped_shirt : g_game.login_screen.equipped_shirt;
    int pants = g_game.local_equipped_pants ? g_game.local_equipped_pants : g_game.login_screen.equipped_pants;
    int head = g_game.local_equipped_head ? g_game.local_equipped_head : g_game.login_screen.equipped_head;
    int accs[PW_MAX_EQUIPPED_ACCESSORIES];
    for (int i = 0; i < PW_MAX_EQUIPPED_ACCESSORIES; i++) {
        accs[i] = g_game.local_equipped_accessories[i] ?
                  g_game.local_equipped_accessories[i] : g_game.login_screen.equipped_accessories[i];
    }
    avatar_editor_open(&g_game.avatar_editor, tok, host, skin, shirt, pants, head, accs);
}

static void open_catalog_ui(void) {
    const char* host = g_game.host[0] ? g_game.host : "https://polyworld.games";
    const char* tok = g_game.session_token[0] ? g_game.session_token : g_game.login_screen.session_token;
    const char* skin = g_game.local_skin_hex[0] ? g_game.local_skin_hex :
                       (g_game.login_screen.skin_color[0] ? g_game.login_screen.skin_color : "#eaeaea");
    int shirt = g_game.local_equipped_shirt ? g_game.local_equipped_shirt : g_game.login_screen.equipped_shirt;
    int pants = g_game.local_equipped_pants ? g_game.local_equipped_pants : g_game.login_screen.equipped_pants;
    int head = g_game.local_equipped_head ? g_game.local_equipped_head : g_game.login_screen.equipped_head;
    int pkg = g_game.show_login ? g_game.login_screen.equipped_package : g_game.local_equipped_package;
    int accs[PW_MAX_EQUIPPED_ACCESSORIES];
    for (int i = 0; i < PW_MAX_EQUIPPED_ACCESSORIES; i++) {
        accs[i] = g_game.local_equipped_accessories[i] ?
                  g_game.local_equipped_accessories[i] : g_game.login_screen.equipped_accessories[i];
    }
    catalog_ui_open(&g_game.catalog_ui, tok, host, skin, shirt, pants, head, accs, pkg);
}

static void poll_avatar_editor_save(void) {
    if (!avatar_editor_consume_saved(&g_game.avatar_editor)) return;
    const char* skin = avatar_editor_skin(&g_game.avatar_editor);
    int shirt = avatar_editor_shirt(&g_game.avatar_editor);
    int pants = avatar_editor_pants(&g_game.avatar_editor);
    int head = avatar_editor_head(&g_game.avatar_editor);
    const int* ed_accs = avatar_editor_accessories(&g_game.avatar_editor);
    int accs[PW_MAX_EQUIPPED_ACCESSORIES] = {0};
    if (ed_accs) memcpy(accs, ed_accs, sizeof(accs));
    strncpy(g_game.login_screen.skin_color, skin, sizeof(g_game.login_screen.skin_color) - 1);
    g_game.login_screen.equipped_shirt = shirt;
    g_game.login_screen.equipped_pants = pants;
    g_game.login_screen.equipped_head = head;
    memcpy(g_game.login_screen.equipped_accessories, accs, sizeof(accs));
    g_game.login_screen.equipped_accessory = accs[0];
    g_game.local_equipped_package = normalize_mesh_flags(avatar_editor_package(&g_game.avatar_editor));
    g_game.login_screen.equipped_package = g_game.local_equipped_package;
    reload_local_avatar_from_ids(skin, shirt, pants, head, accs);
    send_appearance_to_server();
}

void avatar_editor_mouseup_bridge(void) {
    const InputState* in = input_get_state();
    avatar_editor_on_mouseup(&g_game.avatar_editor,
                             in ? in->mouse_x : 0, in ? in->mouse_y : 0, 0);
}

bool avatar_editor_mouse_bridge(int x, int y, int button, int pressed) {
    if (catalog_ui_blocks_input(&g_game.catalog_ui)) {
        if (pressed) catalog_ui_on_mousedown(&g_game.catalog_ui, (float)x, (float)y);
        else catalog_ui_on_mouseup(&g_game.catalog_ui, (float)x, (float)y);
        return true;
    }
    if (!avatar_editor_blocks_input(&g_game.avatar_editor)) return false;
    if (pressed) {
        avatar_editor_on_mousedown(&g_game.avatar_editor, (float)x, (float)y, button);
        poll_avatar_editor_save();
    } else {
        avatar_editor_on_mouseup(&g_game.avatar_editor, (float)x, (float)y, button);
        poll_avatar_editor_save();
    }
    return true;
}

bool avatar_editor_scroll_bridge(float x, float y, float delta) {
    if (catalog_ui_blocks_input(&g_game.catalog_ui))
        return catalog_ui_on_scroll(&g_game.catalog_ui, x, y, delta);
    if (!avatar_editor_blocks_input(&g_game.avatar_editor)) return false;
    return avatar_editor_on_scroll(&g_game.avatar_editor, x, y, delta);
}
#endif

bool login_handle_key(int keycode, bool shift, bool ctrl) {
#ifndef __EMSCRIPTEN__
    if (platform_ui_events_blocked()) return true;
    if (keycode == 0) return false;
    if (catalog_ui_blocks_input(&g_game.catalog_ui)) {
        return catalog_ui_on_key(&g_game.catalog_ui, keycode);
    }
    if (avatar_editor_blocks_input(&g_game.avatar_editor)) {
        return avatar_editor_on_key(&g_game.avatar_editor, keycode);
    }

    if (keycode == 27 && !g_game.show_login && (g_game.multiplayer || g_game.world_ready)) {
        if (g_game.chat.focused) {
            chat_blur(&g_game.chat);
        } else {
            game_menu_toggle(&g_game.menu);
        }
        return true;
    }
#if defined(VR) && !defined(__EMSCRIPTEN__)
    if (vr_hub_active()) {
        login_screen_on_key(&g_game.login_screen, keycode, shift, ctrl);
        return false;
    }
#endif
    if (!g_game.show_login) return false;
    login_screen_on_key(&g_game.login_screen, keycode, shift, ctrl);
    return true;
#else
    (void)keycode; (void)shift; (void)ctrl;
    return false;
#endif
}

bool login_handle_mouse(int x, int y) {
#ifndef __EMSCRIPTEN__
    if (platform_ui_events_blocked()) return true;
    if (catalog_ui_blocks_input(&g_game.catalog_ui)) {
        catalog_ui_on_mousedown(&g_game.catalog_ui, (float)x, (float)y);
        return true;
    }
    if (avatar_editor_blocks_input(&g_game.avatar_editor)) {
        avatar_editor_on_mousedown(&g_game.avatar_editor, (float)x, (float)y, 0);
        poll_avatar_editor_save();
        return true;
    }
#endif
    if (vr_hub_active()) return true;
    if (!g_game.show_login) return false;
    extern void login_screen_on_mousedown(LoginScreen* ls, int x, int y);
    login_screen_on_mousedown(&g_game.login_screen, x, y);
#ifndef __EMSCRIPTEN__
    if (g_game.login_screen.want_catalog_ui) {
        g_game.login_screen.want_catalog_ui = false;
        open_catalog_ui();
    }
    if (g_game.login_screen.want_avatar_editor) {
        g_game.login_screen.want_avatar_editor = false;
        open_avatar_editor_ui();
    }
#endif
    return true;
}

bool login_handle_mouseup(void) {
#ifndef __EMSCRIPTEN__
    if (platform_ui_events_blocked()) return true;
    if (catalog_ui_blocks_input(&g_game.catalog_ui)) {
        const InputState* in = input_get_state();
        catalog_ui_on_mouseup(&g_game.catalog_ui, in ? in->mouse_x : 0, in ? in->mouse_y : 0);
        return true;
    }
    if (avatar_editor_blocks_input(&g_game.avatar_editor)) {
        extern void avatar_editor_mouseup_bridge(void);
        avatar_editor_mouseup_bridge();
        return true;
    }
#endif
    if (vr_hub_active()) return true;
    if (!g_game.show_login) return false;
    extern void login_screen_on_mouseup(LoginScreen* ls);
    login_screen_on_mouseup(&g_game.login_screen);
    return true;
}

bool login_handle_char(unsigned int codepoint) {
#ifndef __EMSCRIPTEN__
    if (platform_ui_events_blocked()) return true;
    if (!g_game.show_login && !vr_hub_active()) return false;
    login_screen_on_char(&g_game.login_screen, codepoint);
    return true;
#else
    (void)codepoint;
    return false;
#endif
}

bool login_handle_copy(void) {
#ifndef __EMSCRIPTEN__
    if (platform_ui_events_blocked()) return true;
    if (!g_game.show_login && !vr_hub_active()) return false;
    return login_screen_copy(&g_game.login_screen);
#else
    return false;
#endif
}

bool login_handle_cut(void) {
#ifndef __EMSCRIPTEN__
    if (platform_ui_events_blocked()) return true;
    if (!g_game.show_login && !vr_hub_active()) return false;
    return login_screen_cut(&g_game.login_screen);
#else
    return false;
#endif
}

bool login_handle_paste(void) {
#ifndef __EMSCRIPTEN__
    if (platform_ui_events_blocked()) return true;
    if (!g_game.show_login && !vr_hub_active()) return false;
    return login_screen_paste(&g_game.login_screen);
#else
    return false;
#endif
}

bool login_handle_select_all(void) {
#ifndef __EMSCRIPTEN__
    if (platform_ui_events_blocked()) return true;
    if (!g_game.show_login && !vr_hub_active()) return false;
    return login_screen_select_all(&g_game.login_screen);
#else
    return false;
#endif
}

bool login_screen_scroll_bridge(float delta) {
#ifndef __EMSCRIPTEN__
    if (!g_game.show_login && !vr_hub_active()) return false;
    if (catalog_ui_blocks_input(&g_game.catalog_ui))
        return catalog_ui_on_scroll(&g_game.catalog_ui, 0, 0, delta);
    if (avatar_editor_blocks_input(&g_game.avatar_editor)) return false;
    return login_screen_on_scroll(&g_game.login_screen, delta);
#else
    (void)delta;
    return false;
#endif
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void player_respawn(void) {
    g_game.avatar.pos = (Vec3){ 0.0f, 5.0f, 0.0f };
    g_game.avatar.vel = (Vec3){ 0.0f, 0.0f, 0.0f };
    g_game.avatar.on_ground = false;
}

static bool draw_load_overlay_ex(const char* title, const char* subtitle,
                                 bool show_leave, float dt, bool clear_fb) {
    int dw = g_game.renderer.canvas_width;
    int dh = g_game.renderer.canvas_height;
    float us = g_game.ui_scale > 0.1f ? g_game.ui_scale : 1.0f;
    g_game.overlay_spin += dt * 2.2f;

    glViewport(0, 0, dw, dh);
    if (clear_fb) {
        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (g_game.load_bg_tex && g_game.load_bg_w > 0 && g_game.load_bg_h > 0 &&
        g_game.chat.initialized && g_game.chat.quad_shader) {
        float tex_a = (float)g_game.load_bg_w / (float)g_game.load_bg_h;
        float win_a = (float)dw / (float)dh;
        float vw, vh, vx, vy;
        if (win_a > tex_a) {
            vw = (float)dw;
            vh = vw / tex_a;
            vx = 0.0f;
            vy = ((float)dh - vh) * 0.5f;
        } else {
            vh = (float)dh;
            vw = vh * tex_a;
            vx = ((float)dw - vw) * 0.5f;
            vy = 0.0f;
        }
        glUseProgram(g_game.chat.quad_shader);
        float proj[16];
        memset(proj, 0, sizeof(proj));
        proj[0] = 2.0f / (float)dw;
        proj[5] = -2.0f / (float)dh;
        proj[10] = 1.0f;
        proj[12] = -1.0f;
        proj[13] = 1.0f;
        proj[15] = 1.0f;
        glUniformMatrix4fv(g_game.chat.quad_u_projection, 1, GL_FALSE, proj);
        glUniform1f(g_game.chat.quad_u_alpha, 1.0f);
        glUniform4f(g_game.chat.quad_u_tint, 1.0f, 1.0f, 1.0f, 1.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_game.load_bg_tex);
        glUniform1i(g_game.chat.quad_u_tex, 0);
        float verts[] = {
            vx,      vy,      0.0f, 0.0f,
            vx + vw, vy,      1.0f, 0.0f,
            vx + vw, vy + vh, 1.0f, 1.0f,
            vx,      vy,      0.0f, 0.0f,
            vx + vw, vy + vh, 1.0f, 1.0f,
            vx,      vy + vh, 0.0f, 1.0f,
        };
        glBindVertexArray(g_game.chat.text_vao);
        glBindBuffer(GL_ARRAY_BUFFER, g_game.chat.text_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    if (g_game.chat.initialized && g_game.chat.nineslice_tex && g_game.chat.quad_shader) {
        glUseProgram(g_game.chat.quad_shader);
        float proj[16];
        memset(proj, 0, sizeof(proj));
        proj[0] = 2.0f / (float)dw;
        proj[5] = -2.0f / (float)dh;
        proj[10] = 1.0f;
        proj[12] = -1.0f;
        proj[13] = 1.0f;
        proj[15] = 1.0f;
        glUniformMatrix4fv(g_game.chat.quad_u_projection, 1, GL_FALSE, proj);
        glUniform1f(g_game.chat.quad_u_alpha, 0.4f);
        glUniform4f(g_game.chat.quad_u_tint, 0.05f, 0.05f, 0.08f, 1.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_game.chat.nineslice_tex);
        glUniform1i(g_game.chat.quad_u_tex, 0);
        float verts[] = {
            0, 0, 0.5f, 0.5f,
            (float)dw, 0, 0.5f, 0.5f,
            (float)dw, (float)dh, 0.5f, 0.5f,
            0, 0, 0.5f, 0.5f,
            (float)dw, (float)dh, 0.5f, 0.5f,
            0, (float)dh, 0.5f, 0.5f,
        };
        glBindVertexArray(g_game.chat.text_vao);
        glBindBuffer(GL_ARRAY_BUFFER, g_game.chat.text_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glUniform4f(g_game.chat.quad_u_tint, 1.0f, 1.0f, 1.0f, 1.0f);
        glUniform1f(g_game.chat.quad_u_alpha, 1.0f);
    }

    if (g_game.logo_tex && g_game.chat.initialized && g_game.chat.quad_shader) {
        float size = 72.0f * us;
        float cx = (float)dw - size * 0.5f - 24.0f * us;
        float cy = (float)dh - size * 0.5f - 24.0f * us;
        float c = cosf(g_game.overlay_spin);
        float s = sinf(g_game.overlay_spin);
        float hx = size * 0.5f, hy = size * 0.5f;
        float rx0 = cx + (-hx) * c - (-hy) * s, ry0 = cy + (-hx) * s + (-hy) * c;
        float rx1 = cx + ( hx) * c - (-hy) * s, ry1 = cy + ( hx) * s + (-hy) * c;
        float rx2 = cx + ( hx) * c - ( hy) * s, ry2 = cy + ( hx) * s + ( hy) * c;
        float rx3 = cx + (-hx) * c - ( hy) * s, ry3 = cy + (-hx) * s + ( hy) * c;

        glUseProgram(g_game.chat.quad_shader);
        float proj[16];
        memset(proj, 0, sizeof(proj));
        proj[0] = 2.0f / (float)dw;
        proj[5] = -2.0f / (float)dh;
        proj[10] = 1.0f;
        proj[12] = -1.0f;
        proj[13] = 1.0f;
        proj[15] = 1.0f;
        glUniformMatrix4fv(g_game.chat.quad_u_projection, 1, GL_FALSE, proj);
        glUniform1f(g_game.chat.quad_u_alpha, 1.0f);
        glUniform4f(g_game.chat.quad_u_tint, 1.0f, 1.0f, 1.0f, 1.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_game.logo_tex);
        glUniform1i(g_game.chat.quad_u_tex, 0);
        float verts[] = {
            rx0, ry0, 0.0f, 0.0f,
            rx1, ry1, 1.0f, 0.0f,
            rx2, ry2, 1.0f, 1.0f,
            rx0, ry0, 0.0f, 0.0f,
            rx2, ry2, 1.0f, 1.0f,
            rx3, ry3, 0.0f, 1.0f,
        };
        glBindVertexArray(g_game.chat.text_vao);
        glBindBuffer(GL_ARRAY_BUFFER, g_game.chat.text_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    if (g_game.chat.initialized && title && title[0]) {
        chat_render_banner(&g_game.chat, title, (float)dw * 0.5f, (float)dh * 0.40f,
                           3.0f * us, 1.0f, 0.0f, dw, dh);
        if (subtitle && subtitle[0]) {
            chat_render_banner(&g_game.chat, subtitle, (float)dw * 0.5f, (float)dh * 0.50f,
                               2.0f * us, 0.92f, 0.0f, dw, dh);
        }
    }

    bool leave_clicked = false;
    float btn_w = 180.0f * us;
    float btn_h = 48.0f * us;
#ifdef __ANDROID__

    if (btn_w < 220.0f * us) btn_w = 220.0f * us;
    if (btn_h < 64.0f * us) btn_h = 64.0f * us;
#endif
    float btn_x = ((float)dw - btn_w) * 0.5f;
    float btn_y = (float)dh * 0.60f;

    if (show_leave && g_game.chat.initialized) {
        chat_render_banner(&g_game.chat,
                           g_game.studio_playtest ? "Stop Playtest" : "Leave",
                           (float)dw * 0.5f, btn_y + btn_h * 0.5f,
                           2.2f * us, 1.0f, 0.0f, dw, dh);
        const InputState* di = input_get_state();

        static bool prev_leave_down = false;
        bool down = di->mouse_left || input_mouse_left_held();
        if (down && !prev_leave_down) {
            float mx = di->mouse_x, my = di->mouse_y;
            if (mx >= btn_x && mx <= btn_x + btn_w && my >= btn_y && my <= btn_y + btn_h)
                leave_clicked = true;
        }
        prev_leave_down = down;
        if (di->key_space) leave_clicked = true;
    }

    glEnable(GL_DEPTH_TEST);
    return leave_clicked;
}

static bool draw_load_overlay(const char* title, const char* subtitle,
                              bool show_leave, float dt) {
    return draw_load_overlay_ex(title, subtitle, show_leave, dt, true);
}

static GfxBenchmarkAssets bench_assets_from_game(void) {
    ensure_avatar_bodies_loaded();
    GfxBenchmarkAssets a;
    memset(&a, 0, sizeof(a));
    a.anim = &g_game.avatar_anim;
    a.tex_shirt = g_game.local_tex_shirt;
    a.tex_pants = g_game.local_tex_pants;
    a.tex_head = g_game.local_tex_head;
    a.skin_color = g_game.skin_color;
    return a;
}

#ifndef __EMSCRIPTEN__
static char g_load_paint_title[64] = "Loading";
static double g_last_load_paint = 0.0;

static void paint_loading_now(const char* title) {
    if (!g_game.initialized) return;
    if (title && title[0])
        snprintf(g_load_paint_title, sizeof(g_load_paint_title), "%s", title);
    double now = platform_get_time();
    if (g_last_load_paint > 0.0 && now - g_last_load_paint < (1.0 / 30.0)) {
#ifdef PW_STUDIO_HOST
        pw_studio_host_busy_redraw();
#endif
        return;
    }
    g_last_load_paint = now;
    draw_load_overlay(g_load_paint_title, NULL, false, 1.0f / 30.0f);
    platform_flush_frame();
#ifdef PW_STUDIO_HOST
    pw_studio_host_busy_redraw();
#endif
}

static void game_busy_redraw(void) {
    if (!g_game.initialized) return;
    if (!g_game.loading_world && !g_game.show_disconnect && !g_game.show_kick)
        return;
    paint_loading_now(NULL);
}

static bool argv_launches_into_game(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (!argv[i]) continue;
        if (strncmp(argv[i], "polyworld://", 12) == 0) return true;
        if (strcmp(argv[i], "--connect") == 0) return true;
    }
    return false;
}

static bool argv_has_flag(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc; i++) {
        if (argv[i] && strcmp(argv[i], flag) == 0) return true;
    }
    return false;
}

static const char* argv_find_launch_arg(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (!argv[i]) continue;
        if (strcmp(argv[i], "--") == 0) continue;
        if (strncmp(argv[i], "polyworld://", 12) == 0) return argv[i];
        if (strcmp(argv[i], "--connect") == 0) return argv[i];
    }
    return NULL;
}

static const char* disconnect_subtitle(void) {
    if (g_game.disconnect_reason[0]) return g_game.disconnect_reason;
    return "Lost connection to the server.";
}

static void begin_disconnect(const char* reason) {
    audio_stop_music();
    if (reason && reason[0]) {
        strncpy(g_game.disconnect_reason, reason, sizeof(g_game.disconnect_reason) - 1);
        g_game.disconnect_reason[sizeof(g_game.disconnect_reason) - 1] = '\0';
    } else {
        g_game.disconnect_reason[0] = '\0';
    }
    g_game.show_disconnect = true;
    g_game.multiplayer = false;
    g_game.loading_world = false;
    g_game.net_proto = PW_PROTO_LEGACY;
    g_game.local_player_id = 0;
    s_netown_lite_ack = -1;
#ifdef __ANDROID__
    touch_controls_set_enabled(false);
#endif
}

static bool join_multiplayer_game(int game_id, int server_id, const char* host, int port, bool shadowed) {
    bool is_guest = (g_game.session_token[0] == '\0');
    paint_loading_now("Joining");
    JoinTicket jt = auth_get_join_ticket(
        is_guest ? NULL : g_game.session_token, game_id, is_guest, server_id, shadowed);
    if (!jt.valid) {
#ifdef __ANDROID__
        if (pw_error_is_client_outdated(jt.error)) {
            login_screen_require_update(&g_game.login_screen);
            g_game.show_login = true;
            g_game.show_disconnect = false;
            g_game.loading_world = false;
            g_game.multiplayer = false;
            return false;
        }
#endif
        begin_disconnect(jt.error[0] ? jt.error : "Could not get a join ticket.");
        return false;
    }
    paint_loading_now("Connecting");
    if (!host || !host[0]) host = PW_TCP_HOST;
    if (port <= 0) port = PW_TCP_PORT;
    if (!net_client_connect(&g_game.net, host, port)) {
        begin_disconnect("Could not reach the game server.");
        return false;
    }
    g_game.multiplayer = true;
    g_game.auth_sent = true;
    net_client_send_auth_ticket(&g_game.net, game_id, jt.ticket);
    strncpy(g_game.username, jt.username, sizeof(g_game.username) - 1);
    g_game.account_id = (uint32_t)jt.user_id;
    strncpy(g_game.avatar_color, jt.avatar_color, sizeof(g_game.avatar_color) - 1);
    g_game.local_equipped_package = normalize_mesh_flags(jt.equipped_package);
    apply_local_emotes_from_ticket(&jt);
    reload_local_avatar_from_ids(
        jt.skin_color[0] ? jt.skin_color : "#eaeaea",
        jt.equipped_shirt, jt.equipped_pants,
        jt.equipped_head, jt.equipped_accessories);
    return true;
}

static void decode_protocol_url(char* url, size_t cap) {
    if (!url || cap < 2) return;
    size_t n = strlen(url);
    if (n >= 2 && ((url[0] == '"' && url[n - 1] == '"') ||
                   (url[0] == '\'' && url[n - 1] == '\''))) {
        memmove(url, url + 1, n - 2);
        url[n - 2] = '\0';
        n -= 2;
    }
    char* d = url;
    const char* s = url;
    while (*s && (size_t)(d - url) + 1 < cap) {
        if (s[0] == '%' && s[1] && s[2]) {
            unsigned int v = 0;
            if (sscanf(s + 1, "%2x", &v) == 1) {
                *d++ = (char)v;
                s += 3;
                continue;
            }
        }
        *d++ = *s++;
    }
    *d = '\0';
}

static bool join_from_polyworld_url(const char* arg) {
    if (!arg || strncmp(arg, "polyworld://", 12) != 0) return false;
    char url[768];
    strncpy(url, arg, sizeof(url) - 1);
    url[sizeof(url) - 1] = '\0';
    decode_protocol_url(url, sizeof(url));
    const char* play = strstr(url + 12, "/play/");
    g_game.show_login = false;
    g_game.loading_world = true;
    paint_loading_now("Loading");
    if (!play) {
        begin_disconnect("Invalid join link.");
        return false;
    }
    int game_id = atoi(play + 6);
    const char* after_gid = play + 6;
    while (*after_gid >= '0' && *after_gid <= '9') after_gid++;
    int server_id = 0;
    bool shadowed = false;
    if (*after_gid == '/') {
        const char* rest = after_gid + 1;
        if (strncmp(rest, "shadowed", 8) == 0 &&
            (rest[8] == '\0' || rest[8] == '/' || rest[8] == '?' || rest[8] == '#')) {
            shadowed = true;
        } else {
            server_id = atoi(rest);
            while (*rest >= '0' && *rest <= '9') rest++;
            if (*rest == '/' && strncmp(rest + 1, "shadowed", 8) == 0)
                shadowed = true;
        }
    }
    if (strstr(play, "shadowed")) shadowed = true;

    size_t token_len = (size_t)(play - (url + 12));
    if (token_len > 0 && token_len < sizeof(g_game.session_token)) {
        memcpy(g_game.session_token, url + 12, token_len);
        g_game.session_token[token_len] = '\0';
    }

    if (game_id > 0) {
        g_game.game_id = game_id;
        return join_multiplayer_game(game_id, server_id, PW_TCP_HOST, PW_TCP_PORT, shadowed);
    }
    begin_disconnect("Invalid join link.");
    return false;
}
#endif

#define WORLD_INIT_OBJ_BYTES 68
#define PW_WORLD_STREAM 1

#define LOAD_SLICE_SEC 0.006

#define PW_PRE_SPAWN_X 1000000.0f
#define PW_PRE_SPAWN_Y 1000000.0f
#define PW_PRE_SPAWN_Z 1000000.0f

static void free_world_init_stream(void) {
    free(g_game.world_init_buf);
    g_game.world_init_buf = NULL;
    g_game.world_init_len = 0;
    g_game.world_init_off = 0;
    g_game.world_init_total = 0;
    g_game.world_init_done = 0;
    g_game.world_init_streaming = false;
    g_game.physics_streaming = false;
    g_game.physics_stream_i = 0;
    free(g_game.pending_connectors);
    g_game.pending_connectors = NULL;
    g_game.pending_connectors_len = 0;
    g_game.connectors_streaming = false;
    g_game.connectors_phase = 0;
    g_game.connectors_stream_i = 0;
    g_game.pending_spawn = false;
    g_game.spawn_received = false;
    g_game.await_batch_ready = false;
    for (int i = 0; i < g_game.pending_script_count; i++) {
        free(g_game.pending_scripts[i].source);
        g_game.pending_scripts[i].source = NULL;
    }
    g_game.pending_script_count = 0;
}

static void pending_nocollide_add(uint32_t obj_id) {
    for (int i = 0; i < g_game.pending_nocollide_count; i++) {
        if (g_game.pending_nocollide_ids[i] == obj_id) return;
    }
    if (g_game.pending_nocollide_count >= MAX_PENDING_NOCOLLIDE) return;
    g_game.pending_nocollide_ids[g_game.pending_nocollide_count++] = obj_id;
}

static void pending_nocollide_remove(uint32_t obj_id) {
    for (int i = 0; i < g_game.pending_nocollide_count; i++) {
        if (g_game.pending_nocollide_ids[i] != obj_id) continue;
        g_game.pending_nocollide_ids[i] =
            g_game.pending_nocollide_ids[g_game.pending_nocollide_count - 1];
        g_game.pending_nocollide_count--;
        return;
    }
}

static bool pending_nocollide_take(uint32_t obj_id) {
    for (int i = 0; i < g_game.pending_nocollide_count; i++) {
        if (g_game.pending_nocollide_ids[i] != obj_id) continue;
        g_game.pending_nocollide_ids[i] =
            g_game.pending_nocollide_ids[g_game.pending_nocollide_count - 1];
        g_game.pending_nocollide_count--;
        return true;
    }
    return false;
}

static void pending_clickable_add(uint32_t obj_id) {
    for (int i = 0; i < g_game.pending_clickable_count; i++) {
        if (g_game.pending_clickable_ids[i] == obj_id) return;
    }
    if (g_game.pending_clickable_count >= MAX_PENDING_CLICKABLE) return;
    g_game.pending_clickable_ids[g_game.pending_clickable_count++] = obj_id;
}

static void pending_clickable_remove(uint32_t obj_id) {
    for (int i = 0; i < g_game.pending_clickable_count; i++) {
        if (g_game.pending_clickable_ids[i] != obj_id) continue;
        g_game.pending_clickable_ids[i] =
            g_game.pending_clickable_ids[g_game.pending_clickable_count - 1];
        g_game.pending_clickable_count--;
        return;
    }
}

static bool pending_clickable_take(uint32_t obj_id) {
    for (int i = 0; i < g_game.pending_clickable_count; i++) {
        if (g_game.pending_clickable_ids[i] != obj_id) continue;
        g_game.pending_clickable_ids[i] =
            g_game.pending_clickable_ids[g_game.pending_clickable_count - 1];
        g_game.pending_clickable_count--;
        return true;
    }
    return false;
}

static void pending_material_add(uint32_t obj_id, uint8_t mat) {
    if (mat == 0) return;
    for (int i = 0; i < g_game.pending_material_count; i++) {
        if (g_game.pending_material_ids[i] == obj_id) {
            g_game.pending_material_vals[i] = mat;
            return;
        }
    }
    if (g_game.pending_material_count >= MAX_PENDING_MATERIAL) return;
    int n = g_game.pending_material_count++;
    g_game.pending_material_ids[n] = obj_id;
    g_game.pending_material_vals[n] = mat;
}

static uint8_t pending_material_take(uint32_t obj_id) {
    for (int i = 0; i < g_game.pending_material_count; i++) {
        if (g_game.pending_material_ids[i] != obj_id) continue;
        uint8_t m = g_game.pending_material_vals[i];
        int last = g_game.pending_material_count - 1;
        g_game.pending_material_ids[i] = g_game.pending_material_ids[last];
        g_game.pending_material_vals[i] = g_game.pending_material_vals[last];
        g_game.pending_material_count--;
        return m;
    }
    return 0;
}

static void apply_net_part_material(uint32_t oid, uint8_t mat) {
    if (mat >= PART_MATERIAL_COUNT) mat = PART_MATERIAL_PLASTIC;
    for (int ni = 0; ni < g_game.net_object_count; ni++) {
        if (g_game.net_objects[ni].net_id != oid) continue;
        Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
        if (ent) {
            ent->material.part_material = mat;
            if (mat != 0) ent->render_batched = false;
        }
        return;
    }
    pending_material_add(oid, mat);
}

#define MAX_NET_CONN 8192
static uint16_t g_conn_pairs[MAX_NET_CONN][2];
static int g_conn_pair_count;
static int g_conn_uf[MAX_NET_OBJECTS];
static bool g_conn_obj_anchored[MAX_NET_OBJECTS];
static bool g_conn_root_anch[MAX_NET_OBJECTS];
static int g_conn_n;
static float g_last_exp[4];
static bool g_has_last_exp;

static void net_brick_deactivate_collision(int ni);

static void clear_game_world(void) {
    for (uint32_t ei = 0; ei < MAX_ENTITIES; ei++) {
        if (g_game.scene.entities[ei].active) {
            if (g_game.scene.entities[ei].physics_body) {
                physics_destroy_body(g_game.physics, g_game.scene.entities[ei].physics_body);
            }
        }
    }
    if (g_game.physics)
        physics_clear_connectors(g_game.physics);

    memset(&g_game.scene, 0, sizeof(g_game.scene));
    brick_batch_clear(NULL);
    clear_net_mesh_cache();
    clear_catalog_visual_caches();
    free_world_init_stream();

    memset(g_game.net_objects, 0, sizeof(g_game.net_objects));
    g_game.net_object_count = 0;
    memset(g_game.net_constraints, 0, sizeof(g_game.net_constraints));
    g_game.net_constraint_count = 0;
    memset(g_client_local_parts, 0, sizeof(g_client_local_parts));
    g_game.pending_nocollide_count = 0;
    g_game.pending_clickable_count = 0;
    g_game.pending_material_count = 0;
    g_game.collision_lod_scan_i = 0;
    g_game.collision_lod_idle_frames = 0;
    g_game.collision_lod_focus_cx = INT_MIN;
    g_game.collision_lod_focus_cz = INT_MIN;
    memset(g_game.dynamic_objects, 0, sizeof(g_game.dynamic_objects));
    g_game.dynamic_object_count = 0;
    g_conn_n = 0;
    g_conn_pair_count = 0;

    g_game.world_ready = false;
    g_game.world_colliders_ready = false;
    g_game.vr.recal_ui = false;
    g_game.camera_mode = CAM_MODE_NORMAL;
    g_game.local_transparency = 0.0f;

    for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
        if (g_game.remote_players[rp].ragdoll.active) {
            ragdoll_destroy((RagdollState*)&g_game.remote_players[rp].ragdoll);
        }
    }
    memset(g_game.remote_players, 0, sizeof(g_game.remote_players));
    g_game.remote_player_count = 0;

    g_game.tool_count = 0;
    g_game.equipped_tool = 0;

    g_game.avatar.pos = (Vec3){PW_PRE_SPAWN_X, PW_PRE_SPAWN_Y, PW_PRE_SPAWN_Z};
    g_game.avatar.vel = (Vec3){0, 0, 0};
    g_game.avatar.on_ground = false;
    g_game.avatar.health = 100;
    g_game.avatar.dead = false;
    g_game.spawn_received = false;
    g_game.collision_chunk_loading = false;
    g_game.collision_load_attempts = 0;
    g_game.collision_load_attempt_time = 0.0;
    g_game.collision_spawn_gate = false;

    ragdoll_destroy((RagdollState*)&g_game.ragdoll);
    memset(g_game.rockets, 0, sizeof(g_game.rockets));
    memset(g_game.explosions, 0, sizeof(g_game.explosions));
    g_game.exp_flash = 0.0f;
    renderer_reset_lights(&g_game.renderer);

    if (g_game.scripts) {
        client_script_destroy(g_game.scripts);
        g_game.scripts = client_script_create(&g_game.scene);
        client_script_set_player(g_game.scripts, &g_game.avatar);
        client_script_set_playtest(g_game.scripts, g_game.studio_playtest);
        if (g_game.username[0])
            client_script_set_local_name(g_game.scripts, g_game.username);
    }

    g_game.chat.msg_count = 0;
    g_game.chat.msg_start = 0;
    g_game.chat.open = false;
    g_game.chat.focused = false;
    g_game.chat.opened_by_slash = false;
    g_game.chat.unread = false;
    g_game.chat.scroll_offset = 0;
    g_game.chat.panel_anim = 0.0f;

    memset(&g_game.scoreboard, 0, sizeof(g_game.scoreboard));
    chat_clear_name_color_overrides(&g_game.chat);
    g_game.local_has_name_color = false;
    g_game.local_name_color_r = g_game.local_name_color_g = g_game.local_name_color_b = 0.0f;
    g_game.chat.pl_entry_count = 0;
    g_game.chat.pl_hit_count = 0;
    g_game.chat.pl_anim_w = 0.0f;
    g_game.chat.pl_anim_h = 0.0f;

    memset(&g_game.script_ui, 0, sizeof(g_game.script_ui));
    memset(&g_game.music_cred, 0, sizeof(g_game.music_cred));
    social_init(&g_game.social);
    social_set_nineslice(&g_game.social, g_game.chat.nineslice_tex);
    social_set_shaders(&g_game.social,
                       g_game.chat.quad_shader, g_game.chat.quad_u_projection,
                       g_game.chat.quad_u_tex, g_game.chat.quad_u_alpha, g_game.chat.quad_u_tint,
                       g_game.chat.text_vao, g_game.chat.text_vbo);
    g_game.social.ui_scale = g_game.ui_scale;

    avatar_init(&g_game.avatar, &g_game.scene, g_game.physics);
}

static bool connectors_prepare(const uint8_t* msg_buf, size_t msg_len) {
    g_conn_pair_count = 0;
    g_conn_n = 0;
    if (msg_len < 4 || g_game.net_object_count <= 0) return false;

    uint32_t conn_count;
    memcpy(&conn_count, msg_buf, 4);
    size_t off = 4;
    for (uint32_t ci = 0; ci < conn_count && off + 4 <= msg_len; ci++) {
        uint16_t a, b;
        memcpy(&a, msg_buf + off, 2); off += 2;
        memcpy(&b, msg_buf + off, 2); off += 2;

        if (g_conn_pair_count < MAX_NET_CONN) {
            g_conn_pairs[g_conn_pair_count][0] = (uint16_t)(a + 1);
            g_conn_pairs[g_conn_pair_count][1] = (uint16_t)(b + 1);
            g_conn_pair_count++;
        }
    }
    if (g_conn_pair_count <= 0) return false;

    g_conn_n = g_game.net_object_count;
    for (int i = 0; i < g_conn_n; i++) g_conn_uf[i] = i;
    for (int i = 0; i < g_conn_pair_count; i++) {
        int a = net_find_ni(g_conn_pairs[i][0]);
        int b = net_find_ni(g_conn_pairs[i][1]);
        if (a < 0 || b < 0) continue;
        while (g_conn_uf[a] != a) { g_conn_uf[a] = g_conn_uf[g_conn_uf[a]]; a = g_conn_uf[a]; }
        while (g_conn_uf[b] != b) { g_conn_uf[b] = g_conn_uf[g_conn_uf[b]]; b = g_conn_uf[b]; }
        if (a != b) g_conn_uf[a] = b;
    }

    memset(g_conn_obj_anchored, 0, sizeof(bool) * (size_t)g_conn_n);
    memset(g_conn_root_anch, 0, sizeof(bool) * (size_t)g_conn_n);
    for (int i = 0; i < g_conn_n; i++)
        g_conn_obj_anchored[i] = g_game.net_objects[i].anchored;
    for (int i = 0; i < g_conn_n; i++) {
        if (!g_conn_obj_anchored[i]) continue;
        int r = i;
        while (g_conn_uf[r] != r) { g_conn_uf[r] = g_conn_uf[g_conn_uf[r]]; r = g_conn_uf[r]; }
        g_conn_root_anch[r] = true;
    }
    return true;
}

static void connectors_make_static_one(int ci2) {
    if (ci2 < 0 || ci2 >= g_conn_n) return;
    if (g_conn_obj_anchored[ci2]) return;
    int r = ci2;
    while (g_conn_uf[r] != r) { g_conn_uf[r] = g_conn_uf[g_conn_uf[r]]; r = g_conn_uf[r]; }
    if (!g_conn_root_anch[r]) return;

    g_game.net_objects[ci2].connector_static = true;
}

static void connectors_joint_one(int ci2) {
    (void)ci2;

}

static void process_connectors_msg(const uint8_t* msg_buf, size_t msg_len) {
    if (!connectors_prepare(msg_buf, msg_len)) return;

    for (int i = 0; i < g_conn_n; i++) {
        if (g_conn_obj_anchored[i]) continue;
        int r = i;
        while (g_conn_uf[r] != r) { g_conn_uf[r] = g_conn_uf[g_conn_uf[r]]; r = g_conn_uf[r]; }
        if (!g_conn_root_anch[r]) continue;
        g_game.net_objects[i].connector_static = true;
    }
}

static void finish_connectors_and_spawn(void);

static void collision_lod_force_resync(void);
static void collision_lod_activate_near_focus(void);
static bool collision_lod_evict_farthest(Vec3 focus);
static bool collision_lod_nearby_incomplete(void);
static void collision_lod_ensure_underfoot(void);

static void apply_spawn_and_start_batch(float cx, float cy, float cz) {

    cx += 0.1f;
    cz += 0.1f;
    g_game.avatar.pos = (Vec3){cx, cy, cz};
    g_game.avatar.vel = (Vec3){0, 0, 0};
    if (g_game.avatar.body)
        physics_set_position(g_game.physics, g_game.avatar.body, g_game.avatar.pos);
    g_game.move_lock_timer = 0.5f;
    g_game.collision_pin_pos = g_game.avatar.pos;
    g_game.collision_chunk_loading = true;
    g_game.avatar.freeze_locomotion = true;
    g_game.collision_load_attempts = 0;
    g_game.collision_load_attempt_time = 0.0;
    g_game.collision_spawn_gate = true;

    collision_lod_force_resync();
    collision_lod_activate_near_focus();
    collision_lod_ensure_underfoot();

    brick_batch_rebuild(&g_game.scene);

    g_game.world_ready = true;
    g_game.await_batch_ready = false;
    vr_calibrate_on_world_ready();
#ifdef __EMSCRIPTEN__
    EM_ASM({
        var el = document.getElementById('loading');
        if (el) el.style.display = 'none';
    });
#endif
}

static void finish_connectors_and_spawn(void) {
    g_game.connectors_streaming = false;
    g_game.connectors_phase = 0;
    g_game.connectors_stream_i = 0;
    free(g_game.pending_connectors);
    g_game.pending_connectors = NULL;
    g_game.pending_connectors_len = 0;
    g_game.world_colliders_ready = true;

    if (g_game.pending_spawn) {
        g_game.pending_spawn = false;
        apply_spawn_and_start_batch(g_game.pending_spawn_pos[0],
                                    g_game.pending_spawn_pos[1],
                                    g_game.pending_spawn_pos[2]);
    }
}

static float quantize_mesh_dim(float v) {
    return floorf(v * 1000.0f + 0.5f) / 1000.0f;
}

static GPUMesh* net_get_or_create_mesh(uint8_t obj_type, float hx, float hy, float hz, float full_height) {

    hx = quantize_mesh_dim(hx);
    hy = quantize_mesh_dim(hy);
    hz = quantize_mesh_dim(hz);
    full_height = quantize_mesh_dim(full_height);

    for (int mc = 0; mc < g_net_mesh_cache_count; mc++) {
        if (g_net_mesh_cache[mc].type == obj_type &&
            fabsf(g_net_mesh_cache[mc].a - hx) < 0.0005f &&
            fabsf(g_net_mesh_cache[mc].b - hy) < 0.0005f &&
            fabsf(g_net_mesh_cache[mc].c - hz) < 0.0005f) {
            return &g_net_mesh_cache[mc].mesh;
        }
    }

    if (g_net_mesh_cache_count >= NET_MESH_CACHE_MAX)
        return net_unit_mesh_for_type(obj_type);

    MeshData md;
    bool ok = false;
    if (obj_type == 1)
        ok = create_sphere_mesh(&md, hx, 32, 24);
    else if (obj_type == 2)
        ok = create_cylinder_mesh(&md, hx, full_height, 20);
    else if (obj_type == 3)
        ok = create_wedge_mesh(&md, hx, hy, hz);
    else
        ok = create_box_mesh(&md, hx, hy, hz);
    if (!ok) return NULL;

    int slot = g_net_mesh_cache_count;
    if (!mesh_upload(&md, &g_net_mesh_cache[slot].mesh)) {
        mesh_data_free(&md);
        return NULL;
    }
    mesh_data_free(&md);
    g_net_mesh_cache[slot].a = hx;
    g_net_mesh_cache[slot].b = hy;
    g_net_mesh_cache[slot].c = hz;
    g_net_mesh_cache[slot].type = obj_type;
    g_net_mesh_cache_count++;
    return &g_net_mesh_cache[slot].mesh;
}

static bool world_init_create_one(const uint8_t* p) {
    uint32_t obj_id;
    uint8_t obj_type, anchored;
    float pos[3], size[3], color[3], rot[3];
    uint8_t surfaces[6];
    float glow = 0.0f;
    float alpha = 1.0f;
    size_t off = 0;

    memcpy(&obj_id, p + off, 4); off += 4;
    uint8_t packed_type = p[off++];
    uint8_t part_mat = 0;
    pw_wire_unpack_type(packed_type, &obj_type, &part_mat);
    {
        uint8_t pend = pending_material_take(obj_id);
        if (pend) part_mat = pend;
    }
    if (part_mat >= PART_MATERIAL_COUNT) part_mat = PART_MATERIAL_PLASTIC;
    anchored = p[off++];
    memcpy(pos, p + off, 12); off += 12;
    memcpy(size, p + off, 12); off += 12;
    memcpy(color, p + off, 12); off += 12;
    memcpy(surfaces, p + off, 6); off += 6;
    memcpy(rot, p + off, 12); off += 12;
    memcpy(&glow, p + off, 4); off += 4;
    memcpy(&alpha, p + off, 4);

    float hx = size[0] * 0.5f, hy = size[1] * 0.5f, hz = size[2] * 0.5f;
    if (hx < 0.01f || hy < 0.01f || hz < 0.01f) return true;

    EntityID eid = scene_create_entity(&g_game.scene);
    Entity* ent = scene_get_entity(&g_game.scene, eid);
    if (!ent) return false;

    ent->transform.position = (Vec3){ pos[0], pos[1], pos[2] };
    ent->transform.rotation = (Vec3){ rot[0], rot[1], rot[2] };

    ent->transform.scale = (Vec3){ size[0], size[1], size[2] };
    ent->material.color = (Vec3){ color[0], color[1], color[2] };
    ent->material.glow = glow;

    if (alpha < 0.0f || alpha > 1.0f || alpha != alpha) alpha = 1.0f;
    ent->material.alpha = alpha;
    ent->material.part_material = part_mat;
    for (int s = 0; s < 6; s++) ent->material.surfaces[s] = surfaces[s];

    if (g_game.net_object_count < MAX_NET_OBJECTS) {
        g_game.net_objects[g_game.net_object_count].net_id = obj_id;
        g_game.net_objects[g_game.net_object_count].entity = eid;
        g_game.net_objects[g_game.net_object_count].anchored = (anchored != 0);
        g_game.net_objects[g_game.net_object_count].connector_static = false;
        g_game.net_objects[g_game.net_object_count].obj_type = obj_type;
        g_game.net_objects[g_game.net_object_count].mesh_id = 0;
        g_game.net_objects[g_game.net_object_count].mesh_collider = 0;
        g_game.net_objects[g_game.net_object_count].size[0] = size[0];
        g_game.net_objects[g_game.net_object_count].size[1] = size[1];
        g_game.net_objects[g_game.net_object_count].size[2] = size[2];
        g_game.net_objects[g_game.net_object_count].has_target = false;
        g_game.net_objects[g_game.net_object_count].lerp_t = 0.0f;
        g_game.net_objects[g_game.net_object_count].lerp_duration = 0.0f;
        g_game.net_objects[g_game.net_object_count].lerp_interval_ema = 0.0f;
        g_game.net_objects[g_game.net_object_count].collide_wanted =
            !pending_nocollide_take(obj_id);
        g_game.net_objects[g_game.net_object_count].clickable =
            pending_clickable_take(obj_id);
        g_game.net_objects[g_game.net_object_count].collision_lod_active = false;
        g_game.net_objects[g_game.net_object_count].net_owned = false;
        g_game.net_objects[g_game.net_object_count].never_netown = false;
        g_game.net_object_count++;
        {
            uint8_t mcol = 0;
            uint32_t mid = pending_mesh_take(obj_id, &mcol);
            if (mid) apply_net_part_mesh(obj_id, mid, mcol);
            flush_pending_decals_for(obj_id);
        }
    }

    ent->mesh = net_unit_mesh_for_type(obj_type);

    ColliderType coll_type = collider_for_obj_type(obj_type);
    (void)coll_type;

    ent->physics_body = 0;
    ent->static_batch = anchored && (obj_type == 0);
    ent->render_batched = false;

    if (!anchored && g_game.dynamic_object_count < MAX_DYNAMIC_OBJECTS) {
        int di = g_game.dynamic_object_count++;
        g_game.dynamic_objects[di].net_id = obj_id;
        g_game.dynamic_objects[di].entity = eid;
        g_game.dynamic_objects[di].body = 0;
        g_game.dynamic_objects[di].active = true;
        g_game.dynamic_objects[di].owned_locally = false;
        g_game.dynamic_objects[di].own_timer = 0.0f;
        g_game.dynamic_objects[di].last_pos = (Vec3){ pos[0], pos[1], pos[2] };
    }
    return true;
}

static void finish_object_stream_start_physics(void) {
    g_game.world_init_streaming = false;
    free(g_game.world_init_buf);
    g_game.world_init_buf = NULL;
    g_game.world_init_len = 0;
    g_game.avatar_ready = true;
    g_game.physics_streaming = true;
    g_game.physics_stream_i = 0;
}

#define COLLISION_CHUNK_SIZE 64.0f

#define COLLISION_CHUNK_ORIGIN_BIAS 32.0f
#define COLLISION_CHUNK_KEEP 1
#define COLLISION_CHUNK_DROP 2

#define COLLISION_UNDERFOOT_RADIUS 96.0f
#define COLLISION_LOAD_MAX_ATTEMPTS 5
#define COLLISION_LOAD_ATTEMPT_INTERVAL 0.2
#define COLLISION_LOD_SLICE_SEC 0.001
#define COLLISION_LOD_MAX_OPS 16
#define COLLISION_LOD_MOTION_KEEP_SEC 0.5
#define COLLISION_LOD_IDLE_SKIP 16

static int collision_chunk_coord(float x) {
    return (int)floorf((x + COLLISION_CHUNK_ORIGIN_BIAS) / COLLISION_CHUNK_SIZE);
}

static float collision_chunk_world_min(int c) {
    return (float)c * COLLISION_CHUNK_SIZE - COLLISION_CHUNK_ORIGIN_BIAS;
}

static int collision_chunk_chebyshev(Vec3 a, Vec3 b) {
    int dx = collision_chunk_coord(a.x) - collision_chunk_coord(b.x);
    int dz = collision_chunk_coord(a.z) - collision_chunk_coord(b.z);
    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;
    return dx > dz ? dx : dz;
}

static int collision_chunk_dist_aabb(Vec3 pos, float hx, float hz, Vec3 focus) {
    float nx = focus.x;
    if (nx < pos.x - hx) nx = pos.x - hx;
    if (nx > pos.x + hx) nx = pos.x + hx;
    float nz = focus.z;
    if (nz < pos.z - hz) nz = pos.z - hz;
    if (nz > pos.z + hz) nz = pos.z + hz;
    return collision_chunk_chebyshev((Vec3){nx, 0.0f, nz}, (Vec3){focus.x, 0.0f, focus.z});
}

static Vec3 collision_lod_focus(void) {
    if (g_game.pending_spawn) {
        return (Vec3){
            g_game.pending_spawn_pos[0],
            g_game.pending_spawn_pos[1],
            g_game.pending_spawn_pos[2]
        };
    }
    return g_game.avatar.pos;
}

static bool net_brick_recently_moving(int ni, double now) {
    if (ni < 0 || ni >= g_game.net_object_count) return false;
    if (!g_game.net_objects[ni].has_target) return false;
    if (g_game.net_objects[ni].lerp_t < 1.0f) return true;
    double age = now - g_game.net_objects[ni].last_update_time;
    return age >= 0.0 && age < COLLISION_LOD_MOTION_KEEP_SEC;
}

static bool net_brick_always_keep_collision_at(int ni, double now) {
    if (ni < 0 || ni >= g_game.net_object_count) return true;
    if (g_game.net_objects[ni].net_owned) return true;
    if (!g_game.net_objects[ni].anchored) return true;
    if (g_game.net_objects[ni].connector_static) return true;
    if (net_brick_recently_moving(ni, now)) return true;
    return false;
}

static bool net_brick_always_keep_collision(int ni) {
    return net_brick_always_keep_collision_at(ni, platform_get_time());
}

static void net_brick_clear_dynamic_body(EntityID eid) {
    for (int di = 0; di < g_game.dynamic_object_count; di++) {
        if (g_game.dynamic_objects[di].entity == eid) {
            g_game.dynamic_objects[di].body = 0;
            break;
        }
    }
}

static void net_brick_bind_dynamic_body(EntityID eid, PhysicsBodyID body) {
    for (int di = 0; di < g_game.dynamic_object_count; di++) {
        if (g_game.dynamic_objects[di].entity == eid) {
            g_game.dynamic_objects[di].body = body;
            break;
        }
    }
}

static void net_brick_deactivate_collision(int ni) {
    if (ni < 0 || ni >= g_game.net_object_count) return;
    if (g_game.net_objects[ni].net_owned) return;
    Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
    if (!ent || !ent->physics_body) {
        g_game.net_objects[ni].collision_lod_active = false;
        return;
    }

    PhysicsBodyID body = ent->physics_body;

    if (g_game.avatar.ground_body == body)
        return;

    physics_destroy_body(g_game.physics, body);
    ent->physics_body = 0;
    net_brick_clear_dynamic_body(g_game.net_objects[ni].entity);
    g_game.net_objects[ni].collision_lod_active = false;
}

static float net_part_mass_from_size(const float size[3]) {
    float v = size[0] * size[1] * size[2];
    if (v < 1.0f) v = 1.0f;
    if (v > 250.0f) v = 250.0f;
    return v;
}

static bool net_brick_activate_collision(int ni) {
    if (ni < 0 || ni >= g_game.net_object_count) return true;
    if (!g_game.net_objects[ni].collide_wanted) {
        g_game.net_objects[ni].collision_lod_active = false;
        return true;
    }

    Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
    if (!ent) return true;
    if (ent->physics_body) {
        g_game.net_objects[ni].collision_lod_active = true;
        return true;
    }

    float hx = g_game.net_objects[ni].size[0] * 0.5f;
    float hy = g_game.net_objects[ni].size[1] * 0.5f;
    float hz = g_game.net_objects[ni].size[2] * 0.5f;
    if (hx < 0.01f || hy < 0.01f || hz < 0.01f) return true;

    bool anchored = g_game.net_objects[ni].anchored;

    BodyDesc desc = {
        .type = BODY_STATIC,
        .collider = COLLIDER_BOX,
        .position = ent->transform.position,
        .half_extents = { hx, hy, hz },
        .radius = hx,
        .mass = anchored ? 0.0f : net_part_mass_from_size(g_game.net_objects[ni].size),
        .restitution = anchored ? 0.3f : 0.0f,
        .friction = anchored ? 1.0f : 0.8f
    };
    net_apply_part_collider(&desc, ni, hx, hy, hz);

    Vec3 focus = collision_lod_focus();
    for (int attempt = 0; attempt < 48; attempt++) {
        ent->physics_body = physics_create_body(g_game.physics, &desc);
        if (ent->physics_body)
            break;
        if (desc.collider == COLLIDER_HULL) {
            desc.collider = COLLIDER_BOX;
            desc.hull_point_count = 0;
            desc.hull_points_ext = NULL;
            continue;
        }

        if (!collision_lod_evict_farthest(focus))
            break;
    }

    Vec3 rot = ent->transform.rotation;
    if (ent->physics_body && (rot.x != 0.0f || rot.y != 0.0f || rot.z != 0.0f))
        physics_set_rotation_euler(g_game.physics, ent->physics_body, rot);

    if (!anchored && ent->physics_body)
        net_brick_bind_dynamic_body(g_game.net_objects[ni].entity, ent->physics_body);

    g_game.net_objects[ni].collision_lod_active = (ent->physics_body != 0);
    return true;
}

static void net_brick_rebuild_collision(int ni) {
    if (ni < 0 || ni >= g_game.net_object_count) return;
    if (g_game.net_objects[ni].net_owned) return;
    Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
    if (!ent || !ent->physics_body) return;
    physics_destroy_body(g_game.physics, ent->physics_body);
    ent->physics_body = 0;
    net_brick_clear_dynamic_body(g_game.net_objects[ni].entity);
    g_game.net_objects[ni].collision_lod_active = false;
    net_brick_activate_collision(ni);
}

static int net_find_ni(uint32_t id) {
    for (int ni = 0; ni < g_game.net_object_count; ni++) {
        if (g_game.net_objects[ni].net_id == id) return ni;
    }
    return -1;
}

static void net_remove_object(uint32_t did) {
    destroy_net_decals_for(did);
    for (int ni = 0; ni < g_game.net_object_count; ni++) {
        if (g_game.net_objects[ni].net_id != did) continue;
        Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
        bool was_batched = ent && ent->render_batched;
        net_brick_deactivate_collision(ni);
        if (ent)
            scene_destroy_entity(&g_game.scene, g_game.net_objects[ni].entity);
        g_game.net_objects[ni] = g_game.net_objects[g_game.net_object_count - 1];
        g_game.net_object_count--;
        if (was_batched)
            brick_batch_mark_dirty();
        return;
    }
}

static PhysicsBodyID net_id_body(uint32_t id) {
    int ni = net_find_ni(id);
    if (ni < 0) return 0;
    Entity* e = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
    return e ? e->physics_body : 0;
}

static void client_send_protocol_ack(void) {
    if (g_game.net.state != NET_STATE_CONNECTED) return;
    uint8_t abuf[3 + 32 + 1];
    uint16_t ack = g_game.net_proto;
    memcpy(abuf, &ack, 2);
    const char* ver = CLIENT_VERSION;
    size_t n = strlen(ver);
    if (n > 31) n = 31;
    abuf[2] = (uint8_t)n;
    memcpy(abuf + 3, ver, n);
    abuf[3 + n] = client_netown_lite() ? (uint8_t)PW_PROTO_ACK_FLAG_NO_NETOWN : 0;
    net_client_send(&g_game.net, MSG_PROTOCOL_ACK, abuf, 4 + n);
    s_netown_lite_ack = client_netown_lite() ? 1 : 0;
}

static void netown_sync_constraints(void) {
    if (!g_game.physics) return;
    if (client_netown_lite()) return;
    physics_weld_batch_begin(g_game.physics);
    for (int i = 0; i < g_game.net_constraint_count; i++) {
        if (!g_game.net_constraints[i].active) continue;
        int na = net_find_ni(g_game.net_constraints[i].id_a);
        int nb = net_find_ni(g_game.net_constraints[i].id_b);
        bool both = na >= 0 && nb >= 0 &&
                    g_game.net_objects[na].net_owned &&
                    g_game.net_objects[nb].net_owned;
        if (!both) {
            if (g_game.net_constraints[i].local_conn) {
                physics_destroy_connector(g_game.physics, g_game.net_constraints[i].local_conn);
                g_game.net_constraints[i].local_conn = 0;
            }
            continue;
        }
        if (g_game.net_constraints[i].local_conn) continue;
        PhysicsBodyID ba = net_id_body(g_game.net_constraints[i].id_a);
        PhysicsBodyID bb = net_id_body(g_game.net_constraints[i].id_b);
        if (!ba || !bb) continue;
        ConstraintDesc d = g_game.net_constraints[i].desc;
        d.point = netown_body_local_to_world(ba, d.point);
        d.point_set = 1;
        {
            Vec3 ax = d.axis;
            if (fabsf(ax.x) + fabsf(ax.y) + fabsf(ax.z) > 1e-6f)
                d.axis = netown_body_local_dir_to_world(ba, ax);
        }
        g_game.net_constraints[i].local_conn =
            physics_create_constraint(g_game.physics, ba, bb, &d);
    }
    physics_weld_batch_end(g_game.physics);
}

#define NETOWN_BREAK_IGNORE 16
static uint32_t s_brk_ign_a[NETOWN_BREAK_IGNORE], s_brk_ign_b[NETOWN_BREAK_IGNORE];
static double s_brk_ign_t[NETOWN_BREAK_IGNORE];
static int s_brk_ign_n;
static uint32_t s_brk_pend_a[PW_CONN_BREAK_MAX], s_brk_pend_b[PW_CONN_BREAK_MAX];
static int s_brk_pend_n;

static void netown_pair_order(uint32_t* a, uint32_t* b) {
    if (a && b && *a > *b) {
        uint32_t t = *a;
        *a = *b;
        *b = t;
    }
}

static bool netown_conn_break_ignored(uint32_t a, uint32_t b) {
    netown_pair_order(&a, &b);
    double now = platform_get_time();
    int i = 0;
    while (i < s_brk_ign_n) {
        if (now - s_brk_ign_t[i] > 2.5) {
            s_brk_ign_a[i] = s_brk_ign_a[s_brk_ign_n - 1];
            s_brk_ign_b[i] = s_brk_ign_b[s_brk_ign_n - 1];
            s_brk_ign_t[i] = s_brk_ign_t[s_brk_ign_n - 1];
            s_brk_ign_n--;
            continue;
        }
        if (s_brk_ign_a[i] == a && s_brk_ign_b[i] == b) return true;
        i++;
    }
    return false;
}

static void netown_note_broken_conn(uint32_t a, uint32_t b) {
    if (a == 0 || b == 0 || a == b) return;
    netown_pair_order(&a, &b);
    if (netown_conn_break_ignored(a, b)) return;
    if (s_brk_ign_n < NETOWN_BREAK_IGNORE) {
        s_brk_ign_a[s_brk_ign_n] = a;
        s_brk_ign_b[s_brk_ign_n] = b;
        s_brk_ign_t[s_brk_ign_n] = platform_get_time();
        s_brk_ign_n++;
    }
    for (int i = 0; i < s_brk_pend_n; i++) {
        if (s_brk_pend_a[i] == a && s_brk_pend_b[i] == b) return;
    }
    if (s_brk_pend_n < (int)PW_CONN_BREAK_MAX) {
        s_brk_pend_a[s_brk_pend_n] = a;
        s_brk_pend_b[s_brk_pend_n] = b;
        s_brk_pend_n++;
    }
}

static void netown_flush_broken_conns(void) {
    if (s_brk_pend_n <= 0) return;
    if (g_game.net_proto < PW_PROTO_NETOWN || g_game.net.state != NET_STATE_CONNECTED) {
        s_brk_pend_n = 0;
        return;
    }
    uint8_t buf[2 + PW_CONN_BREAK_MAX * 8];
    uint16_t n = (uint16_t)s_brk_pend_n;
    memcpy(buf, &n, 2);
    size_t off = 2;
    for (int i = 0; i < s_brk_pend_n; i++) {
        memcpy(buf + off, &s_brk_pend_a[i], 4); off += 4;
        memcpy(buf + off, &s_brk_pend_b[i], 4); off += 4;
    }
    net_client_send(&g_game.net, MSG_CONN_BREAK, buf, off);
    s_brk_pend_n = 0;
}

static void netown_scan_broken_conns(void) {
    if (g_game.net_proto < PW_PROTO_NETOWN || !g_game.world_ready) return;
    if (client_netown_lite()) return;
    for (int i = 0; i < g_game.net_constraint_count; i++) {
        if (!g_game.net_constraints[i].active) continue;
        if (constraint_is_nocollide(g_game.net_constraints[i].desc.type)) continue;
        int na = net_find_ni(g_game.net_constraints[i].id_a);
        int nb = net_find_ni(g_game.net_constraints[i].id_b);
        if (na < 0 || nb < 0) continue;
        if (!g_game.net_objects[na].net_owned || !g_game.net_objects[nb].net_owned)
            continue;

        bool dead = false;
        if (g_game.net_constraints[i].local_conn &&
            !physics_connector_is_active(g_game.physics, g_game.net_constraints[i].local_conn))
            dead = true;

        Entity* ea = scene_get_entity(&g_game.scene, g_game.net_objects[na].entity);
        Entity* eb = scene_get_entity(&g_game.scene, g_game.net_objects[nb].entity);
        if (ea && eb) {
            Vec3 pa = (ea->physics_body && g_game.physics)
                ? physics_get_position(g_game.physics, ea->physics_body)
                : ea->transform.position;
            Vec3 pb = (eb->physics_body && g_game.physics)
                ? physics_get_position(g_game.physics, eb->physics_body)
                : eb->transform.position;
            float dx = pa.x - pb.x, dy = pa.y - pb.y, dz = pa.z - pb.z;
            float dist = sqrtf(dx * dx + dy * dy + dz * dz);
            const float* sa = g_game.net_objects[na].size;
            const float* sb = g_game.net_objects[nb].size;
            float ra = 0.5f * sqrtf(sa[0] * sa[0] + sa[1] * sa[1] + sa[2] * sa[2]);
            float rb = 0.5f * sqrtf(sb[0] * sb[0] + sb[1] * sb[1] + sb[2] * sb[2]);
            float lim = ra + rb + 8.0f;
            if (lim < 12.0f) lim = 12.0f;

            if (!constraint_is_weld(g_game.net_constraints[i].desc.type) && dist > lim)
                dead = true;
        }

        if (!dead) continue;
        if (g_game.net_constraints[i].local_conn) {
            if (physics_connector_is_active(g_game.physics, g_game.net_constraints[i].local_conn))
                physics_destroy_connector(g_game.physics, g_game.net_constraints[i].local_conn);
            g_game.net_constraints[i].local_conn = 0;
        }
        g_game.net_constraints[i].active = false;
        netown_note_broken_conn(g_game.net_constraints[i].id_a, g_game.net_constraints[i].id_b);
    }
    netown_flush_broken_conns();
}

static void netown_apply_phys_model(Entity* e) {
    if (!e || !e->physics_body || !g_game.physics) return;
    Mat4 m = physics_get_transform_mat4(g_game.physics, e->physics_body);
    e->use_phys_model = true;
    e->phys_model = mat4_multiply(m, mat4_scale(e->transform.scale));
    e->transform.position = physics_get_position(g_game.physics, e->physics_body);
}

static bool s_netown_need_sync;
static void netown_revoke_oversized(void);

static void netown_sync_motion_types(void) {
    if (!g_game.physics) return;
    for (int ni = 0; ni < g_game.net_object_count; ni++) {
        if (!g_game.net_objects[ni].net_owned) continue;
        Entity* e = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
        if (e && e->physics_body)
            physics_make_dynamic(g_game.physics, e->physics_body, 0.0f);
    }
    for (int ni = 0; ni < g_game.net_object_count; ni++) {
        if (g_game.net_objects[ni].net_owned) continue;
        Entity* e = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
        if (!e || !e->physics_body) continue;
        bool sibling_owned = false;
        for (int nj = 0; nj < g_game.net_object_count; nj++) {
            if (!g_game.net_objects[nj].net_owned) continue;
            Entity* o = scene_get_entity(&g_game.scene, g_game.net_objects[nj].entity);
            if (!o || !o->physics_body) continue;
            if (physics_same_rigid_body(g_game.physics, e->physics_body, o->physics_body)) {
                sibling_owned = true;
                break;
            }
        }
        if (!sibling_owned)
            physics_make_static(g_game.physics, e->physics_body);
    }
}

static void netown_flush_sync(void) {
    if (!s_netown_need_sync) return;
    s_netown_need_sync = false;
    netown_revoke_oversized();
    netown_sync_motion_types();
    netown_sync_constraints();
}

static void netown_set(int ni, bool mine) {
    if (ni < 0 || ni >= g_game.net_object_count) return;
    if (client_netown_lite()) mine = false;
    if (g_game.net_objects[ni].never_netown) mine = false;
    if (g_game.net_objects[ni].anchored || g_game.net_objects[ni].connector_static) {
        g_game.net_objects[ni].net_owned = false;
        return;
    }
    Entity* e = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
    if (mine) {
        if (g_game.net_objects[ni].net_owned && e && e->physics_body)
            return;
        g_game.net_objects[ni].net_owned = true;
        net_brick_activate_collision(ni);
        e = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
        if (e && e->physics_body) {
            netown_apply_phys_model(e);
            g_game.net_objects[ni].collision_lod_active = true;
        }
        for (int di = 0; di < g_game.dynamic_object_count; di++) {
            if (g_game.dynamic_objects[di].entity == g_game.net_objects[ni].entity) {
                g_game.dynamic_objects[di].owned_locally = true;
                break;
            }
        }
        s_netown_need_sync = true;
    } else if (g_game.net_objects[ni].net_owned) {
        g_game.net_objects[ni].net_owned = false;
        if (e) {
            if (e->physics_body && g_game.physics) {
                Vec3 p = physics_get_position(g_game.physics, e->physics_body);
                Vec3 r;
                physics_get_rotation_euler(g_game.physics, e->physics_body, &r);
                r = euler_follow(e->transform.rotation, r);
                if (isfinite(p.x) && isfinite(p.y) && isfinite(p.z) &&
                    fabsf(p.x) < 80000.0f && fabsf(p.y) < 80000.0f && fabsf(p.z) < 80000.0f) {
                    e->transform.position = p;
                    e->transform.rotation = r;
                    g_game.net_objects[ni].target_pos = p;
                    g_game.net_objects[ni].target_rot = r;
                    g_game.net_objects[ni].lerp_start_pos = p;
                    g_game.net_objects[ni].lerp_start_rot = r;
                    g_game.net_objects[ni].lerp_t = 1.0f;
                    g_game.net_objects[ni].has_target = true;
                }
            }
            e->use_phys_model = false;
        }
        for (int di = 0; di < g_game.dynamic_object_count; di++) {
            if (g_game.dynamic_objects[di].entity == g_game.net_objects[ni].entity) {
                g_game.dynamic_objects[di].owned_locally = false;
                break;
            }
        }
        s_netown_need_sync = true;
    }
}

static void netown_drop_all_local(void) {
    if (!g_game.physics) {
        for (int ni = 0; ni < g_game.net_object_count; ni++)
            g_game.net_objects[ni].net_owned = false;
        g_game.net_constraint_count = 0;
        return;
    }
    for (int ni = 0; ni < g_game.net_object_count; ni++) {
        if (g_game.net_objects[ni].net_owned)
            netown_set(ni, false);
    }
    for (int ci = 0; ci < g_game.net_constraint_count; ci++) {
        if (g_game.net_constraints[ci].local_conn) {
            physics_destroy_connector(g_game.physics, g_game.net_constraints[ci].local_conn);
            g_game.net_constraints[ci].local_conn = 0;
        }
        g_game.net_constraints[ci].active = false;
    }
    g_game.net_constraint_count = 0;
    s_netown_need_sync = true;
    netown_flush_sync();
}

static int netown_uf_find(int* p, int x) {
    while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; }
    return x;
}

static void netown_revoke_oversized(void) {
    int n = g_game.net_object_count;
    if (n <= 0) return;
    static int parent[MAX_NET_OBJECTS];
    static int counts[MAX_NET_OBJECTS];
    static float rminx[MAX_NET_OBJECTS], rminy[MAX_NET_OBJECTS], rminz[MAX_NET_OBJECTS];
    static float rmaxx[MAX_NET_OBJECTS], rmaxy[MAX_NET_OBJECTS], rmaxz[MAX_NET_OBJECTS];
    static float rvol[MAX_NET_OBJECTS];
    static uint8_t owned_root[MAX_NET_OBJECTS];
    for (int i = 0; i < n; i++) {
        parent[i] = i;
        counts[i] = 0;
        owned_root[i] = 0;
        rminx[i] = rminy[i] = rminz[i] = 1e30f;
        rmaxx[i] = rmaxy[i] = rmaxz[i] = -1e30f;
        rvol[i] = 0.0f;
    }

    for (int i = 0; i < g_conn_pair_count; i++) {
        int a = net_find_ni(g_conn_pairs[i][0]);
        int b = net_find_ni(g_conn_pairs[i][1]);
        if (a < 0 || b < 0) continue;
        if (g_game.net_objects[a].anchored || g_game.net_objects[b].anchored) continue;
        a = netown_uf_find(parent, a);
        b = netown_uf_find(parent, b);
        if (a != b) parent[a] = b;
    }
    for (int i = 0; i < g_game.net_constraint_count; i++) {
        if (!g_game.net_constraints[i].active) continue;
        if (constraint_is_nocollide(g_game.net_constraints[i].desc.type)) continue;
        int na = net_find_ni(g_game.net_constraints[i].id_a);
        int nb = net_find_ni(g_game.net_constraints[i].id_b);
        if (na < 0 || nb < 0) continue;
        if (g_game.net_objects[na].anchored || g_game.net_objects[nb].anchored) continue;
        na = netown_uf_find(parent, na);
        nb = netown_uf_find(parent, nb);
        if (na != nb) parent[na] = nb;
    }

    for (int i = 0; i < n; i++) {
        if (g_game.net_objects[i].anchored) continue;
        int r = netown_uf_find(parent, i);
        counts[r]++;
        if (g_game.net_objects[i].net_owned) owned_root[r] = 1;
        Entity* e = scene_get_entity(&g_game.scene, g_game.net_objects[i].entity);
        Vec3 p = e ? e->transform.position : (Vec3){ 0, 0, 0 };
        float hx = fabsf(g_game.net_objects[i].size[0]) * 0.5f;
        float hy = fabsf(g_game.net_objects[i].size[1]) * 0.5f;
        float hz = fabsf(g_game.net_objects[i].size[2]) * 0.5f;
        if (p.x - hx < rminx[r]) rminx[r] = p.x - hx;
        if (p.y - hy < rminy[r]) rminy[r] = p.y - hy;
        if (p.z - hz < rminz[r]) rminz[r] = p.z - hz;
        if (p.x + hx > rmaxx[r]) rmaxx[r] = p.x + hx;
        if (p.y + hy > rmaxy[r]) rmaxy[r] = p.y + hy;
        if (p.z + hz > rmaxz[r]) rmaxz[r] = p.z + hz;
        rvol[r] += fabsf(g_game.net_objects[i].size[0] *
                         g_game.net_objects[i].size[1] *
                         g_game.net_objects[i].size[2]);
    }

    for (int i = 0; i < n; i++) {
        if (g_game.net_objects[i].anchored || !g_game.net_objects[i].net_owned) continue;
        int r = netown_uf_find(parent, i);
        if (!owned_root[r]) continue;
        bool too = counts[r] >= (int)PW_NETOWN_MAX_ASM_PARTS
            || (rmaxx[r] - rminx[r]) > PW_NETOWN_MAX_ASM_SPAN
            || (rmaxy[r] - rminy[r]) > PW_NETOWN_MAX_ASM_SPAN
            || (rmaxz[r] - rminz[r]) > PW_NETOWN_MAX_ASM_SPAN
            || rvol[r] > PW_NETOWN_MAX_ASM_VOL;
        if (!too) continue;
        netown_set(i, false);
    }
}

static void netown_send_poses(void) {
    if (g_game.net_proto < PW_PROTO_NETOWN || !g_game.world_ready) return;
    if (client_netown_lite()) return;
    if (g_game.avatar.dead) return;
    uint8_t buf[2 + 64 * PW_OWNED_POSE_WIRE];
    uint16_t n = 0;
    size_t off = 2;
    for (int ni = 0; ni < g_game.net_object_count && n < 64; ni++) {
        if (!g_game.net_objects[ni].net_owned) continue;
        Entity* e = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
        if (!e || !e->physics_body) continue;
        Vec3 p = physics_get_position(g_game.physics, e->physics_body);
        Vec3 r;
        physics_get_rotation_euler(g_game.physics, e->physics_body, &r);
        r = euler_follow(e->transform.rotation, r);
        Vec3 v = physics_get_velocity(g_game.physics, e->physics_body);
        Vec3 a = physics_get_angular_velocity(g_game.physics, e->physics_body);
        pw_clamp_fall_vel(net_part_mass_from_size(g_game.net_objects[ni].size),
                          &v.x, &v.y, &v.z);
        physics_set_velocity(g_game.physics, e->physics_body, v);
        memcpy(buf + off, &g_game.net_objects[ni].net_id, 4); off += 4;
        memcpy(buf + off, &p.x, 12); off += 12;
        memcpy(buf + off, &r.x, 12); off += 12;
        memcpy(buf + off, &v.x, 12); off += 12;
        memcpy(buf + off, &a.x, 12); off += 12;
        n++;
    }
    if (n == 0) return;
    memcpy(buf, &n, 2);
    net_client_send(&g_game.net, MSG_OWNED_POSE, buf, off);
}

static void netown_query_parts(void) {
    if (g_game.net_proto < PW_PROTO_NETOWN || !g_game.world_ready) return;
    if (g_game.net.state != NET_STATE_CONNECTED) return;
    static double s_t = 0.0;
    static int s_cursor = 0;
    double now = platform_get_time();
    if (now - s_t < 0.08) return;
    s_t = now;
    int total = g_game.net_object_count;
    if (total <= 0) return;
    if (s_cursor < 0 || s_cursor >= total) s_cursor = 0;

    uint8_t buf[2 + PW_PART_QUERY_MAX * 4];
    uint16_t n = 0;
    size_t off = 2;
    uint32_t picked[PW_PART_QUERY_MAX];
    Vec3 me = g_game.avatar.pos;

    for (int ni = 0; ni < total && n < 12; ni++) {
        if (g_game.net_objects[ni].anchored && !g_game.net_objects[ni].never_netown)
            continue;
        Entity* e = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
        if (!e) continue;
        float dx = e->transform.position.x - me.x;
        float dy = e->transform.position.y - me.y;
        float dz = e->transform.position.z - me.z;
        if (dx * dx + dy * dy + dz * dz > 80.0f * 80.0f) continue;
        picked[n] = g_game.net_objects[ni].net_id;
        memcpy(buf + off, &g_game.net_objects[ni].net_id, 4);
        off += 4;
        n++;
    }

    int scanned = 0;
    while (scanned < total && n < PW_PART_QUERY_MAX) {
        int ni = (s_cursor + scanned) % total;
        scanned++;
        uint32_t id = g_game.net_objects[ni].net_id;
        bool dup = false;
        for (uint16_t k = 0; k < n; k++) {
            if (picked[k] == id) { dup = true; break; }
        }
        if (dup) continue;
        picked[n] = id;
        memcpy(buf + off, &id, 4);
        off += 4;
        n++;
    }
    s_cursor = (s_cursor + (scanned > 0 ? scanned : 1)) % total;
    if (n == 0) return;
    memcpy(buf, &n, 2);
    net_client_send(&g_game.net, MSG_PART_QUERY, buf, off);
}

static void net_apply_part_alive(uint32_t oid, uint8_t otype, uint8_t anchored, uint8_t can_collide,
                                 const float pos[3], const float rot[3], const float size[3],
                                 const float color[3], const float vel[3], const float ang[3],
                                 uint32_t owner_pid, bool apply_vel, bool force_correct) {
    int ni = net_find_ni(oid);
    if (ni < 0) return;
    Entity* target = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
    if (!target) return;

    g_game.net_objects[ni].anchored = (anchored != 0);
    g_game.net_objects[ni].collide_wanted = (can_collide != 0);
    if (otype <= 3) g_game.net_objects[ni].obj_type = otype;
    if (!can_collide)
        net_brick_deactivate_collision(ni);
    else if (!target->physics_body)
        net_brick_activate_collision(ni);
    bool mine = (g_game.local_player_id != 0 && owner_pid == g_game.local_player_id);
    netown_set(ni, mine);

    target->material.color = (Vec3){ color[0], color[1], color[2] };

    if (size[0] > 0.0f && size[1] > 0.0f && size[2] > 0.0f) {
        float* ps = g_game.net_objects[ni].size;
        float dsz = fabsf(size[0] - ps[0]) + fabsf(size[1] - ps[1]) + fabsf(size[2] - ps[2]);
        if (dsz > 0.01f) {
            ps[0] = size[0]; ps[1] = size[1]; ps[2] = size[2];
            target->transform.scale = (Vec3){ size[0], size[1], size[2] };
        }
    }

    if (apply_vel && target->physics_body && g_game.physics && vel && ang) {
        if (isfinite(vel[0]) && isfinite(vel[1]) && isfinite(vel[2]) &&
            isfinite(ang[0]) && isfinite(ang[1]) && isfinite(ang[2])) {
            physics_set_velocity(g_game.physics, target->physics_body,
                                 (Vec3){ vel[0], vel[1], vel[2] });
            physics_set_angular_velocity(g_game.physics, target->physics_body,
                                         (Vec3){ ang[0], ang[1], ang[2] });
            physics_activate(g_game.physics, target->physics_body);
        }
    }

    if (g_game.net_objects[ni].net_owned && !force_correct) return;

    Vec3 tgt = (Vec3){ pos[0], pos[1], pos[2] };
    if (!isfinite(tgt.x) || !isfinite(tgt.y) || !isfinite(tgt.z) ||
        fabsf(tgt.x) > 80000.0f || fabsf(tgt.y) > 80000.0f || fabsf(tgt.z) > 80000.0f)
        return;
    Vec3 nr = euler_follow(target->transform.rotation, (Vec3){ rot[0], rot[1], rot[2] });
    net_apply_replicated_pose(target, ni, tgt, nr, !force_correct && !apply_vel, !apply_vel);
}

static void netown_emit_walk_hit(uint32_t oid, float dx, float dz) {
    if (g_game.net_proto < PW_PROTO_NETOWN || !g_game.world_ready) return;
    if (g_game.net.state != NET_STATE_CONNECTED) return;

    static uint32_t s_last_oid = 0;
    static double s_last_t = 0.0;
    double now = platform_get_time();
    if (oid == 0) {
        if (s_last_oid == 0) return;
    } else if (oid == s_last_oid && now - s_last_t < 0.10) {
        return;
    }
    s_last_oid = oid;
    s_last_t = now;

    uint8_t buf[12];
    memcpy(buf, &oid, 4);
    memcpy(buf + 4, &dx, 4);
    memcpy(buf + 8, &dz, 4);
    net_client_send(&g_game.net, MSG_WALK_HIT, buf, 12);
}

static void netown_local_push(void) {
    if (client_netown_lite()) return;
    if (g_game.net_proto < PW_PROTO_NETOWN || !g_game.world_ready) return;
    if (g_game.avatar.dead || g_game.camera_mode != CAM_MODE_NORMAL) return;
    PhysicsBodyID hit = g_game.avatar.walk_hit_body;
    if (!hit) return;
    float ix = g_game.avatar.move_intent_x;
    float iz = g_game.avatar.move_intent_z;
    float il = sqrtf(ix * ix + iz * iz);
    if (il < 0.4f) return;
    float wdx = ix / il, wdz = iz / il;
    float walk_spd = g_game.avatar.walk_speed;
    if (walk_spd < 1.0f) walk_spd = 16.0f;

    int ni_hit = -1;
    bool owned = false;
    for (int ni = 0; ni < g_game.net_object_count; ni++) {
        if (g_game.net_objects[ni].anchored || g_game.net_objects[ni].never_netown)
            continue;
        Entity* e = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
        if (!e || !e->physics_body) continue;
        if (e->physics_body != hit &&
            !physics_same_rigid_body(g_game.physics, e->physics_body, hit))
            continue;
        if (g_game.net_objects[ni].net_owned) {
            ni_hit = ni;
            owned = true;
            break;
        }
        if (ni_hit < 0)
            ni_hit = ni;
    }
    if (ni_hit < 0) return;
    if (!owned) {

        netown_emit_walk_hit(g_game.net_objects[ni_hit].net_id, wdx, wdz);
        return;
    }

    Entity* e = scene_get_entity(&g_game.scene, g_game.net_objects[ni_hit].entity);
    if (!e || !e->physics_body) return;

    float mass = physics_get_mass(g_game.physics, hit);
    if (mass < 0.25f) mass = 0.25f;
    const float ref = 8.0f;
    const float maxm = 64.0f;
    if (mass > maxm) return;
    float scale = ref / mass;
    if (scale > 1.0f) scale = 1.0f;
    float target = walk_spd * 0.85f * scale;
    float max_hspd = walk_spd * 1.15f * scale;
    if (max_hspd < 0.5f) max_hspd = 0.5f;

    Vec3 bp = physics_get_position(g_game.physics, hit);
    Vec3 pp = g_game.avatar.pos;
    float dx = bp.x - pp.x;
    float dz = bp.z - pp.z;
    Vec3 v = physics_get_velocity(g_game.physics, hit);
    float along = v.x * wdx + v.z * wdz;
    if (along < target) {
        v.x += wdx * (target - along);
        v.z += wdz * (target - along);
        along = target;
    }
    if (along > max_hspd) {
        float extra = along - max_hspd;
        v.x -= wdx * extra;
        v.z -= wdz * extra;
    }
    physics_set_velocity(g_game.physics, hit, v);
    if (mass > ref * 1.5f) return;
    Vec3 ang = physics_get_angular_velocity(g_game.physics, hit);
    float spin = (dx * wdz - dz * wdx) * 2.5f * scale;
    ang.y += spin;
    if (ang.y > 8.0f) ang.y = 8.0f;
    if (ang.y < -8.0f) ang.y = -8.0f;
    physics_set_angular_velocity(g_game.physics, hit, ang);
}

static void netown_send_walk_hit(void) {
    if (g_game.net_proto < PW_PROTO_NETOWN || !g_game.world_ready) return;
    if (g_game.net.state != NET_STATE_CONNECTED) return;
    if (!client_netown_lite()) return;

    uint32_t oid = 0;
    float dx = 0.0f, dz = 0.0f;
    float ix = g_game.avatar.move_intent_x;
    float iz = g_game.avatar.move_intent_z;
    float il = sqrtf(ix * ix + iz * iz);
    if (il >= 0.4f && !g_game.avatar.dead && g_game.camera_mode == CAM_MODE_NORMAL &&
        g_game.physics) {
        dx = ix / il;
        dz = iz / il;
        Vec3 origin = {
            g_game.avatar.pos.x - dx * 0.25f,
            g_game.avatar.pos.y + 1.0f,
            g_game.avatar.pos.z - dz * 0.25f
        };
        Vec3 dir = { dx, 0.0f, dz };
        if (g_game.avatar.body)
            physics_disable_geom(g_game.physics, g_game.avatar.body);
        RaycastHit hit = physics_raycast(g_game.physics, origin, dir, 5.0f);
        if (g_game.avatar.body)
            physics_enable_geom(g_game.physics, g_game.avatar.body);
        if (hit.hit && hit.body && hit.normal.y < 0.65f) {
            for (int ni = 0; ni < g_game.net_object_count; ni++) {
                if (g_game.net_objects[ni].anchored || g_game.net_objects[ni].never_netown)
                    continue;
                Entity* e = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
                if (!e || !e->physics_body) continue;
                if (e->physics_body != hit.body &&
                    !physics_same_rigid_body(g_game.physics, e->physics_body, hit.body))
                    continue;
                oid = g_game.net_objects[ni].net_id;
                break;
            }
        }
    }

    netown_emit_walk_hit(oid, dx, dz);
}

static int net_brick_lod_dist(int ni, Entity* ent, Vec3 focus) {
    float hx = g_game.net_objects[ni].size[0] * 0.5f;
    float hz = g_game.net_objects[ni].size[2] * 0.5f;
    if (hx < 0.01f) hx = 0.01f;
    if (hz < 0.01f) hz = 0.01f;
    Vec3 r = ent->transform.rotation;
    if (fabsf(r.x) > 0.5f || fabsf(r.y) > 0.5f || fabsf(r.z) > 0.5f) {
        float m = hx > hz ? hx : hz;
        hx = hz = m * 1.42f;
    }
    return collision_chunk_dist_aabb(ent->transform.position, hx, hz, focus);
}

static bool net_brick_within_xz_radius(int ni, Entity* ent, Vec3 focus, float radius) {
    float hx = g_game.net_objects[ni].size[0] * 0.5f;
    float hz = g_game.net_objects[ni].size[2] * 0.5f;
    if (hx < 0.01f) hx = 0.01f;
    if (hz < 0.01f) hz = 0.01f;
    Vec3 r = ent->transform.rotation;
    if (fabsf(r.x) > 0.5f || fabsf(r.y) > 0.5f || fabsf(r.z) > 0.5f) {
        float m = hx > hz ? hx : hz;
        hx = hz = m * 1.42f;
    }
    Vec3 pos = ent->transform.position;
    float nx = focus.x;
    if (nx < pos.x - hx) nx = pos.x - hx;
    if (nx > pos.x + hx) nx = pos.x + hx;
    float nz = focus.z;
    if (nz < pos.z - hz) nz = pos.z - hz;
    if (nz > pos.z + hz) nz = pos.z + hz;
    float dx = nx - focus.x;
    float dz = nz - focus.z;
    return (dx * dx + dz * dz) <= (radius * radius);
}

static bool net_brick_is_baseplate_like(int ni) {
    if (ni < 0 || ni >= g_game.net_object_count) return false;
    float sx = g_game.net_objects[ni].size[0];
    float sy = g_game.net_objects[ni].size[1];
    float sz = g_game.net_objects[ni].size[2];
    if (sx >= 64.0f && sz >= 64.0f && sy <= 4.0f) return true;
    if (sx >= 128.0f && sz >= 128.0f) return true;
    return false;
}

static bool collision_lod_focus_chunk_needs_reload(void) {
    if (!g_game.multiplayer || g_game.net_object_count <= 0) return false;
    Vec3 focus = collision_lod_focus();
    int fcx = collision_chunk_coord(focus.x);
    int fcz = collision_chunk_coord(focus.z);
    int expected = 0;
    int loaded = 0;
    for (int ni = 0; ni < g_game.net_object_count; ni++) {
        if (!g_game.net_objects[ni].collide_wanted) continue;
        if (net_brick_is_baseplate_like(ni)) continue;
        Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
        if (!ent || !ent->active) continue;

        if (collision_chunk_coord(ent->transform.position.x) != fcx) continue;
        if (collision_chunk_coord(ent->transform.position.z) != fcz) continue;
        expected++;
        if (ent->physics_body) loaded++;
    }
    return expected > 0 && loaded == 0;
}

static bool create_physics_for_net_index(int ni) {
    if (ni < 0 || ni >= g_game.net_object_count) return true;
    if (!g_game.net_objects[ni].collide_wanted) {
        g_game.net_objects[ni].collision_lod_active = false;
        return true;
    }

    if (net_brick_is_baseplate_like(ni))
        return net_brick_activate_collision(ni);

    if (!g_game.spawn_received) {
        g_game.net_objects[ni].collision_lod_active = false;
        return true;
    }
    Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
    if (!ent) return true;
    Vec3 focus = collision_lod_focus();
    if (!net_brick_always_keep_collision(ni) &&
        !net_brick_within_xz_radius(ni, ent, focus, COLLISION_UNDERFOOT_RADIUS) &&
        net_brick_lod_dist(ni, ent, focus) > COLLISION_CHUNK_KEEP) {
        g_game.net_objects[ni].collision_lod_active = false;
        return true;
    }
    return net_brick_activate_collision(ni);
}

static void collision_lod_force_resync(void) {
    g_game.collision_lod_focus_cx = INT_MIN;
    g_game.collision_lod_focus_cz = INT_MIN;
    g_game.collision_lod_idle_frames = 0;
    g_game.collision_lod_scan_i = 0;
}

static bool collision_lod_evict_farthest(Vec3 focus) {
    int best = -1;
    int best_dist = COLLISION_CHUNK_KEEP;
    for (int ni = 0; ni < g_game.net_object_count; ni++) {
        Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
        if (!ent || !ent->physics_body) continue;
        if (net_brick_always_keep_collision(ni)) continue;
        if (g_game.avatar.ground_body == ent->physics_body) continue;

        if (net_brick_within_xz_radius(ni, ent, focus, COLLISION_UNDERFOOT_RADIUS))
            continue;
        int dist = net_brick_lod_dist(ni, ent, focus);
        if (dist > best_dist) {
            best_dist = dist;
            best = ni;
        }
    }
    if (best < 0) return false;
    net_brick_deactivate_collision(best);
    return true;
}

static bool collision_lod_nearby_incomplete(void) {
    if (!g_game.multiplayer || g_game.net_object_count <= 0) return false;
    Vec3 focus = collision_lod_focus();
    for (int ni = 0; ni < g_game.net_object_count; ni++) {
        if (!g_game.net_objects[ni].collide_wanted) continue;
        Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
        if (!ent || !ent->active) continue;
        if (net_brick_within_xz_radius(ni, ent, focus, COLLISION_UNDERFOOT_RADIUS) &&
            !ent->physics_body)
            return true;
    }
    return false;
}

static void collision_lod_activate_near_focus(void) {
    if (!g_game.multiplayer || g_game.net_object_count <= 0) return;
    Vec3 focus = collision_lod_focus();
    double now = platform_get_time();

    for (int ni = 0; ni < g_game.net_object_count; ni++) {
        if (!g_game.net_objects[ni].collide_wanted) continue;
        if (!net_brick_is_baseplate_like(ni)) continue;
        Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
        if (!ent || !ent->active) continue;

        if (!net_brick_within_xz_radius(ni, ent, focus, COLLISION_UNDERFOOT_RADIUS * 2.0f))
            continue;
        if (!ent->physics_body)
            net_brick_activate_collision(ni);
        else
            g_game.net_objects[ni].collision_lod_active = true;
    }

    for (int ni = 0; ni < g_game.net_object_count; ni++) {
        Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
        if (!ent || !ent->active) continue;
        if (!g_game.net_objects[ni].collide_wanted) continue;
        if (net_brick_is_baseplate_like(ni)) continue;
        bool near_focus = net_brick_within_xz_radius(ni, ent, focus, COLLISION_UNDERFOOT_RADIUS) ||
                          net_brick_lod_dist(ni, ent, focus) <= COLLISION_CHUNK_KEEP;
        if (!near_focus) continue;
        if (!ent->physics_body)
            net_brick_activate_collision(ni);
        else
            g_game.net_objects[ni].collision_lod_active = true;
    }

    for (int ni = 0; ni < g_game.net_object_count; ni++) {
        Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
        if (!ent || !ent->active) continue;
        if (!g_game.net_objects[ni].collide_wanted) continue;
        if (!net_brick_always_keep_collision_at(ni, now)) continue;
        if (net_brick_within_xz_radius(ni, ent, focus, COLLISION_UNDERFOOT_RADIUS))
            continue;
        if (net_brick_lod_dist(ni, ent, focus) <= COLLISION_CHUNK_KEEP) continue;
        if (!ent->physics_body)
            net_brick_activate_collision(ni);
        else
            g_game.net_objects[ni].collision_lod_active = true;
    }
    if (g_game.physics)
        physics_optimize_broadphase(g_game.physics);
}

static void collision_lod_ensure_underfoot(void) {
    if (!g_game.multiplayer || !g_game.spawn_received) {
        g_game.collision_chunk_loading = false;
        g_game.avatar.freeze_locomotion = false;
        return;
    }

    if (g_game.world_colliders_ready || g_game.world_ready) {
        if (collision_lod_focus_chunk_needs_reload())
            collision_lod_force_resync();
        collision_lod_activate_near_focus();
    }

    if (!g_game.collision_spawn_gate) {
        g_game.collision_chunk_loading = false;
        g_game.avatar.freeze_locomotion = false;
        return;
    }

    if (!g_game.world_colliders_ready && !g_game.world_ready) {
        g_game.collision_chunk_loading = true;
        g_game.avatar.freeze_locomotion = true;
        return;
    }

    double now = platform_get_time();
    bool can_count_attempt =
        (g_game.collision_load_attempts < COLLISION_LOAD_MAX_ATTEMPTS) &&
        (g_game.collision_load_attempt_time <= 0.0 ||
         (now - g_game.collision_load_attempt_time) >= COLLISION_LOAD_ATTEMPT_INTERVAL);

    if (can_count_attempt) {
        g_game.collision_load_attempts++;
        g_game.collision_load_attempt_time = now;
        if (collision_lod_focus_chunk_needs_reload())
            collision_lod_force_resync();
        collision_lod_activate_near_focus();
    }

    bool no_ground = true;
    if (g_game.physics && g_game.avatar.body) {
        physics_disable_geom(g_game.physics, g_game.avatar.body);
        Vec3 base = g_game.collision_chunk_loading ? g_game.collision_pin_pos : g_game.avatar.pos;

        const float offs[5][2] = {
            {0.0f, 0.0f}, {0.6f, 0.0f}, {-0.6f, 0.0f}, {0.0f, 0.6f}, {0.0f, -0.6f}
        };
        for (int pi = 0; pi < 5; pi++) {
            Vec3 ro = { base.x + offs[pi][0], base.y + 3.0f, base.z + offs[pi][1] };
            RaycastHit hit = physics_raycast(g_game.physics, ro, (Vec3){0, -1, 0}, 64.0f);
            if (hit.hit && hit.body != g_game.avatar.body) {
                no_ground = false;
                float feet = hit.point.y + AVATAR_ROOT_HALF_Y;
                g_game.collision_pin_pos = (Vec3){ base.x, feet, base.z };
                g_game.avatar.pos = g_game.collision_pin_pos;
                break;
            }
        }
        physics_enable_geom(g_game.physics, g_game.avatar.body);
    } else {
        no_ground = collision_lod_nearby_incomplete();
    }

    if (!no_ground || g_game.collision_load_attempts >= COLLISION_LOAD_MAX_ATTEMPTS) {

        g_game.collision_spawn_gate = false;
        g_game.collision_chunk_loading = false;
        g_game.avatar.freeze_locomotion = false;
        if (g_game.avatar.body)
            physics_set_position(g_game.physics, g_game.avatar.body, g_game.avatar.pos);
        return;
    }

    if (!g_game.collision_chunk_loading)
        g_game.collision_pin_pos = g_game.avatar.pos;
    g_game.collision_chunk_loading = true;
    g_game.avatar.freeze_locomotion = true;
    g_game.avatar.pos = g_game.collision_pin_pos;
    g_game.avatar.vel = (Vec3){0, 0, 0};
    if (g_game.avatar.body)
        physics_set_position(g_game.physics, g_game.avatar.body, g_game.avatar.pos);
    if (g_game.move_lock_timer < 0.25f)
        g_game.move_lock_timer = 0.25f;
}

static void update_collision_chunk_lod(void) {
    if (!g_game.multiplayer || !g_game.world_colliders_ready) return;
    if (g_game.net_object_count <= 0) return;
    if (g_game.physics_streaming || g_game.connectors_streaming) return;

    Vec3 focus = collision_lod_focus();
    int fcx = collision_chunk_coord(focus.x);
    int fcz = collision_chunk_coord(focus.z);

    if (collision_lod_focus_chunk_needs_reload()) {
        g_game.collision_lod_focus_cx = fcx;
        g_game.collision_lod_focus_cz = fcz;
        g_game.collision_lod_idle_frames = 0;
        collision_lod_activate_near_focus();
        return;
    }

    if (fcx != g_game.collision_lod_focus_cx || fcz != g_game.collision_lod_focus_cz) {
        g_game.collision_lod_focus_cx = fcx;
        g_game.collision_lod_focus_cz = fcz;
        g_game.collision_lod_idle_frames = 0;
    } else if (g_game.collision_lod_idle_frames > 0) {
        g_game.collision_lod_idle_frames--;
        return;
    }

    double t0 = platform_get_time();
    double now = t0;
    int ops = 0;
    int scanned = 0;
    int n = g_game.net_object_count;
    if (g_game.collision_lod_scan_i < 0 || g_game.collision_lod_scan_i >= n)
        g_game.collision_lod_scan_i = 0;

    for (int k = 0; k < n; k++) {
        if (ops >= COLLISION_LOD_MAX_OPS) break;
        if ((k & 7) == 0 && platform_get_time() - t0 >= COLLISION_LOD_SLICE_SEC) break;

        int ni = g_game.collision_lod_scan_i;
        g_game.collision_lod_scan_i = (ni + 1) % n;
        scanned++;

        Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
        if (!ent || !ent->active) continue;

        if (!g_game.net_objects[ni].collide_wanted) {
            if (ent->physics_body) {
                net_brick_deactivate_collision(ni);
                ops++;
            }
            continue;
        }

        bool always = net_brick_always_keep_collision_at(ni, now);
        int dist = net_brick_lod_dist(ni, ent, focus);
        bool underfoot = net_brick_within_xz_radius(ni, ent, focus, COLLISION_UNDERFOOT_RADIUS) ||
                         net_brick_is_baseplate_like(ni);

        if (always || underfoot || dist <= COLLISION_CHUNK_KEEP) {
            if (!ent->physics_body) {
                net_brick_activate_collision(ni);
                ops++;
            } else {
                g_game.net_objects[ni].collision_lod_active = true;
            }
        } else if (!always && !underfoot && dist > COLLISION_CHUNK_DROP) {
            if (ent->physics_body) {

                if (g_game.avatar.ground_body == ent->physics_body)
                    continue;
                net_brick_deactivate_collision(ni);
                ops++;
            }
        }
    }

    if (ops == 0 && scanned >= n)
        g_game.collision_lod_idle_frames = COLLISION_LOD_IDLE_SKIP;
}

static void draw_collision_chunk_borders(const Mat4* view, const Mat4* projection) {
    if (!g_game.show_chunk_borders) return;

    Vec3 focus = collision_lod_focus();
    int fcx = collision_chunk_coord(focus.x);
    int fcz = collision_chunk_coord(focus.z);
    float y0 = focus.y - 8.0f;
    float y1 = focus.y + 48.0f;
    float cs = COLLISION_CHUNK_SIZE;

    int rmax = COLLISION_CHUNK_DROP + 1;
    for (int dz = -rmax; dz <= rmax; dz++) {
        for (int dx = -rmax; dx <= rmax; dx++) {
            int cheb = dx < 0 ? -dx : dx;
            int adz = dz < 0 ? -dz : dz;
            if (adz > cheb) cheb = adz;
            if (cheb > COLLISION_CHUNK_DROP) continue;

            float x0 = collision_chunk_world_min(fcx + dx);
            float z0 = collision_chunk_world_min(fcz + dz);
            float x1 = x0 + cs;
            float z1 = z0 + cs;

            Vec3 color = (cheb <= COLLISION_CHUNK_KEEP)
                ? (Vec3){0.2f, 0.95f, 1.0f}
                : (Vec3){1.0f, 0.7f, 0.15f};

            renderer_debug_line(&g_game.renderer, (Vec3){x0, y0, z0}, (Vec3){x0, y1, z0}, color, view, projection);
            renderer_debug_line(&g_game.renderer, (Vec3){x1, y0, z0}, (Vec3){x1, y1, z0}, color, view, projection);
            renderer_debug_line(&g_game.renderer, (Vec3){x0, y0, z1}, (Vec3){x0, y1, z1}, color, view, projection);
            renderer_debug_line(&g_game.renderer, (Vec3){x1, y0, z1}, (Vec3){x1, y1, z1}, color, view, projection);
            renderer_debug_line(&g_game.renderer, (Vec3){x0, y1, z0}, (Vec3){x1, y1, z0}, color, view, projection);
            renderer_debug_line(&g_game.renderer, (Vec3){x1, y1, z0}, (Vec3){x1, y1, z1}, color, view, projection);
            renderer_debug_line(&g_game.renderer, (Vec3){x1, y1, z1}, (Vec3){x0, y1, z1}, color, view, projection);
            renderer_debug_line(&g_game.renderer, (Vec3){x0, y1, z1}, (Vec3){x0, y1, z0}, color, view, projection);
            renderer_debug_line(&g_game.renderer, (Vec3){x0, y0, z0}, (Vec3){x1, y0, z0}, color, view, projection);
            renderer_debug_line(&g_game.renderer, (Vec3){x1, y0, z0}, (Vec3){x1, y0, z1}, color, view, projection);
            renderer_debug_line(&g_game.renderer, (Vec3){x1, y0, z1}, (Vec3){x0, y0, z1}, color, view, projection);
            renderer_debug_line(&g_game.renderer, (Vec3){x0, y0, z1}, (Vec3){x0, y0, z0}, color, view, projection);
        }
    }
}

static void finish_physics_stream(void) {
    g_game.physics_streaming = false;
    g_game.physics_stream_i = 0;

    if (g_game.spawn_received)
        collision_lod_activate_near_focus();
    else {
        for (int ni = 0; ni < g_game.net_object_count; ni++)
            net_brick_activate_collision(ni);
        if (g_game.physics)
            physics_optimize_broadphase(g_game.physics);
    }

    if (g_game.pending_connectors && g_game.pending_connectors_len > 0) {
        if (connectors_prepare(g_game.pending_connectors, g_game.pending_connectors_len)) {
            g_game.connectors_streaming = true;
            g_game.connectors_phase = 1;
            g_game.connectors_stream_i = 0;
            return;
        }
        free(g_game.pending_connectors);
        g_game.pending_connectors = NULL;
        g_game.pending_connectors_len = 0;
    }

    finish_connectors_and_spawn();
}

static void stream_world_init_objects(void) {
#if PW_WORLD_STREAM
    double t0 = platform_get_time();
#endif

    if (g_game.world_init_streaming && g_game.world_init_buf) {
#if !PW_WORLD_STREAM

        size_t need = 4u + (size_t)g_game.world_init_total * (size_t)WORLD_INIT_OBJ_BYTES;
        if (g_game.world_init_len < need)
            return;
        while (g_game.world_init_done < g_game.world_init_total) {
            if (g_game.world_init_off + WORLD_INIT_OBJ_BYTES > g_game.world_init_len)
                return;
            world_init_create_one(g_game.world_init_buf + g_game.world_init_off);
            g_game.world_init_off += WORLD_INIT_OBJ_BYTES;
            g_game.world_init_done++;
        }
        if (g_game.world_init_done >= g_game.world_init_total)
            finish_object_stream_start_physics();

#else
        while (g_game.world_init_done < g_game.world_init_total) {

            if (g_game.world_init_off + WORLD_INIT_OBJ_BYTES > g_game.world_init_len)
                return;
            if (platform_get_time() - t0 >= LOAD_SLICE_SEC)
                return;
            world_init_create_one(g_game.world_init_buf + g_game.world_init_off);
            g_game.world_init_off += WORLD_INIT_OBJ_BYTES;
            g_game.world_init_done++;
        }

        if (g_game.world_init_done >= g_game.world_init_total)
            finish_object_stream_start_physics();
        return;
#endif
    }

    if (g_game.physics_streaming) {
        while (g_game.physics_stream_i < g_game.net_object_count) {
#if PW_WORLD_STREAM
            if (platform_get_time() - t0 >= LOAD_SLICE_SEC)
                return;
#endif
            create_physics_for_net_index(g_game.physics_stream_i);
            g_game.physics_stream_i++;
        }
        finish_physics_stream();

    }

    if (g_game.connectors_streaming) {
        if (g_game.connectors_phase == 1) {
            while (g_game.connectors_stream_i < g_conn_n) {
#if PW_WORLD_STREAM
                if (platform_get_time() - t0 >= LOAD_SLICE_SEC)
                    return;
#endif
                connectors_make_static_one(g_game.connectors_stream_i);
                g_game.connectors_stream_i++;
            }
            g_game.connectors_phase = 2;
            g_game.connectors_stream_i = 0;
        }
        if (g_game.connectors_phase == 2) {
            while (g_game.connectors_stream_i < g_conn_pair_count) {
#if PW_WORLD_STREAM
                if (platform_get_time() - t0 >= LOAD_SLICE_SEC)
                    return;
#endif
                connectors_joint_one(g_game.connectors_stream_i);
                g_game.connectors_stream_i++;
            }
            finish_connectors_and_spawn();
        }
    }
}

static void poll_await_batch_ready(void) {
    if (!g_game.await_batch_ready) return;
    brick_batch_update(&g_game.scene);
    if (!brick_batch_is_building()) {
        g_game.world_ready = true;
        g_game.await_batch_ready = false;
    }
}

static void flush_pending_scripts(int max_n) {
    if (!g_game.world_ready || !g_game.scripts || max_n <= 0) return;
    while (max_n-- > 0 && g_game.pending_script_count > 0) {
        int si = 0;
        uint32_t parent_obj_id = g_game.pending_scripts[si].parent_obj_id;
        char* source = g_game.pending_scripts[si].source;

        g_game.pending_script_count--;
        for (int j = 0; j < g_game.pending_script_count; j++)
            g_game.pending_scripts[j] = g_game.pending_scripts[j + 1];

        EntityID parent_eid = ENTITY_INVALID;
        if (parent_obj_id != 0) {
            for (int ni = 0; ni < g_game.net_object_count; ni++) {
                if (g_game.net_objects[ni].net_id == parent_obj_id) {
                    parent_eid = g_game.net_objects[ni].entity;
                    break;
                }
            }
        }
        if (parent_obj_id == 0 || parent_eid != ENTITY_INVALID)
            client_script_load(g_game.scripts, parent_eid, source);
        free(source);
    }
}

static void game_frame(double dt) {
    if (!g_game.initialized) return;

#ifndef __EMSCRIPTEN__

    if (g_game.show_kick || g_game.show_disconnect) {
#ifdef __ANDROID__

        touch_controls_set_enabled(false);
#endif
        const char* title = g_game.show_kick ? "Kicked" : "Disconnected";
        const char* sub = g_game.show_kick
            ? (g_game.kick_reason[0] ? g_game.kick_reason : "You were removed from the game.")
            : disconnect_subtitle();
        bool leave = draw_load_overlay(title, sub, true, (float)dt);
        if (leave) {
            g_game.show_kick = false;
            g_game.show_disconnect = false;
            g_game.disconnect_reason[0] = '\0';
            leave_game_ui();
            g_game.loading_time = 0.0f;
        }
        input_pre_frame();
        input_post_frame();
        return;
    }
#endif

#ifndef __EMSCRIPTEN__
#ifdef VR
    if (g_vr_hub_play_pending) {
        g_vr_hub_play_pending = false;
        vr_hub_set_active(false);
        vr_hub_shutdown();
        g_game.vr.hmd_yaw = 0.0f;
        vr_openxr_set_yaw_offset(0.0f);
        consume_login_play(&g_game.login_screen);
    }
#endif

    if (g_game.show_login) {
        LoginScreen* ls = &g_game.login_screen;

#ifdef VR

        if (g_game.vr.active && !vr_hub_active()) {
            enter_vr_hub(ls);
        }
#endif

        if (g_game.show_login) {

        if (!ls->skip_benchmark_dirty)
            ls->skip_benchmark = g_game.menu.skip_startup_benchmark;
        if (ls->skip_benchmark_dirty) {
            g_game.menu.skip_startup_benchmark = ls->skip_benchmark;
            game_menu_save_settings(&g_game.menu);
            ls->skip_benchmark_dirty = false;
        }

#ifdef VR
        if (!g_game.vr.active &&
#else
        if (
#endif
            !ls->update_required &&
            (game_menu_first_run_active(&g_game.menu) ||
            (ls->logged_in && ls->phase >= 1 && game_menu_needs_first_run(&g_game.menu)))) {
            if (!game_menu_first_run_active(&g_game.menu))
                game_menu_begin_first_run(&g_game.menu);

            int sw = g_game.renderer.canvas_width;
            int sh = g_game.renderer.canvas_height;

            {
                g_game.renderer.shadows_enabled = false;
                g_game.renderer.fog_enabled = false;
                g_game.renderer.shadow_range = 50.0f;
                g_game.renderer.shadow_soft = 0;
                g_game.renderer.glow_leak_mode = (int)GFX_GLOW_LEAK_DISTANCE;
                renderer_set_shadow_map_size(&g_game.renderer, 2048);
#if defined(__ANDROID__)
                renderer_set_render_scale(&g_game.renderer, 1.0f);
#endif
            }

            game_menu_update(&g_game.menu, (float)dt);

            if (game_menu_first_run_active(&g_game.menu)) {
                GfxBenchmarkAssets ba = bench_assets_from_game();
                gfx_benchmark_render(&g_game.renderer, g_game.menu.benchmark_timer, sw, sh, &ba);
                draw_load_overlay_ex("Benchmarking", NULL, false, (float)dt, false);
                input_pre_frame();
                input_post_frame();
                return;
            }

        }

        if (login_screen_update(&g_game.login_screen, (float)dt)) {
            consume_login_play(&g_game.login_screen);
        }

        if (g_game.show_login && ls->logged_in && ls->phase >= 1 &&
            !ls->update_required &&
#ifdef VR
            !g_game.vr.active &&
#endif
            game_menu_needs_first_run(&g_game.menu) && !game_menu_first_run_active(&g_game.menu)) {
            game_menu_begin_first_run(&g_game.menu);
            int sw = g_game.renderer.canvas_width;
            int sh = g_game.renderer.canvas_height;
            {
                g_game.renderer.shadows_enabled = false;
                g_game.renderer.fog_enabled = false;
                g_game.renderer.shadow_range = 50.0f;
                g_game.renderer.shadow_soft = 0;
                renderer_set_shadow_map_size(&g_game.renderer, 2048);
            }
            GfxBenchmarkAssets ba = bench_assets_from_game();
            gfx_benchmark_render(&g_game.renderer, 0.0f, sw, sh, &ba);
            draw_load_overlay_ex("Benchmarking", NULL, false, (float)dt, false);
            input_pre_frame();
            input_post_frame();
            return;
        }

        if (g_game.show_login) {
        login_screen_init(&g_game.login_screen);
        if (!ls->skip_benchmark_dirty)
            ls->skip_benchmark = g_game.menu.skip_startup_benchmark;
#ifndef __EMSCRIPTEN__
        avatar_editor_init(&g_game.avatar_editor);
        catalog_ui_init(&g_game.catalog_ui);
#endif
        login_screen_render(&g_game.login_screen, g_game.renderer.canvas_width, g_game.renderer.canvas_height);
#ifndef __EMSCRIPTEN__
        if (g_game.login_screen.want_catalog_ui) {
            g_game.login_screen.want_catalog_ui = false;
            open_catalog_ui();
        }
        if (g_game.login_screen.want_avatar_editor) {
            g_game.login_screen.want_avatar_editor = false;
            open_avatar_editor_ui();
        }
        poll_avatar_editor_save();

        glViewport(0, 0, g_game.renderer.canvas_width, g_game.renderer.canvas_height);
        avatar_editor_render(&g_game.avatar_editor, &g_game.renderer,
                             g_game.renderer.canvas_width, g_game.renderer.canvas_height, (float)dt);
        catalog_ui_render(&g_game.catalog_ui, &g_game.renderer,
                          g_game.renderer.canvas_width, g_game.renderer.canvas_height, (float)dt);
#endif
        input_pre_frame();
        input_post_frame();
        return;
        }
        }
    }
#endif

    if (g_game.username[0]) {
        chat_set_local_username(&g_game.chat, g_game.username);
        if (g_game.scripts)
            client_script_set_local_name(g_game.scripts, g_game.username);
    }

    bool show_loading = !g_game.world_ready && !g_game.show_login &&
                        (g_game.loading_world || g_game.multiplayer ||
                         g_game.world_init_streaming || g_game.physics_streaming ||
                         g_game.connectors_streaming);
    if (show_loading) {
        g_game.loading_time += (float)dt;
    } else {
        g_game.loading_time = 0.0f;
    }

    if (show_loading) {
        goto polyworld_load_pump;
    }

    chat_update(&g_game.chat, (float)dt);

    const InputState* input = input_get_state();
#if defined(VR) && !defined(__EMSCRIPTEN__)
    Vec3 hub_laser_from = {0}, hub_laser_to = {0};
    bool hub_laser_hit = false;
    float hub_u = 0.5f, hub_v = 0.5f;
#endif

    bool chat_active = g_game.chat.focused || g_game.menu.open || show_loading;
    static bool g_ui_consumed_click = false;
    g_ui_consumed_click = false;
#ifndef __EMSCRIPTEN__
    if (avatar_editor_blocks_input(&g_game.avatar_editor) ||
        catalog_ui_blocks_input(&g_game.catalog_ui)) chat_active = true;
#endif

#ifdef __ANDROID__

    {
        bool want_touch = !g_game.show_login && g_game.world_ready && !chat_active
            && !g_game.show_disconnect && !g_game.show_kick
            && (g_game.menu.force_mobile_controls || platform_prefers_touch_controls());
        touch_controls_set_enabled(want_touch);
    }
#elif !defined(__EMSCRIPTEN__)

    {
        bool want_touch = g_game.menu.force_mobile_controls
            && !g_game.show_login && g_game.world_ready && !chat_active
            && !g_game.show_disconnect && !g_game.show_kick;
        touch_controls_set_enabled(want_touch);
    }
#endif

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    {
        static bool prev_ui_ml = false;
        static bool prev_ui_held = false;
        bool held = input_mouse_left_held();
        if (input->mouse_left && !prev_ui_ml) {
            g_ui_consumed_click = chat_handle_click(input->mouse_x, input->mouse_y);
        }
        if (prev_ui_held && !held) {
            extern bool chat_handle_mouseup(float x, float y);
            chat_handle_mouseup(input->mouse_x, input->mouse_y);
        }
        prev_ui_ml = input->mouse_left;
        prev_ui_held = held;
    }
#endif

#ifdef PW_DEBUG
    if (!chat_active && input->key_shift && input->key_f) {
        g_game.debug_draw = !g_game.debug_draw;
    }

    if (!chat_active && input->key_shift && input->key_g) {
        avatar_toggle_mode(&g_game.avatar, g_game.physics);
    }
#endif

    if (!chat_active && input->key_f1)
        g_game.hide_hud = !g_game.hide_hud;

    {
        static bool f3_chord_used = false;
        if (!chat_active && input->key_f3)
            f3_chord_used = false;
        if (!chat_active && input->key_f3_held && input->key_c) {
            g_game.show_chunk_borders = !g_game.show_chunk_borders;
            f3_chord_used = true;
        }
        if (!chat_active && input->key_f3_held && input->key_t) {
            g_game.trail_recording = !g_game.trail_recording;
            if (g_game.trail_recording) {
                g_game.trail_count = 0;
                g_game.trail_points[g_game.trail_count++] = g_game.avatar.pos;
                g_game.trail_last_sample = g_game.avatar.pos;
            }
            f3_chord_used = true;
        }
        static bool prev_f3_held = false;
        if (!chat_active && prev_f3_held && !input->key_f3_held && !f3_chord_used)
            g_game.show_fps = !g_game.show_fps;
        prev_f3_held = input->key_f3_held;
    }

    vidactor_handle_input(chat_active, input->key_f6, input->key_f7, input->key_f8,
                          input->key_f9, input->key_f10);
#ifdef VIDACTOR
    if (g_game.world_ready)
        vid_stage_update(dt);
#endif

    {
        static bool prev_shift = false;
        bool mobile_toggle = touch_controls_enabled() && touch_controls_consume_shiftlock_toggle();
        if (!chat_active && g_game.camera_mode == CAM_MODE_NORMAL) {
#ifdef VR
            if (g_game.vr.active) {

            } else
#endif
            if (mobile_toggle ||
                (input->key_shift && !prev_shift && !input->key_f && !input->key_g && !input->key_z)) {
                g_game.avatar.shift_lock = !g_game.avatar.shift_lock;
            }
        }
        prev_shift = input->key_shift;
        if (touch_controls_enabled())
            touch_controls_set_shift_lock(g_game.avatar.shift_lock);
    }

    g_game.fps_frames++;
    g_game.fps_timer += (float)dt;
    if (g_game.fps_timer >= 1.0f) {
        g_game.fps_display = g_game.fps_frames;
        g_game.fps_frames = 0;
        g_game.fps_timer -= 1.0f;
    }

    if (!chat_active && input->key_shift && input->key_z) {
        if (g_game.avatar_anim.state >= ANIM_STATE_DANCING && g_game.avatar_anim.state <= ANIM_STATE_DANCING3) {
            g_game.avatar_anim.state = ANIM_STATE_IDLE;
        } else {
            g_game.avatar_anim.state = ANIM_STATE_DANCING;
        }
    }

    {
        bool blocks = chat_active || hud_hidden() || g_game.show_disconnect;
#ifndef __EMSCRIPTEN__
        if (avatar_editor_blocks_input(&g_game.avatar_editor) ||
            catalog_ui_blocks_input(&g_game.catalog_ui)) blocks = true;
#endif
        AnimState picked = ANIM_STATE_IDLE;
        uint32_t picked_emote = 0;
        bool play = emote_wheel_update(&g_game.emote_wheel, input->key_b_held && !blocks, blocks,
                                       input->mouse_x, input->mouse_y,
                                       g_game.renderer.canvas_width, g_game.renderer.canvas_height,
                                       g_game.local_equipped_emotes,
                                       g_game.local_emote_anims, &picked, &picked_emote);
        if (play) {
            bool same = (g_game.avatar_anim.state == picked) &&
                        (picked != ANIM_STATE_EMOTE || g_game.active_emote_id == picked_emote);
            if (same) {
                g_game.avatar_anim.state = ANIM_STATE_IDLE;
                g_game.active_emote_id = 0;
                g_game.avatar_anim.emote_id = 0;
                g_game.avatar_anim.emote_clip = NULL;
            } else if (picked == ANIM_STATE_EMOTE && picked_emote) {
                emote_clip_request(picked_emote);
                if (emote_clip_failed(picked_emote)) {

                } else {
                    g_game.active_emote_id = picked_emote;
                    g_game.avatar_anim.emote_id = picked_emote;
                    g_game.avatar_anim.emote_time = 0.0f;
                    g_game.avatar_anim.emote_clip = emote_clip_get(picked_emote);
                    g_game.avatar_anim.state = ANIM_STATE_EMOTE;
                }
            } else {
                g_game.active_emote_id = 0;
                g_game.avatar_anim.emote_id = 0;
                g_game.avatar_anim.emote_clip = NULL;
                g_game.avatar_anim.state = picked;
            }
        }
    }

    if (!chat_active && !emote_wheel_is_open(&g_game.emote_wheel)) {
        int pressed_slot = 0;
        if (input->key_1) pressed_slot = 1;
        else if (input->key_2) pressed_slot = 2;
        else if (input->key_3) pressed_slot = 3;
        else if (input->key_4) pressed_slot = 4;
        else if (input->key_5) pressed_slot = 5;
        else if (input->key_6) pressed_slot = 6;
        else if (input->key_7) pressed_slot = 7;
        else if (input->key_8) pressed_slot = 8;
        else if (input->key_9) pressed_slot = 9;
        if (pressed_slot > 0 && pressed_slot <= g_game.tool_count && g_game.tools[pressed_slot - 1].available) {
            g_game.equipped_tool = (g_game.equipped_tool == pressed_slot) ? 0 : pressed_slot;
        }
    }

    {
    static bool prev_tool_ml = false;
    bool from_tap = false;
    float tap_aim_x = 0.0f, tap_aim_y = 0.0f;
#if defined(__ANDROID__)
    from_tap = touch_controls_consume_tap_fire();
    if (from_tap)
        touch_controls_tap_fire_pos(&tap_aim_x, &tap_aim_y);
#endif
    bool tool_pressed = (input->mouse_left && !prev_tool_ml) || from_tap;
    prev_tool_ml = input->mouse_left;
    if (!chat_active && tool_pressed && g_game.equipped_tool > 0 && g_game.tool_cooldown <= 0.0f) {
        int ti = g_game.equipped_tool - 1;
        if (ti >= 0 && ti < g_game.tool_count) {
            const char* preset = g_game.tools[ti].name;

            if (strcmp(preset, "PolySoda") == 0) {
                g_game.tool_cooldown = 5.0f;
                g_game.tool_cooldown_max = 5.0f;
                g_game.avatar.health += 25;
                if (g_game.avatar.health > 100) g_game.avatar.health = 100;
            } else if (strcmp(preset, "Cheeseburger") == 0) {
                g_game.tool_cooldown = 15.0f;
                g_game.tool_cooldown_max = 15.0f;
                g_game.avatar.health = 100;
            } else {

                float cd = 2.0f;
                if (strcmp(preset, "Sword") == 0) cd = 0.5f;
                else if (strcmp(preset, "RocketLauncher") == 0) cd = 2.0f;
                else if (strcmp(preset, "TimeBomb") == 0) cd = 1.0f;
                else if (strcmp(preset, "Superball") == 0) cd = 1.0f;
                else if (strcmp(preset, "Slingshot") == 0) cd = 0.6f;
                else if (strcmp(preset, "PaintballGun") == 0) cd = 0.4f;
                else if (strcmp(preset, "Trowel") == 0) cd = 3.0f;
                g_game.tool_cooldown = cd;
                g_game.tool_cooldown_max = cd;

                float yaw_rad = g_game.camera.yaw * (3.14159265f / 180.0f);
                float pitch_rad = g_game.camera.pitch * (3.14159265f / 180.0f);
                float cos_p = cosf(pitch_rad), sin_p = sinf(pitch_rad);
                float cos_y = cosf(yaw_rad), sin_y = sinf(yaw_rad);
                Vec3 cam_offset = {
                    g_game.camera.distance * cos_p * sin_y,
                    g_game.camera.distance * sin_p,
                    g_game.camera.distance * cos_p * cos_y
                };
                Vec3 cam_eye = vec3_add(g_game.camera.target, cam_offset);
                Vec3 cam_up = {0, 1, 0};
                Mat4 view;
                if (g_game.camera.distance < 0.5f) {

                    cam_eye = g_game.camera.target;
                    Vec3 dir = { cos_p * sin_y, sin_p, cos_p * cos_y };
                    Vec3 look_at = { cam_eye.x - dir.x, cam_eye.y - dir.y, cam_eye.z - dir.z };
                    view = mat4_look_at(cam_eye, look_at, cam_up);
                } else {
                    view = mat4_look_at(cam_eye, g_game.camera.target, cam_up);
                }
                int sw = g_game.renderer.canvas_width;
                int sh = g_game.renderer.canvas_height;
                float mx = input->mouse_x;
                float my = input->mouse_y;
                if (from_tap) {

                    mx = tap_aim_x;
                    my = tap_aim_y;
                } else if (g_game.camera.distance < 0.5f || input->mouse_right ||
                           (mx <= 0.0f && my <= 0.0f)) {

                    mx = (float)sw * 0.5f;
                    my = (float)sh * 0.5f;
                }

                float aspect = (float)sw / (float)sh;
                Mat4 proj = mat4_perspective(60.0f, aspect, PW_CAMERA_NEAR, PW_CAMERA_FAR);
                Mat4 inv_proj = mat4_inverse(proj);
                Mat4 inv_view = mat4_inverse(view);
                float ndc_x = (2.0f*mx/(float)sw) - 1.0f;
                float ndc_y = 1.0f - (2.0f*my/(float)sh);
                Vec4 clip_near = {ndc_x, ndc_y, -1.0f, 1.0f};
                Vec4 eye_near = mat4_mul_vec4(inv_proj, clip_near);
                eye_near.x /= eye_near.w; eye_near.y /= eye_near.w; eye_near.z /= eye_near.w; eye_near.w = 1.0f;
                Vec4 world_near = mat4_mul_vec4(inv_view, eye_near);
                Vec4 clip_far = {ndc_x, ndc_y, 1.0f, 1.0f};
                Vec4 eye_far = mat4_mul_vec4(inv_proj, clip_far);
                eye_far.x /= eye_far.w; eye_far.y /= eye_far.w; eye_far.z /= eye_far.w; eye_far.w = 1.0f;
                Vec4 world_far = mat4_mul_vec4(inv_view, eye_far);
                Vec3 ray_dir = { world_far.x-world_near.x, world_far.y-world_near.y, world_far.z-world_near.z };
                float rlen = vec3_length(ray_dir);
                if (rlen > 0.001f) ray_dir = vec3_scale(ray_dir, 1.0f/rlen);

                Vec3 ray_start = vec3_add(cam_eye, vec3_scale(ray_dir, 1.5f));
                physics_disable_geom(g_game.physics, g_game.avatar.body);

                for (int oni = 0; oni < g_game.net_object_count; oni++) {
                    if (!g_game.net_objects[oni].net_owned) continue;
                    Entity* oe = scene_get_entity(&g_game.scene, g_game.net_objects[oni].entity);
                    if (oe && oe->physics_body)
                        physics_disable_geom(g_game.physics, oe->physics_body);
                }
                RaycastHit aim_hit = physics_raycast(g_game.physics, ray_start, ray_dir, 500.0f);
                if (!g_game.avatar.dead)
                    physics_enable_geom(g_game.physics, g_game.avatar.body);
                for (int oni = 0; oni < g_game.net_object_count; oni++) {
                    if (!g_game.net_objects[oni].net_owned) continue;
                    Entity* oe = scene_get_entity(&g_game.scene, g_game.net_objects[oni].entity);
                    if (oe && oe->physics_body)
                        physics_enable_geom(g_game.physics, oe->physics_body);
                }

                Vec3 rocket_origin = g_game.avatar.pos;
                rocket_origin.y += 2.0f;
                Vec3 aim_dir;
                if (aim_hit.hit) aim_dir = vec3_sub(aim_hit.point, rocket_origin);
                else { Vec3 fp = vec3_add(ray_start, vec3_scale(ray_dir, 200.0f)); aim_dir = vec3_sub(fp, rocket_origin); }
                float aim_len = vec3_length(aim_dir);
                if (aim_len > 0.5f) aim_dir = vec3_scale(aim_dir, 1.0f/aim_len);
                else aim_dir = ray_dir;

                if (g_game.multiplayer && g_game.net.state == NET_STATE_CONNECTED) {
                    uint8_t evt_buf[128];
                    const char* evt_name = preset;
                    uint8_t name_len2 = (uint8_t)strlen(evt_name);
                    evt_buf[0] = name_len2;
                    memcpy(evt_buf + 1, evt_name, name_len2);
                    size_t off = 1 + name_len2;
                    memcpy(evt_buf + off, &aim_dir.x, 4); off += 4;
                    memcpy(evt_buf + off, &aim_dir.y, 4); off += 4;
                    memcpy(evt_buf + off, &aim_dir.z, 4); off += 4;
                    float ox = rocket_origin.x, oy = rocket_origin.y, oz = rocket_origin.z;
                    memcpy(evt_buf + off, &ox, 4); off += 4;
                    memcpy(evt_buf + off, &oy, 4); off += 4;
                    memcpy(evt_buf + off, &oz, 4); off += 4;
                    Vec3 hit_pt = aim_hit.hit
                        ? aim_hit.point
                        : vec3_add(ray_start, vec3_scale(ray_dir, 200.0f));
                    memcpy(evt_buf + off, &hit_pt.x, 4); off += 4;
                    memcpy(evt_buf + off, &hit_pt.y, 4); off += 4;
                    memcpy(evt_buf + off, &hit_pt.z, 4); off += 4;
                    net_client_send(&g_game.net, MSG_REMOTE_EVENT, evt_buf, off);
                }

                if (strcmp(preset, "RocketLauncher") == 0) audio_play(SFX_ROCKET);
            }
        }
    } else if (!chat_active && !g_ui_consumed_click && tool_pressed &&
               g_game.equipped_tool == 0 && g_game.world_ready) {
        float cmx = input->mouse_x, cmy = input->mouse_y;
#if defined(__ANDROID__)
        if (from_tap) {
            cmx = tap_aim_x;
            cmy = tap_aim_y;
        }
#endif
        try_part_click(cmx, cmy);
    }
    }

#ifndef __EMSCRIPTEN__
    if (!catalog_ui_blocks_input(&g_game.catalog_ui))
#endif
    update_click_cursor(input, chat_active);

    if (g_game.tool_cooldown > 0.0f) {
        g_game.tool_cooldown -= (float)dt;
        if (g_game.tool_cooldown <= 0.0f) {
            g_game.tool_cooldown = 0.0f;
            g_game.tool_cooldown_max = 0.0f;
        }
    }

    if (g_game.world_ready) {
    netown_flush_sync();
    g_game.accumulator += (float)dt;
    float max_accumulator = PHYSICS_FIXED_DT * MAX_PHYSICS_STEPS_PER_FRAME;
    if (g_game.accumulator > max_accumulator) {
        g_game.accumulator = max_accumulator;
    }
    while (g_game.accumulator >= PHYSICS_FIXED_DT) {
        physics_step(g_game.physics, PHYSICS_FIXED_DT);
        g_game.accumulator -= PHYSICS_FIXED_DT;
    }
    if (g_game.multiplayer && g_game.net_proto >= PW_PROTO_NETOWN) {
        static double s_own_scan_t = 0.0;
        double now = platform_get_time();
        if (now - s_own_scan_t >= 0.05) {
            s_own_scan_t = now;
            if (!client_netown_lite())
                netown_scan_broken_conns();
        }
        netown_query_parts();
    }
    }

    if (g_game.world_ready) {
        interpolate_net_objects((float)dt);
        interpolate_remote_players((float)dt);
    }

    if (g_game.world_ready)
        collision_lod_ensure_underfoot();

    if (g_game.world_ready) {
    if (g_game.scripts)
        client_script_apply_avatar(g_game.scripts, &g_game.avatar, &g_game.camera);
    if (g_game.camera_mode == CAM_MODE_FREEFLY && !chat_active && !g_game.avatar.dead) {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
        float yaw_rad = g_game.camera.yaw * (float)M_PI / 180.0f;
        float pitch_rad = g_game.camera.pitch * (float)M_PI / 180.0f;
        float cos_p = cosf(pitch_rad), sin_p = sinf(pitch_rad);
        float cos_y = cosf(yaw_rad), sin_y = sinf(yaw_rad);

        Vec3 forward = { -cos_p * sin_y, -sin_p, -cos_p * cos_y };

        Vec3 right = { -forward.z, 0.0f, forward.x };
        float rlen = sqrtf(right.x * right.x + right.z * right.z);
        if (rlen > 0.001f) { right.x /= rlen; right.z /= rlen; }

        Vec3 move = {0, 0, 0};

        float ax = input->move_x, ay = input->move_y;
        float amag = sqrtf(ax * ax + ay * ay);
        if (amag > 1.0f) { ax /= amag; ay /= amag; amag = 1.0f; }
        if (amag > 0.05f) {
            move = vec3_add(vec3_scale(forward, -ay), vec3_scale(right, ax));
        } else {
            if (input->key_w) move = vec3_add(move, forward);
            if (input->key_s) move = vec3_sub(move, forward);
            if (input->key_d) move = vec3_add(move, right);
            if (input->key_a) move = vec3_sub(move, right);
        }
        if (input->key_space_held) move.y += 1.0f;
        if (input->key_shift) move.y -= 1.0f;

        float mlen = vec3_length(move);
        if (mlen > 0.001f) {
            move = vec3_scale(move, 1.0f / mlen);
            float speed = 30.0f;
            g_game.avatar.pos = vec3_add(g_game.avatar.pos, vec3_scale(move, speed * (float)dt));
        }
        g_game.avatar.vel = (Vec3){0, 0, 0};
        g_game.avatar.on_ground = false;

        g_game.avatar.current_yaw = g_game.camera.yaw + 180.0f + 270.0f;
        while (g_game.avatar.current_yaw >= 360.0f) g_game.avatar.current_yaw -= 360.0f;
        while (g_game.avatar.current_yaw < 0.0f) g_game.avatar.current_yaw += 360.0f;

        physics_set_position(g_game.physics, g_game.avatar.body, g_game.avatar.pos);
        Entity* ent = scene_get_entity(&g_game.scene, g_game.avatar.entity);
        if (ent) {
            ent->transform.position = (Vec3){
                g_game.avatar.pos.x,
                g_game.avatar.pos.y - AVATAR_ROOT_HALF_Y + g_game.avatar.step_offset,
                g_game.avatar.pos.z
            };
            ent->transform.rotation.y = g_game.avatar.current_yaw;
        }
    } else if (vidactor_is_playing() || vidactor_is_armed()) {

        g_game.avatar.freeze_locomotion = true;
        InputState no_input = {0};
        avatar_update(&g_game.avatar, &no_input, &g_game.camera,
                      g_game.physics, &g_game.scene, (float)dt);
        VidPose pose;
        char tape_chat[CHAT_MAX_INPUT];
        if (vidactor_playback_tick(dt, &pose, tape_chat, sizeof(tape_chat))) {
            g_game.avatar.pos = pose.pos;
            g_game.avatar.vel = (Vec3){0, 0, 0};
            g_game.avatar.current_yaw = pose.yaw;
            while (g_game.avatar.current_yaw >= 360.0f) g_game.avatar.current_yaw -= 360.0f;
            while (g_game.avatar.current_yaw < 0.0f) g_game.avatar.current_yaw += 360.0f;

            if (pose.anim == ANIM_STATE_WALKING || pose.anim == ANIM_STATE_JUMPING ||
                pose.anim == ANIM_STATE_IDLE || pose.anim == ANIM_STATE_CLIMBING ||
                (pose.anim >= ANIM_STATE_DANCING && pose.anim <= ANIM_STATE_DANCING3)) {
                g_game.avatar_anim.state = (AnimState)pose.anim;
            }
            g_game.avatar.on_ground = (pose.anim != ANIM_STATE_JUMPING);
            if (g_game.physics && g_game.avatar.body) {
                physics_set_position(g_game.physics, g_game.avatar.body, g_game.avatar.pos);
                physics_set_velocity(g_game.physics, g_game.avatar.body, (Vec3){0, 0, 0});
            }
            Entity* ent = scene_get_entity(&g_game.scene, g_game.avatar.entity);
            if (ent) {
                ent->transform.position = (Vec3){
                    g_game.avatar.pos.x,
                    g_game.avatar.pos.y - AVATAR_ROOT_HALF_Y + g_game.avatar.step_offset,
                    g_game.avatar.pos.z
                };
                ent->transform.rotation.y = g_game.avatar.current_yaw;
            }
            if (tape_chat[0] && g_game.multiplayer && g_game.net.state == NET_STATE_CONNECTED) {
                net_client_send(&g_game.net, MSG_CHAT, (const uint8_t*)tape_chat, strlen(tape_chat));
            }

            g_game.avatar.vel.x = (pose.anim == ANIM_STATE_WALKING) ? pose.move_speed : 0.0f;
        }
        g_game.avatar.freeze_locomotion = false;
    } else if (g_game.camera_mode == CAM_MODE_FREECAM || chat_active) {

        g_game.avatar.freeze_locomotion = g_game.collision_chunk_loading;
        InputState no_input = {0};
        avatar_update(&g_game.avatar, &no_input, &g_game.camera,
                      g_game.physics, &g_game.scene, (float)dt);
    } else if (g_game.collision_chunk_loading) {

        g_game.avatar.freeze_locomotion = true;
        g_game.avatar.pos = g_game.collision_pin_pos;
        g_game.avatar.vel = (Vec3){0, 0, 0};
        g_game.avatar.on_ground = true;
        InputState no_input = {0};
        avatar_update(&g_game.avatar, &no_input, &g_game.camera,
                      g_game.physics, &g_game.scene, (float)dt);
        if (g_game.move_lock_timer > 0.0f) {
            g_game.move_lock_timer -= (float)dt;
            if (g_game.move_lock_timer < 0.0f) g_game.move_lock_timer = 0.0f;
        }
    } else if (g_game.move_lock_timer > 0.0f) {
        g_game.move_lock_timer -= (float)dt;
        if (g_game.move_lock_timer < 0.0f) g_game.move_lock_timer = 0.0f;

        g_game.avatar.freeze_locomotion = true;
        InputState no_input = {0};
        avatar_update(&g_game.avatar, &no_input, &g_game.camera,
                      g_game.physics, &g_game.scene, (float)dt);
        g_game.avatar.freeze_locomotion = false;
    } else {
        g_game.avatar.freeze_locomotion = false;
        avatar_update(&g_game.avatar, input, &g_game.camera,
                      g_game.physics, &g_game.scene, (float)dt);
    }
        if (client_netown_lite())
            netown_send_walk_hit();
        else
            netown_local_push();
    }

    if (g_game.trail_recording) {
        Vec3 p = g_game.avatar.pos;
        float dx = p.x - g_game.trail_last_sample.x;
        float dy = p.y - g_game.trail_last_sample.y;
        float dz = p.z - g_game.trail_last_sample.z;
        if (dx * dx + dy * dy + dz * dz >= 0.04f) {
            if (g_game.trail_count < (int)(sizeof(g_game.trail_points) / sizeof(g_game.trail_points[0]))) {
                g_game.trail_points[g_game.trail_count++] = p;
            } else {
                g_game.trail_recording = false;
            }
            g_game.trail_last_sample = p;
        }
    }

    sync_physics_to_scene();

    update_collision_chunk_lod();

    if (g_game.scripts) {

        if (!g_game.avatar.dead) {
            Vec3 ppos = g_game.avatar.pos;
            float px_half = 0.5f, py_half = 1.7f, pz_half = 0.5f;
            for (uint32_t i = 0; i < g_game.scene.count; i++) {
                Entity* e = &g_game.scene.entities[i];
                if (!e->active || !e->mesh || e->id == g_game.avatar.entity) continue;
                Vec3 epos = e->transform.position;

                float ex = e->transform.scale.x * 0.5f;
                float ey = e->transform.scale.y * 0.5f;
                float ez = e->transform.scale.z * 0.5f;
                if (ex < 0.01f) ex = 2.0f;
                if (ey < 0.01f) ey = 2.0f;
                if (ez < 0.01f) ez = 2.0f;
                if (e->physics_body) {
                    PhysicsBodyInfo info = physics_get_body_info(g_game.physics, e->physics_body);
                    if (info.active) { ex = info.half_extents.x; ey = info.half_extents.y; ez = info.half_extents.z; }
                }

                if (fabsf(ppos.x - epos.x) < px_half + ex &&
                    fabsf(ppos.y - epos.y) < py_half + ey &&
                    fabsf(ppos.z - epos.z) < pz_half + ez) {
                    client_script_fire_touched(g_game.scripts, e->id);
                }
            }
        }
        client_script_tick(g_game.scripts, (float)dt);
    }

#ifdef VR
    if (g_game.vr.active && g_game.world_ready && !g_game.avatar.dead) {
        bool ui_blocks = chat_active || g_game.menu.open || g_game.show_login ||
                         g_game.show_disconnect || g_game.show_kick;
#ifndef __EMSCRIPTEN__
        if (avatar_editor_blocks_input(&g_game.avatar_editor) ||
            catalog_ui_blocks_input(&g_game.catalog_ui))
            ui_blocks = true;
#endif
        Vec3 feet = {
            g_game.avatar.pos.x,
            g_game.avatar.pos.y - AVATAR_FEET_OFFSET,
            g_game.avatar.pos.z
        };
        if (!ui_blocks && input->key_c && !input->key_f3_held) {
            g_game.vr.inspect = !g_game.vr.inspect;
            if (g_game.vr.inspect && g_game.vr.inspect_dist < 2.0f) {
                g_game.vr.inspect_dist = 14.0f;
                g_game.vr.inspect_pitch = 55.0f;
                g_game.vr.inspect_yaw = g_game.vr.hmd_yaw;
            }
        }
        vr_session_update(&g_game.vr, (float)dt, feet, input, ui_blocks);
        if (g_game.world_ready)
            vr_calibrate_on_world_ready();
        g_game.avatar.shift_lock = true;

        {
            float mesh_yaw = g_game.vr.hmd_yaw + 180.0f + 270.0f;
            while (mesh_yaw >= 360.0f) mesh_yaw -= 360.0f;
            while (mesh_yaw < 0.0f) mesh_yaw += 360.0f;
            g_game.avatar.current_yaw = mesh_yaw;
            Entity* vr_ent = scene_get_entity(&g_game.scene, g_game.avatar.entity);
            if (vr_ent)
                vr_ent->transform.rotation.y = mesh_yaw;
        }
        g_game.camera.target = g_game.vr.hmd_eye;
        if (g_game.vr.inspect) {
            if (!ui_blocks && input->mouse_right) {
                g_game.vr.inspect_yaw -= input->mouse_dx * 0.3f;
                g_game.vr.inspect_pitch += input->mouse_dy * 0.3f;
                if (g_game.vr.inspect_pitch < 8.0f) g_game.vr.inspect_pitch = 8.0f;
                if (g_game.vr.inspect_pitch > 85.0f) g_game.vr.inspect_pitch = 85.0f;
            }
            bool posing = input_key_held(81) || input_key_held(69);
            if (!ui_blocks && !posing && input->scroll_delta != 0.0f) {
                g_game.vr.inspect_dist += input->scroll_delta * 1.2f;
                if (g_game.vr.inspect_dist < 6.0f) g_game.vr.inspect_dist = 6.0f;
                if (g_game.vr.inspect_dist > 28.0f) g_game.vr.inspect_dist = 28.0f;
            }
            if (posing && input->scroll_delta != 0.0f)
                input_clear_scroll();
            g_game.camera.yaw = g_game.vr.inspect_yaw;
            g_game.camera.pitch = g_game.vr.inspect_pitch;
            g_game.camera.distance = g_game.vr.inspect_dist;
            g_game.camera.distance_goal = g_game.vr.inspect_dist;
            g_game.camera.target = (Vec3){
                g_game.avatar.pos.x,
                g_game.avatar.pos.y + 1.5f,
                g_game.avatar.pos.z
            };
        } else {
            g_game.camera.yaw = g_game.vr.hmd_yaw;
            g_game.camera.pitch = g_game.vr.hmd_pitch;
            g_game.camera.distance = 0.0f;
            g_game.camera.distance_goal = 0.0f;
        }
        if (!ui_blocks)
            platform_set_cursor_captured(true);
        else
            platform_set_cursor_captured(false);
        if (!chat_active && (input->key_f10 || vr_openxr_consume_recalibrate())) {
            if (g_game.vr.recal_ui) {
                vr_session_recalibrate(&g_game.vr, feet, g_game.avatar.current_yaw);
                g_game.vr.recal_ui = false;
            } else {
                g_game.vr.recal_ui = true;
            }
        }
        if (g_game.vr.recal_ui && input->mouse_left) {
            vr_session_recalibrate(&g_game.vr, feet, g_game.avatar.current_yaw);
            g_game.vr.recal_ui = false;
        }
        if (g_game.vr.recal_ui && vr_openxr_consume_pause()) {
            g_game.vr.recal_ui = false;
        } else if (vr_openxr_consume_pause())
            chat_handle_key(27, false, false);
        vr_openxr_set_comfort(g_game.menu.vr_turn, g_game.menu.open);
    }
#if !defined(__EMSCRIPTEN__)
    if (vr_hub_active() && g_game.world_ready && g_game.vr.active) {
        hub_laser_hit = vr_hub_laser(&g_game.vr.local, &g_game.scene,
                                     &hub_u, &hub_v, &hub_laser_from, &hub_laser_to);
        static bool hub_click_down;
        bool overlay = g_game.menu.open;
        if (avatar_editor_blocks_input(&g_game.avatar_editor) ||
            catalog_ui_blocks_input(&g_game.catalog_ui))
            overlay = true;
        if (!overlay && hub_laser_hit && !g_game.vr.recal_ui) {
            int mx = (int)(hub_u * (float)VR_HUB_FB_W);
            int my = (int)((1.0f - hub_v) * (float)VR_HUB_FB_H);
            input_set_mouse_pos((float)mx, (float)my);
            bool down = input_mouse_left_held();
            if (down && !hub_click_down)
                login_screen_on_mousedown(&g_game.login_screen, mx, my);
            if (!down && hub_click_down)
                login_screen_on_mouseup(&g_game.login_screen);
            hub_click_down = down;
        } else if (!overlay) {
            input_set_mouse_pos(-100.0f, -100.0f);
            if (hub_click_down && !input_mouse_left_held())
                login_screen_on_mouseup(&g_game.login_screen);
            hub_click_down = false;
        }
        if (!overlay) {
            if (login_screen_update(&g_game.login_screen, (float)dt))
                g_vr_hub_play_pending = true;
            if (g_game.login_screen.want_catalog_ui) {
                g_game.login_screen.want_catalog_ui = false;
                open_catalog_ui();
            }
            if (g_game.login_screen.want_avatar_editor) {
                g_game.login_screen.want_avatar_editor = false;
                open_avatar_editor_ui();
            }
        }
    }
#endif
#endif

    {
        float speed = sqrtf(g_game.avatar.vel.x * g_game.avatar.vel.x +
                           g_game.avatar.vel.z * g_game.avatar.vel.z);
#ifdef VR
        if (g_game.vr.active) {
            float ix = g_game.avatar.move_intent_x;
            float iz = g_game.avatar.move_intent_z;
            float is = sqrtf(ix * ix + iz * iz);
            if (is > speed) speed = is;
            float stick = sqrtf(input->move_x * input->move_x +
                                input->move_y * input->move_y);
            if (stick > 0.18f) {
                float ss = stick * g_game.avatar.walk_speed;
                if (ss > speed) speed = ss;
            }
        }
#endif
        AnimState astate = ANIM_STATE_IDLE;
        static float airborne_timer = 0.0f;

        if (vidactor_is_playing() || vidactor_is_armed()) {

            astate = g_game.avatar_anim.state;
            if (astate == ANIM_STATE_WALKING && speed < 0.5f)
                speed = 16.0f;
            float anim_speed = speed;
            if (astate == ANIM_STATE_CLIMBING)
                anim_speed = g_game.avatar.vel.y;
            g_game.avatar_anim.tool_hold = local_held_tool_name() != NULL;
            g_game.avatar_anim.vr_ik = false;
            avatar_anim_update(&g_game.avatar_anim, astate, anim_speed, (float)dt);
        } else {

        if (!g_game.avatar.on_ground) {
            airborne_timer += (float)dt;
        } else {
            airborne_timer = 0.0f;
        }
        bool anim_airborne = (airborne_timer > 0.15f);

        if (emote_anim_is_hold(g_game.avatar_anim.state)) {
            astate = g_game.avatar_anim.state;

            if (speed > 0.5f || anim_airborne || g_game.avatar.climbing || input->key_space_held) {
                if (g_game.avatar.climbing)
                    astate = ANIM_STATE_CLIMBING;
                else if (anim_airborne || input->key_space_held)
                    astate = ANIM_STATE_JUMPING;
                else
                    astate = (speed > 0.5f ? ANIM_STATE_WALKING : ANIM_STATE_IDLE);
                if (astate != ANIM_STATE_EMOTE) {
                    g_game.active_emote_id = 0;
                    g_game.avatar_anim.emote_id = 0;
                    g_game.avatar_anim.emote_clip = NULL;
                }
            } else if (astate == ANIM_STATE_EMOTE) {

                if (!g_game.avatar_anim.emote_clip && g_game.active_emote_id) {
                    g_game.avatar_anim.emote_id = g_game.active_emote_id;
                    g_game.avatar_anim.emote_clip = emote_clip_get(g_game.active_emote_id);
                }
                if (emote_clip_failed(g_game.active_emote_id)) {
                    astate = ANIM_STATE_IDLE;
                    g_game.active_emote_id = 0;
                    g_game.avatar_anim.emote_id = 0;
                    g_game.avatar_anim.emote_clip = NULL;
                }
            }
        } else if (g_game.avatar.climbing) {
            astate = ANIM_STATE_CLIMBING;
        } else if (anim_airborne || input->key_space_held) {

            astate = ANIM_STATE_JUMPING;
        } else if (speed > 0.5f) {
            astate = ANIM_STATE_WALKING;
        }
        float anim_speed = speed;
        if (astate == ANIM_STATE_CLIMBING)
            anim_speed = g_game.avatar.vel.y;
        g_game.avatar_anim.tool_hold = local_held_tool_name() != NULL;
#ifdef VR
        g_game.avatar_anim.vr_ik = g_game.vr.active;
#endif
        avatar_anim_update(&g_game.avatar_anim, astate, anim_speed, (float)dt);
#ifdef VR
        if (g_game.vr.active) {
            Entity* av_ent = scene_get_entity(&g_game.scene, g_game.avatar.entity);
            Vec3 feet = av_ent ? av_ent->transform.position : (Vec3){
                g_game.avatar.pos.x,
                g_game.avatar.pos.y - AVATAR_FEET_OFFSET,
                g_game.avatar.pos.z
            };
            float yaw = av_ent ? av_ent->transform.rotation.y : g_game.avatar.current_yaw;
            vr_ik_apply(&g_game.avatar_anim, &g_game.vr.local, &g_game.vr.calib,
                        feet, yaw);
        }
#endif
        }

        if (!(vidactor_is_playing() || vidactor_is_armed() || vidactor_is_staging()))
        {
            static bool was_on_ground = true;
            static float airtime = 0.0f;
            static bool jumped = false;

            if (!g_game.avatar.on_ground) {
                airtime += (float)dt;
            }

            if (was_on_ground && !g_game.avatar.on_ground && g_game.avatar.vel.y > 10.0f
                && !g_game.avatar.climbing) {
                audio_play(SFX_JUMP);
                jumped = true;
            }

            if (!was_on_ground && g_game.avatar.on_ground && airtime > 0.3f) {
                audio_play(SFX_LAND);
            }

            if (g_game.avatar.on_ground) {
                airtime = 0.0f;
                jumped = false;
            }
            was_on_ground = g_game.avatar.on_ground;

            float hx = g_game.avatar.vel.x;
            float hz = g_game.avatar.vel.z;
            float hspeed = sqrtf(hx * hx + hz * hz);
            bool walking = !g_game.avatar.dead
                && g_game.avatar.on_ground
                && !g_game.avatar.climbing
                && hspeed > 1.5f
                && g_game.camera_mode != CAM_MODE_FREECAM
                && g_game.camera_mode != CAM_MODE_FREEFLY
                && !chat_active;
            if (walking) {
                audio_start_loop(SFX_FOOTSTEP, 0.55f);
            } else {
                audio_stop_loop(SFX_FOOTSTEP);
            }
        }
    }

    if (g_game.avatar.dead && !g_game.ragdoll.active) {
        audio_stop_loop(SFX_FOOTSTEP);
        audio_play(SFX_DEAD);

        Vec3 feet = g_game.avatar.pos;
        float yaw = g_game.avatar.current_yaw;
        Entity* av_ent = scene_get_entity(&g_game.scene, g_game.avatar.entity);
        if (av_ent) {
            feet = av_ent->transform.position;
            yaw = av_ent->transform.rotation.y;
        } else {
            feet.y -= AVATAR_FEET_OFFSET;
        }
        ragdoll_spawn((RagdollState*)&g_game.ragdoll, &g_game.avatar_anim,
                      g_game.local_accessory, feet, yaw, g_game.avatar.death_vel);
        physics_disable_geom(g_game.physics, g_game.avatar.body);
    }
    ragdoll_update((RagdollState*)&g_game.ragdoll, (float)dt, g_game.avatar.dead);

    for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
        if (!g_game.remote_players[rp].active) continue;
        if (g_game.remote_players[rp].dead && !g_game.remote_players[rp].ragdoll.active) {
            Entity* rent = scene_get_entity(&g_game.scene, g_game.remote_players[rp].entity);
            if (rent) {
                ragdoll_spawn((RagdollState*)&g_game.remote_players[rp].ragdoll,
                              &g_game.remote_players[rp].anim,
                              g_game.remote_players[rp].accessory,
                              rent->transform.position, rent->transform.rotation.y,
                              g_game.remote_players[rp].last_vel);
            }
        }
        ragdoll_update((RagdollState*)&g_game.remote_players[rp].ragdoll, (float)dt,
                       g_game.remote_players[rp].dead);
    }

    Vec3 avatar_pos = g_game.avatar.pos;
    if (g_game.avatar.dead && g_game.ragdoll.active && g_game.ragdoll.bodies[ANIM_PART_HEAD]) {

        avatar_pos = physics_get_position(g_game.physics, g_game.ragdoll.bodies[ANIM_PART_HEAD]);
    } else {
        avatar_pos.y += (AVATAR_CAMERA_ORBIT_Y - AVATAR_ROOT_HALF_Y) + g_game.avatar.step_offset;
    }

    if (g_game.camera_mode == CAM_MODE_FREECAM) {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
        if (!chat_active && input->mouse_right) {
            g_game.freecam_yaw -= input->mouse_dx * g_game.camera.orbit_speed;
            g_game.freecam_pitch += input->mouse_dy * g_game.camera.orbit_speed;
            if (g_game.freecam_pitch < -89.0f) g_game.freecam_pitch = -89.0f;
            if (g_game.freecam_pitch > 89.0f) g_game.freecam_pitch = 89.0f;
        }
        if (!chat_active) {
            const float rot = 120.0f * (float)dt;
            if (input_key_held(37)) g_game.freecam_yaw += rot;
            if (input_key_held(39)) g_game.freecam_yaw -= rot;
            if (input_key_held(38)) g_game.freecam_pitch += rot;
            if (input_key_held(40)) g_game.freecam_pitch -= rot;
            if (g_game.freecam_pitch < -89.0f) g_game.freecam_pitch = -89.0f;
            if (g_game.freecam_pitch > 89.0f) g_game.freecam_pitch = 89.0f;
            float yaw_rad = g_game.freecam_yaw * (float)M_PI / 180.0f;
            float pitch_rad = g_game.freecam_pitch * (float)M_PI / 180.0f;
            float cos_p = cosf(pitch_rad), sin_p = sinf(pitch_rad);
            float cos_y = cosf(yaw_rad), sin_y = sinf(yaw_rad);
            Vec3 forward = { -cos_p * sin_y, -sin_p, -cos_p * cos_y };

            Vec3 right = { -forward.z, 0.0f, forward.x };
            float rlen = sqrtf(right.x * right.x + right.z * right.z);
            if (rlen > 0.001f) { right.x /= rlen; right.z /= rlen; }

            Vec3 move = {0, 0, 0};
            float ax = input->move_x, ay = input->move_y;
            float amag = sqrtf(ax * ax + ay * ay);
            if (amag > 1.0f) { ax /= amag; ay /= amag; amag = 1.0f; }
            if (amag > 0.05f) {
                move = vec3_add(vec3_scale(forward, -ay), vec3_scale(right, ax));
            } else {
                if (input->key_w) move = vec3_add(move, forward);
                if (input->key_s) move = vec3_sub(move, forward);
                if (input->key_d) move = vec3_add(move, right);
                if (input->key_a) move = vec3_sub(move, right);
            }
            if (input->key_space_held) move.y += 1.0f;
            if (input->key_shift) move.y -= 1.0f;

            float mlen = vec3_length(move);
            if (mlen > 0.001f) {
                move = vec3_scale(move, 1.0f / mlen);
                float speed = 30.0f;
                g_game.freecam_pos = vec3_add(g_game.freecam_pos, vec3_scale(move, speed * (float)dt));
            }
        }
        if (g_game.menu.open && input->scroll_delta != 0.0f) {
            game_menu_on_scroll(&g_game.menu, input->scroll_delta,
                               g_game.renderer.canvas_height);
            input_clear_scroll();
        } else if (input->scroll_delta != 0.0f &&
                   chat_on_scroll(&g_game.chat, input->mouse_x, input->mouse_y,
                                  input->scroll_delta,
                                  g_game.renderer.canvas_width,
                                  g_game.renderer.canvas_height)) {
            input_clear_scroll();
        }

        g_game.camera.target = avatar_pos;
    } else {
        if (g_game.menu.open && input->scroll_delta != 0.0f) {
            game_menu_on_scroll(&g_game.menu, input->scroll_delta,
                               g_game.renderer.canvas_height);
            input_clear_scroll();
        } else if (input->scroll_delta != 0.0f &&
                   chat_on_scroll(&g_game.chat, input->mouse_x, input->mouse_y,
                                  input->scroll_delta,
                                  g_game.renderer.canvas_width,
                                  g_game.renderer.canvas_height)) {
            input_clear_scroll();
        }
        if (g_game.scripts && client_script_camera_override(g_game.scripts, NULL, NULL, NULL,
                                                           NULL, NULL, NULL, NULL)) {
            g_game.camera.target = avatar_pos;
#ifdef VR
        } else if (g_game.vr.active) {

#endif
        } else {
            camera_update(&g_game.camera, avatar_pos, input, g_game.avatar.shift_lock, (float)dt, !chat_active);
        }
    }

    if (g_game.camera_mode == CAM_MODE_FREECAM) {
        audio_set_listener(g_game.freecam_pos.x, g_game.freecam_pos.y, g_game.freecam_pos.z);
    } else {
        audio_set_listener(g_game.avatar.pos.x, g_game.avatar.pos.y, g_game.avatar.pos.z);
    }

    float aspect = (float)g_game.renderer.canvas_width / (float)g_game.renderer.canvas_height;

    int pw_vr_eyes = 1;
#if defined(VR) && defined(PW_QUEST)
    if (g_game.vr.active && g_game.vr.openxr)
        pw_vr_eyes = vr_openxr_draw_eyes();
    if (pw_vr_eyes < 1) pw_vr_eyes = 1;
#endif
    for (int pw_vr_eye = 0; pw_vr_eye < pw_vr_eyes; pw_vr_eye++) {
#if defined(VR) && defined(PW_QUEST)
    if (g_game.vr.active)
        vr_openxr_select_eye(pw_vr_eye);
#endif

    #ifndef M_PI
    #define M_PI 3.14159265358979323846
    #endif

    Mat4 view;
    bool first_person = false;
    Vec3 cam_eye_live = g_game.camera.target;
    float cam_yaw_live = g_game.camera.yaw;
    float cam_pitch_live = g_game.camera.pitch;
    float cam_roll_live = 0.0f;
    float ox, oy, oz, oyaw, opitch, oroll, ofov;
    if (g_game.camera_mode != CAM_MODE_FREECAM && g_game.scripts &&
        client_script_camera_override(g_game.scripts, &ox, &oy, &oz, &oyaw, &opitch, &oroll, &ofov)) {
        cam_eye_live = (Vec3){ ox, oy, oz };
        cam_yaw_live = oyaw;
        cam_pitch_live = opitch;
        cam_roll_live = oroll;
        view = camera_look_from_pose(cam_eye_live, oyaw, opitch, oroll);
        (void)ofov;
        Vec3 dlt = vec3_sub(cam_eye_live, g_game.avatar.pos);
        if (vec3_length(dlt) < 0.5f && !g_game.avatar.dead)
            first_person = true;
#ifdef VR
    } else if (g_game.vr.active && g_game.world_ready && !g_game.avatar.dead &&
               g_game.camera_mode != CAM_MODE_FREECAM && !g_game.vr.inspect) {
        if (g_game.vr.local.flags & PW_VR_FLAG_HEAD) {
            Vec3 epos = g_game.vr.hmd_eye;
            float qx = g_game.vr.local.head.qx, qy = g_game.vr.local.head.qy;
            float qz = g_game.vr.local.head.qz, qw = g_game.vr.local.head.qw;
            vr_openxr_eye_camera(&epos, &qx, &qy, &qz, &qw);

            Vec3 head = vr_local_mesh_head_world();
            Vec3 cyclops = g_game.vr.hmd_eye;
            epos.x = head.x + (epos.x - cyclops.x);
            epos.y = head.y + (epos.y - cyclops.y);
            epos.z = head.z + (epos.z - cyclops.z);
            view = camera_look_from_quat(epos, qx, qy, qz, qw);
            cam_eye_live = epos;
        } else {
            Vec3 epos = vr_local_mesh_head_world();
            view = camera_look_from_pose(epos, g_game.vr.hmd_yaw,
                                         g_game.vr.hmd_pitch, 0.0f);
            cam_eye_live = epos;
        }
        cam_yaw_live = g_game.vr.hmd_yaw;
        cam_pitch_live = g_game.vr.hmd_pitch;
        first_person = true;
#endif
    } else if (g_game.camera_mode == CAM_MODE_FREECAM) {
        float yaw_rad = g_game.freecam_yaw * (float)M_PI / 180.0f;
        float pitch_rad = g_game.freecam_pitch * (float)M_PI / 180.0f;
        float cos_p = cosf(pitch_rad), sin_p = sinf(pitch_rad);
        float cos_y = cosf(yaw_rad), sin_y = sinf(yaw_rad);
        Vec3 forward = { -cos_p * sin_y, -sin_p, -cos_p * cos_y };
        Vec3 look_at = {
            g_game.freecam_pos.x + forward.x,
            g_game.freecam_pos.y + forward.y,
            g_game.freecam_pos.z + forward.z
        };
        view = mat4_look_at(g_game.freecam_pos, look_at, (Vec3){0, 1, 0});
        cam_eye_live = g_game.freecam_pos;
        cam_yaw_live = g_game.freecam_yaw;
        cam_pitch_live = g_game.freecam_pitch;
    } else {
        float yaw_rad = g_game.camera.yaw * (float)M_PI / 180.0f;
        float pitch_rad = g_game.camera.pitch * (float)M_PI / 180.0f;
        float cos_p = cosf(pitch_rad), sin_p = sinf(pitch_rad);
        float cos_y = cosf(yaw_rad), sin_y = sinf(yaw_rad);

        Vec3 dir = { cos_p * sin_y, sin_p, cos_p * cos_y };
        float dist = g_game.camera.distance;

        if (dist < 0.5f && !g_game.avatar.dead) {
            first_person = true;
            Vec3 eye = g_game.camera.target;
            Vec3 look_at = { eye.x - dir.x, eye.y - dir.y, eye.z - dir.z };
            view = mat4_look_at(eye, look_at, (Vec3){0, 1, 0});
            cam_eye_live = eye;
        } else {

            physics_disable_geom(g_game.physics, g_game.avatar.body);

            if (g_game.ragdoll.active) {
                for (int ri = 0; ri < AVATAR_PART_COUNT; ri++) {
                    if (g_game.ragdoll.bodies[ri])
                        physics_disable_geom(g_game.physics, g_game.ragdoll.bodies[ri]);
                }
                for (int ai = 0; ai < PW_MAX_EQUIPPED_ACCESSORIES; ai++) {
                    for (int ri = 0; ri < ACCESSORY_MAX_PARTS; ri++) {
                        if (g_game.ragdoll.acc_bodies[ai][ri])
                            physics_disable_geom(g_game.physics, g_game.ragdoll.acc_bodies[ai][ri]);
                    }
                }
            }
            RaycastHit hit = {0};

            hit = physics_raycast(g_game.physics, g_game.camera.target, dir, dist + 0.5f);
            if (g_game.ragdoll.active) {
                for (int ri = 0; ri < AVATAR_PART_COUNT; ri++) {
                    if (g_game.ragdoll.bodies[ri])
                        physics_enable_geom(g_game.physics, g_game.ragdoll.bodies[ri]);
                }
                for (int ai = 0; ai < PW_MAX_EQUIPPED_ACCESSORIES; ai++) {
                    for (int ri = 0; ri < ACCESSORY_MAX_PARTS; ri++) {
                        if (g_game.ragdoll.acc_bodies[ai][ri])
                            physics_enable_geom(g_game.physics, g_game.ragdoll.acc_bodies[ai][ri]);
                    }
                }
            }

            if (!g_game.avatar.dead)
                physics_enable_geom(g_game.physics, g_game.avatar.body);

            if (hit.hit && hit.distance < dist) {

                const float cam_clearance = 0.55f;
                dist = hit.distance - cam_clearance;
                if (dist < 0.25f) dist = 0.25f;
            }

            Vec3 eye = {
                g_game.camera.target.x + dir.x * dist,
                g_game.camera.target.y + dir.y * dist,
                g_game.camera.target.z + dir.z * dist
            };
            view = mat4_look_at(eye, g_game.camera.target, (Vec3){0, 1, 0});
            cam_eye_live = eye;
        }
    }

    float cam_fov = g_game.scripts ? client_script_camera_fov(g_game.scripts) : 60.0f;
    Mat4 projection;
#if defined(VR) && !defined(__EMSCRIPTEN__)
    if (g_game.vr.active &&
        vr_openxr_fill_projection(&projection, PW_CAMERA_NEAR, PW_CAMERA_FAR)) {

    } else
#endif
    {
        projection = camera_get_projection_matrix(&g_game.camera, aspect, cam_fov, PW_CAMERA_NEAR, PW_CAMERA_FAR);
    }
    g_game.last_view = view;
    g_game.last_projection = projection;
    g_game.last_view_valid = true;
    if (g_game.scripts)
        client_script_set_camera_live(g_game.scripts, cam_eye_live.x, cam_eye_live.y, cam_eye_live.z,
                                      cam_yaw_live, cam_pitch_live, cam_roll_live, cam_fov);

    {
        float fr, fg, fb;
        if (skybox_get_fog_color(&g_game.skybox, &fr, &fg, &fb)) {
            g_game.renderer.clear_r = fr;
            g_game.renderer.clear_g = fg;
            g_game.renderer.clear_b = fb;
        }
    }

    renderer_begin_frame(&g_game.renderer);

    {
        GameMenu* menu = &g_game.menu;
        GfxQuality q = game_menu_get_effective_quality(menu);
        bool lite_now = client_netown_lite();
        if (s_netown_lite_ack >= 0 && (lite_now ? 1 : 0) != s_netown_lite_ack) {
            if (lite_now)
                netown_drop_all_local();
            if (g_game.net.state == NET_STATE_CONNECTED && g_game.net_proto >= PW_PROTO_NETOWN)
                client_send_protocol_ack();
            else
                s_netown_lite_ack = lite_now ? 1 : 0;
        }
        if (q == GFX_QUALITY_MANUAL) {
            g_game.renderer.shadows_enabled = true;
            g_game.renderer.fog_enabled = menu->manual_fog;
            int leak = (int)menu->manual_glow_leak;
            if (leak > (int)GFX_GLOW_LEAK_DISTANCE) leak = (int)GFX_GLOW_LEAK_DISTANCE;
            g_game.renderer.glow_leak_mode = leak;
            g_game.renderer.shadow_range = 50.0f;
            g_game.renderer.shadow_near_range = 24.0f;
            g_game.renderer.shadow_soft = 1;
            renderer_set_shadow_map_size(&g_game.renderer, 2048);
            if (g_game.renderer.glow_shadow_fbo)
                g_game.renderer.glow_shadow_face = RENDERER_GLOW_SHADOW_FACE;
            g_game.renderer.glow_light_max = RENDERER_MAX_GLOW_LIGHTS;
            g_game.renderer.ssao_enabled = 1;
        } else {
            g_game.renderer.shadows_enabled = (q >= GFX_QUALITY_HIGH);
            g_game.renderer.fog_enabled = (q >= GFX_QUALITY_LOW);
            g_game.renderer.glow_leak_mode = (int)GFX_GLOW_LEAK_DISTANCE;
            if (q == GFX_QUALITY_SUPER) {
                g_game.renderer.shadow_range = 160.0f;
                g_game.renderer.shadow_near_range = 52.0f;
                g_game.renderer.shadow_soft = 1;
                renderer_set_shadow_map_size(&g_game.renderer, 4096);
            } else if (q == GFX_QUALITY_ULTRA) {
                g_game.renderer.shadow_range = 100.0f;
                g_game.renderer.shadow_near_range = 36.0f;
                g_game.renderer.shadow_soft = 1;
                renderer_set_shadow_map_size(&g_game.renderer, 4096);
            } else if (q >= GFX_QUALITY_HIGH) {
                g_game.renderer.shadow_range = 64.0f;
                g_game.renderer.shadow_near_range = 28.0f;
                g_game.renderer.shadow_soft = 1;
                renderer_set_shadow_map_size(&g_game.renderer, 2048);
            } else if (q >= GFX_QUALITY_MEDIUM) {
                g_game.renderer.shadow_range = 50.0f;
                g_game.renderer.shadow_soft = 0;
            } else {
                g_game.renderer.shadow_range = 40.0f;
                g_game.renderer.shadow_soft = 0;
            }
#if PW_MOBILE

            g_game.renderer.fog_enabled = false;
#endif
            if (q <= GFX_QUALITY_POTATO) {
                g_game.renderer.glow_leak_mode = (int)GFX_GLOW_LEAK_DISTANCE;
                g_game.renderer.glow_shadow_face = 0;
                g_game.renderer.glow_light_max = PW_MOBILE ? 4 : 8;
                g_game.renderer.ssao_enabled = 0;
            } else {
                if (g_game.renderer.glow_shadow_fbo)
                    g_game.renderer.glow_shadow_face = RENDERER_GLOW_SHADOW_FACE;
                g_game.renderer.glow_light_max = (q <= GFX_QUALITY_LOW) ? (PW_MOBILE ? 8 : 12)
                                                                        : RENDERER_MAX_GLOW_LIGHTS;
                g_game.renderer.ssao_enabled = (q >= GFX_QUALITY_MEDIUM);
            }
#if !PW_USE_GLES
            if (q <= GFX_QUALITY_POTATO) glDisable(GL_MULTISAMPLE);
            else glEnable(GL_MULTISAMPLE);
#endif
        }
        if (!g_game.renderer.shadow_fbo)
            g_game.renderer.shadows_enabled = false;
        g_game.renderer.voxel_enabled = (menu->lighting_tech == GFX_LIGHTING_VOXEL);
        if (g_game.renderer.voxel_enabled)
            g_game.renderer.shadows_enabled = false;
        {
            float vox_r;
            if (q == GFX_QUALITY_SUPER) vox_r = 128.0f;
            else if (q == GFX_QUALITY_ULTRA) vox_r = 96.0f;
            else if (q >= GFX_QUALITY_HIGH || q == GFX_QUALITY_MANUAL) vox_r = 72.0f;
            else if (q >= GFX_QUALITY_MEDIUM) vox_r = 56.0f;
            else if (q >= GFX_QUALITY_LOW) vox_r = 40.0f;
            else vox_r = PW_MOBILE ? 20.0f : 24.0f;
            renderer_set_voxel_range(&g_game.renderer, vox_r);
        }
        {
            int tq = 2;
            if (q == GFX_QUALITY_MANUAL) tq = 4;
            else if (q <= GFX_QUALITY_POTATO) tq = 0;
            else if (q <= GFX_QUALITY_LOW) tq = 1;
            else if (q <= GFX_QUALITY_MEDIUM) tq = 2;
            else if (q <= GFX_QUALITY_HIGH) tq = 3;
            else if (q == GFX_QUALITY_ULTRA) tq = 4;
            else tq = 5;
            g_game.renderer.curve_tess_quality = tq;
        }
#if PW_MOBILE
        g_game.renderer.fog_enabled = false;
        {
            float scale = 1.0f;
            if (q == GFX_QUALITY_MANUAL) {
                scale = menu->manual_render_scale;
                if (scale < 0.28f) scale = 0.28f;
                if (scale > 1.0f) scale = 1.0f;
            } else if (q <= GFX_QUALITY_POTATO) {
                scale = 0.38f;
            } else if (q <= GFX_QUALITY_LOW) {
                scale = 0.55f;
            } else if (q <= GFX_QUALITY_MEDIUM) {
                scale = 0.72f;
            }
            renderer_set_render_scale(&g_game.renderer, scale);
        }
#else

        {
            float scale = 1.0f;
            if (q == GFX_QUALITY_MANUAL) {
                scale = menu->manual_render_scale;
                if (scale < 0.28f) scale = 0.28f;
                if (scale > 1.0f) scale = 1.0f;
            } else {
                int cw = g_game.renderer.canvas_width;
                int ch = g_game.renderer.canvas_height;
                if (cw > 0 && ch > 0 && q != GFX_QUALITY_SUPER) {
                    float budget;
                    if (q <= GFX_QUALITY_POTATO) budget = 960.0f * 540.0f;
                    else if (q <= GFX_QUALITY_MEDIUM) budget = 1280.0f * 720.0f;
                    else if (q <= GFX_QUALITY_HIGH) budget = 1280.0f * 720.0f;
                    else if (q == GFX_QUALITY_ULTRA) budget = 1600.0f * 900.0f;
                    else budget = 1280.0f * 720.0f;
                    float px = (float)cw * (float)ch;
                    if (px > budget)
                        scale = sqrtf(budget / px);
                    if (scale < 0.28f) scale = 0.28f;
                }
            }
            renderer_set_render_scale(&g_game.renderer, scale);
        }
#endif

        g_game.ui_scale = menu->ui_scale;
        g_game.chat.ui_scale = menu->ui_scale;
        g_game.social.ui_scale = menu->ui_scale;
        g_game.menu.ui_scale = menu->ui_scale;
    }

    if (g_game.menu.benchmark_running) {
        GfxBenchmarkAssets ba = bench_assets_from_game();
        gfx_benchmark_render(&g_game.renderer, g_game.menu.benchmark_timer,
                             g_game.renderer.canvas_width, g_game.renderer.canvas_height, &ba);
    } else {
#if defined(VR) && !defined(__EMSCRIPTEN__)
    if (pw_vr_eye == 0 && vr_hub_active() && g_game.world_ready) {
        vr_hub_render_ui(&g_game.login_screen);
        glViewport(0, 0, g_game.renderer.canvas_width, g_game.renderer.canvas_height);
    }
#endif
    skybox_render(&g_game.skybox, &view, &projection);

    if (g_game.world_ready) {
        if (pw_vr_eye == 0) {
        brick_batch_update(&g_game.scene);
        fill_player_shadow_skip();
        if (g_game.renderer.voxel_enabled)
            renderer_voxel_update(&g_game.renderer, &g_game.scene, g_game.avatar.pos);
        if (g_game.renderer.shadows_enabled) {
            g_game.renderer.extra_caster_count = 0;
            if (g_game.avatar_anim.parts[0].valid)
                cast_player_shadows_meshes();
            renderer_shadow_pass(&g_game.renderer, &g_game.scene, g_game.avatar.pos);
        }

    explosion_update((float)dt);
    explosion_push_lights();
        }

    renderer_begin_world_pass(&g_game.renderer);

    WorldMidDrawCtx mid_ctx = { first_person, &view, &projection, dt };
    renderer_render_scene_ex(&g_game.renderer, &g_game.scene, &view, &projection,
                             draw_world_avatars_and_rockets, &mid_ctx);

    explosion_draw(&view, &projection);

    renderer_apply_fog(&g_game.renderer, PW_CAMERA_NEAR, PW_CAMERA_FAR);
#if defined(VR) && !defined(__EMSCRIPTEN__)
    if (vr_hub_active()) {
        vr_hub_draw(&g_game.renderer, &g_game.scene, &view, &projection,
                    hub_laser_from, hub_laser_to, hub_laser_hit, hub_u, hub_v);
    }
    draw_vr_recalibrate_ui(&view, &projection);
    draw_vr_ik_debug(&view, &projection);
    if (g_game.vr.active && !g_game.menu.open) {
        static float vig;
        int lvl = g_game.menu.vr_vignette;
        float cap = 0.0f;
        if (lvl == 1) cap = 0.42f;
        else if (lvl == 2) cap = 0.62f;
        else if (lvl >= 3) cap = 0.82f;
        float target = 0.0f;
        if (cap > 0.0f) {
            float mx = input->move_x, my = input->move_y;
            float move = sqrtf(mx * mx + my * my);
            if (move > 1.0f) move = 1.0f;
            float spd = sqrtf(g_game.avatar.vel.x * g_game.avatar.vel.x +
                              g_game.avatar.vel.z * g_game.avatar.vel.z);
            float spdn = spd / 18.0f;
            if (spdn > 1.0f) spdn = 1.0f;
            target = move;
            if (spdn > target) target = spdn;
            float turn = vr_openxr_turn_amount();
            if (turn > target) target = turn;
            target *= cap;
        }
        if (pw_vr_eye == 0) {
            float k = (target > vig) ? 12.0f : 7.0f;
            float fade = 1.0f - expf(-k * (float)dt);
            vig += (target - vig) * fade;
        }
        if (vig > 0.012f)
            renderer_draw_comfort_vignette(&g_game.renderer, vig);
    }
#endif

    if (g_game.trail_count > 1) {
        Vec3 c_move = {0.15f, 1.0f, 0.3f};
        Vec3 c_climb = {1.0f, 0.45f, 0.1f};
        for (int i = 1; i < g_game.trail_count; i++) {
            Vec3 a = g_game.trail_points[i - 1];
            Vec3 b = g_game.trail_points[i];
            float rise = b.y - a.y;
            Vec3 col = (rise > 0.15f || rise < -0.15f) ? c_climb : c_move;
            renderer_debug_line(&g_game.renderer, a, b, col, &view, &projection);
        }
    }

    renderer_present_scaled_3d(&g_game.renderer);
    explosion_draw_flash();
    }

    if (!hud_hidden() && g_game.chat.initialized && g_game.tool_count > 0) {
        int sw = g_game.renderer.canvas_width;
        int sh = g_game.renderer.canvas_height;
        float uis = g_game.ui_scale > 0.1f ? g_game.ui_scale : 1.0f;

        float tool_lift = 0.0f;
        if (g_game.music_cred.title[0] && g_game.music_cred.alpha > 0.01f) {
            tool_lift = (chat_music_credit_stack_height(&g_game.chat, 0.72f) + 8.0f * uis)
                        * g_game.music_cred.alpha;
        }

        ToolSlotInfo slots[MAX_TOOLS];
        for (int ti = 0; ti < g_game.tool_count; ti++) {
            slots[ti].name = g_game.tools[ti].name;
            slots[ti].icon_tex = g_game.tools[ti].icon_tex;
            slots[ti].icon_w = g_game.tools[ti].icon_w;
            slots[ti].icon_h = g_game.tools[ti].icon_h;
            slots[ti].selected = (g_game.equipped_tool == ti + 1);
        }
        chat_render_toolbar(&g_game.chat, slots, g_game.tool_count, tool_lift, sw, sh);

        if (g_game.equipped_tool > 0 && g_game.tool_cooldown > 0.0f && g_game.tool_cooldown_max > 0.0f) {
            float ready = 1.0f - (g_game.tool_cooldown / g_game.tool_cooldown_max);
            if (ready < 0.0f) ready = 0.0f;
            if (ready > 1.0f) ready = 1.0f;
            chat_render_tool_cooldown(&g_game.chat, ready, g_game.tool_count, tool_lift, sw, sh);
        }
    }

    if (!hud_hidden() && g_game.chat.initialized && input->mouse_right) {
        int sw = g_game.renderer.canvas_width;
        int sh = g_game.renderer.canvas_height;
        float uis = g_game.ui_scale > 0.1f ? g_game.ui_scale : 1.0f;
        float ts = 1.5f * uis;
        chat_render_hud_text(&g_game.chat, "+",
                             (float)sw * 0.5f - 6.0f * uis, (float)sh * 0.5f - 6.0f * uis, ts, sw, sh);
    }

    if (!hud_hidden() && g_game.chat.initialized && (g_game.trail_recording || g_game.trail_count > 0)) {
        char tbuf[64];
        if (g_game.trail_recording)
            snprintf(tbuf, sizeof(tbuf), "Trail REC (%d)", g_game.trail_count);
        else
            snprintf(tbuf, sizeof(tbuf), "Trail (%d)  F3+T new", g_game.trail_count);
        float uis = g_game.ui_scale > 0.1f ? g_game.ui_scale : 1.0f;
        chat_render_hud_text(&g_game.chat, tbuf, 10.0f * uis, 36.0f * uis, 1.4f * uis,
                             g_game.renderer.canvas_width, g_game.renderer.canvas_height);
    }

    if (g_game.debug_draw) {
        uint32_t body_count = physics_get_body_count(g_game.physics);
        for (uint32_t i = 1; i < body_count; i++) {
            PhysicsBodyInfo info = physics_get_body_info(g_game.physics, (PhysicsBodyID)i);
            if (!info.active) continue;

            Vec3 color = {0.0f, 1.0f, 0.0f};
            if (info.collider == COLLIDER_SPHERE) {
                renderer_debug_sphere(&g_game.renderer, info.position, info.radius, color, &view, &projection);
            } else {
                renderer_debug_box_matrix(&g_game.renderer, info.transform, color, &view, &projection);
            }
        }
    }

    draw_collision_chunk_borders(&view, &projection);

    renderer_end_frame(&g_game.renderer);
    }

    if (!g_game.menu.benchmark_running && g_game.multiplayer) {
        ChatBubbleMsg recent[CHAT_MAX_MESSAGES];
        int recent_n = chat_collect_recent_bubbles(&g_game.chat, CHAT_BUBBLE_MAX_AGE, recent, CHAT_MAX_MESSAGES);
        int sw = g_game.renderer.canvas_width;
        int sh = g_game.renderer.canvas_height;
        chat_nametag_hits_clear(&g_game.chat);

        {
            Entity* av_ent = scene_get_entity(&g_game.scene, g_game.avatar.entity);
            if (av_ent && recent_n > 0) {
                size_t uname_len = strlen(g_game.username);
                const ChatBubbleMsg* stack[CHAT_MAX_MESSAGES];
                int stack_n = 0;
                for (int i = 0; i < recent_n; i++) {
                    if (recent[i].sender_id != 0) continue;
                    if (strncmp(recent[i].text, g_game.username, uname_len) != 0) continue;
                    if (recent[i].text[uname_len] != ':') continue;
                    stack[stack_n++] = &recent[i];
                }
                if (stack_n > 0) {
                    float bx = av_ent->transform.position.x;
                    float by = av_ent->transform.position.y + 9.5f * AVATAR_SCALE + 1.8f;
                    float bz = av_ent->transform.position.z;
                    float lift = 0.0f;

                    for (int i = stack_n - 1; i >= 0; i--) {
                        bool pointer = (i == stack_n - 1);
                        const char* colon = strchr(stack[i]->text, ':');
                        const char* body = colon ? colon + 2 : stack[i]->text;
                        lift += chat_draw_bubble(&g_game.chat, body,
                                                 bx, by, bz, view.m, projection.m,
                                                 sw, sh, pointer, lift,
                                                 stack[i]->fade, stack[i]->age);
                    }
                }
            }
        }

        for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
            if (!g_game.remote_players[rp].active) continue;
            if (g_game.remote_players[rp].transparency >= 0.99f) continue;
            Entity* ent = scene_get_entity(&g_game.scene, g_game.remote_players[rp].entity);
            if (!ent) continue;
            Vec3 d = vec3_sub(ent->transform.position, g_game.avatar.pos);
            float dist2 = d.x * d.x + d.y * d.y + d.z * d.z;
            if (dist2 > (180.0f * 180.0f)) continue;
            float bx = ent->transform.position.x;
            float by = ent->transform.position.y + 9.5f * AVATAR_SCALE + 1.0f;
            float bz = ent->transform.position.z;
            chat_draw_billboard(&g_game.chat, g_game.remote_players[rp].name,
                                g_game.remote_players[rp].badges,
                                bx, by, bz, view.m, projection.m, sw, sh);

            uint32_t pid = g_game.remote_players[rp].id;
            const ChatBubbleMsg* stack[CHAT_MAX_MESSAGES];
            int stack_n = 0;
            for (int i = 0; i < recent_n; i++) {
                if (recent[i].sender_id != pid || pid == 0) continue;
                stack[stack_n++] = &recent[i];
            }
            if (stack_n > 0) {
                float lift = 0.0f;
                float bubble_y = by + 0.8f;
                for (int i = stack_n - 1; i >= 0; i--) {
                    bool pointer = (i == stack_n - 1);
                    const char* colon = strchr(stack[i]->text, ':');
                    const char* body = colon ? colon + 2 : stack[i]->text;
                    lift += chat_draw_bubble(&g_game.chat, body,
                                             bx, bubble_y, bz, view.m, projection.m,
                                             sw, sh, pointer, lift,
                                             stack[i]->fade, stack[i]->age);
                }
            }
        }
    }

polyworld_load_pump:

    if (g_game.multiplayer) {

        net_client_poll(&g_game.net);
    }

    bool net_drain = g_game.multiplayer &&
        (g_game.net.state == NET_STATE_CONNECTED || g_game.net.recv_len > 0);
    if (net_drain) {

        if (g_game.net.state == NET_STATE_CONNECTED && !g_game.auth_sent) {
            if (g_game.join_ticket[0]) {

                net_client_send_auth_ticket(&g_game.net, g_game.game_id, g_game.join_ticket);
            } else {

                net_client_send_auth(&g_game.net, g_game.game_id, g_game.username);
            }
            g_game.auth_sent = true;
            apply_local_avatar();
        }

        uint8_t* msg_buf = g_net_msg_buf;
        size_t msg_len = 0;
        uint8_t msg_type;

        int msg_budget = g_game.world_ready ? 512 : 64;
        if (g_game.net.recv_len > 200000)
            msg_budget = 2048;
        while (msg_budget-- > 0 &&
               (msg_type = net_client_recv(&g_game.net, msg_buf, &msg_len)) != 0) {
            switch (msg_type) {
                case MSG_WORLD_INIT: {

                    if (msg_len < 4) break;
                    uint32_t hdr = 0;
                    memcpy(&hdr, msg_buf, 4);
                    size_t payload = msg_len - 4;
                    payload -= payload % (size_t)WORLD_INIT_OBJ_BYTES;
                    uint32_t nobj = (uint32_t)(payload / (size_t)WORLD_INIT_OBJ_BYTES);

                    bool same_total = (g_game.world_init_total > 0 && hdr == g_game.world_init_total);
                    bool awaiting_more = same_total &&
                        (g_game.world_init_streaming ||
                         (g_game.world_init_buf && g_game.world_init_done < g_game.world_init_total) ||
                         (!g_game.world_colliders_ready && g_game.net_object_count > 0 &&
                          g_game.net_object_count < (int)g_game.world_init_total));

                    bool legacy_more = !g_game.world_ready && !same_total &&
                        g_game.world_init_buf && g_game.world_init_total > 0 &&
                        (g_game.world_init_streaming || g_game.physics_streaming ||
                         g_game.connectors_streaming || !g_game.world_colliders_ready) &&
                        hdr > 0 && hdr == nobj;

                    if (awaiting_more || legacy_more) {
                        if (payload == 0) break;
                        uint8_t* nb = (uint8_t*)realloc(g_game.world_init_buf,
                                                        g_game.world_init_len + payload);
                        if (!nb) break;
                        memcpy(nb + g_game.world_init_len, msg_buf + 4, payload);
                        g_game.world_init_buf = nb;
                        g_game.world_init_len += payload;
                        if (legacy_more)
                            g_game.world_init_total += hdr;
                        g_game.world_init_streaming = true;

                        if (g_game.physics_streaming || g_game.connectors_streaming) {
                            g_game.physics_streaming = false;
                            g_game.connectors_streaming = false;
                            g_game.world_init_streaming = true;
                        }
                        break;
                    }

                    clear_game_world();
                    apply_local_avatar();
                    refresh_local_avatar_meshes();
                    clear_net_mesh_cache();
                    free(g_game.world_init_buf);
                    g_game.world_init_buf = (uint8_t*)malloc(msg_len ? msg_len : 4);
                    if (!g_game.world_init_buf) break;
                    memcpy(g_game.world_init_buf, msg_buf, 4);
                    if (payload)
                        memcpy(g_game.world_init_buf + 4, msg_buf + 4, payload);
                    g_game.world_init_len = 4 + payload;
                    g_game.world_init_total = hdr;

                    if (g_game.world_init_total < nobj)
                        g_game.world_init_total = nobj;
                    g_game.world_init_off = 4;
                    g_game.world_init_done = 0;
                    g_game.world_init_streaming = true;
                    g_game.physics_streaming = false;
                    g_game.connectors_streaming = false;
                    g_game.world_ready = false;
                    g_game.world_colliders_ready = false;
                    g_game.pending_spawn = false;
                    g_game.spawn_received = false;
                    break;
                }
                case MSG_CONNECTORS: {
                    if (g_game.world_init_streaming || g_game.physics_streaming ||
                        g_game.connectors_streaming) {
                        free(g_game.pending_connectors);
                        g_game.pending_connectors = (uint8_t*)malloc(msg_len ? msg_len : 1);
                        if (g_game.pending_connectors) {
                            memcpy(g_game.pending_connectors, msg_buf, msg_len);
                            g_game.pending_connectors_len = msg_len;
                        }
                        break;
                    }
                    process_connectors_msg(msg_buf, msg_len);
                    break;
                }
                case MSG_YOUR_SPAWN: {
                    float cx = 0, cy = 5, cz = 0;
                    if (msg_len >= 12) {
                        memcpy(&cx, msg_buf, 4);
                        memcpy(&cy, msg_buf+4, 4);
                        memcpy(&cz, msg_buf+8, 4);
                    }
                    g_game.spawn_received = true;

                    g_game.avatar.pos = (Vec3){cx + 0.1f, cy, cz + 0.1f};
                    g_game.avatar.vel = (Vec3){0, 0, 0};
                    if (g_game.avatar.body)
                        physics_set_position(g_game.physics, g_game.avatar.body, g_game.avatar.pos);
                    g_game.collision_pin_pos = g_game.avatar.pos;
                    g_game.collision_chunk_loading = true;
                    g_game.collision_load_attempts = 0;
                    g_game.collision_load_attempt_time = 0.0;
                    g_game.collision_spawn_gate = true;

                    if (!g_game.world_colliders_ready ||
                        g_game.world_init_streaming || g_game.physics_streaming ||
                        g_game.connectors_streaming) {
                        g_game.pending_spawn = true;
                        g_game.pending_spawn_pos[0] = cx;
                        g_game.pending_spawn_pos[1] = cy;
                        g_game.pending_spawn_pos[2] = cz;
                        break;
                    }
                    apply_spawn_and_start_batch(cx, cy, cz);
                    break;
                }
                case MSG_PLAYER_JOIN: {

                    if (msg_len >= 31) {
                        uint32_t pid;
                        memcpy(&pid, msg_buf, 4);

                        char skin_hex[8] = {0};
                        memcpy(skin_hex, msg_buf + 4, 7);

                        const uint8_t* buf = (const uint8_t*)msg_buf;
                        uint32_t eq_shirt   = ((uint32_t)buf[11] << 24) | ((uint32_t)buf[12] << 16) | ((uint32_t)buf[13] << 8) | (uint32_t)buf[14];
                        uint32_t eq_pants   = ((uint32_t)buf[15] << 24) | ((uint32_t)buf[16] << 16) | ((uint32_t)buf[17] << 8) | (uint32_t)buf[18];
                        uint32_t eq_head    = ((uint32_t)buf[19] << 24) | ((uint32_t)buf[20] << 16) | ((uint32_t)buf[21] << 8) | (uint32_t)buf[22];
                        uint32_t eq_package = ((uint32_t)buf[23] << 24) | ((uint32_t)buf[24] << 16) | ((uint32_t)buf[25] << 8) | (uint32_t)buf[26];
                        uint32_t eq_accessories[PW_MAX_EQUIPPED_ACCESSORIES];
                        parse_appearance_accessory_ids(buf, msg_len, eq_accessories);
                        uint32_t eq_emotes[PW_MAX_EQUIPPED_EMOTES];
                        parse_appearance_emote_ids(buf, msg_len, eq_emotes);

                        size_t name_off = (msg_len >= PW_APPEARANCE_HDR) ? PW_APPEARANCE_HDR
                                          : ((msg_len >= PW_APPEARANCE_HDR_EMOTES_3ACC) ? PW_APPEARANCE_HDR_EMOTES_3ACC
                                          : ((msg_len >= PW_APPEARANCE_HDR_LEGACY_3ACC) ? PW_APPEARANCE_HDR_LEGACY_3ACC : 31));
                        char name[32] = {0};
                        uint32_t join_account_id = 0;
                        if (msg_len > name_off) {
                            uint8_t nlen = (uint8_t)msg_buf[name_off];
                            if (nlen <= 31 && name_off + 1u + nlen + 4u == (size_t)msg_len) {
                                if (nlen > 0) memcpy(name, msg_buf + name_off + 1, nlen);
                                memcpy(&join_account_id, msg_buf + name_off + 1 + nlen, 4);
                            } else {
                                size_t rem = (size_t)msg_len - name_off;
                                bool trailing_binary = false;
                                if (rem >= 4) {
                                    for (size_t bi = msg_len - 4; bi < (size_t)msg_len; bi++) {
                                        unsigned char c = (unsigned char)msg_buf[bi];
                                        if (c < 32 || c > 126) { trailing_binary = true; break; }
                                    }
                                }
                                size_t copy_len = rem;
                                if (rem >= 4 && trailing_binary) {
                                    memcpy(&join_account_id, msg_buf + msg_len - 4, 4);
                                    copy_len = rem - 4;
                                }
                                if (copy_len > 31) copy_len = 31;
                                if (copy_len > 0) memcpy(name, msg_buf + name_off, copy_len);
                            }
                        }

                        Vec3 remote_skin = {0.918f, 0.918f, 0.918f};
                        if (skin_hex[0] == '#' && strlen(skin_hex) >= 7) {
                            unsigned int hex = 0;
                            sscanf(skin_hex + 1, "%06x", &hex);
                            remote_skin = (Vec3){
                                ((hex >> 16) & 0xFF) / 255.0f,
                                ((hex >> 8) & 0xFF) / 255.0f,
                                (hex & 0xFF) / 255.0f
                            };
                        }

                        int existing_rp = -1;
                        for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
                            if (g_game.remote_players[rp].active && g_game.remote_players[rp].id == pid) {
                                existing_rp = rp;
                                break;
                            }
                        }
                        if (existing_rp >= 0) {
                            int rp = existing_rp;
                            g_game.remote_players[rp].skin_color = remote_skin;
                            strncpy(g_game.remote_players[rp].name, name, 31);
                            g_game.remote_players[rp].name[31] = '\0';
                            g_game.remote_players[rp].account_id = join_account_id;
                            Entity* ent = scene_get_entity(&g_game.scene, g_game.remote_players[rp].entity);
                            if (ent) ent->material.color = remote_skin;
                            g_game.remote_players[rp].mesh_flags =
                                normalize_mesh_flags((int)eq_package);
                            ensure_avatar_bodies_loaded();
                            refresh_remote_avatar_meshes(rp);
                            load_player_accessories(1 + rp, eq_accessories);
                            memcpy(g_game.remote_players[rp].equipped_emotes, eq_emotes,
                                   sizeof(eq_emotes));
                            break;
                        }

                        if (join_account_id > 0) {
                            for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
                                if (!g_game.remote_players[rp].active) continue;
                                if (g_game.remote_players[rp].account_id == join_account_id &&
                                    g_game.remote_players[rp].id != pid)
                                    remove_remote_player_by_id(g_game.remote_players[rp].id);
                            }
                        }

                        for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
                            if (!g_game.remote_players[rp].active) {
                                EntityID eid = scene_create_entity(&g_game.scene);
                                Entity* ent = scene_get_entity(&g_game.scene, eid);
                                if (ent) {
                                    ent->transform.position = (Vec3){0, 0, 0};
                                    ent->transform.scale = (Vec3){AVATAR_SCALE, AVATAR_SCALE, AVATAR_SCALE};
                                    if (!g_game.avatar_anim.parts[0].valid)
                                        ent->mesh = &g_game.avatar_gpu_mesh;
                                    ent->material.color = remote_skin;
                                    ent->material.texture_mode = 3;
                                }
                                g_game.remote_players[rp].id = pid;
                                g_game.remote_players[rp].entity = eid;
                                g_game.remote_players[rp].active = true;
                                g_game.remote_players[rp].skin_color = remote_skin;
                                g_game.remote_players[rp].tex_shirt = 0;
                                g_game.remote_players[rp].tex_pants = 0;
                                g_game.remote_players[rp].tex_head = 0;
                                g_game.remote_players[rp].tex_package = 0;
                                for (int ai = 0; ai < PW_MAX_EQUIPPED_ACCESSORIES; ai++)
                                    g_game.remote_players[rp].accessory_tex[ai] = 0;
                                strncpy(g_game.remote_players[rp].name, name, 31);
                                g_game.remote_players[rp].name[31] = '\0';
                                g_game.remote_players[rp].badges = 0;
                                g_game.remote_players[rp].account_id = join_account_id;
                                g_game.remote_players[rp].transparency = 0.0f;
                                g_game.remote_players[rp].has_target = false;
                                g_game.remote_players[rp].lerp_t = 1.0f;
                                g_game.remote_players[rp].lerp_duration = 0.0f;
                                g_game.remote_players[rp].lerp_interval_ema = 0.0f;
                                g_game.remote_players[rp].last_update_time = 0.0;
                                g_game.remote_players[rp].has_name_color = false;
                                g_game.remote_players[rp].is_vr = false;
                                memset(&g_game.remote_players[rp].vr_pose, 0, sizeof(PwVrPose));
                                vr_ik_calib_reset(&g_game.remote_players[rp].vr_calib);
                                g_game.remote_player_count++;

                                {
                                    char turl[256];
                                    const char* base = "/uploads";
                                    if (eq_shirt > 0)
                                        snprintf(turl, sizeof(turl), "%s%s/shirts/%u.png", g_game.host, base, eq_shirt);
                                    else
                                        snprintf(turl, sizeof(turl), "%s%s/shirts/guest.png", g_game.host, base);
                                    platform_load_file(turl, on_avatar_texture_loaded, (void*)(intptr_t)(100 + rp * 4 + 0));

                                    if (eq_pants > 0)
                                        snprintf(turl, sizeof(turl), "%s%s/pants/%u.png", g_game.host, base, eq_pants);
                                    else
                                        snprintf(turl, sizeof(turl), "%s%s/pants/guest.png", g_game.host, base);
                                    platform_load_file(turl, on_avatar_texture_loaded, (void*)(intptr_t)(100 + rp * 4 + 1));

                                    if (eq_head > 0)
                                        snprintf(turl, sizeof(turl), "%s%s/heads/%u.png", g_game.host, base, eq_head);
                                    else
                                        snprintf(turl, sizeof(turl), "%s%s/heads/19.png", g_game.host, base);
                                    platform_load_file(turl, on_avatar_texture_loaded, (void*)(intptr_t)(100 + rp * 4 + 2));

                                    g_game.remote_players[rp].mesh_flags =
                                        normalize_mesh_flags((int)eq_package);
                                    ensure_avatar_bodies_loaded();
                                    refresh_remote_avatar_meshes(rp);
                                    load_player_accessories(1 + rp, eq_accessories);
                                    memcpy(g_game.remote_players[rp].equipped_emotes, eq_emotes,
                                           sizeof(eq_emotes));
                                }
                                break;
                            }
                        }
                    }
                    break;
                }

                case MSG_PLAYER_APPEARANCE:

                    if (msg_len >= 31) {
                        uint32_t pid;
                        memcpy(&pid, msg_buf, 4);
                        char skin_hex[8] = {0};
                        memcpy(skin_hex, msg_buf + 4, 7);
                        const uint8_t* buf = (const uint8_t*)msg_buf;
                        uint32_t eq_shirt = ((uint32_t)buf[11]<<24)|((uint32_t)buf[12]<<16)|((uint32_t)buf[13]<<8)|(uint32_t)buf[14];
                        uint32_t eq_pants = ((uint32_t)buf[15]<<24)|((uint32_t)buf[16]<<16)|((uint32_t)buf[17]<<8)|(uint32_t)buf[18];
                        uint32_t eq_head  = ((uint32_t)buf[19]<<24)|((uint32_t)buf[20]<<16)|((uint32_t)buf[21]<<8)|(uint32_t)buf[22];
                        uint32_t eq_package = ((uint32_t)buf[23]<<24)|((uint32_t)buf[24]<<16)|((uint32_t)buf[25]<<8)|(uint32_t)buf[26];
                        uint32_t eq_accessories[PW_MAX_EQUIPPED_ACCESSORIES];
                        parse_appearance_accessory_ids(buf, msg_len, eq_accessories);
                        uint32_t eq_emotes[PW_MAX_EQUIPPED_EMOTES];
                        parse_appearance_emote_ids(buf, msg_len, eq_emotes);
                        Vec3 remote_skin = {0.918f, 0.918f, 0.918f};
                        if (skin_hex[0] == '#') {
                            unsigned int hex = 0;
                            sscanf(skin_hex + 1, "%06x", &hex);
                            remote_skin = (Vec3){ ((hex>>16)&0xFF)/255.0f, ((hex>>8)&0xFF)/255.0f, (hex&0xFF)/255.0f };
                        }
                        for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
                            if (!g_game.remote_players[rp].active || g_game.remote_players[rp].id != pid) continue;
                            g_game.remote_players[rp].skin_color = remote_skin;
                            char turl[256];
                            const char* base = "/uploads";
                            if (eq_shirt > 0) snprintf(turl, sizeof(turl), "%s%s/shirts/%u.png", g_game.host, base, eq_shirt);
                            else snprintf(turl, sizeof(turl), "%s%s/shirts/guest.png", g_game.host, base);
                            platform_load_file(turl, on_avatar_texture_loaded, (void*)(intptr_t)(100 + rp * 4 + 0));
                            if (eq_pants > 0) snprintf(turl, sizeof(turl), "%s%s/pants/%u.png", g_game.host, base, eq_pants);
                            else snprintf(turl, sizeof(turl), "%s%s/pants/guest.png", g_game.host, base);
                            platform_load_file(turl, on_avatar_texture_loaded, (void*)(intptr_t)(100 + rp * 4 + 1));
                            if (eq_head > 0) snprintf(turl, sizeof(turl), "%s%s/heads/%u.png", g_game.host, base, eq_head);
                            else snprintf(turl, sizeof(turl), "%s%s/heads/19.png", g_game.host, base);
                            platform_load_file(turl, on_avatar_texture_loaded, (void*)(intptr_t)(100 + rp * 4 + 2));
                            g_game.remote_players[rp].mesh_flags = normalize_mesh_flags((int)eq_package);
                            ensure_avatar_bodies_loaded();
                            refresh_remote_avatar_meshes(rp);
                            load_player_accessories(1 + rp, eq_accessories);
                            memcpy(g_game.remote_players[rp].equipped_emotes, eq_emotes,
                                   sizeof(eq_emotes));
                            break;
                        }
                    }
                    break;
                case MSG_PLAYER_UPDATE:

                    if (msg_len >= 20) {
                        uint32_t pid;
                        float pos[3], yaw;
                        uint8_t remote_anim = 0;
                        uint32_t remote_emote_id = 0;
                        char remote_tool[32] = {0};
                        memcpy(&pid, msg_buf, 4);
                        memcpy(pos, msg_buf + 4, 12);
                        memcpy(&yaw, msg_buf + 16, 4);
                        if (msg_len >= 21) remote_anim = msg_buf[20];
                        if (msg_len >= 25) memcpy(&remote_emote_id, msg_buf + 21, 4);
                        if (msg_len >= 26) {
                            uint8_t tlen = msg_buf[25];
                            if (tlen > 31) tlen = 31;
                            if ((size_t)(26 + tlen) <= msg_len && tlen > 0) {
                                memcpy(remote_tool, msg_buf + 26, tlen);
                                remote_tool[tlen] = '\0';
                            }
                        }

                        for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
                            if (g_game.remote_players[rp].active && g_game.remote_players[rp].id == pid) {
                                Entity* ent = scene_get_entity(&g_game.scene, g_game.remote_players[rp].entity);
                                bool is_dead = (remote_anim == ANIM_STATE_DEAD);
                                Vec3 new_feet = { pos[0], pos[1] - AVATAR_FEET_OFFSET, pos[2] };
                                float mesh_yaw = wrap_deg360(yaw + 270.0f);
                                if (strcmp(g_game.remote_players[rp].held_tool, remote_tool) != 0) {
                                    snprintf(g_game.remote_players[rp].held_tool,
                                             sizeof(g_game.remote_players[rp].held_tool),
                                             "%s", remote_tool);
                                    if (remote_tool[0]) tool_hold_ensure(remote_tool);
                                }

                                if (is_dead) {
                                    if (!g_game.remote_players[rp].dead) {
                                        g_game.remote_players[rp].dead = true;
                                        g_game.remote_players[rp].anim.state = ANIM_STATE_DEAD;
                                        g_game.remote_players[rp].anim.emote_id = 0;
                                        g_game.remote_players[rp].anim.emote_clip = NULL;

                                        g_game.remote_players[rp].has_target = false;
                                        g_game.remote_players[rp].lerp_t = 1.0f;
                                        if (ent && !g_game.remote_players[rp].ragdoll.active) {
                                            ragdoll_spawn((RagdollState*)&g_game.remote_players[rp].ragdoll,
                                                          &g_game.remote_players[rp].anim,
                                                          g_game.remote_players[rp].accessory,
                                                          ent->transform.position,
                                                          ent->transform.rotation.y,
                                                          g_game.remote_players[rp].last_vel);
                                        }
                                    }
                                } else if (g_game.remote_players[rp].dead) {

                                    g_game.remote_players[rp].dead = false;
                                    ragdoll_destroy((RagdollState*)&g_game.remote_players[rp].ragdoll);
                                    remote_player_snap_pose(rp, ent, new_feet, mesh_yaw);
                                    g_game.remote_players[rp].anim.state = (AnimState)remote_anim;
                                    if (remote_anim == ANIM_STATE_EMOTE && remote_emote_id) {
                                        if (g_game.remote_players[rp].anim.emote_id != remote_emote_id)
                                            g_game.remote_players[rp].anim.emote_time = 0.0f;
                                        g_game.remote_players[rp].anim.emote_id = remote_emote_id;
                                        emote_clip_request(remote_emote_id);
                                        g_game.remote_players[rp].anim.emote_clip = emote_clip_get(remote_emote_id);
                                    } else {
                                        g_game.remote_players[rp].anim.emote_id = 0;
                                        g_game.remote_players[rp].anim.emote_clip = NULL;
                                    }
                                } else if (ent) {
                                    Vec3 cur = ent->transform.position;
                                    double now = platform_get_time();
                                    float dt_u = (float)(now - g_game.remote_players[rp].last_update_time);
                                    if (dt_u < 1e-4f) dt_u = 0.025f;
                                    g_game.remote_players[rp].last_vel = (Vec3){
                                        (new_feet.x - cur.x) / dt_u,
                                        (new_feet.y - cur.y) / dt_u,
                                        (new_feet.z - cur.z) / dt_u
                                    };
                                    float dx = new_feet.x - cur.x;
                                    float dy = new_feet.y - cur.y;
                                    float dz = new_feet.z - cur.z;
                                    float d2 = dx * dx + dy * dy + dz * dz;
                                    bool snap = !g_game.remote_players[rp].has_target || d2 > 16.0f * 16.0f;
                                    double last_t = g_game.remote_players[rp].last_update_time;
                                    float gap = (last_t > 0.0) ? (float)(now - last_t) : 1.0f;
                                    bool burst = !snap && gap < 0.018f &&
                                                 g_game.remote_players[rp].lerp_t < 0.99f;
                                    if (snap) {
                                        remote_player_snap_pose(rp, ent, new_feet, mesh_yaw);
                                    } else {
                                        float vis = wrap_deg360(ent->transform.rotation.y);
                                        g_game.remote_players[rp].lerp_start_pos = cur;
                                        g_game.remote_players[rp].lerp_start_yaw =
                                            angle_follow_deg(g_game.remote_players[rp].lerp_start_yaw, vis);
                                        g_game.remote_players[rp].target_yaw =
                                            angle_follow_deg(g_game.remote_players[rp].lerp_start_yaw, mesh_yaw);
                                        if (burst) {
                                            float remain = g_game.remote_players[rp].lerp_duration *
                                                           (1.0f - g_game.remote_players[rp].lerp_t);
                                            if (remain < 0.020f) remain = 0.020f;
                                            g_game.remote_players[rp].lerp_duration = remain;
                                        } else {
                                            g_game.remote_players[rp].lerp_duration = net_player_lerp_duration(
                                                &g_game.remote_players[rp].lerp_interval_ema,
                                                now,
                                                last_t,
                                                last_t > 0.0);
                                        }
                                        g_game.remote_players[rp].lerp_t = 0.0f;
                                    }
                                    g_game.remote_players[rp].last_update_time = now;
                                    g_game.remote_players[rp].target_pos = new_feet;
                                    g_game.remote_players[rp].has_target = true;
                                    g_game.remote_players[rp].anim.state = (AnimState)remote_anim;
                                    if (remote_anim == ANIM_STATE_EMOTE && remote_emote_id) {
                                        if (g_game.remote_players[rp].anim.emote_id != remote_emote_id)
                                            g_game.remote_players[rp].anim.emote_time = 0.0f;
                                        g_game.remote_players[rp].anim.emote_id = remote_emote_id;
                                        emote_clip_request(remote_emote_id);
                                        g_game.remote_players[rp].anim.emote_clip = emote_clip_get(remote_emote_id);
                                    } else {
                                        g_game.remote_players[rp].anim.emote_id = 0;
                                        g_game.remote_players[rp].anim.emote_clip = NULL;
                                    }
                                }
                                break;
                            }
                        }
                    }
                    break;
                case MSG_PLAYER_LEAVE:
                    if (msg_len >= 4) {
                        uint32_t pid;
                        memcpy(&pid, msg_buf, 4);
                        remove_remote_player_by_id(pid);
                    }
                    break;
                case MSG_OBJECT_CREATE: {

                    if (msg_len < 68) break;
                    uint32_t obj_id; uint8_t obj_type, anchored;
                    float opos[3], osize[3], ocolor[3], orot[3]; uint8_t surfaces[6];
                    float oglow = 0.0f;
                    float oalpha = 1.0f;
                    size_t off2 = 0;
                    memcpy(&obj_id, msg_buf+off2, 4); off2+=4;
                    uint8_t packed_type = msg_buf[off2++];
                    uint8_t part_mat = 0;
                    pw_wire_unpack_type(packed_type, &obj_type, &part_mat);
                    anchored = msg_buf[off2++];
                    memcpy(opos, msg_buf+off2, 12); off2+=12;
                    memcpy(osize, msg_buf+off2, 12); off2+=12;
                    memcpy(ocolor, msg_buf+off2, 12); off2+=12;
                    memcpy(surfaces, msg_buf+off2, 6); off2+=6;
                    memcpy(orot, msg_buf+off2, 12); off2+=12;
                    memcpy(&oglow, msg_buf+off2, 4); off2+=4;
                    memcpy(&oalpha, msg_buf+off2, 4); off2+=4;
                    if (msg_len >= 69) part_mat = msg_buf[68];
                    {
                        uint8_t pend = pending_material_take(obj_id);
                        if (pend) part_mat = pend;
                    }
                    if (part_mat >= PART_MATERIAL_COUNT) part_mat = PART_MATERIAL_PLASTIC;

                    float hx=osize[0]*0.5f, hy=osize[1]*0.5f, hz=osize[2]*0.5f;
                    if (hx < 0.01f || hy < 0.01f || hz < 0.01f) break;
                    EntityID eid = scene_create_entity(&g_game.scene);
                    Entity* ent = scene_get_entity(&g_game.scene, eid);
                    if (ent) {

                        ent->transform.position = (Vec3){opos[0], opos[1], opos[2]};
                        ent->transform.rotation = (Vec3){orot[0], orot[1], orot[2]};
                        ent->transform.scale = (Vec3){osize[0], osize[1], osize[2]};

                        ent->material.color = (Vec3){ocolor[0], ocolor[1], ocolor[2]};
                        ent->material.glow = oglow;
                        ent->material.alpha = oalpha;
                        ent->material.part_material = part_mat;

                        for (int s = 0; s < 6; s++) ent->material.surfaces[s] = surfaces[s];

                        ent->new_object = true;

                        ent->mesh = net_unit_mesh_for_type(obj_type);

                        bool want_collide = !pending_nocollide_take(obj_id);
                        if (g_game.net_object_count < MAX_NET_OBJECTS) {
                            g_game.net_objects[g_game.net_object_count].net_id = obj_id;
                            g_game.net_objects[g_game.net_object_count].entity = eid;
                            g_game.net_objects[g_game.net_object_count].anchored = (anchored != 0);
                            g_game.net_objects[g_game.net_object_count].connector_static = false;
                            g_game.net_objects[g_game.net_object_count].obj_type = obj_type;
                            g_game.net_objects[g_game.net_object_count].mesh_id = 0;
                            g_game.net_objects[g_game.net_object_count].mesh_collider = 0;
                            g_game.net_objects[g_game.net_object_count].size[0] = osize[0];
                            g_game.net_objects[g_game.net_object_count].size[1] = osize[1];
                            g_game.net_objects[g_game.net_object_count].size[2] = osize[2];
                            g_game.net_objects[g_game.net_object_count].has_target = false;
                            g_game.net_objects[g_game.net_object_count].lerp_t = 0.0f;
                            g_game.net_objects[g_game.net_object_count].lerp_duration = 0.0f;
                            g_game.net_objects[g_game.net_object_count].lerp_interval_ema = 0.0f;
                            g_game.net_objects[g_game.net_object_count].last_update_time = 0.0;
                            g_game.net_objects[g_game.net_object_count].collide_wanted = want_collide;
                            g_game.net_objects[g_game.net_object_count].clickable =
                                pending_clickable_take(obj_id);
                            g_game.net_objects[g_game.net_object_count].collision_lod_active = false;
                            g_game.net_objects[g_game.net_object_count].net_owned = false;
                            g_game.net_objects[g_game.net_object_count].never_netown = false;
                            g_game.net_object_count++;
                            {
                                uint8_t mcol = 0;
                                uint32_t mid = pending_mesh_take(obj_id, &mcol);
                                if (mid) apply_net_part_mesh(obj_id, mid, mcol);
                                flush_pending_decals_for(obj_id);
                            }
                        }

                        if (!anchored && g_game.dynamic_object_count < MAX_DYNAMIC_OBJECTS) {
                            int di = g_game.dynamic_object_count++;
                            g_game.dynamic_objects[di].net_id = obj_id;
                            g_game.dynamic_objects[di].entity = eid;
                            g_game.dynamic_objects[di].body = 0;
                            g_game.dynamic_objects[di].active = true;
                            g_game.dynamic_objects[di].owned_locally = false;
                            g_game.dynamic_objects[di].own_timer = 0.0f;
                            g_game.dynamic_objects[di].last_pos = (Vec3){opos[0], opos[1], opos[2]};
                        }

                        if (!want_collide) {
                            ent->physics_body = 0;
                            ent->static_batch = false;
                            ent->render_batched = false;
                            break;
                        }

                        int cni = net_find_ni(obj_id);
                        if (cni >= 0)
                            net_brick_activate_collision(cni);
                        else {
                            BodyDesc desc = {
                                .type = BODY_STATIC,
                                .collider = COLLIDER_BOX,
                                .position = {opos[0], opos[1], opos[2]},
                                .half_extents = {hx, hy, hz},
                                .radius = hx,
                                .mass = anchored ? 0.0f : net_part_mass_from_size(osize),
                                .restitution = 0.0f,
                                .friction = 0.8f
                            };
                            if (obj_type == 3) body_desc_apply_wedge(&desc, hx, hy, hz);
                            else desc.collider = collider_for_obj_type(obj_type);
                            ent->physics_body = physics_create_body(g_game.physics, &desc);
                            if (ent->physics_body && (orot[0] != 0.0f || orot[1] != 0.0f || orot[2] != 0.0f))
                                physics_set_rotation_euler(g_game.physics, ent->physics_body,
                                    (Vec3){orot[0], orot[1], orot[2]});
                        }

                        ent->static_batch = false;
                        ent->render_batched = false;
                    }
                    break;
                }
                case MSG_OBJECT_DESTROY: {

                    if (msg_len < 4) break;
                    uint32_t did; memcpy(&did, msg_buf, 4);
                    net_remove_object(did);
                    break;
                }
                case MSG_OBJECT_UPDATE: {

                    const uint8_t* entries = msg_buf;
                    int nentries = 1;
                    size_t stride = 0;
                    if (msg_len >= 2 + 2 * 53) {
                        uint16_t bc = 0;
                        memcpy(&bc, msg_buf, 2);
                        if (bc >= 2 && (size_t)msg_len == 2u + (size_t)bc * 53u) {
                            nentries = (int)bc;
                            entries = msg_buf + 2;
                            stride = 53;
                        }
                    }
                    for (int ei = 0; ei < nentries; ei++) {
                        const uint8_t* e = (stride > 0) ? (entries + (size_t)ei * stride) : msg_buf;
                        size_t elen = (stride > 0) ? stride : msg_len;
                        if (elen < 28) continue;

                        uint32_t obj_id;
                        float opos[3], orot[3];
                        memcpy(&obj_id, e, 4);
                        memcpy(opos, e + 4, 12);
                        memcpy(orot, e + 16, 12);

                        float ocolor[3] = {-1, -1, -1};
                        float osize[3] = {-1, -1, -1};
                        bool should_tween = true;
                        if (elen >= 40) memcpy(ocolor, e + 28, 12);
                        if (elen >= 53) {
                            memcpy(osize, e + 40, 12);
                            should_tween = (e[52] != 0);
                        } else if (elen >= 41) {
                            should_tween = (e[40] != 0);
                        }

                        Entity* target = NULL;
                        int found_ni = -1;
                        for (int ni = 0; ni < g_game.net_object_count; ni++) {
                            if (g_game.net_objects[ni].net_id == obj_id) {
                                target = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
                                found_ni = ni;
                                break;
                            }
                        }
                        if (!target || found_ni < 0) continue;
                        if (g_game.net_objects[found_ni].net_owned && should_tween)
                            continue;

                        if (target->render_batched) {
                            float mdx = opos[0] - target->transform.position.x;
                            float mdy = opos[1] - target->transform.position.y;
                            float mdz = opos[2] - target->transform.position.z;
                            bool moved = (mdx*mdx + mdy*mdy + mdz*mdz) > 0.0001f;
                            bool recolor = (ocolor[0] >= 0.0f);
                            bool resize = (osize[0] > 0.0f);
                            if (moved || recolor || resize) {
                                target->static_batch = false;
                                target->render_batched = false;
                                brick_batch_mark_dirty();
                            }
                        }

                        if (osize[0] > 0.0f && osize[1] > 0.0f && osize[2] > 0.0f) {
                            float* ps = g_game.net_objects[found_ni].size;
                            float dsz = fabsf(osize[0] - ps[0]) + fabsf(osize[1] - ps[1]) + fabsf(osize[2] - ps[2]);
                            if (dsz > 0.001f) {
                                float hx = osize[0] * 0.5f, hy = osize[1] * 0.5f, hz = osize[2] * 0.5f;
                                uint8_t ot = g_game.net_objects[found_ni].obj_type;
                                target->transform.scale = (Vec3){osize[0], osize[1], osize[2]};
                                target->mesh = net_unit_mesh_for_type(ot);
                                ps[0] = osize[0]; ps[1] = osize[1]; ps[2] = osize[2];
                                if (target->physics_body) {
                                    physics_destroy_body(g_game.physics, target->physics_body);
                                    BodyDesc desc = {
                                        .type = g_game.net_objects[found_ni].net_owned ? BODY_DYNAMIC : BODY_STATIC,
                                        .collider = COLLIDER_BOX,
                                        .position = target->transform.position,
                                        .half_extents = {hx, hy, hz},
                                        .radius = hx,
                                        .mass = g_game.net_objects[found_ni].net_owned
                                            ? net_part_mass_from_size(osize) : 0.0f,
                                        .restitution = 0.0f,
                                        .friction = 0.8f
                                    };
                                    net_apply_part_collider(&desc, found_ni, hx, hy, hz);
                                    target->physics_body = physics_create_body(g_game.physics, &desc);
                                    if (target->physics_body) {
                                        Vec3 nr = target->transform.rotation;
                                        physics_set_rotation_euler(g_game.physics, target->physics_body, nr);
                                    }
                                }
                            }
                        }

                        for (int ni = 0; ni < g_game.net_object_count; ni++) {
                            if (g_game.net_objects[ni].net_id == obj_id && g_game.net_objects[ni].connector_static) {

                                g_game.net_objects[ni].connector_static = false;
                                g_game.net_objects[ni].anchored = false;
                                break;
                            }
                        }

                        bool locally_owned = false;
                        int owned_di = -1;
                        for (int di = 0; di < g_game.dynamic_object_count; di++) {
                            if (g_game.dynamic_objects[di].active &&
                                g_game.dynamic_objects[di].net_id == obj_id &&
                                g_game.dynamic_objects[di].owned_locally) {
                                locally_owned = true;
                                owned_di = di;
                                break;
                            }
                        }

                        if (locally_owned && should_tween) continue;
                        if (locally_owned && owned_di >= 0)
                            g_game.dynamic_objects[owned_di].owned_locally = false;

                        Vec3 tgt = (Vec3){ opos[0], opos[1], opos[2] };
                        if (!isfinite(tgt.x) || !isfinite(tgt.y) || !isfinite(tgt.z) ||
                            fabsf(tgt.x) > 80000.0f || fabsf(tgt.y) > 80000.0f ||
                            fabsf(tgt.z) > 80000.0f) {
                            continue;
                        }
                        Vec3 nr = euler_follow(target->transform.rotation,
                                               (Vec3){ orot[0], orot[1], orot[2] });
                        net_apply_replicated_pose(target, found_ni, tgt, nr, should_tween, true);
                        if (ocolor[0] >= 0.0f) {
                            target->material.color = (Vec3){ ocolor[0], ocolor[1], ocolor[2] };
                        }
                        for (int di = 0; di < g_game.dynamic_object_count; di++) {
                            if (g_game.dynamic_objects[di].active && g_game.dynamic_objects[di].net_id == obj_id) {
                                g_game.dynamic_objects[di].last_pos = (Vec3){ opos[0], opos[1], opos[2] };
                                break;
                            }
                        }
                    }
                    break;
                }
                case 0x20: {

                    if (msg_len < 5) break;
                    uint32_t parent_obj_id;
                    memcpy(&parent_obj_id, msg_buf, 4);

                    char* source = (char*)malloc(msg_len - 4 + 1);
                    if (!source) break;
                    memcpy(source, msg_buf + 4, msg_len - 4);
                    source[msg_len - 4] = '\0';

                    if (!g_game.world_ready) {
                        if (g_game.pending_script_count < MAX_PENDING_SCRIPTS) {
                            int si = g_game.pending_script_count++;
                            g_game.pending_scripts[si].parent_obj_id = parent_obj_id;
                            g_game.pending_scripts[si].source = source;
                            source = NULL;
                        }
                        free(source);
                        break;
                    }

                    EntityID parent_eid = ENTITY_INVALID;
                    if (parent_obj_id != 0) {
                        for (int ni = 0; ni < g_game.net_object_count; ni++) {
                            if (g_game.net_objects[ni].net_id == parent_obj_id) {
                                parent_eid = g_game.net_objects[ni].entity;
                                break;
                            }
                        }
                    }

                    if (g_game.scripts && (parent_obj_id == 0 || parent_eid != ENTITY_INVALID)) {
                        client_script_load(g_game.scripts, parent_eid, source);
                    }
                    free(source);
                    break;
                }
                case MSG_PLAYER_KICK: {

                    char reason[128] = "Kicked from server";
                    if (msg_len > 0) {
                        size_t rlen = msg_len < 127 ? msg_len : 127;
                        memcpy(reason, msg_buf, rlen);
                        reason[rlen] = '\0';
                    }
                    audio_stop_music();
                    net_client_disconnect(&g_game.net);
                    g_game.multiplayer = false;
                    g_game.world_ready = false;
                    strncpy(g_game.kick_reason, reason, sizeof(g_game.kick_reason) - 1);
                    g_game.kick_reason[sizeof(g_game.kick_reason) - 1] = '\0';
                    g_game.show_kick = true;
#ifdef __ANDROID__
                    touch_controls_set_enabled(false);
#endif
#ifdef __EMSCRIPTEN__
                    EM_ASM({
                        if (window.parent && window.parent !== window) {
                            window.parent.postMessage({type: 'connection_error'}, '*');
                        }
                    });
#endif
                    break;
                }
                case MSG_CHAT: {

                    if (msg_len < 2) break;
                    uint8_t name_len = msg_buf[0];
                    if (name_len > 31 || (size_t)(1 + name_len) > msg_len) break;
                    char text[200] = {0};
                    size_t text_len = msg_len - 1 - name_len;
                    if (text_len > 199) text_len = 199;
                    memcpy(text, msg_buf + 1 + name_len, text_len);

                    if (name_len == 0) {
                        chat_add_system_message(&g_game.chat, text);
                        break;
                    }

                    char name[32] = {0};
                    memcpy(name, msg_buf + 1, name_len);

                    uint32_t sender_pid = 0;
                    for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
                        if (g_game.remote_players[rp].active && strcmp(g_game.remote_players[rp].name, name) == 0) {
                            sender_pid = g_game.remote_players[rp].id;
                            break;
                        }
                    }

                    char formatted[CHAT_MSG_LEN];
                    snprintf(formatted, sizeof(formatted), "%s: %s", name, text);
                    chat_add_message_from(&g_game.chat, formatted, sender_pid);
                    audio_play(SFX_CHAT);
                    break;
                }
                case MSG_ROCKET: {

                    if (msg_len < 1) break;
                    uint8_t rcount = msg_buf[0];

                    for (int ri = 0; ri < MAX_ROCKETS; ri++) g_game.rockets[ri].active = false;
                    size_t roff = 1;
                    for (int ri = 0; ri < (int)rcount && ri < MAX_ROCKETS && roff + 12 <= msg_len; ri++) {
                        g_game.rockets[ri].active = true;
                        memcpy(&g_game.rockets[ri].position.x, msg_buf + roff, 4); roff += 4;
                        memcpy(&g_game.rockets[ri].position.y, msg_buf + roff, 4); roff += 4;
                        memcpy(&g_game.rockets[ri].position.z, msg_buf + roff, 4); roff += 4;
                    }
                    break;
                }

                case MSG_FRIEND: {
                    if (msg_len < 2) break;
                    uint8_t subtype = msg_buf[0];
                    if (subtype == 10) {

                        if (msg_len < 2) break;
                        uint8_t count = msg_buf[1];
                        size_t off = 2;
                        for (uint8_t ci = 0; ci < count && off + 5 <= msg_len; ci++) {
                            uint32_t from_id = 0;
                            memcpy(&from_id, msg_buf + off, 4); off += 4;
                            uint8_t nlen = msg_buf[off++];
                            char fname[32] = {0};
                            if (nlen > 31) nlen = 31;
                            if (off + nlen > msg_len) break;
                            memcpy(fname, msg_buf + off, nlen);
                            off += nlen;
                            if (from_id > 0)
                                social_push_toast(&g_game.social, from_id, fname);
                        }
                    } else if (subtype == 11) {

                        int ok = msg_len > 1 ? msg_buf[1] : 0;
                        char err[96] = {0};
                        if (msg_len > 2) {
                            uint8_t el = msg_buf[2];
                            if (el > 95) el = 95;
                            if ((size_t)(3 + el) <= msg_len && el > 0)
                                memcpy(err, msg_buf + 3, el);
                        }
                        g_game.social.send_busy = false;
                        if (ok) {
                            g_game.social.status[0] = '\0';
                            if (g_game.social.card_open)
                                g_game.social.friend_rel = SOCIAL_REL_OUTGOING;
                        } else {
                            snprintf(g_game.social.status, sizeof(g_game.social.status), "%s",
                                     err[0] ? err : "Request failed");
                        }
                    } else if (subtype == 12) {

                        if (msg_len < 6) break;
                        uint32_t other_id = 0;
                        memcpy(&other_id, msg_buf + 1, 4);
                        uint8_t st = msg_buf[5];
                        SocialFriendRel rel = SOCIAL_REL_NONE;
                        if (st == 1) rel = SOCIAL_REL_FRIENDS;
                        else if (st == 2) rel = SOCIAL_REL_OUTGOING;
                        else if (st == 3) rel = SOCIAL_REL_INCOMING;
                        social_set_friend_rel(&g_game.social, other_id, rel);
                    }
                    break;
                }
                case MSG_PLAYER_UNSTUCK: {

                    if (msg_len >= 12) {
                        float cx, cy, cz;
                        memcpy(&cx, msg_buf, 4);
                        memcpy(&cy, msg_buf+4, 4);
                        memcpy(&cz, msg_buf+8, 4);
                        g_game.avatar.pos = (Vec3){cx, cy, cz};
                        g_game.avatar.vel = (Vec3){0, 0, 0};
                        g_game.avatar.death_vel = (Vec3){0, 0, 0};
                        g_game.avatar.step_offset = 0.0f;
                        g_game.avatar.on_ground = false;
                        ragdoll_destroy((RagdollState*)&g_game.ragdoll);
                        if (g_game.avatar.body) {
                            physics_set_position(g_game.physics, g_game.avatar.body, g_game.avatar.pos);
                            physics_set_velocity(g_game.physics, g_game.avatar.body, (Vec3){0, 0, 0});
                            physics_enable_geom(g_game.physics, g_game.avatar.body);
                        }
                        Entity* unstuck_ent = scene_get_entity(&g_game.scene, g_game.avatar.entity);
                        if (unstuck_ent) {
                            unstuck_ent->transform.position = (Vec3){
                                cx, cy - AVATAR_ROOT_HALF_Y, cz
                            };
                        }
                        g_game.camera.target = (Vec3){
                            cx, cy + (AVATAR_CAMERA_ORBIT_Y - AVATAR_ROOT_HALF_Y), cz
                        };
                        g_game.move_lock_timer = 0.5f;
                        g_game.collision_pin_pos = g_game.avatar.pos;
                        g_game.collision_chunk_loading = true;
                        g_game.avatar.freeze_locomotion = true;
                        g_game.collision_load_attempts = 0;
                        g_game.collision_load_attempt_time = 0.0;
                        g_game.collision_spawn_gate = true;
                        collision_lod_force_resync();
                        collision_lod_activate_near_focus();
                        collision_lod_ensure_underfoot();
                    }
                    break;
                }
                case MSG_STARTER_TOOLS: {

                    if (msg_len < 1) break;
                    char keep_name[32] = {0};
                    if (g_game.equipped_tool > 0 && g_game.equipped_tool <= MAX_TOOLS)
                        memcpy(keep_name, g_game.tools[g_game.equipped_tool - 1].name, 32);
                    g_game.tool_count = msg_buf[0];
                    if (g_game.tool_count > MAX_TOOLS) g_game.tool_count = MAX_TOOLS;
                    size_t toff2 = 1;
                    for (int ti = 0; ti < g_game.tool_count && toff2 < msg_len; ti++) {
                        uint8_t nlen = msg_buf[toff2++];
                        if (toff2 + nlen > msg_len) break;
                        size_t cplen = nlen > 31 ? 31 : nlen;
                        memcpy(g_game.tools[ti].name, msg_buf + toff2, cplen);
                        g_game.tools[ti].name[cplen] = '\0';
                        g_game.tools[ti].available = true;
                        toff2 += nlen;
                    }
                    for (int ti = g_game.tool_count; ti < MAX_TOOLS; ti++) {
                        g_game.tools[ti].available = false;
                        g_game.tools[ti].name[0] = '\0';
                    }
                    g_game.equipped_tool = 0;
                    if (keep_name[0]) {
                        for (int ti = 0; ti < g_game.tool_count; ti++) {
                            if (strcmp(g_game.tools[ti].name, keep_name) == 0) {
                                g_game.equipped_tool = ti + 1;
                                break;
                            }
                        }
                    }
                    for (int ti = 0; ti < g_game.tool_count; ti++) {
                        tool_hold_ensure(g_game.tools[ti].name);
                        char tool_path[128];
                        snprintf(tool_path, sizeof(tool_path), "assets/tools/%s.png", g_game.tools[ti].name);
                        platform_load_file(tool_path, on_tool_icon_loaded, (void*)(intptr_t)ti);
                    }
                    break;
                }
                case MSG_OBJECT_UNANCHOR: {

                    if (msg_len < 2) break;
                    uint16_t ucount;
                    memcpy(&ucount, msg_buf, 2);
                    size_t uoff = 2;

                    for (int ui = 0; ui < (int)ucount && uoff + 16 <= msg_len; ui++) {
                        uint32_t oid;
                        float vel[3];
                        memcpy(&oid, msg_buf + uoff, 4); uoff += 4;
                        memcpy(vel, msg_buf + uoff, 12); uoff += 12;
                        (void)vel;

                        for (int ni = 0; ni < g_game.net_object_count; ni++) {
                            if (g_game.net_objects[ni].net_id != oid) continue;
                            g_game.net_objects[ni].connector_static = false;
                            g_game.net_objects[ni].anchored = false;
                            Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
                            if (ent && ent->render_batched) {
                                ent->static_batch = false;
                                ent->render_batched = false;
                                brick_batch_mark_dirty();
                            }
                            break;
                        }
                    }
                    g_has_last_exp = false;
                    break;
                }
                case MSG_PLAY_SOUND: {

                    if (msg_len >= 14) {
                        uint8_t sfx = msg_buf[0];
                        uint8_t flags = msg_buf[1];
                        float sx, sy, sz;
                        memcpy(&sx, msg_buf + 2, 4);
                        memcpy(&sy, msg_buf + 6, 4);
                        memcpy(&sz, msg_buf + 10, 4);
                        if (flags & 1)
                            audio_play_at((int)sfx, sx, sy, sz);
                        else
                            audio_play((int)sfx);
                    } else if (msg_len >= 13) {

                        uint8_t sfx = msg_buf[0];
                        float sx, sy, sz;
                        memcpy(&sx, msg_buf + 1, 4);
                        memcpy(&sy, msg_buf + 5, 4);
                        memcpy(&sz, msg_buf + 9, 4);
                        audio_play_at((int)sfx, sx, sy, sz);
                    }
                    break;
                }
                case MSG_MUSIC: {

                    if (msg_len < 1) break;
                    uint8_t action = msg_buf[0];
                    if (action == 0) {
                        audio_stop_music();
                    } else if (action == 1 && msg_len >= 5) {
                        float vol;
                        memcpy(&vol, msg_buf + 1, 4);
                        size_t ulen = msg_len - 5;
                        if (ulen > 255) ulen = 255;
                        char url[256];
                        memcpy(url, msg_buf + 5, ulen);
                        url[ulen] = '\0';
                        audio_play_music(url, vol);
                    }
                    break;
                }
                case MSG_UI: {

                    if (msg_len < 4) break;
                    uint8_t kind = msg_buf[0];
                    uint16_t dcs = (uint16_t)msg_buf[1] | ((uint16_t)msg_buf[2] << 8);
                    uint8_t tlen = msg_buf[3];
                    if ((size_t)(4 + tlen) > msg_len) break;
                    char text[201];
                    if (tlen > 200) tlen = 200;
                    if (tlen > 0) memcpy(text, msg_buf + 4, tlen);
                    text[tlen] = '\0';

                    if (kind == 2) {
                        g_game.script_ui.toast_out = true;
                        g_game.script_ui.hud_out = true;
                    } else if (kind == 0) {
                        strncpy(g_game.script_ui.toast, text, sizeof(g_game.script_ui.toast) - 1);
                        g_game.script_ui.toast[sizeof(g_game.script_ui.toast) - 1] = '\0';
                        float secs = (dcs > 0) ? (dcs / 100.0f) : 3.0f;
                        g_game.script_ui.toast_timer = secs;
                        g_game.script_ui.toast_out = false;
                        g_game.script_ui.toast_alpha = 0.0f;
                        g_game.script_ui.toast_yoff = 28.0f;
                    } else if (kind == 1) {
                        bool had_hud = g_game.script_ui.hud[0] &&
                                       !g_game.script_ui.hud_out &&
                                       g_game.script_ui.hud_alpha > 0.5f;
                        strncpy(g_game.script_ui.hud, text, sizeof(g_game.script_ui.hud) - 1);
                        g_game.script_ui.hud[sizeof(g_game.script_ui.hud) - 1] = '\0';
                        if (dcs == 0)
                            g_game.script_ui.hud_timer = -1.0f;
                        else
                            g_game.script_ui.hud_timer = dcs / 100.0f;
                        g_game.script_ui.hud_out = false;

                        g_game.script_ui.hud_alpha = had_hud ? 1.0f : 0.0f;
                    }
                    break;
                }
                case MSG_EXPLOSION: {

                    if (msg_len >= 16) {
                        float ex, ey, ez, er;
                        memcpy(&ex, msg_buf, 4);
                        memcpy(&ey, msg_buf + 4, 4);
                        memcpy(&ez, msg_buf + 8, 4);
                        memcpy(&er, msg_buf + 12, 4);
                        spawn_explosion(ex, ey, ez, er);
                        g_last_exp[0] = ex; g_last_exp[1] = ey; g_last_exp[2] = ez; g_last_exp[3] = er;
                        g_has_last_exp = true;
                        if (g_game.physics)
                            physics_break_connectors_in_radius(g_game.physics, (Vec3){ex, ey, ez}, er);
                        if (g_game.net_proto >= PW_PROTO_NETOWN)
                            netown_scan_broken_conns();

                        if (msg_len >= 29 && !g_game.avatar.dead) {
                            float pvx, pvy, pvz;
                            memcpy(&pvx, msg_buf + 16, 4);
                            memcpy(&pvy, msg_buf + 20, 4);
                            memcpy(&pvz, msg_buf + 24, 4);
                            uint8_t dmg = msg_buf[28];
                            g_game.avatar.vel.x += pvx;
                            g_game.avatar.vel.y += pvy;
                            g_game.avatar.vel.z += pvz;
                            g_game.avatar.on_ground = false;
                        }
                    }
                    break;
                }
                case MSG_REMOTE_EVENT: {

                    if (msg_len < 6) break;
                    uint32_t sender_pid2;
                    memcpy(&sender_pid2, msg_buf, 4);
                    uint8_t ename_len = msg_buf[4];
                    if (5 + ename_len > msg_len) break;
                    char ename[64] = {0};
                    size_t enl = ename_len > 63 ? 63 : ename_len;
                    memcpy(ename, msg_buf + 5, enl);
                    const uint8_t* edata = msg_buf + 5 + ename_len;
                    size_t edata_len = msg_len - (5 + ename_len);

                    if (sender_pid2 == 0) {
                        if (g_game.scripts)
                            client_script_fire_message_from_server(g_game.scripts, ename,
                                                                   edata, edata_len);
                        break;
                    }

                    if (strcmp(ename, "PlayerDied") == 0) {
                        for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
                            if (!g_game.remote_players[rp].active) continue;
                            if (g_game.remote_players[rp].id != sender_pid2) continue;
                            if (g_game.remote_players[rp].dead) break;
                            g_game.remote_players[rp].dead = true;
                            g_game.remote_players[rp].anim.state = ANIM_STATE_DEAD;
                            Entity* rent = scene_get_entity(&g_game.scene, g_game.remote_players[rp].entity);
                            if (rent && !g_game.remote_players[rp].ragdoll.active) {
                                ragdoll_spawn((RagdollState*)&g_game.remote_players[rp].ragdoll,
                                              &g_game.remote_players[rp].anim,
                                              g_game.remote_players[rp].accessory,
                                              rent->transform.position,
                                              rent->transform.rotation.y,
                                              g_game.remote_players[rp].last_vel);
                            }
                            break;
                        }
                    }
                    break;
                }
                case MSG_DAMAGE: {

                    if (msg_len < 4) break;
                    uint32_t new_health = *(uint32_t*)msg_buf;
                    bool was_alive = !g_game.avatar.dead;
                    avatar_set_health(&g_game.avatar, (int)new_health);

                    if (was_alive && g_game.avatar.dead &&
                        g_game.multiplayer && g_game.net.state == NET_STATE_CONNECTED) {
                        uint8_t evt_buf[32];
                        const char* evt_name = "PlayerDied";
                        uint8_t name_len = (uint8_t)strlen(evt_name);
                        evt_buf[0] = name_len;
                        memcpy(evt_buf + 1, evt_name, name_len);
                        net_client_send(&g_game.net, MSG_REMOTE_EVENT, evt_buf, 1 + name_len);
                    }
                    break;
                }
                case MSG_CLIENT_CAPS: {
                    if (msg_len < 1) break;
                    g_game.allow_freecam = (msg_buf[0] & CLIENT_CAP_FREECAM) != 0;

                    g_game.reset_enabled = (msg_buf[0] & CLIENT_CAP_RESET_DISABLED) == 0;
                    break;
                }
                case MSG_PROTOCOL: {
                    if (msg_len < 8) break;
                    uint16_t srv = 0, minp = 0;
                    memcpy(&srv, msg_buf, 2);
                    memcpy(&minp, msg_buf + 2, 2);
                    memcpy(&g_game.local_player_id, msg_buf + 4, 4);
                    uint16_t ack = (uint16_t)PW_PROTO_CURRENT;
                    if (ack > srv) ack = srv;
                    if (ack < minp) ack = minp;
                    g_game.net_proto = ack;
                    client_send_protocol_ack();
                    break;
                }
                case MSG_CONSTRAINTS: {
                    if (client_netown_lite()) break;
                    if (msg_len < 2) break;
                    uint16_t n = 0;
                    memcpy(&n, msg_buf, 2);
                    size_t need = 2u + (size_t)n * PW_CONSTRAINT_WIRE_SIZE;
                    if (n == 0 || need > msg_len) break;
                    if (g_game.physics) {
                        for (int ci = 0; ci < g_game.net_constraint_count; ci++) {
                            if (g_game.net_constraints[ci].local_conn)
                                physics_destroy_connector(g_game.physics,
                                                          g_game.net_constraints[ci].local_conn);
                        }
                    }
                    g_game.net_constraint_count = 0;
                    memset(g_game.net_constraints, 0, sizeof(g_game.net_constraints));
                    size_t off = 2;
                    int stored = 0;
                    int maxc = n < MAX_NET_CONSTRAINTS ? n : MAX_NET_CONSTRAINTS;
                    for (int i = 0; i < maxc; i++) {
                        uint32_t ida = 0, idb = 0;
                        memcpy(&ida, msg_buf + off, 4); off += 4;
                        memcpy(&idb, msg_buf + off, 4); off += 4;
                        uint8_t typ = msg_buf[off++];
                        uint8_t pset = msg_buf[off++];
                        Vec3 axis, point;
                        memcpy(&axis, msg_buf + off, 12); off += 12;
                        memcpy(&point, msg_buf + off, 12); off += 12;
                        float limn, limx, stf, dmp, mot, trq;
                        memcpy(&limn, msg_buf + off, 4); off += 4;
                        memcpy(&limx, msg_buf + off, 4); off += 4;
                        memcpy(&stf, msg_buf + off, 4); off += 4;
                        memcpy(&dmp, msg_buf + off, 4); off += 4;
                        memcpy(&mot, msg_buf + off, 4); off += 4;
                        memcpy(&trq, msg_buf + off, 4); off += 4;
                        if (netown_conn_break_ignored(ida, idb)) continue;
                        g_game.net_constraints[stored].id_a = ida;
                        g_game.net_constraints[stored].id_b = idb;
                        g_game.net_constraints[stored].desc.type = typ;
                        g_game.net_constraints[stored].desc.point_set = pset;
                        g_game.net_constraints[stored].desc.axis = axis;
                        g_game.net_constraints[stored].desc.point = point;
                        g_game.net_constraints[stored].desc.limits_min = limn;
                        g_game.net_constraints[stored].desc.limits_max = limx;
                        g_game.net_constraints[stored].desc.stiffness = stf;
                        g_game.net_constraints[stored].desc.damping = dmp;
                        g_game.net_constraints[stored].desc.motor = mot;
                        g_game.net_constraints[stored].desc.torque = trq;
                        g_game.net_constraints[stored].active = true;
                        g_game.net_constraints[stored].local_conn = 0;
                        stored++;
                    }
                    g_game.net_constraint_count = stored;
                    s_netown_need_sync = true;
                    break;
                }
                case MSG_NET_OWNER: {
                    if (msg_len < 2) break;
                    uint16_t n = 0;
                    memcpy(&n, msg_buf, 2);
                    if (2u + (size_t)n * 8u > msg_len) break;
                    size_t off = 2;
                    for (uint16_t k = 0; k < n; k++) {
                        uint32_t oid = 0, opid = 0;
                        memcpy(&oid, msg_buf + off, 4);
                        memcpy(&opid, msg_buf + off + 4, 4);
                        off += 8;
                        int ni = net_find_ni(oid);
                        if (ni < 0) continue;
                        bool mine = (g_game.local_player_id != 0 && opid == g_game.local_player_id);
                        netown_set(ni, mine);
                    }
                    break;
                }
                case MSG_PART_STATE: {
                    if (msg_len < 2) break;
                    uint16_t n = 0;
                    memcpy(&n, msg_buf, 2);
                    size_t off = 2;
                    for (uint16_t k = 0; k < n; k++) {
                        if (off + 5 > msg_len) break;
                        uint32_t oid = 0;
                        memcpy(&oid, msg_buf + off, 4);
                        uint8_t status = msg_buf[off + 4];
                        if (status == PW_PART_STATUS_DELETED) {
                            off += 5;
                            net_remove_object(oid);
                            continue;
                        }
                        bool apply_vel = (status == PW_PART_STATUS_ALIVE_VEL);
                        bool force_correct = (status == PW_PART_STATUS_CORRECT);
                        if (force_correct) apply_vel = true;
                        if (status != PW_PART_STATUS_ALIVE && !apply_vel && !force_correct) break;
                        if (off + PW_PART_STATE_ALIVE_BYTES > msg_len) break;
                        uint8_t otype = msg_buf[off + 5];
                        uint8_t anchored = msg_buf[off + 6];
                        uint8_t can_col = msg_buf[off + 7];
                        float pos[3], rot[3], size[3], color[3], vel[3], ang[3];
                        memcpy(pos, msg_buf + off + 8, 12);
                        memcpy(rot, msg_buf + off + 20, 12);
                        memcpy(size, msg_buf + off + 32, 12);
                        memcpy(color, msg_buf + off + 44, 12);
                        memcpy(vel, msg_buf + off + 56, 12);
                        memcpy(ang, msg_buf + off + 68, 12);
                        uint32_t owner_pid = 0;
                        memcpy(&owner_pid, msg_buf + off + 80, 4);
                        off += PW_PART_STATE_ALIVE_BYTES;
                        net_apply_part_alive(oid, otype, anchored, can_col, pos, rot, size, color,
                                             vel, ang, owner_pid, apply_vel, force_correct);
                    }
                    break;
                }
                case MSG_CAMERA_MODE: {
                    if (msg_len < 1) break;
                    uint8_t mode = msg_buf[0];
                    uint8_t prev = g_game.camera_mode;
                    g_game.camera_mode = mode;
                    if (mode == CAM_MODE_FREECAM && prev != CAM_MODE_FREECAM) {
                        freecam_init_from_camera();
                    }
                    break;
                }
                case MSG_PLAYER_BADGES: {
                    if (msg_len < 5) break;
                    uint32_t pid;
                    memcpy(&pid, msg_buf, 4);
                    uint8_t badges = msg_buf[4];
                    bool found = false;
                    for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
                        if (g_game.remote_players[rp].active &&
                            g_game.remote_players[rp].id == pid) {
                            g_game.remote_players[rp].badges = badges;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        g_game.local_badges = badges;
                    }
                    break;
                }
                case MSG_PLAYER_VISUAL: {

                    if (msg_len < 9) break;
                    uint32_t pid;
                    memcpy(&pid, msg_buf, 4);
                    float transparency = msg_buf[4] / 255.0f;
                    bool has_nc = (msg_buf[5] & 0x01) != 0;
                    float ncr = msg_buf[6] / 255.0f;
                    float ncg = msg_buf[7] / 255.0f;
                    float ncb = msg_buf[8] / 255.0f;
                    bool found = false;
                    for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
                        if (g_game.remote_players[rp].active &&
                            g_game.remote_players[rp].id == pid) {
                            g_game.remote_players[rp].transparency = transparency;
                            g_game.remote_players[rp].has_name_color = has_nc;
                            g_game.remote_players[rp].name_color_r = ncr;
                            g_game.remote_players[rp].name_color_g = ncg;
                            g_game.remote_players[rp].name_color_b = ncb;
                            chat_set_name_color_override(&g_game.chat,
                                g_game.remote_players[rp].name, has_nc, ncr, ncg, ncb);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        g_game.local_transparency = transparency;
                        g_game.local_has_name_color = has_nc;
                        g_game.local_name_color_r = ncr;
                        g_game.local_name_color_g = ncg;
                        g_game.local_name_color_b = ncb;
                        if (g_game.username[0]) {
                            chat_set_name_color_override(&g_game.chat,
                                g_game.username, has_nc, ncr, ncg, ncb);
                        }
                    }
                    break;
                }
                case MSG_OBJECT_COLLIDE: {

                    if (msg_len < 5) break;
                    uint32_t oid;
                    memcpy(&oid, msg_buf, 4);
                    bool can_collide = msg_buf[4] != 0;
                    bool found = false;
                    for (int ni = 0; ni < g_game.net_object_count; ni++) {
                        if (g_game.net_objects[ni].net_id != oid) continue;
                        found = true;
                        g_game.net_objects[ni].collide_wanted = can_collide;
                        if (!can_collide) {
                            net_brick_deactivate_collision(ni);
                        } else {

                            Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[ni].entity);
                            bool always = net_brick_always_keep_collision(ni);
                            bool nearby = false;
                            if (ent) {
                                nearby = net_brick_lod_dist(ni, ent, g_game.avatar.pos)
                                    <= COLLISION_CHUNK_KEEP;
                            }
                            if (always || nearby || !g_game.world_colliders_ready)
                                net_brick_activate_collision(ni);
                        }
                        break;
                    }

                    if (!found) {
                        if (!can_collide) pending_nocollide_add(oid);
                        else pending_nocollide_remove(oid);
                    }
                    break;
                }
                case MSG_OBJECT_CLICKABLE: {
                    if (msg_len < 5) break;
                    uint32_t oid;
                    memcpy(&oid, msg_buf, 4);
                    bool clickable = msg_buf[4] != 0;
                    bool found = false;
                    for (int ni = 0; ni < g_game.net_object_count; ni++) {
                        if (g_game.net_objects[ni].net_id != oid) continue;
                        found = true;
                        g_game.net_objects[ni].clickable = clickable;
                        break;
                    }
                    if (!found) {
                        if (clickable) pending_clickable_add(oid);
                        else pending_clickable_remove(oid);
                    }
                    break;
                }
                case MSG_OBJECT_MATERIAL: {
                    if (msg_len < 4) break;
                    uint32_t n = 0;
                    memcpy(&n, msg_buf, 4);
                    size_t offm = 4;
                    for (uint32_t i = 0; i < n && offm + 5 <= msg_len; i++) {
                        uint32_t oid = 0;
                        memcpy(&oid, msg_buf + offm, 4);
                        uint8_t mat = msg_buf[offm + 4];
                        offm += 5;
                        apply_net_part_material(oid, mat);
                    }
                    break;
                }
                case MSG_OBJECT_MESH: {
                    if (msg_len < 4) break;
                    uint32_t n = 0;
                    memcpy(&n, msg_buf, 4);
                    size_t offm = 4;
                    size_t pairs = 4 + (size_t)n * 8;
                    int have_col = (msg_len >= pairs + n);
                    for (uint32_t i = 0; i < n && offm + 8 <= msg_len; i++) {
                        uint32_t oid = 0, mid = 0;
                        memcpy(&oid, msg_buf + offm, 4);
                        memcpy(&mid, msg_buf + offm + 4, 4);
                        offm += 8;
                        uint8_t col = 0;
                        if (have_col) col = msg_buf[pairs + i];
                        apply_net_part_mesh(oid, mid, col);
                    }
                    break;
                }
                case MSG_OBJECT_DECAL: {
                    if (msg_len < 4) break;
                    uint32_t n = 0;
                    memcpy(&n, msg_buf, 4);
                    size_t rec = 4 + (size_t)n * 10;
                    int have_tile = (msg_len >= rec + (size_t)n * 8);
                    size_t offd = 4;
                    for (uint32_t i = 0; i < n && offd + 10 <= msg_len; i++) {
                        uint32_t parent = 0, tex = 0;
                        memcpy(&parent, msg_buf + offd, 4);
                        memcpy(&tex, msg_buf + offd + 4, 4);
                        uint8_t mode = msg_buf[offd + 8];
                        uint8_t face = msg_buf[offd + 9];
                        offd += 10;
                        float tx = 1.0f, ty = 1.0f;
                        if (have_tile) {
                            memcpy(&tx, msg_buf + rec + (size_t)i * 8, 4);
                            memcpy(&ty, msg_buf + rec + (size_t)i * 8 + 4, 4);
                        }
                        spawn_net_decal(parent, tex, mode, face, tx, ty);
                    }
                    break;
                }
                case MSG_SCOREBOARD: {

                    if (msg_len < 4) break;
                    const uint8_t* p = (const uint8_t*)msg_buf;
                    size_t off = 0;
                    uint8_t ver = p[off++];
                    if (ver != 1) break;
                    memset(&g_game.scoreboard, 0, sizeof(g_game.scoreboard));
                    g_game.scoreboard.active = true;

                    if (off >= (size_t)msg_len) break;
                    uint8_t tcount = p[off++];
                    if (tcount > CHAT_PL_TEAM_MAX) tcount = CHAT_PL_TEAM_MAX;
                    for (int ti = 0; ti < (int)tcount; ti++) {
                        if (off >= (size_t)msg_len) break;
                        uint8_t nlen = p[off++];
                        if (off + nlen + 3 > (size_t)msg_len) break;
                        size_t copy = nlen < 31 ? nlen : 31;
                        memcpy(g_game.scoreboard.teams[ti].name, p + off, copy);
                        g_game.scoreboard.teams[ti].name[copy] = '\0';
                        off += nlen;
                        g_game.scoreboard.teams[ti].r = p[off++] / 255.0f;
                        g_game.scoreboard.teams[ti].g = p[off++] / 255.0f;
                        g_game.scoreboard.teams[ti].b = p[off++] / 255.0f;
                    }
                    g_game.scoreboard.team_count = (int)tcount;

                    if (off >= (size_t)msg_len) break;
                    uint8_t scount = p[off++];
                    if (scount > CHAT_PL_STAT_MAX) scount = CHAT_PL_STAT_MAX;
                    for (int si = 0; si < (int)scount; si++) {
                        if (off >= (size_t)msg_len) break;
                        uint8_t nlen = p[off++];
                        if (off + nlen > (size_t)msg_len) break;
                        size_t copy = nlen < 23 ? nlen : 23;
                        memcpy(g_game.scoreboard.stat_names[si], p + off, copy);
                        g_game.scoreboard.stat_names[si][copy] = '\0';
                        off += nlen;
                    }
                    g_game.scoreboard.stat_count = (int)scount;

                    if (off >= (size_t)msg_len) break;
                    uint8_t pcount = p[off++];
                    if (pcount > CHAT_PL_MAX) pcount = CHAT_PL_MAX;
                    int ec = 0;
                    for (int pi = 0; pi < (int)pcount; pi++) {
                        if (off + 5 > (size_t)msg_len) break;
                        uint32_t pid;
                        memcpy(&pid, p + off, 4); off += 4;
                        int8_t tidx = (int8_t)p[off++];
                        g_game.scoreboard.entries[ec].player_id = pid;
                        g_game.scoreboard.entries[ec].team_idx = tidx;
                        for (int s = 0; s < (int)scount; s++) {
                            if (off + 4 > (size_t)msg_len) break;
                            float v;
                            memcpy(&v, p + off, 4); off += 4;
                            g_game.scoreboard.entries[ec].stats[s] = v;
                        }
                        ec++;
                    }
                    g_game.scoreboard.entry_count = ec;
                    break;
                }

                case MSG_VR: {
                    uint32_t vpid = 0;
                    PwVrPose pose;
                    if (!pw_vr_unpack_sc(msg_buf, (size_t)msg_len, &vpid, &pose)) break;
                    if (vpid == 0 || vpid == g_game.local_player_id) break;
                    for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
                        if (!g_game.remote_players[rp].active ||
                            g_game.remote_players[rp].id != vpid)
                            continue;
                        g_game.remote_players[rp].is_vr =
                            (pose.flags & PW_VR_FLAG_ACTIVE) != 0;
                        g_game.remote_players[rp].vr_pose = pose;
                        if (pose.flags & PW_VR_FLAG_CALIB)
                            vr_ik_calib_from_pose(&g_game.remote_players[rp].vr_calib, &pose);
                        else if (!g_game.remote_players[rp].vr_calib.ready)
                            vr_ik_calib_defaults(&g_game.remote_players[rp].vr_calib);
                        break;
                    }
                    break;
                }

                default:
                    break;
            }
        }

        netown_flush_sync();

        if (g_game.multiplayer && g_game.net_proto >= PW_PROTO_NETOWN &&
            g_game.net.state == NET_STATE_CONNECTED && g_game.world_ready)
            netown_send_poses();

        if (g_game.net.state == NET_STATE_CONNECTED &&
            g_game.world_ready && !g_game.avatar.dead &&
            g_game.camera_mode != CAM_MODE_FREECAM) {
            float send_yaw = g_game.avatar.current_yaw - 270.0f;
            uint8_t pos_buf[72];
            float px = g_game.avatar.pos.x, py = g_game.avatar.pos.y, pz = g_game.avatar.pos.z;
            memcpy(pos_buf, &px, 4);
            memcpy(pos_buf + 4, &py, 4);
            memcpy(pos_buf + 8, &pz, 4);
            memcpy(pos_buf + 12, &send_yaw, 4);
            pos_buf[16] = (uint8_t)g_game.avatar_anim.state;
            uint32_t send_emote = (g_game.avatar_anim.state == ANIM_STATE_EMOTE)
                ? g_game.active_emote_id : 0u;
            memcpy(pos_buf + 17, &send_emote, 4);
            const char* hold = local_held_tool_name();
            uint8_t tlen = 0;
            if (hold) {
                size_t n = strlen(hold);
                if (n > 31) n = 31;
                tlen = (uint8_t)n;
            }
            pos_buf[21] = tlen;
            if (tlen) memcpy(pos_buf + 22, hold, tlen);
            size_t plen = (size_t)(22 + tlen);
            float ix = g_game.avatar.move_intent_x, iz = g_game.avatar.move_intent_z;
            memcpy(pos_buf + plen, &ix, 4);
            memcpy(pos_buf + plen + 4, &iz, 4);
            net_client_send(&g_game.net, MSG_PLAYER_INPUT, pos_buf, plen + 8);
            vr_send_local_pose();
            vidactor_record_move(px, py, pz, g_game.avatar.current_yaw,
                                 (uint8_t)g_game.avatar_anim.state);
        }
    }

    if (g_game.multiplayer &&
        g_game.net.state == NET_STATE_DISCONNECTED &&
        g_game.net.recv_len == 0 &&
        !g_game.show_login && !g_game.show_disconnect && !g_game.show_kick) {
#ifndef __EMSCRIPTEN__
        begin_disconnect("Lost connection to the server.");
#else
        audio_stop_music();
        g_game.show_disconnect = true;
        g_game.multiplayer = false;
#endif
    }

    stream_world_init_objects();
    poll_await_batch_ready();
    if (g_game.world_ready)
        flush_pending_scripts(1);

    if (show_loading) {
        renderer_begin_frame(&g_game.renderer);
#ifndef __EMSCRIPTEN__
        if (g_game.show_kick || g_game.show_disconnect) {
            const char* title = g_game.show_kick ? "Kicked" : "Disconnected";
            const char* sub = g_game.show_kick
                ? (g_game.kick_reason[0] ? g_game.kick_reason : "You were removed from the game.")
                : disconnect_subtitle();
            bool leave = draw_load_overlay(title, sub, true, (float)dt);
            renderer_end_frame(&g_game.renderer);
            if (leave) {
                g_game.show_kick = false;
                g_game.show_disconnect = false;
                g_game.disconnect_reason[0] = '\0';
                leave_game_ui();
                g_game.loading_time = 0.0f;
            }
            input_pre_frame();
            input_post_frame();
            return;
        }
#endif
        bool show_leave = g_game.loading_time > 8.0f;
        bool leave = draw_load_overlay("Loading...", NULL, show_leave, (float)dt);
        renderer_end_frame(&g_game.renderer);
        if (leave) {
            audio_stop_music();
            net_client_disconnect(&g_game.net);
            g_game.multiplayer = false;
            g_game.loading_world = false;
            g_game.world_ready = false;
            g_game.loading_time = 0.0f;
            free_world_init_stream();
            memset(&g_game.script_ui, 0, sizeof(g_game.script_ui));
            memset(&g_game.music_cred, 0, sizeof(g_game.music_cred));
#ifndef __EMSCRIPTEN__
            leave_game_ui();
#else
            EM_ASM({
                if (window.parent && window.parent !== window) {
                    window.parent.postMessage({type: 'leave_game'}, '*');
                } else {
                    window.location.href = '/';
                }
            });
#endif
        }
        input_pre_frame();
        input_post_frame();
        return;
    }

    {
        const char* pending = chat_get_pending_send(&g_game.chat);
        if (pending) {

            bool handled = false;
            if (strcmp(pending, "/dance") == 0) {

                int r = rand() % 3;
                g_game.avatar_anim.state = ANIM_STATE_DANCING + r;
                handled = true;
            } else if (strcmp(pending, "/dance1") == 0) {
                g_game.avatar_anim.state = ANIM_STATE_DANCING;
                handled = true;
            } else if (strcmp(pending, "/dance2") == 0) {
                g_game.avatar_anim.state = ANIM_STATE_DANCING2;
                handled = true;
            } else if (strcmp(pending, "/dance3") == 0) {
                g_game.avatar_anim.state = ANIM_STATE_DANCING3;
                handled = true;
            } else if (!g_game.multiplayer && g_game.allow_freecam &&
                       (strcmp(pending, "/freecam") == 0 || strcmp(pending, "/freefly") == 0)) {

                uint8_t want = (strcmp(pending, "/freefly") == 0) ? CAM_MODE_FREEFLY : CAM_MODE_FREECAM;
                uint8_t prev = g_game.camera_mode;
                if (g_game.camera_mode == want) {
                    g_game.camera_mode = CAM_MODE_NORMAL;
                } else {
                    g_game.camera_mode = want;
                    if (want == CAM_MODE_FREECAM && prev != CAM_MODE_FREECAM) {
                        freecam_init_from_camera();
                    }
                }
                handled = true;
            } else if (g_game.allow_freecam &&
                       (strcmp(pending, "/freezecull") == 0 || strcmp(pending, "/freezeculling") == 0)) {
                bool on = !renderer_get_frustum_cull_frozen(&g_game.renderer);
                renderer_set_frustum_cull_frozen(&g_game.renderer, on);
                chat_add_message(&g_game.chat, on
                    ? "Frustum culling frozen (move camera to inspect)"
                    : "Frustum culling unfrozen");
                handled = true;
            }
            #ifdef BE_EVIL
            else if (strcmp(pending, "/evil tp x") == 0) {
                g_game.avatar.pos.x += 100;
            } else if (strcmp(pending, "/evil tp y") == 0) {
                g_game.avatar.pos.y += 100;
            } else if (strcmp(pending, "/evil tp z") == 0) {
                g_game.avatar.pos.z += 100;
            } else if (strcmp(pending, "/evil speedup") == 0) {
                g_game.avatar.walk_speed *= 1.25;
            } else if (strcmp(pending, "/evil jumpup") == 0) {
                g_game.avatar.jump_impulse *= 1.25;
            }
            #endif

            if (vidactor_handle_chat(pending)) {
                handled = true;
            }

            if (!handled && g_game.multiplayer && g_game.net.state == NET_STATE_CONNECTED) {
                net_client_send(&g_game.net, MSG_CHAT, (const uint8_t*)pending, strlen(pending));
                vidactor_record_chat(pending);
            }
            chat_clear_pending(&g_game.chat);
        }
    }

    if (!hud_hidden())
    {
        int sw = g_game.renderer.canvas_width;
        int sh = g_game.renderer.canvas_height;
        float list_left = -1.0f;
        float list_top = 10.0f * (g_game.ui_scale > 0.1f ? g_game.ui_scale : 1.0f);

        if (g_game.multiplayer && g_game.chat.initialized) {
            if (g_game.scoreboard.active &&
                (g_game.scoreboard.team_count > 0 || g_game.scoreboard.stat_count > 0)) {
                ChatPlayerListEntry rows[CHAT_PL_MAX];
                memset(rows, 0, sizeof(rows));
                int row_count = 0;

                typedef struct { uint32_t pid; const char* name; uint8_t badges; bool has_nc; float nr, ng, nb; } Roster;
                Roster roster[CHAT_PL_MAX];
                int roster_n = 0;
                roster[roster_n++] = (Roster){ g_game.local_player_id, g_game.username, g_game.local_badges,
                    g_game.local_has_name_color, g_game.local_name_color_r,
                    g_game.local_name_color_g, g_game.local_name_color_b };
                for (int rp = 0; rp < MAX_REMOTE_PLAYERS && roster_n < CHAT_PL_MAX; rp++) {
                    if (!g_game.remote_players[rp].active) continue;
                    if (g_game.local_player_id != 0 &&
                        g_game.remote_players[rp].id == g_game.local_player_id)
                        continue;
                    roster[roster_n++] = (Roster){
                        g_game.remote_players[rp].id,
                        g_game.remote_players[rp].name,
                        g_game.remote_players[rp].badges,
                        g_game.remote_players[rp].has_name_color,
                        g_game.remote_players[rp].name_color_r,
                        g_game.remote_players[rp].name_color_g,
                        g_game.remote_players[rp].name_color_b
                    };
                }

                uint32_t remote_pids[CHAT_PL_MAX];
                int remote_pid_n = 0;
                for (int r = 0; r < roster_n; r++) {
                    if (roster[r].pid == 0) continue;
                    if (g_game.local_player_id != 0 && roster[r].pid == g_game.local_player_id)
                        continue;
                    if (remote_pid_n < CHAT_PL_MAX)
                        remote_pids[remote_pid_n++] = roster[r].pid;
                }

                int used[CHAT_PL_MAX];
                int placed[CHAT_PL_MAX];
                memset(used, 0, sizeof(used));
                memset(placed, 0, sizeof(placed));

                if (g_game.scoreboard.team_count > 0) {
                    for (int ti = 0; ti < g_game.scoreboard.team_count && row_count < CHAT_PL_MAX; ti++) {
                        rows[row_count].name = g_game.scoreboard.teams[ti].name;
                        rows[row_count].team_idx = -2;
                        rows[row_count].has_name_color = true;
                        rows[row_count].name_r = g_game.scoreboard.teams[ti].r;
                        rows[row_count].name_g = g_game.scoreboard.teams[ti].g;
                        rows[row_count].name_b = g_game.scoreboard.teams[ti].b;
                        row_count++;

                        for (int r = 0; r < roster_n && row_count < CHAT_PL_MAX; r++) {
                            int sbi = scoreboard_find_entry(roster[r].pid, remote_pids, remote_pid_n, used);
                            int8_t tidx = (sbi >= 0) ? g_game.scoreboard.entries[sbi].team_idx : (int8_t)-1;
                            if (tidx != (int8_t)ti) continue;
                            if (sbi >= 0) used[sbi] = 1;
                            placed[r] = 1;
                            rows[row_count].name = roster[r].name;
                            rows[row_count].badges = roster[r].badges;
                            rows[row_count].player_id = roster[r].pid;
                            rows[row_count].team_idx = ti;

                            rows[row_count].has_name_color = true;
                            rows[row_count].name_r = g_game.scoreboard.teams[ti].r;
                            rows[row_count].name_g = g_game.scoreboard.teams[ti].g;
                            rows[row_count].name_b = g_game.scoreboard.teams[ti].b;
                            rows[row_count].stat_count = g_game.scoreboard.stat_count;
                            if (sbi >= 0) {
                                memcpy(rows[row_count].stats, g_game.scoreboard.entries[sbi].stats,
                                       sizeof(float) * (size_t)g_game.scoreboard.stat_count);
                            }
                            row_count++;
                        }
                    }

                    int no_team_hdr = -1;
                    for (int r = 0; r < roster_n && row_count < CHAT_PL_MAX; r++) {
                        if (placed[r]) continue;
                        int sbi = scoreboard_find_entry(roster[r].pid, remote_pids, remote_pid_n, used);
                        int8_t tidx = (sbi >= 0) ? g_game.scoreboard.entries[sbi].team_idx : (int8_t)-1;
                        if (tidx >= 0 && tidx < g_game.scoreboard.team_count) continue;
                        if (no_team_hdr < 0 && row_count < CHAT_PL_MAX) {
                            no_team_hdr = row_count;
                            rows[row_count].name = "No Team";
                            rows[row_count].team_idx = -2;
                            rows[row_count].has_name_color = true;

                            rows[row_count].name_r = 0.72f;
                            rows[row_count].name_g = 0.72f;
                            rows[row_count].name_b = 0.78f;
                            row_count++;
                        }
                        if (row_count >= CHAT_PL_MAX) break;
                        if (sbi >= 0) used[sbi] = 1;
                        rows[row_count].name = roster[r].name;
                        rows[row_count].badges = roster[r].badges;
                        rows[row_count].player_id = roster[r].pid;
                        rows[row_count].team_idx = -1;
                        rows[row_count].has_name_color = roster[r].has_nc;
                        rows[row_count].name_r = roster[r].nr;
                        rows[row_count].name_g = roster[r].ng;
                        rows[row_count].name_b = roster[r].nb;
                        rows[row_count].stat_count = g_game.scoreboard.stat_count;
                        if (sbi >= 0) {
                            memcpy(rows[row_count].stats, g_game.scoreboard.entries[sbi].stats,
                                   sizeof(float) * (size_t)g_game.scoreboard.stat_count);
                        }
                        row_count++;
                    }
                } else {
                    for (int r = 0; r < roster_n && row_count < CHAT_PL_MAX; r++) {
                        int sbi = scoreboard_find_entry(roster[r].pid, remote_pids, remote_pid_n, used);
                        if (sbi >= 0) used[sbi] = 1;
                        rows[row_count].name = roster[r].name;
                        rows[row_count].badges = roster[r].badges;
                        rows[row_count].player_id = roster[r].pid;
                        rows[row_count].team_idx = -1;
                        rows[row_count].has_name_color = roster[r].has_nc;
                        rows[row_count].name_r = roster[r].nr;
                        rows[row_count].name_g = roster[r].ng;
                        rows[row_count].name_b = roster[r].nb;
                        rows[row_count].stat_count = g_game.scoreboard.stat_count;
                        if (sbi >= 0) {
                            memcpy(rows[row_count].stats, g_game.scoreboard.entries[sbi].stats,
                                   sizeof(float) * (size_t)g_game.scoreboard.stat_count);
                        }
                        row_count++;
                    }
                }

                const char* sn[CHAT_PL_STAT_MAX];
                for (int s = 0; s < g_game.scoreboard.stat_count; s++)
                    sn[s] = g_game.scoreboard.stat_names[s];
                list_left = chat_render_player_list_ex(&g_game.chat, rows, row_count,
                    sn, g_game.scoreboard.stat_count, sw, sh);
            } else {
                const char* names[MAX_REMOTE_PLAYERS + 1];
                uint8_t badges[MAX_REMOTE_PLAYERS + 1];
                int name_count = 0;
                names[name_count] = g_game.username;
                badges[name_count] = g_game.local_badges;
                name_count++;
                for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
                    if (g_game.remote_players[rp].active) {
                        names[name_count] = g_game.remote_players[rp].name;
                        badges[name_count] = g_game.remote_players[rp].badges;
                        name_count++;
                    }
                }
                list_left = chat_render_player_list(&g_game.chat, names, badges, name_count, sw, sh);
            }
        }

        if (g_game.chat.initialized && !g_game.avatar.dead) {
            chat_render_health_bar(&g_game.chat, g_game.avatar.health, 100,
                                   list_left, list_top, sw, sh);
        }
    }

    if (!hud_hidden())
        chat_render(&g_game.chat, g_game.renderer.canvas_width, g_game.renderer.canvas_height);

    if (!hud_hidden() && emote_wheel_is_open(&g_game.emote_wheel) && g_game.chat.initialized) {
        emote_wheel_render(&g_game.emote_wheel, &g_game.chat,
                           g_game.renderer.canvas_width, g_game.renderer.canvas_height,
                           g_game.local_equipped_emotes, g_game.local_emote_names);
    }

#if defined(__ANDROID__)

#ifdef VR
    if (!(g_game.vr.active && g_game.vr.openxr))
#endif
    if (touch_controls_enabled()) {
        touch_controls_render(g_game.renderer.canvas_width, g_game.renderer.canvas_height,
                              g_game.ui_scale);
    }
#endif

#ifdef VR
    if (!hud_hidden() && g_game.vr.active && g_game.world_ready && g_game.chat.initialized) {
        const char* hint = g_game.vr.inspect
            ? "VR inspect  mouse=look (body follows)  Q/E+mouse=hands  scroll=reach  RMB=orbit  C=headset  F10=calib"
            : "VR desktop  mouse=head  hold Q/E+mouse=hands  scroll=reach  C=see body  F10=calib";
        float hy = (g_game.show_fps ? 40.0f : 10.0f);
        chat_render_hud_text(&g_game.chat, hint, 10.0f, hy, 1.35f * g_game.ui_scale,
                             g_game.renderer.canvas_width, g_game.renderer.canvas_height);
    }
#endif

    if (!hud_hidden() && g_game.show_fps && g_game.chat.initialized) {
        char fps_buf[128];
        float ms = (g_game.fps_display > 0) ? (1000.0f / (float)g_game.fps_display) : 0.0f;
        uint32_t bodies = g_game.physics ? physics_get_body_count(g_game.physics) : 0;
        uint32_t awake = g_game.physics ? physics_get_active_body_count(g_game.physics) : 0;
        int cx = collision_chunk_coord(g_game.avatar.pos.x);
        int cz = collision_chunk_coord(g_game.avatar.pos.z);
#ifndef __EMSCRIPTEN__
        extern int platform_get_fps_limit(void);
        extern int g_fps_limit_setting;
        int lim = platform_get_fps_limit();
        char lim_buf[16];
        if (g_fps_limit_setting <= 0) snprintf(lim_buf, sizeof(lim_buf), "auto");
        else snprintf(lim_buf, sizeof(lim_buf), "%d", lim);
        snprintf(fps_buf, sizeof(fps_buf), "FPS: %d (%.1fms) lim:%s rs:%.2f bodies:%u/%u chunk:%d,%d%s%s%s%s",
                 g_game.fps_display, ms, lim_buf, g_game.renderer.render_scale, awake, bodies, cx, cz,
                 g_game.show_chunk_borders ? " CHUNKS" : "",
                 g_game.trail_recording ? " TRAIL" : (g_game.trail_count > 0 ? " TRAIL*" : ""),
                 renderer_get_frustum_cull_frozen(&g_game.renderer) ? " CULLFREEZE" : "",
                 g_game.collision_chunk_loading ? " LOADCOL" : "");
#else
        snprintf(fps_buf, sizeof(fps_buf), "FPS: %d (%.1fms) bodies:%u/%u chunk:%d,%d%s%s%s%s",
                 g_game.fps_display, ms, awake, bodies, cx, cz,
                 g_game.show_chunk_borders ? " CHUNKS" : "",
                 g_game.trail_recording ? " TRAIL" : (g_game.trail_count > 0 ? " TRAIL*" : ""),
                 renderer_get_frustum_cull_frozen(&g_game.renderer) ? " CULLFREEZE" : "",
                 g_game.collision_chunk_loading ? " LOADCOL" : "");
#endif
        float fs = 1.5f * g_game.ui_scale;
        chat_render_hud_text(&g_game.chat, fps_buf, 10.0f, 10.0f, fs,
                             g_game.renderer.canvas_width, g_game.renderer.canvas_height);
    }

    if (!hud_hidden() && g_game.chat.initialized) {
        const char* vline = vidactor_status_line();
        if (vline && vline[0]) {
            float uis = g_game.ui_scale > 0.1f ? g_game.ui_scale : 1.0f;
            float y = (float)g_game.renderer.canvas_height - 28.0f * uis;
            chat_render_hud_text(&g_game.chat, vline, 10.0f * uis, y, 1.35f * uis,
                                 g_game.renderer.canvas_width, g_game.renderer.canvas_height);
        }
    }

    if (g_game.chat.initialized) {
        float fdt = (float)dt;

        if (g_game.script_ui.toast[0]) {
            if (!g_game.script_ui.toast_out) {
                g_game.script_ui.toast_alpha += fdt / 0.22f;
                if (g_game.script_ui.toast_alpha > 1.0f) g_game.script_ui.toast_alpha = 1.0f;
                g_game.script_ui.toast_yoff += (0.0f - g_game.script_ui.toast_yoff) * (1.0f - expf(-fdt * 10.0f));
                if (g_game.script_ui.toast_timer > 0.0f) {
                    g_game.script_ui.toast_timer -= fdt;
                    if (g_game.script_ui.toast_timer <= 0.0f) {
                        g_game.script_ui.toast_timer = 0.0f;
                        g_game.script_ui.toast_out = true;
                    }
                }
            } else {
                g_game.script_ui.toast_alpha -= fdt / 0.28f;
                g_game.script_ui.toast_yoff += fdt * 36.0f;
                if (g_game.script_ui.toast_alpha <= 0.0f) {
                    g_game.script_ui.toast_alpha = 0.0f;
                    g_game.script_ui.toast[0] = '\0';
                    g_game.script_ui.toast_out = false;
                    g_game.script_ui.toast_yoff = 0.0f;
                }
            }
        }

        if (g_game.script_ui.hud[0]) {
            if (!g_game.script_ui.hud_out) {
                g_game.script_ui.hud_alpha += fdt / 0.2f;
                if (g_game.script_ui.hud_alpha > 1.0f) g_game.script_ui.hud_alpha = 1.0f;
                if (g_game.script_ui.hud_timer > 0.0f) {
                    g_game.script_ui.hud_timer -= fdt;
                    if (g_game.script_ui.hud_timer <= 0.0f) {
                        g_game.script_ui.hud_timer = 0.0f;
                        g_game.script_ui.hud_out = true;
                    }
                }
            } else {
                g_game.script_ui.hud_alpha -= fdt / 0.25f;
                if (g_game.script_ui.hud_alpha <= 0.0f) {
                    g_game.script_ui.hud_alpha = 0.0f;
                    g_game.script_ui.hud[0] = '\0';
                    g_game.script_ui.hud_out = false;
                }
            }
        }

        int sw = g_game.renderer.canvas_width;
        int sh = g_game.renderer.canvas_height;
        float us = g_game.ui_scale;

        if (!hud_hidden() && g_game.script_ui.hud[0] && g_game.script_ui.hud_alpha > 0.01f) {
            float scale = 2.0f * us;
            float y = 28.0f * us;
            chat_render_banner(&g_game.chat, g_game.script_ui.hud,
                               (float)sw * 0.5f, y, scale,
                               g_game.script_ui.hud_alpha, 0.0f, sw, sh);
        }
        if (!hud_hidden() && g_game.script_ui.toast[0] && g_game.script_ui.toast_alpha > 0.01f) {
            float scale = 2.4f * us;
            float y = (float)sh * 0.42f;
            chat_render_banner(&g_game.chat, g_game.script_ui.toast,
                               (float)sw * 0.5f, y, scale,
                               g_game.script_ui.toast_alpha,
                               g_game.script_ui.toast_yoff, sw, sh);
        }

        {
            AudioMusicCred want;
            audio_music_cred_get(&want);
            bool want_on = want.title[0] != '\0';
            bool showing = g_game.music_cred.title[0] != '\0';

            if (want.gen != g_game.music_cred.last_gen) {
                g_game.music_cred.last_gen = want.gen;
                if (!want_on) {
                    if (showing && g_game.music_cred.phase != 4) {
                        g_game.music_cred.phase = 4;
                        g_game.music_cred.pending_title[0] = '\0';
                        g_game.music_cred.pending_author[0] = '\0';
                    }
                } else {
                    bool same = showing &&
                        strcmp(want.title, g_game.music_cred.title) == 0 &&
                        strcmp(want.author, g_game.music_cred.author) == 0;
                    if (!showing || g_game.music_cred.phase == 0 ||
                        g_game.music_cred.phase == 4 ||
                        g_game.music_cred.alpha <= 0.01f) {
                        strncpy(g_game.music_cred.title, want.title,
                                sizeof(g_game.music_cred.title) - 1);
                        g_game.music_cred.title[sizeof(g_game.music_cred.title) - 1] = '\0';
                        strncpy(g_game.music_cred.author, want.author,
                                sizeof(g_game.music_cred.author) - 1);
                        g_game.music_cred.author[sizeof(g_game.music_cred.author) - 1] = '\0';
                        g_game.music_cred.phase = 1;
                        g_game.music_cred.rise = true;
                        g_game.music_cred.alpha = 0.0f;
                        g_game.music_cred.yoff = 28.0f * us;
                    } else if (!same) {
                        strncpy(g_game.music_cred.pending_title, want.title,
                                sizeof(g_game.music_cred.pending_title) - 1);
                        g_game.music_cred.pending_title[sizeof(g_game.music_cred.pending_title) - 1] = '\0';
                        strncpy(g_game.music_cred.pending_author, want.author,
                                sizeof(g_game.music_cred.pending_author) - 1);
                        g_game.music_cred.pending_author[sizeof(g_game.music_cred.pending_author) - 1] = '\0';
                        g_game.music_cred.phase = 3;
                    }
                }
            }

            if (g_game.music_cred.phase == 1) {
                g_game.music_cred.alpha += fdt / 0.28f;
                if (g_game.music_cred.alpha > 1.0f) g_game.music_cred.alpha = 1.0f;
                if (g_game.music_cred.rise) {
                    g_game.music_cred.yoff += (0.0f - g_game.music_cred.yoff) *
                        (1.0f - expf(-fdt * 10.0f));
                } else {
                    g_game.music_cred.yoff = 0.0f;
                }
                if (g_game.music_cred.alpha >= 0.999f &&
                    (!g_game.music_cred.rise || fabsf(g_game.music_cred.yoff) < 0.5f)) {
                    g_game.music_cred.alpha = 1.0f;
                    g_game.music_cred.yoff = 0.0f;
                    g_game.music_cred.phase = 2;
                }
            } else if (g_game.music_cred.phase == 3) {
                g_game.music_cred.alpha -= fdt / 0.22f;
                if (g_game.music_cred.alpha <= 0.0f) {
                    g_game.music_cred.alpha = 0.0f;
                    strncpy(g_game.music_cred.title, g_game.music_cred.pending_title,
                            sizeof(g_game.music_cred.title) - 1);
                    g_game.music_cred.title[sizeof(g_game.music_cred.title) - 1] = '\0';
                    strncpy(g_game.music_cred.author, g_game.music_cred.pending_author,
                            sizeof(g_game.music_cred.author) - 1);
                    g_game.music_cred.author[sizeof(g_game.music_cred.author) - 1] = '\0';
                    g_game.music_cred.pending_title[0] = '\0';
                    g_game.music_cred.pending_author[0] = '\0';
                    if (g_game.music_cred.title[0]) {
                        g_game.music_cred.phase = 1;
                        g_game.music_cred.rise = false;
                        g_game.music_cred.yoff = 0.0f;
                    } else {
                        g_game.music_cred.phase = 0;
                    }
                }
            } else if (g_game.music_cred.phase == 4) {
                g_game.music_cred.alpha -= fdt / 0.28f;
                g_game.music_cred.yoff += fdt * 36.0f * us;
                if (g_game.music_cred.alpha <= 0.0f) {
                    g_game.music_cred.alpha = 0.0f;
                    g_game.music_cred.yoff = 0.0f;
                    g_game.music_cred.title[0] = '\0';
                    g_game.music_cred.author[0] = '\0';
                    g_game.music_cred.phase = 0;
                }
            }

            if (!hud_hidden() && g_game.music_cred.title[0] && g_game.music_cred.alpha > 0.01f) {
                float wave[AUDIO_MUSIC_WAVE_BARS];
                audio_music_waveform(wave, AUDIO_MUSIC_WAVE_BARS);
                float mscale = (g_game.tool_count > 0) ? 0.72f : 1.0f;
                chat_render_music_credit(&g_game.chat,
                                         g_game.music_cred.title,
                                         g_game.music_cred.author,
                                         g_game.music_cred.alpha,
                                         g_game.music_cred.yoff,
                                         mscale,
                                         wave, AUDIO_MUSIC_WAVE_BARS,
                                         sw, sh);
            }
        }
    }

    if (!g_game.world_ready && !g_game.show_login &&
        (g_game.loading_world || g_game.multiplayer)) {
        bool show_leave = g_game.loading_time > 8.0f;
        bool leave = draw_load_overlay("Loading...", NULL, show_leave, (float)dt);
        if (leave) {
            audio_stop_music();
            net_client_disconnect(&g_game.net);
            g_game.multiplayer = false;
            g_game.loading_world = false;
            g_game.world_ready = false;
            g_game.loading_time = 0.0f;
            memset(&g_game.script_ui, 0, sizeof(g_game.script_ui));
            memset(&g_game.music_cred, 0, sizeof(g_game.music_cred));
#ifndef __EMSCRIPTEN__
            leave_game_ui();
#else
            EM_ASM({
                if (window.parent && window.parent !== window) {
                    window.parent.postMessage({type: 'leave_game'}, '*');
                } else {
                    window.location.href = '/';
                }
            });
#endif
        }
    }

#ifdef __EMSCRIPTEN__

    if (g_game.show_kick) {
        const char* sub = g_game.kick_reason[0] ? g_game.kick_reason : "You were removed from the game.";
        bool leave = draw_load_overlay("Kicked", sub, true, (float)dt);
        if (leave) {
            g_game.show_kick = false;
            EM_ASM({
                if (window.parent && window.parent !== window) {
                    window.parent.postMessage({type: 'leave_game'}, '*');
                } else {
                    window.location.href = '/';
                }
            });
        }
    }
#endif

    if (pw_vr_eye == 0 && (g_game.menu.open || g_game.menu.benchmark_running))
        game_menu_update(&g_game.menu, (float)dt);
    if (g_game.menu.open) {
        MenuAction pending = game_menu_poll_pending_click(
            &g_game.menu, g_game.renderer.canvas_width, g_game.renderer.canvas_height);
        if (pending != MENU_ACTION_NONE && pw_vr_eye == 0)
            apply_menu_action(pending);
    }
    if (g_game.menu.benchmark_running) {

        draw_load_overlay_ex("Benchmarking", NULL, false, (float)dt, false);
    } else if (g_game.menu.open) {
        g_game.menu.reset_enabled = g_game.reset_enabled;
        g_game.menu.fullscreen = platform_is_fullscreen();
        game_menu_on_mouse_move(&g_game.menu, input->mouse_x, input->mouse_y,
                                g_game.renderer.canvas_width, g_game.renderer.canvas_height);
        game_menu_render(&g_game.menu, g_game.renderer.canvas_width, g_game.renderer.canvas_height);
    }

#ifndef __EMSCRIPTEN__

    if (pw_vr_eye == 0)
        poll_avatar_editor_save();
    avatar_editor_render(&g_game.avatar_editor, &g_game.renderer,
                         g_game.renderer.canvas_width, g_game.renderer.canvas_height,
                         pw_vr_eye == 0 ? (float)dt : 0.0f);
    catalog_ui_render(&g_game.catalog_ui, &g_game.renderer,
                      g_game.renderer.canvas_width, g_game.renderer.canvas_height,
                      pw_vr_eye == 0 ? (float)dt : 0.0f);
#endif

    if (pw_vr_eye == 0)
        social_update(&g_game.social, (float)dt);
    g_game.social.ui_scale = g_game.ui_scale;
    if (!hud_hidden())
        social_render(&g_game.social, g_game.renderer.canvas_width, g_game.renderer.canvas_height);

#if defined(VR) && defined(PW_QUEST)
    if (g_game.vr.active)
        vr_openxr_blit_eye(pw_vr_eye);
#endif
    }

#ifndef __EMSCRIPTEN__
    discord_update();

    {
        int join_game = discord_get_pending_join();
        if (join_game > 0 && !g_game.multiplayer) {
            g_game.game_id = join_game;
            g_game.loading_world = true;
            g_game.show_login = false;
            paint_loading_now("Joining");
            if (net_client_connect(&g_game.net, pw_tcp_host(), pw_tcp_port())) {
                clear_game_world();
                g_game.multiplayer = true;
                g_game.auth_sent = true;
                bool used_ticket = false;
                if (g_game.session_token[0]) {
                    JoinTicket jt = auth_get_join_ticket(g_game.session_token, join_game, false, 0, false);
                    paint_loading_now("Connecting");
                    if (jt.valid) {
                        net_client_send_auth_ticket(&g_game.net, join_game, jt.ticket);
                        strncpy(g_game.username, jt.username, sizeof(g_game.username) - 1);
                        g_game.account_id = (uint32_t)jt.user_id;
                        used_ticket = true;
                    } else {
                        PW_ERR(ERR_GENERIC, "Discord ticket failed\n");
                    }
                }
                if (!used_ticket) {

                    char guest_name[32];
                    snprintf(guest_name, sizeof(guest_name), "Guest%d", rand() % 10000);
                    net_client_send_auth(&g_game.net, join_game, guest_name);
                    strncpy(g_game.username, guest_name, sizeof(g_game.username) - 1);
                }
                char details[64];
                snprintf(details, sizeof(details), "Playing game %d", join_game);
                discord_update_presence(details, "In-game", 1, 64, NULL, join_game, true);
            }
        }
    }
#endif
    input_pre_frame();
    input_post_frame();
}

static void apply_local_avatar(void) {
    Entity* av_ent = scene_get_entity(&g_game.scene, g_game.avatar.entity);
    if (!av_ent) return;

    if (strncmp(g_game.username, "Guest", 5) == 0 && g_game.guest_avatar_texture) {
        av_ent->material.texture_id = g_game.guest_avatar_texture;
        av_ent->material.texture_mode = 1;
        av_ent->material.color = (Vec3){1.0f, 1.0f, 1.0f};
    } else if (g_game.avatar_texture) {
        av_ent->material.texture_id = g_game.avatar_texture;
        av_ent->material.texture_mode = 3;

        if (g_game.avatar_color[0] == '#' && strlen(g_game.avatar_color) >= 7) {
            unsigned int hex = 0;
            sscanf(g_game.avatar_color + 1, "%06x", &hex);
            av_ent->material.color = (Vec3){
                ((hex >> 16) & 0xFF) / 255.0f,
                ((hex >> 8) & 0xFF) / 255.0f,
                (hex & 0xFF) / 255.0f
            };
        }
    }
}

void client_script_send_remote(const char* name, const uint8_t* data, size_t data_len) {
    if (!name || !name[0]) return;
    if (!g_game.multiplayer || g_game.net.state != NET_STATE_CONNECTED) return;
    uint8_t evt_buf[256];
    size_t name_len = strlen(name);
    if (name_len > 63) name_len = 63;
    if (1 + name_len + data_len > sizeof(evt_buf)) return;
    evt_buf[0] = (uint8_t)name_len;
    memcpy(evt_buf + 1, name, name_len);
    if (data && data_len > 0)
        memcpy(evt_buf + 1 + name_len, data, data_len);
    net_client_send(&g_game.net, MSG_REMOTE_EVENT, evt_buf, 1 + name_len + data_len);
}

void client_script_part_meta(EntityID entity, const char** shape_out, int* can_collide_out) {
    const char* sh = "Box";
    int can = 1;
    int lp = client_local_part_slot(entity);
    if (lp >= 0) {
        uint8_t t = g_client_local_parts[lp].shape;
        if (t == 1) sh = "Sphere";
        else if (t == 2) sh = "Cylinder";
        else if (t == 3) sh = "Wedge";
        can = g_client_local_parts[lp].can_collide ? 1 : 0;
        if (shape_out) *shape_out = sh;
        if (can_collide_out) *can_collide_out = can;
        return;
    }
    int ni = net_index_for_entity(entity);
    if (ni >= 0) {
        uint8_t t = g_game.net_objects[ni].obj_type;
        if (t == 1) sh = "Sphere";
        else if (t == 2) sh = "Cylinder";
        else if (t == 3) sh = "Wedge";
        can = g_game.net_objects[ni].collide_wanted ? 1 : 0;
    }
    if (shape_out) *shape_out = sh;
    if (can_collide_out) *can_collide_out = can;
}

int client_script_world_raycast(float ox, float oy, float oz,
                                float dx, float dy, float dz, float max_dist,
                                float* hx, float* hy, float* hz,
                                float* nx, float* ny, float* nz,
                                float* dist, EntityID* entity) {
    Vec3 origin = {ox, oy, oz};
    Vec3 dir = {dx, dy, dz};
    float len = vec3_length(dir);
    if (len < 1e-6f || max_dist <= 0.0f) return 0;
    dir = vec3_scale(dir, 1.0f / len);
    if (max_dist > 0.0f && max_dist < 5000.0f)
        ;
    else
        max_dist = len;
    if (g_game.avatar.body)
        physics_disable_geom(g_game.physics, g_game.avatar.body);
    RaycastHit hit = physics_raycast(g_game.physics, origin, dir, max_dist);
    if (g_game.avatar.body && !g_game.avatar.dead)
        physics_enable_geom(g_game.physics, g_game.avatar.body);
    EntityID ent_id = ENTITY_INVALID;
    if (hit.hit) {
        int ni = net_index_for_body(hit.body);
        if (ni >= 0) ent_id = g_game.net_objects[ni].entity;
        else {
            for (uint32_t i = 0; i < g_game.scene.count; i++) {
                Entity* e = &g_game.scene.entities[i];
                if (e->active && e->physics_body == hit.body) {
                    ent_id = e->id;
                    break;
                }
            }
        }
        if (hx) *hx = hit.point.x;
        if (hy) *hy = hit.point.y;
        if (hz) *hz = hit.point.z;
        if (nx) *nx = hit.normal.x;
        if (ny) *ny = hit.normal.y;
        if (nz) *nz = hit.normal.z;
        if (dist) *dist = hit.distance;
        if (entity) *entity = ent_id;
        return 1;
    }

    float best_t = max_dist;
    int best_ni = -1;
    for (int i = 0; i < g_game.net_object_count; i++) {
        Entity* ent = scene_get_entity(&g_game.scene, g_game.net_objects[i].entity);
        if (!ent || !ent->active) continue;
        Vec3 half = {
            g_game.net_objects[i].size[0] * 0.5f,
            g_game.net_objects[i].size[1] * 0.5f,
            g_game.net_objects[i].size[2] * 0.5f
        };
        float t = 0;
        if (!ray_aabb(origin, dir, ent->transform.position, half, best_t, &t))
            continue;
        if (t < 0.0f || t >= best_t) continue;
        best_t = t;
        best_ni = i;
    }
    if (best_ni < 0) {
        float lt = 0;
        EntityID le = ENTITY_INVALID;
        if (client_script_local_part_aabb_ray(origin.x, origin.y, origin.z,
                                              dir.x, dir.y, dir.z, best_t, &lt, &le)) {
            Vec3 pt = vec3_add(origin, vec3_scale(dir, lt));
            if (hx) *hx = pt.x;
            if (hy) *hy = pt.y;
            if (hz) *hz = pt.z;
            if (nx) *nx = 0;
            if (ny) *ny = 1;
            if (nz) *nz = 0;
            if (dist) *dist = lt;
            if (entity) *entity = le;
            return 1;
        }
        return 0;
    }
    {
        float lt = 0;
        EntityID le = ENTITY_INVALID;
        if (client_script_local_part_aabb_ray(origin.x, origin.y, origin.z,
                                              dir.x, dir.y, dir.z, best_t, &lt, &le) && lt < best_t) {
            Vec3 pt = vec3_add(origin, vec3_scale(dir, lt));
            if (hx) *hx = pt.x;
            if (hy) *hy = pt.y;
            if (hz) *hz = pt.z;
            if (nx) *nx = 0;
            if (ny) *ny = 1;
            if (nz) *nz = 0;
            if (dist) *dist = lt;
            if (entity) *entity = le;
            return 1;
        }
    }
    Vec3 pt = vec3_add(origin, vec3_scale(dir, best_t));
    if (hx) *hx = pt.x;
    if (hy) *hy = pt.y;
    if (hz) *hz = pt.z;
    if (nx) *nx = 0;
    if (ny) *ny = 1;
    if (nz) *nz = 0;
    if (dist) *dist = best_t;
    if (entity) *entity = g_game.net_objects[best_ni].entity;
    return 1;
}

void client_script_screen_to_world(float sx, float sy, float depth,
                                   float* ox, float* oy, float* oz,
                                   float* dx, float* dy, float* dz) {
    Vec3 origin, dir;
    game_camera_screen_ray(sx, sy, &origin, &dir);
    float zfar = (depth > 1.0f) ? depth : PW_CAMERA_FAR;
    if (ox) *ox = origin.x;
    if (oy) *oy = origin.y;
    if (oz) *oz = origin.z;
    if (dx) *dx = dir.x * zfar;
    if (dy) *dy = dir.y * zfar;
    if (dz) *dz = dir.z * zfar;
}

void client_script_point_to_screen(float wx, float wy, float wz,
                                   float* sx, float* sy, int* on_screen) {
    int sw = g_game.renderer.canvas_width;
    int sh = g_game.renderer.canvas_height;
    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;
    Mat4 view = g_game.last_view_valid ? g_game.last_view : camera_get_view_matrix(&g_game.camera);
    float aspect = (float)sw / (float)sh;
    Mat4 proj = g_game.last_view_valid ? g_game.last_projection
        : camera_get_projection_matrix(&g_game.camera, aspect,
            g_game.scripts ? client_script_camera_fov(g_game.scripts) : 60.0f,
            PW_CAMERA_NEAR, PW_CAMERA_FAR);
    Mat4 vp = mat4_multiply(proj, view);
    Vec4 clip = mat4_mul_vec4(vp, (Vec4){wx, wy, wz, 1.0f});
    int on = 0;
    float x = 0, y = 0;
    if (clip.w > 0.001f) {
        float ndc_x = clip.x / clip.w;
        float ndc_y = clip.y / clip.w;
        x = (ndc_x * 0.5f + 0.5f) * (float)sw;
        y = (1.0f - (ndc_y * 0.5f + 0.5f)) * (float)sh;
        on = (ndc_x >= -1.0f && ndc_x <= 1.0f && ndc_y >= -1.0f && ndc_y <= 1.0f);
    }
    if (sx) *sx = x;
    if (sy) *sy = y;
    if (on_screen) *on_screen = on;
}

void client_script_ui_notify(const char* text, float seconds) {
    if (!text) return;
    strncpy(g_game.script_ui.toast, text, sizeof(g_game.script_ui.toast) - 1);
    g_game.script_ui.toast[sizeof(g_game.script_ui.toast) - 1] = '\0';
    g_game.script_ui.toast_timer = seconds > 0.0f ? seconds : 3.0f;
    g_game.script_ui.toast_out = false;
    g_game.script_ui.toast_alpha = 0.0f;
    g_game.script_ui.toast_yoff = 28.0f;
}

void client_script_ui_hud(const char* text) {
    if (!text) text = "";
    strncpy(g_game.script_ui.hud, text, sizeof(g_game.script_ui.hud) - 1);
    g_game.script_ui.hud[sizeof(g_game.script_ui.hud) - 1] = '\0';
    g_game.script_ui.hud_out = false;
    g_game.script_ui.hud_alpha = 1.0f;
}

static uint32_t explosion_rand(uint32_t* s) {
    *s = *s * 1664525u + 1013904223u;
    return *s;
}

static float explosion_frand(uint32_t* s) {
    return (float)(explosion_rand(s) >> 8) * (1.0f / 16777216.0f);
}

static Vec3 explosion_rand_dir(uint32_t* s) {
    float u = explosion_frand(s) * 2.0f - 1.0f;
    float th = explosion_frand(s) * 6.2831853f;
    float rxy_sq = 1.0f - u * u;
    if (rxy_sq < 0.0f) rxy_sq = 0.0f;
    float rxy = sqrtf(rxy_sq);
    return (Vec3){ rxy * cosf(th), u, rxy * sinf(th) };
}

static float explosion_ease_out(float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

#define EXP_TILE_FIRE0 0
#define EXP_TILE_FIRE1 1
#define EXP_TILE_SMOKE 2
#define EXP_TILE_RING  3
#define EXP_KIND_FIRE   0
#define EXP_KIND_EMBER  1
#define EXP_KIND_SMOKE  2
#define EXP_KIND_RING   3
#define EXP_KIND_FLASH  4
#define EXP_KIND_SHOCK  5
#define EXP_KIND_SCORCH 6

static int explosion_kind_stationary(uint8_t kind) {
    return kind == EXP_KIND_RING || kind == EXP_KIND_FLASH
        || kind == EXP_KIND_SHOCK || kind == EXP_KIND_SCORCH;
}

static uint8_t explosion_kind_layer(uint8_t kind, int is_debris) {

    if (is_debris) return 3;
    switch (kind) {
        case EXP_KIND_SCORCH: return 6;
        case EXP_KIND_RING:   return 5;
        case EXP_KIND_SMOKE:  return 4;
        case EXP_KIND_SHOCK:  return 3;
        case EXP_KIND_FIRE:   return 2;
        case EXP_KIND_EMBER:  return 1;
        default:              return 0;
    }
}

static Vec3 explosion_pull_toward(Vec3 pos, Vec3 cam, float amount) {
    Vec3 d = { cam.x - pos.x, cam.y - pos.y, cam.z - pos.z };
    float len = vec3_length(d);
    if (len < 1e-4f || amount <= 0.0f) return pos;
    float s = amount / len;
    pos.x += d.x * s;
    pos.y += d.y * s;
    pos.z += d.z * s;
    return pos;
}

static void explosion_tile_uv(int tile, float* u, float* v, float* su, float* sv) {
    if (tile < 0) tile = 0;
    if (tile > 3) tile = 3;

    const float pad = 2.0f / 256.0f;
    *su = 0.5f - pad * 2.0f;
    *sv = 0.5f - pad * 2.0f;
    *u = (float)(tile & 1) * 0.5f + pad;
    *v = (float)(tile >> 1) * 0.5f + pad;
}

static Mat4 explosion_billboard(Vec3 pos, float w, float h, float roll, const Mat4* view) {
    Mat4 inv = mat4_inverse(*view);
    Vec3 right = { inv.m[0], inv.m[1], inv.m[2] };
    Vec3 up = { inv.m[4], inv.m[5], inv.m[6] };
    float rl = vec3_length(right);
    float ul = vec3_length(up);
    if (rl > 1e-5f) right = vec3_scale(right, 1.0f / rl);
    if (ul > 1e-5f) up = vec3_scale(up, 1.0f / ul);
    float c = cosf(roll), s = sinf(roll);
    Vec3 rr = {
        right.x * c + up.x * s,
        right.y * c + up.y * s,
        right.z * c + up.z * s
    };
    Vec3 uu = {
        up.x * c - right.x * s,
        up.y * c - right.y * s,
        up.z * c - right.z * s
    };
    Vec3 f = vec3_cross(rr, uu);

    rr.x = -rr.x; rr.y = -rr.y; rr.z = -rr.z;
    f.x = -f.x; f.y = -f.y; f.z = -f.z;
    Mat4 m = mat4_identity();
    m.m[0] = rr.x * w; m.m[1] = rr.y * w; m.m[2] = rr.z * w;
    m.m[4] = uu.x * h; m.m[5] = uu.y * h; m.m[6] = uu.z * h;
    m.m[8] = f.x;      m.m[9] = f.y;      m.m[10] = f.z;
    m.m[12] = pos.x;   m.m[13] = pos.y;   m.m[14] = pos.z;
    return m;
}

static void explosion_draw_sprite(Vec3 pos, float size, float roll, int tile,
                                  Vec3 tint, float alpha, float glow, int additive,
                                  int ground, Vec3 cam, const Mat4* view, const Mat4* projection) {
    if (alpha <= 0.001f || size <= 0.01f) return;
    if (!g_game.explosion_tex || !g_game.explosion_quad_ready) return;

    if (ground) {
        pos.y += 0.08f;
    } else {
        pos = explosion_pull_toward(pos, cam, 0.20f);
    }
    Mat4 m;
    if (ground) {

        m = mat4_multiply(mat4_translate(pos),
            mat4_multiply(mat4_rotate_y(roll * (180.0f / 3.14159265f)),
            mat4_multiply(mat4_rotate_x(90.0f), mat4_scale((Vec3){size, size, 1.0f}))));
    } else {
        m = explosion_billboard(pos, size, size, roll, view);
    }
    float u, v, su, sv;
    explosion_tile_uv(tile, &u, &v, &su, &sv);
    renderer_set_shadow_id(&g_game.renderer, 0);
    renderer_set_mesh_fx(&g_game.renderer, glow, additive, 2);
    renderer_set_mesh_uv_rect(&g_game.renderer, u, v, su, sv);
    renderer_draw_mesh_alpha(&g_game.renderer, &g_game.explosion_quad, &m, tint,
                             g_game.explosion_tex, 4, view, projection, alpha);
}

static int explosion_add_part(int slot, uint8_t kind, uint8_t tile,
                              Vec3 pos, Vec3 vel, float size0, float size1,
                              float roll, float rollvel, float delay, float dur) {
    if (slot < 0 || g_game.explosions[slot].part_count >= EXPLOSION_PARTS) return -1;
    int i = g_game.explosions[slot].part_count++;
    g_game.explosions[slot].parts[i].kind = kind;
    g_game.explosions[slot].parts[i].tile = tile;
    g_game.explosions[slot].parts[i].pos = pos;
    g_game.explosions[slot].parts[i].vel = vel;
    g_game.explosions[slot].parts[i].size0 = size0;
    g_game.explosions[slot].parts[i].size1 = size1;
    g_game.explosions[slot].parts[i].roll = roll;
    g_game.explosions[slot].parts[i].rollvel = rollvel;
    g_game.explosions[slot].parts[i].delay = delay;
    g_game.explosions[slot].parts[i].dur = dur > 0.01f ? dur : 0.01f;
    return i;
}

static Vec3 explosion_fire_tint(float t) {

    if (t < 0.18f) {
        float u = t / 0.18f;
        return (Vec3){ 1.0f, 1.0f - 0.12f * u, 0.72f - 0.45f * u };
    }
    if (t < 0.55f) {
        float u = (t - 0.18f) / 0.37f;
        return (Vec3){ 1.0f, 0.88f - 0.48f * u, 0.27f - 0.18f * u };
    }
    float u = (t - 0.55f) / 0.45f;
    return (Vec3){ 1.0f - 0.35f * u, 0.40f - 0.28f * u, 0.09f - 0.04f * u };
}

static void explosion_update(float dt) {
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;

    g_game.exp_flash *= expf(-9.0f * dt);
    if (g_game.exp_flash < 0.002f) g_game.exp_flash = 0.0f;

    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!g_game.explosions[i].active) continue;
        g_game.explosions[i].age += dt;
        if (g_game.explosions[i].age >= g_game.explosions[i].life) {
            g_game.explosions[i].active = false;
            continue;
        }
        Vec3 origin = g_game.explosions[i].position;
        for (int p = 0; p < g_game.explosions[i].part_count; p++) {
            uint8_t kind = g_game.explosions[i].parts[p].kind;
            g_game.explosions[i].parts[p].roll += g_game.explosions[i].parts[p].rollvel * dt;
            if (explosion_kind_stationary(kind)) continue;
            Vec3* pos = &g_game.explosions[i].parts[p].pos;
            Vec3* vel = &g_game.explosions[i].parts[p].vel;
            if (kind == EXP_KIND_EMBER) vel->y -= 42.0f * dt;
            if (kind == EXP_KIND_SMOKE) vel->y += 2.2f * dt;
            pos->x += vel->x * dt;
            pos->y += vel->y * dt;
            pos->z += vel->z * dt;
            float drag = (kind == EXP_KIND_SMOKE) ? 1.1f : 1.8f;
            vel->x *= (1.0f - drag * dt);
            vel->z *= (1.0f - drag * dt);
        }
        for (int d = 0; d < EXPLOSION_DEBRIS; d++) {
            Vec3* p = &g_game.explosions[i].debris[d].pos;
            Vec3* vel = &g_game.explosions[i].debris[d].vel;
            vel->y -= 78.0f * dt;
            p->x += vel->x * dt;
            p->y += vel->y * dt;
            p->z += vel->z * dt;
            if (p->y < origin.y - 2.4f && vel->y < 0.0f) {
                p->y = origin.y - 2.4f;
                vel->y *= -0.28f;
                vel->x *= 0.55f;
                vel->z *= 0.55f;
            }
            g_game.explosions[i].debris[d].rot.x += g_game.explosions[i].debris[d].rotvel.x * dt;
            g_game.explosions[i].debris[d].rot.y += g_game.explosions[i].debris[d].rotvel.y * dt;
            g_game.explosions[i].debris[d].rot.z += g_game.explosions[i].debris[d].rotvel.z * dt;
        }
    }
}

static Vec3 acc_glow_boost_color(Vec3 c) {
    float luma = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
    float sat = 2.35f;
    c.x = luma + (c.x - luma) * sat;
    c.y = luma + (c.y - luma) * sat;
    c.z = luma + (c.z - luma) * sat;
    if (c.x < 0.0f) c.x = 0.0f;
    if (c.x > 1.0f) c.x = 1.0f;
    if (c.y < 0.0f) c.y = 0.0f;
    if (c.y > 1.0f) c.y = 1.0f;
    if (c.z < 0.0f) c.z = 0.0f;
    if (c.z > 1.0f) c.z = 1.0f;
    float strength = RENDERER_GLOW_LIGHT_STRENGTH;
    c.x *= strength; c.y *= strength; c.z *= strength;
    return c;
}

static void push_one_acc_glows(Accessory* acc, AvatarAnim* anim, RagdollState* rd,
                               int acc_index, Vec3 pos, float yaw, float scale) {
    if (!acc || acc->glow_count < 1) return;
    for (int gi = 0; gi < acc->glow_count; gi++) {
        AccessoryGlow* g = &acc->glows[gi];
        if (!g->valid) continue;
        int attach = g->attach_part;
        if (attach < 0 || attach >= AVATAR_PART_COUNT) continue;
        Mat4 part_mat;
        if (rd && rd->active) {
            int found = -1;
            if (acc_index >= 0 && acc_index < PW_MAX_EQUIPPED_ACCESSORIES) {
                for (int i = 0; i < ACCESSORY_MAX_PARTS; i++) {
                    if (acc->parts[i].valid && acc->parts[i].attach_part == attach &&
                        rd->acc_bodies[acc_index][i]) {
                        found = i;
                        break;
                    }
                }
            }
            if (found >= 0) {
                Mat4 body = physics_get_transform_mat4(g_game.physics, rd->acc_bodies[acc_index][found]);
                part_mat = mat4_multiply(body, rd->acc_mesh_from_body[acc_index][found]);
            } else {
                part_mat = ragdoll_part_matrix(rd, attach);
            }
        } else if (anim) {
            part_mat = avatar_anim_get_part_matrix(anim, attach, pos, yaw, scale);
        } else {
            continue;
        }
        Vec4 w = mat4_mul_vec4(part_mat, (Vec4){
            g->local_pos.x, g->local_pos.y, g->local_pos.z, 1.0f
        });
        float sm = vec3_length((Vec3){ part_mat.m[0], part_mat.m[1], part_mat.m[2] });
        if (sm < 0.01f) sm = 1.0f;
        float range = g->radius * sm * 6.0f;
        if (range < 4.0f) range = 4.0f;
        if (g_game.renderer.glow_leak_mode >= 1) {
            float cap = RENDERER_GLOW_RANGE_CAP;
            if (g_game.renderer.glow_light_max > 0 && g_game.renderer.glow_light_max <= 8)
                cap *= 0.5f;
            if (range > cap) range = cap;
        }
        renderer_add_acc_glow_light(&g_game.renderer, (Vec3){ w.x, w.y, w.z },
                                    acc_glow_boost_color(g->color), range, 1.0f);
    }
}

static void push_accs_glows(Accessory* accs, AvatarAnim* anim, RagdollState* rd,
                            Vec3 pos, float yaw, float scale) {
    if (!accs) return;
    for (int ai = 0; ai < PW_MAX_EQUIPPED_ACCESSORIES; ai++)
        push_one_acc_glows(&accs[ai], anim, rd, ai, pos, yaw, scale);
}

static void push_accessory_glow_lights(void) {
    if (g_game.avatar_anim.parts[0].valid) {
        if (g_game.ragdoll.active) {
            push_accs_glows(g_game.local_accessory, &g_game.avatar_anim,
                            (RagdollState*)&g_game.ragdoll,
                            (Vec3){0, 0, 0}, 0.0f, AVATAR_SCALE);
        } else {
            Entity* av_ent = scene_get_entity(&g_game.scene, g_game.avatar.entity);
            float yaw = av_ent ? av_ent->transform.rotation.y : g_game.avatar.current_yaw;
            Vec3 pos = av_ent ? av_ent->transform.position : (Vec3){
                g_game.avatar.pos.x,
                g_game.avatar.pos.y - AVATAR_FEET_OFFSET + g_game.avatar.step_offset,
                g_game.avatar.pos.z
            };
            push_accs_glows(g_game.local_accessory, &g_game.avatar_anim, NULL,
                            pos, yaw, AVATAR_SCALE);
        }
    }
    for (int rp = 0; rp < MAX_REMOTE_PLAYERS; rp++) {
        if (!g_game.remote_players[rp].active) continue;
        Entity* rent = scene_get_entity(&g_game.scene, g_game.remote_players[rp].entity);
        if (!rent) continue;
        Vec3 d = vec3_sub(rent->transform.position, g_game.avatar.pos);
        float dist2 = d.x * d.x + d.y * d.y + d.z * d.z;
        if (dist2 > (120.0f * 120.0f)) continue;
        RagdollState* rd = g_game.remote_players[rp].ragdoll.active
            ? (RagdollState*)&g_game.remote_players[rp].ragdoll : NULL;
        push_accs_glows(g_game.remote_players[rp].accessory,
                        &g_game.remote_players[rp].anim, rd,
                        rent->transform.position, rent->transform.rotation.y, AVATAR_SCALE);
    }
}

static void explosion_push_lights(void) {
    renderer_clear_fx_lights(&g_game.renderer);
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!g_game.explosions[i].active) continue;
        float age = g_game.explosions[i].age;
        float R = g_game.explosions[i].radius;
        if (R < 4.0f) R = 4.0f;
        float pulse = 1.0f;
        if (age < 0.08f) pulse = 1.35f;
        else if (age < 0.35f) pulse = 1.0f - (age - 0.08f) / 0.27f * 0.55f;
        else pulse = 0.45f * (1.0f - (age - 0.35f) / 0.55f);
        if (pulse < 0.0f) continue;
        Vec3 col = { 4.2f * pulse, 2.1f * pulse, 0.45f * pulse };
        renderer_add_fx_light(&g_game.renderer, g_game.explosions[i].position,
                              col, R * 1.65f, pulse);
    }
    push_accessory_glow_lights();
}

#define EXPLOSION_DRAW_MAX (MAX_EXPLOSIONS * (EXPLOSION_PARTS + EXPLOSION_DEBRIS))

typedef struct {
    float dist2;
    uint8_t layer;
    uint8_t is_debris;
    uint8_t tile;
    uint8_t additive;
    uint8_t ground;
    Vec3 pos;
    Vec3 tint;
    Vec3 rot;
    float size;
    float roll;
    float alpha;
    float glow;
} ExplosionDrawItem;

static int explosion_draw_cmp(const void* a, const void* b) {
    const ExplosionDrawItem* ia = (const ExplosionDrawItem*)a;
    const ExplosionDrawItem* ib = (const ExplosionDrawItem*)b;
    float d = ia->dist2 - ib->dist2;

    if (d < -4.0f) return 1;
    if (d > 4.0f) return -1;
    if (ia->layer != ib->layer) return (int)ib->layer - (int)ia->layer;
    return (d < 0.0f) - (d > 0.0f);
}

static void explosion_draw(const Mat4* view, const Mat4* projection) {
    if (!view || !projection) return;

    Mat4 inv = mat4_inverse(*view);
    Vec3 cam = { inv.m[12], inv.m[13], inv.m[14] };

    static ExplosionDrawItem items[EXPLOSION_DRAW_MAX];
    int n = 0;

    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!g_game.explosions[i].active) continue;
        float age = g_game.explosions[i].age;
        float tlife = g_game.explosions[i].life;
        float t = (tlife > 0.001f) ? (age / tlife) : 1.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        for (int p = 0; p < g_game.explosions[i].part_count && n < EXPLOSION_DRAW_MAX; p++) {
            uint8_t kind = g_game.explosions[i].parts[p].kind;
            float local = (age - g_game.explosions[i].parts[p].delay)
                        / g_game.explosions[i].parts[p].dur;
            if (local <= 0.0f || local >= 1.0f) continue;
            float grow = explosion_ease_out(local);
            ExplosionDrawItem* it = &items[n++];
            memset(it, 0, sizeof(*it));
            it->pos = g_game.explosions[i].parts[p].pos;
            it->size = g_game.explosions[i].parts[p].size0
                     + (g_game.explosions[i].parts[p].size1
                        - g_game.explosions[i].parts[p].size0) * grow;
            it->roll = g_game.explosions[i].parts[p].roll;
            it->tile = g_game.explosions[i].parts[p].tile;
            it->tint = (Vec3){1.0f, 1.0f, 1.0f};
            it->layer = explosion_kind_layer(kind, 0);
            if (kind == EXP_KIND_FIRE) {
                it->alpha = (local < 0.25f) ? 1.0f : (1.0f - (local - 0.25f) / 0.75f);
                it->alpha *= 0.90f;
                it->glow = 0.65f * (1.0f - local);
                it->additive = 1;
                it->tint = explosion_fire_tint(local);
            } else if (kind == EXP_KIND_EMBER) {
                it->alpha = 1.0f - local;
                it->alpha *= it->alpha;
                it->glow = 0.9f * (1.0f - local);
                it->additive = 1;
                it->tint = explosion_fire_tint(local * 0.7f);
            } else if (kind == EXP_KIND_SMOKE) {
                it->alpha = (local < 0.2f) ? (local / 0.2f) : (1.0f - (local - 0.2f) / 0.8f);
                it->alpha *= 0.55f;
                it->tint = (Vec3){ 0.55f, 0.48f, 0.42f };
            } else if (kind == EXP_KIND_RING) {
                float fade = (local < 0.22f) ? 1.0f : (1.0f - (local - 0.22f) / 0.78f);
                it->alpha = fade * 0.92f;
                it->glow = 0.45f * fade;
                it->additive = 1;
                it->ground = 1;
                it->tint = (Vec3){ 1.0f, 0.82f, 0.38f };
            } else if (kind == EXP_KIND_SCORCH) {
                float fade = (local < 0.18f) ? (local / 0.18f)
                           : (1.0f - (local - 0.18f) / 0.82f);
                it->alpha = fade * 0.28f;
                it->glow = 0.20f * (1.0f - local);
                it->additive = 1;
                it->ground = 1;
                it->tint = explosion_fire_tint(local * 0.55f);
            } else if (kind == EXP_KIND_SHOCK) {
                float fade = (local < 0.16f) ? 1.0f : (1.0f - (local - 0.16f) / 0.84f);
                it->alpha = fade * 0.80f;
                it->glow = 0.35f * fade;
                it->additive = 1;
                it->tint = (Vec3){ 1.0f, 0.86f, 0.42f };
            } else {
                it->alpha = 1.0f - local;
                it->glow = 1.4f;
                it->additive = 1;
                it->tint = (Vec3){ 1.0f, 0.95f, 0.75f };
            }
            float dx = it->pos.x - cam.x, dy = it->pos.y - cam.y, dz = it->pos.z - cam.z;
            it->dist2 = dx * dx + dy * dy + dz * dz;
        }

        if (g_game.cube_gpu_mesh.vao) {
            float da = 1.0f - t;
            da *= da;
            if (da > 0.02f) {
                for (int d = 0; d < EXPLOSION_DEBRIS && n < EXPLOSION_DRAW_MAX; d++) {
                    ExplosionDrawItem* it = &items[n++];
                    memset(it, 0, sizeof(*it));
                    it->is_debris = 1;
                    it->pos = g_game.explosions[i].debris[d].pos;
                    it->rot = g_game.explosions[i].debris[d].rot;
                    it->size = g_game.explosions[i].debris[d].size;
                    it->tint = g_game.explosions[i].debris[d].color;
                    it->alpha = da * 0.98f;
                    it->glow = 0.12f;
                    it->layer = explosion_kind_layer(0, 1);
                    float dx = it->pos.x - cam.x, dy = it->pos.y - cam.y, dz = it->pos.z - cam.z;
                    it->dist2 = dx * dx + dy * dy + dz * dz;
                }
            }
        }
    }

    if (n > 1)
        qsort(items, (size_t)n, sizeof(items[0]), explosion_draw_cmp);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-4.0f, -8.0f);
    g_game.renderer.mesh_fx_hold = 1;

    for (int k = 0; k < n; k++) {
        ExplosionDrawItem* it = &items[k];
        if (it->is_debris) {
            Mat4 m = mat4_translate(it->pos);
            m = mat4_multiply(m, mat4_rotate_y(it->rot.y));
            m = mat4_multiply(m, mat4_rotate_x(it->rot.x));
            m = mat4_multiply(m, mat4_rotate_z(it->rot.z));
            m = mat4_multiply(m, mat4_scale((Vec3){it->size, it->size, it->size}));
            renderer_set_shadow_id(&g_game.renderer, 0);
            renderer_set_mesh_fx(&g_game.renderer, it->glow, 0, 0);
            renderer_draw_mesh_alpha(&g_game.renderer, &g_game.cube_gpu_mesh, &m,
                                     it->tint, 0, 0, view, projection, it->alpha);
        } else {
            explosion_draw_sprite(it->pos, it->size, it->roll, it->tile,
                                  it->tint, it->alpha, it->glow, it->additive,
                                  it->ground, cam, view, projection);
        }
    }

    g_game.renderer.mesh_fx_hold = 0;
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(0.0f, 0.0f);
    glDepthFunc(GL_LESS);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
#if !PW_USE_GLES
    if (g_game.renderer.fog_world_pass && g_game.renderer.scene_fog_depth_tex)
        glEnablei(GL_BLEND, 1);
#endif
}

static void explosion_draw_flash(void) {
    if (g_game.exp_flash <= 0.01f) return;
    if (!g_game.chat.initialized || !g_game.chat.quad_shader) return;
    int sw = g_game.renderer.canvas_width;
    int sh = g_game.renderer.canvas_height;
    if (sw < 1 || sh < 1) return;
    unsigned int tex = g_game.chat.nineslice_tex;
    if (!tex) return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glUseProgram(g_game.chat.quad_shader);
    float proj[16];
    memset(proj, 0, sizeof(proj));
    proj[0] = 2.0f / (float)sw;
    proj[5] = -2.0f / (float)sh;
    proj[10] = 1.0f;
    proj[12] = -1.0f;
    proj[13] = 1.0f;
    proj[15] = 1.0f;
    glUniformMatrix4fv(g_game.chat.quad_u_projection, 1, GL_FALSE, proj);
    glUniform1f(g_game.chat.quad_u_alpha, g_game.exp_flash * 0.50f);
    glUniform4f(g_game.chat.quad_u_tint, 1.0f, 0.48f, 0.10f, 1.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(g_game.chat.quad_u_tex, 0);
    float verts[] = {
        0.0f, 0.0f, 0.5f, 0.5f,
        (float)sw, 0.0f, 0.5f, 0.5f,
        (float)sw, (float)sh, 0.5f, 0.5f,
        0.0f, 0.0f, 0.5f, 0.5f,
        (float)sw, (float)sh, 0.5f, 0.5f,
        0.0f, (float)sh, 0.5f, 0.5f,
    };
    glBindVertexArray(g_game.chat.text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_game.chat.text_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform4f(g_game.chat.quad_u_tint, 1.0f, 1.0f, 1.0f, 1.0f);
    glUniform1f(g_game.chat.quad_u_alpha, 1.0f);
}

void spawn_explosion(float x, float y, float z, float radius) {
    int slot = -1;
    float oldest = -1.0f;
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!g_game.explosions[i].active) {
            slot = i;
            break;
        }
        if (g_game.explosions[i].age > oldest) {
            oldest = g_game.explosions[i].age;
            slot = i;
        }
    }
    if (slot < 0) return;

    if (radius < 1.0f) radius = 1.0f;
    uint32_t seed = (uint32_t)(x * 73856093.0f) ^ (uint32_t)(y * 19349663.0f)
                  ^ (uint32_t)(z * 83492791.0f) ^ (uint32_t)(radius * 1009.0f);
    if (seed == 0) seed = 1u;

    memset(&g_game.explosions[slot], 0, sizeof(g_game.explosions[slot]));
    g_game.explosions[slot].active = true;
    g_game.explosions[slot].position = (Vec3){x, y, z};
    g_game.explosions[slot].radius = radius;
    g_game.explosions[slot].age = 0.0f;
    g_game.explosions[slot].life = 1.25f;
    g_game.explosions[slot].part_count = 0;

    Vec3 origin = {x, y, z};
    Vec3 ground = {x, y, z};

    for (int f = 0; f < 4; f++) {
        Vec3 dir = explosion_rand_dir(&seed);
        float off = radius * (0.03f + explosion_frand(&seed) * 0.07f);
        Vec3 p = {
            x + dir.x * off,
            y + dir.y * off * 0.55f,
            z + dir.z * off
        };
        explosion_add_part(slot, EXP_KIND_FIRE, (uint8_t)(f & 1), p, (Vec3){0, 0.4f, 0},
                           radius * (0.12f + explosion_frand(&seed) * 0.06f),
                           radius * (0.62f + explosion_frand(&seed) * 0.16f),
                           0.0f, 0.0f,
                           explosion_frand(&seed) * 0.03f,
                           0.42f + explosion_frand(&seed) * 0.16f);
    }

    for (int e = 0; e < 14; e++) {
        Vec3 dir = explosion_rand_dir(&seed);
        if (dir.y < 0.05f) dir.y = 0.05f + explosion_frand(&seed) * 0.4f;
        dir = vec3_normalize(dir);
        float spd = (8.0f + explosion_frand(&seed) * 16.0f) * (0.45f + radius * 0.04f);
        explosion_add_part(slot, EXP_KIND_EMBER, (uint8_t)(e & 1), origin, vec3_scale(dir, spd),
                           radius * (0.06f + explosion_frand(&seed) * 0.05f),
                           radius * (0.14f + explosion_frand(&seed) * 0.10f),
                           explosion_frand(&seed) * 6.2831853f,
                           (explosion_frand(&seed) * 2.0f - 1.0f) * 6.0f,
                           explosion_frand(&seed) * 0.04f,
                           0.22f + explosion_frand(&seed) * 0.20f);
    }

    for (int s = 0; s < 7; s++) {
        Vec3 dir = explosion_rand_dir(&seed);
        dir.y = fabsf(dir.y) * 0.4f + 0.35f;
        float off = radius * (0.08f + explosion_frand(&seed) * 0.16f);
        Vec3 p = { x + dir.x * off, y + 0.2f, z + dir.z * off };
        Vec3 vel = {
            dir.x * (0.8f + explosion_frand(&seed) * 1.6f),
            2.0f + explosion_frand(&seed) * 2.8f,
            dir.z * (0.8f + explosion_frand(&seed) * 1.6f)
        };
        explosion_add_part(slot, EXP_KIND_SMOKE, EXP_TILE_SMOKE, p, vel,
                           radius * (0.18f + explosion_frand(&seed) * 0.12f),
                           radius * (0.70f + explosion_frand(&seed) * 0.35f),
                           explosion_frand(&seed) * 6.2831853f,
                           (explosion_frand(&seed) * 2.0f - 1.0f) * 0.8f,
                           0.06f + explosion_frand(&seed) * 0.10f,
                           0.70f + explosion_frand(&seed) * 0.30f);
    }
    explosion_add_part(slot, EXP_KIND_RING, EXP_TILE_RING, ground, (Vec3){0, 0, 0},
                       radius * 0.50f, radius * 1.85f,
                       explosion_frand(&seed) * 6.2831853f, 0.12f,
                       0.0f, 0.62f);
    explosion_add_part(slot, EXP_KIND_SHOCK, EXP_TILE_RING, origin, (Vec3){0, 0, 0},
                       radius * 0.28f, radius * 1.15f,
                       0.0f, 0.0f,
                       0.0f, 0.42f);
    explosion_add_part(slot, EXP_KIND_FLASH, EXP_TILE_FIRE0, origin, (Vec3){0, 0, 0},
                       radius * 0.18f, radius * 0.72f,
                       0.0f, 0.0f,
                       0.0f, 0.11f);

    static const Vec3 debris_pal[] = {
        {0.95f, 0.45f, 0.08f},
        {0.55f, 0.18f, 0.06f},
        {0.22f, 0.18f, 0.16f},
        {0.75f, 0.62f, 0.20f},
        {0.38f, 0.32f, 0.30f},
        {1.00f, 0.72f, 0.18f},
    };
    for (int d = 0; d < EXPLOSION_DEBRIS; d++) {
        Vec3 dir = explosion_rand_dir(&seed);
        if (dir.y < 0.15f) dir.y = 0.15f + explosion_frand(&seed) * 0.55f;
        dir = vec3_normalize(dir);
        float speed = (10.0f + explosion_frand(&seed) * 22.0f) * (0.45f + radius * 0.045f);
        g_game.explosions[slot].debris[d].pos = (Vec3){
            x + dir.x * 0.35f, y + dir.y * 0.35f, z + dir.z * 0.35f
        };
        g_game.explosions[slot].debris[d].vel = vec3_scale(dir, speed);
        g_game.explosions[slot].debris[d].rot = (Vec3){
            explosion_frand(&seed) * 360.0f,
            explosion_frand(&seed) * 360.0f,
            explosion_frand(&seed) * 360.0f
        };
        g_game.explosions[slot].debris[d].rotvel = (Vec3){
            (explosion_frand(&seed) * 2.0f - 1.0f) * 720.0f,
            (explosion_frand(&seed) * 2.0f - 1.0f) * 720.0f,
            (explosion_frand(&seed) * 2.0f - 1.0f) * 480.0f
        };
        g_game.explosions[slot].debris[d].size = 0.22f + explosion_frand(&seed) * 0.42f;
        g_game.explosions[slot].debris[d].color = debris_pal[d % 6];
    }

    audio_play_at(SFX_EXPLOSION, x, y, z);

    Vec3 ear = g_game.camera.target;
    if (g_game.camera_mode == CAM_MODE_FREECAM)
        ear = g_game.freecam_pos;
    float dx = x - ear.x, dy = y - ear.y, dz = z - ear.z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    float reach = radius * 2.6f;
    if (reach < 14.0f) reach = 14.0f;
    float fall = 1.0f - dist / reach;
    if (fall > 0.0f) {
        if (fall > 1.0f) fall = 1.0f;
        g_game.exp_flash += fall * 0.58f;
        if (g_game.exp_flash > 0.90f) g_game.exp_flash = 0.90f;
    }
}

static void on_tool_hold_obj_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    (void)path;
    int slot = (int)(intptr_t)user;
    if (!data || len == 0 || slot < 0 || slot >= TOOL_HOLD_MAX) return;
    accessory_load(&g_tool_holds[slot].acc, (const char*)data, len);
}

static void on_tool_hold_tex_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    (void)path;
    int slot = (int)(intptr_t)user;
    if (!data || len == 0 || slot < 0 || slot >= TOOL_HOLD_MAX) return;

    extern unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
    extern void stbi_image_free(void*);
    extern void stbi_set_flip_vertically_on_load(int);

    int w, h, channels;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* pixels = stbi_load_from_memory(data, (int)len, &w, &h, &channels, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!pixels) return;

    uint32_t tex = texture_load_from_memory(pixels, w, h, 4);
    accessory_set_atlas(&g_tool_holds[slot].acc, pixels, w, h);
    stbi_image_free(pixels);
    if (tex)
        g_tool_holds[slot].tex = tex;
}

static void on_tool_icon_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    int ti = (int)(intptr_t)user;
    if (!data || len == 0 || ti < 0 || ti >= MAX_TOOLS) return;

    extern unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
    extern void stbi_image_free(void*);
    extern void stbi_set_flip_vertically_on_load(int);

    int w, h, channels;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* pixels = stbi_load_from_memory(data, (int)len, &w, &h, &channels, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!pixels) return;

    uint32_t tex = texture_load_from_memory(pixels, w, h, 4);
    stbi_image_free(pixels);

    if (tex) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
        g_game.tools[ti].icon_tex = tex;
        g_game.tools[ti].icon_w = w;
        g_game.tools[ti].icon_h = h;
    }
}

static void on_world_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    (void)user;

    if (!data || len == 0) {
        PW_ERR(ERR_GENERIC, "World load failed\n");
        g_game.loading_world = false;
#ifndef __EMSCRIPTEN__
        if (g_game.show_login == false && !g_game.multiplayer)
            leave_game_ui();
#endif
        return;
    }

    int count = world_load_from_xml((const char*)data, len,
                                     &g_game.scene, g_game.physics, &g_game.renderer);
    if (count >= 0) {
        g_game.world_ready = true;
        g_game.world_colliders_ready = true;
        g_game.loading_world = false;
        g_game.allow_freecam = true;
        if (vr_hub_active())
            g_game.allow_freecam = false;
        g_game.reset_enabled = true;
        g_game.avatar.health = 100;
        g_game.avatar.dead = false;
        brick_batch_rebuild(&g_game.scene);

        Vec3 spawn = { 0.0f, 5.0f, 0.0f };
        const char* rs = strstr((const char*)data, "<respawn>");
        if (rs) {
            const char* re = strstr(rs, "</respawn>");
            if (re) {
                const char* xv = strstr(rs, "<x>");
                const char* yv = strstr(rs, "<y>");
                const char* zv = strstr(rs, "<z>");
                if (xv && xv < re) spawn.x = (float)atof(xv + 3);
                if (yv && yv < re) spawn.y = (float)atof(yv + 3);
                if (zv && zv < re) spawn.z = (float)atof(zv + 3);
            }
        }
        if (vr_hub_active()) {

            const float hub_floor_top = 5.10f;
            spawn.y = hub_floor_top + AVATAR_ROOT_HALF_Y + 0.25f;
            ensure_avatar_bodies_loaded();
            refresh_local_avatar_meshes();

            g_game.vr.hmd_yaw = -90.0f;
            g_game.camera.yaw = -90.0f;
            vr_openxr_set_yaw_offset(90.0f);
        }
        g_game.avatar.pos = spawn;
        g_game.avatar.vel = (Vec3){ 0.0f, 0.0f, 0.0f };
        g_game.avatar.on_ground = false;
        if (g_game.avatar.body)
            physics_set_position(g_game.physics, g_game.avatar.body, g_game.avatar.pos);
        vr_calibrate_on_world_ready();

        vr_hub_on_world_loaded();
        if (vr_hub_active()) {
            g_game.tool_count = 0;
            g_game.equipped_tool = 0;
        } else if (!g_game.multiplayer) {
            g_game.tool_count = 0;
            g_game.equipped_tool = 0;
            const char* p = (const char*)data;
            const char* end = (const char*)data + len;
            while (p < end && g_game.tool_count < MAX_TOOLS) {
                const char* pre = strstr(p, "<preset>");
                if (!pre || pre >= end) break;
                pre += 8;
                const char* pre_end = strstr(pre, "</preset>");
                if (!pre_end || pre_end >= end) break;
                size_t n = (size_t)(pre_end - pre);
                if (n > 0 && n < 32) {
                    int ti = g_game.tool_count++;
                    memcpy(g_game.tools[ti].name, pre, n);
                    g_game.tools[ti].name[n] = '\0';
                    g_game.tools[ti].available = true;
                    tool_hold_ensure(g_game.tools[ti].name);
                    char tool_path[128];
                    snprintf(tool_path, sizeof(tool_path), "assets/tools/%s.png", g_game.tools[ti].name);
                    platform_load_file(tool_path, on_tool_icon_loaded, (void*)(intptr_t)ti);
                }
                p = pre_end + 9;
            }
        }
    } else {
        PW_ERR(ERR_GENERIC, "Failed to parse XML\n");
        g_game.loading_world = false;
#ifndef __EMSCRIPTEN__
        if (!g_game.multiplayer)
            leave_game_ui();
#endif
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void resize_canvas(int width, int height) {
    if (g_game.initialized) {
        renderer_resize(&g_game.renderer, width, height);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void set_mobile_mode(int enabled) {
    g_game.ui_scale = enabled ? 2.0f : 1.0f;
    g_game.chat.ui_scale = g_game.ui_scale;
    g_game.social.ui_scale = g_game.ui_scale;
    g_game.menu.ui_scale = g_game.ui_scale;
}

#if defined(__ANDROID__) || defined(PW_IOS)
#include "pw_android_game.h"

bool pw_game_init(void) {
    g_game.show_login = true;
    g_game.host = "https://polyworld.games";
    emote_clip_set_host(g_game.host);
    if (!game_init()) return false;

    ensure_avatar_bodies_loaded();
    platform_load_file("assets/guestavatar.png", on_guest_avatar_texture_loaded, NULL);
    {
        char path_shirt[512], path_pants[512], path_heads[512];
        const char* host = g_game.host[0] ? g_game.host : "https://polyworld.games";
        snprintf(path_shirt, sizeof(path_shirt), "%s/uploads/shirts/guest.png", host);
        snprintf(path_pants, sizeof(path_pants), "%s/uploads/pants/guest.png", host);
        snprintf(path_heads, sizeof(path_heads), "%s/uploads/heads/19.png", host);
        platform_load_file(path_shirt, on_avatar_texture_loaded, (void*)(intptr_t)0);
        platform_load_file(path_pants, on_avatar_texture_loaded, (void*)(intptr_t)1);
        platform_load_file(path_heads, on_avatar_texture_loaded, (void*)(intptr_t)2);
    }
    refresh_local_avatar_meshes();
    apply_local_avatar();

    discord_update_presence("Browsing menus", "Not In-game...", 0, 0, NULL, 0, false);
    login_screen_init(&g_game.login_screen);

    char saved_token[128] = {0};
    if (auth_load_session(saved_token, sizeof(saved_token))) {
        AuthResult vr = auth_validate_token(saved_token);
        if (vr.authenticated) {
            strncpy(g_game.login_screen.session_token, vr.token,
                    sizeof(g_game.login_screen.session_token) - 1);
            strncpy(g_game.login_screen.username, vr.username,
                    sizeof(g_game.login_screen.username) - 1);
            g_game.login_screen.username_len = (int)strlen(g_game.login_screen.username);
            g_game.login_screen.user_id = vr.user_id;
            g_game.login_screen.logged_in = true;
            g_game.login_screen.phase = 1;
            g_game.login_screen.games_fetched = false;
            g_game.login_screen.games_loading = false;
            strncpy(g_game.session_token, vr.token, sizeof(g_game.session_token) - 1);
            strncpy(g_game.username, vr.username, sizeof(g_game.username) - 1);
        } else {
            auth_clear_session();
        }
    }
#ifdef VR
    if (g_game.vr.active)
        enter_vr_hub(&g_game.login_screen);
#endif
    return true;
}

void pw_game_frame(double dt) {
    game_frame(dt);
}

void pw_game_restore_gl(void) {

    PW_LOG("pw_game_restore_gl: rebuilding GPU resources\n");

    font_invalidate_gl(false);
    font_init();

    login_screen_invalidate_gl(&g_game.login_screen, false);
    vr_hub_invalidate_gl(false);
    login_screen_init(&g_game.login_screen);

    chat_recreate_gl(&g_game.chat, false);
    {
        pw_load_png("assets/bubble_nineslice.png", &g_game.chat.bubble_texture, 1);
        pw_load_png("assets/bubble_bottom.png", &g_game.chat.bubble_bottom_tex, 1);
        pw_load_png("assets/dark_nineslice.png", &g_game.chat.nineslice_tex, 1);
        pw_load_png("assets/chat_closed.png", &g_game.chat.chat_closed_tex, 1);
        pw_load_png("assets/chat_open.png", &g_game.chat.chat_open_tex, 1);
        pw_load_png("assets/chat_unread.png", &g_game.chat.chat_unread_tex, 1);
        pw_load_png("assets/menu.png", &g_game.chat.menu_tex, 1);
        pw_load_png("assets/badges/creator.png", &g_game.chat.badge_creator, 1);
        pw_load_png("assets/badges/verified.png", &g_game.chat.badge_verified, 1);
        pw_load_png("assets/badges/shield.png", &g_game.chat.badge_shield, 1);
        pw_load_png("assets/badges/tester.png", &g_game.chat.badge_tester, 1);
        pw_load_png("assets/music.png", &g_game.chat.music_icon_tex, 1);
        pw_load_png_sized("assets/load_bg.png", &g_game.load_bg_tex, &g_game.load_bg_w, &g_game.load_bg_h, 0);
        pw_load_png_sized("assets/polyworld.png", &g_game.logo_tex, &g_game.logo_w, &g_game.logo_h, 0);
        emote_wheel_init(&g_game.emote_wheel);
        pw_load_png("assets/emote_wheel.png", &g_game.emote_wheel.wheel_tex, 1);
        pw_load_png("assets/emote_wheel_select.png", &g_game.emote_wheel.select_tex, 1);
        if (g_game.emote_wheel.wheel_tex) {
            glBindTexture(GL_TEXTURE_2D, g_game.emote_wheel.wheel_tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        if (g_game.emote_wheel.select_tex) {
            glBindTexture(GL_TEXTURE_2D, g_game.emote_wheel.select_tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    g_game.menu.nineslice_tex = g_game.chat.nineslice_tex;
    g_game.menu.menu_tex = g_game.chat.menu_tex;
    social_set_nineslice(&g_game.social, g_game.chat.nineslice_tex);
    social_set_shaders(&g_game.social,
                       g_game.chat.quad_shader, g_game.chat.quad_u_projection,
                       g_game.chat.quad_u_tex, g_game.chat.quad_u_alpha, g_game.chat.quad_u_tint,
                       g_game.chat.text_vao, g_game.chat.text_vbo);
    game_menu_set_shaders(g_game.chat.quad_shader, g_game.chat.quad_u_projection,
                          g_game.chat.quad_u_tex, g_game.chat.quad_u_alpha, g_game.chat.quad_u_tint,
                          g_game.chat.text_shader, g_game.chat.u_projection,
                          g_game.chat.u_tex, g_game.chat.u_color,
                          g_game.chat.font_texture, g_game.chat.text_vao, g_game.chat.text_vbo);

    touch_controls_invalidate_gl(false);

    world_loader_invalidate_unit_meshes();
    renderer_invalidate_curve_meshes();
    clear_net_mesh_cache();
    g_unit_meshes_ready = false;
    ensure_unit_net_meshes();

    for (uint32_t i = 0; i < g_game.scene.count; i++) {
        Entity* e = &g_game.scene.entities[i];
        if (!e->active || !e->mesh) continue;
        if (e->mesh == &g_unit_box_mesh || e->mesh == &g_unit_sphere_mesh ||
            e->mesh == &g_unit_cylinder_mesh || e->mesh == &g_unit_wedge_mesh) {
            continue;
        }
        if (e->mesh->index_count == 36 || e->mesh->vao == 0)
            e->mesh = &g_unit_box_mesh;
    }

    renderer_recreate_gl(&g_game.renderer);

    memset(&g_game.skybox, 0, sizeof(g_game.skybox));
    skybox_init(&g_game.skybox);
    {
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
        Vec3 ld = vec3_normalize(g_game.renderer.light_dir);
        Vec3 sun = {-ld.x, -ld.y, -ld.z};
        float sun_yaw = atan2f(sun.x, sun.z);
        float cubemap_sun_yaw = (float)M_PI * 0.5f;
        skybox_set_yaw(&g_game.skybox, (sun_yaw - cubemap_sun_yaw) * (180.0f / (float)M_PI));
    }

    memset(&g_game.avatar_gpu_mesh, 0, sizeof(g_game.avatar_gpu_mesh));
    {
        MeshData avatar_box;
        if (create_box_mesh(&avatar_box, 0.4f, 0.9f, 0.4f)) {
            mesh_upload(&avatar_box, &g_game.avatar_gpu_mesh);
            mesh_data_free(&avatar_box);
        }
    }
    ensure_avatar_bodies_loaded();
    refresh_local_avatar_meshes();
    apply_local_avatar();
    platform_load_file("assets/guestavatar.png", on_guest_avatar_texture_loaded, NULL);
}

LoginScreen* pw_game_login_screen(void) {
    return &g_game.login_screen;
}

Chat* pw_game_chat(void) {
    return &g_game.chat;
}

int pw_android_chat_wants_ime(void) {
    return (!g_game.show_login && g_game.chat.focused) ? 1 : 0;
}
#endif

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !defined(PW_IOS)
static void pw_studio_play_load_guest_avatar(void) {

    g_game.local_equipped_package = 0;
    g_game.login_screen.equipped_package = 0;
    ensure_avatar_bodies_loaded();
    platform_load_file("assets/guestavatar.png", on_guest_avatar_texture_loaded, NULL);
    {
        char path_shirt[512], path_pants[512], path_heads[512];
        const char* host = g_game.host[0] ? g_game.host : "https://polyworld.games";
        snprintf(path_shirt, sizeof(path_shirt), "%s/uploads/shirts/guest.png", host);
        snprintf(path_pants, sizeof(path_pants), "%s/uploads/pants/guest.png", host);
        snprintf(path_heads, sizeof(path_heads), "%s/uploads/heads/19.png", host);
        platform_load_file(path_shirt, on_avatar_texture_loaded, (void*)(intptr_t)0);
        platform_load_file(path_pants, on_avatar_texture_loaded, (void*)(intptr_t)1);
        platform_load_file(path_heads, on_avatar_texture_loaded, (void*)(intptr_t)2);
    }
    refresh_local_avatar_meshes();
    apply_local_avatar();
}

bool pw_studio_host_intercept_close(void) {
    if (g_studio_host_play) {
        g_studio_host_stop = true;
        return true;
    }
    return false;
}

bool pw_studio_play_wants_stop(void) {
    return g_studio_host_stop;
}

bool pw_studio_play_blocks_look(void) {
    return g_game.menu.open || g_game.chat.focused ||
           g_game.show_disconnect || g_game.show_kick || g_game.show_login;
}

void pw_studio_play_set_view(int view_w, int view_h) {
    if (view_w < 16) view_w = 16;
    if (view_h < 16) view_h = 16;
    g_host_canvas_w = view_w;
    g_host_canvas_h = view_h;
    platform_host_set_canvas(view_w, view_h);
    if (g_game.initialized)
        renderer_resize(&g_game.renderer, view_w, view_h);
}

unsigned int pw_studio_play_fbo(void) {
    return renderer_host_fbo(&g_game.renderer);
}

void pw_studio_play_frame(double dt) {
    if (!g_game.initialized) return;
    platform_http_pump();
    game_frame(dt);
}

void pw_studio_play_stop(void) {
    if (g_game.multiplayer)
        net_client_disconnect(&g_game.net);
    clear_game_world();
    g_game.studio_playtest = false;
    g_game.menu.studio_playtest = false;
    g_game.loading_world = false;
    g_game.show_disconnect = false;
    g_game.show_kick = false;
    renderer_set_host_present(&g_game.renderer, false);
    input_release_all();
    platform_set_cursor_captured(false);
    g_studio_host_play = false;
    g_studio_host_stop = false;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool pw_studio_play_start(const char* polyworld_url, int view_w, int view_h) {
    if (!polyworld_url || !polyworld_url[0]) return false;
    g_studio_host_stop = false;
    g_studio_host_play = true;
    pw_studio_play_set_view(view_w, view_h);

    g_game.host = "https://polyworld.games";
    emote_clip_set_host(g_game.host);
    g_game.studio_playtest = true;
    g_game.show_login = false;
    g_game.loading_world = true;

    if (!g_studio_game_inited) {
        if (!game_init()) {
            g_studio_host_play = false;
            return false;
        }
        g_studio_game_inited = true;
        pw_studio_play_load_guest_avatar();
    } else {
        g_game.studio_playtest = true;
        g_game.menu.studio_playtest = true;
        g_game.show_login = false;
        g_game.loading_world = true;
        renderer_resize(&g_game.renderer, g_host_canvas_w, g_host_canvas_h);
    }
    renderer_set_host_present(&g_game.renderer, true);
    g_game.studio_playtest = true;
    g_game.menu.studio_playtest = true;
    join_from_polyworld_url(polyworld_url);
    return true;
}
#endif

#if !defined(__ANDROID__) && !defined(PW_STUDIO_HOST) && !defined(PW_IOS)
#ifndef __EMSCRIPTEN__

static bool pw_handler_exec_is(const char* text, const char* exe) {
    if (!text || !exe || !exe[0]) return false;
    size_t n = strlen(exe);
    const char* p = strstr(text, exe);
    while (p) {
        char next = p[n];
        if (next == '\0' || next == ' ' || next == '"' || next == '\t' || next == '\n')
            return true;
        p = strstr(p + 1, exe);
    }
    return false;
}

#ifdef VR
static void pw_vr_sibling_client(const char* vr_exe, char* out, size_t out_sz) {
    if (!out || out_sz < 2) return;
    snprintf(out, out_sz, "%s", vr_exe ? vr_exe : "");
    char* base = strrchr(out, '/');
#ifdef _WIN32
    if (!base) base = strrchr(out, '\\');
#endif
    base = base ? base + 1 : out;
    if (strncmp(base, "polyworld_vr", 12) != 0) return;
    char rest[64];
    snprintf(rest, sizeof(rest), "%s", base + 12);
    size_t cap = out_sz - (size_t)(base - out);
    if (cap < 10) return;
    snprintf(base, cap, "polyworld%s", rest);
}

static void pw_unsteal_vr_protocol_handler(const char* vr_exe) {
    if (!vr_exe || !vr_exe[0]) return;
    char game[512];
    pw_vr_sibling_client(vr_exe, game, sizeof(game));
    if (!game[0] || strcmp(game, vr_exe) == 0) return;
#ifdef _WIN32
    {
        FILE* t = fopen(game, "rb");
        if (!t) return;
        fclose(t);
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER,
                          "Software\\Classes\\polyworld\\shell\\open\\command",
                          0, KEY_READ, &hKey) != ERROR_SUCCESS)
            return;
        char existing[MAX_PATH + 32];
        DWORD existing_sz = sizeof(existing);
        DWORD type = 0;
        LONG ok = RegQueryValueExA(hKey, NULL, NULL, &type, (LPBYTE)existing, &existing_sz);
        RegCloseKey(hKey);
        if (ok != ERROR_SUCCESS || type != REG_SZ || !pw_handler_exec_is(existing, vr_exe))
            return;
        char cmd_val[MAX_PATH + 16];
        snprintf(cmd_val, sizeof(cmd_val), "\"%s\" \"%%1\"", game);
        if (RegCreateKeyExA(HKEY_CURRENT_USER,
                            "Software\\Classes\\polyworld\\shell\\open\\command",
                            0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
            return;
        RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE*)cmd_val, (DWORD)strlen(cmd_val) + 1);
        RegCloseKey(hKey);
        PW_WARN("[VR] Restored polyworld:// handler to %s\n", game);
    }
#else
    if (access(game, X_OK) != 0) return;
    const char* home = getenv("HOME");
    if (!home) return;
    char desktop_path[512];
    snprintf(desktop_path, sizeof(desktop_path),
             "%s/.local/share/applications/polyworld.desktop", home);
    FILE* df = fopen(desktop_path, "r");
    if (!df) return;
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, df);
    buf[n] = '\0';
    fclose(df);
    if (!pw_handler_exec_is(buf, vr_exe)) return;
    char out[4096];
    char* d = out;
    const char* s = buf;
    size_t elen = strlen(vr_exe);
    size_t glen = strlen(game);
    while (*s && (size_t)(d - out) + glen + 1 < sizeof(out)) {
        if (strncmp(s, vr_exe, elen) == 0) {
            char next = s[elen];
            if (next == '\0' || next == ' ' || next == '"' || next == '\t' || next == '\n') {
                memcpy(d, game, glen);
                d += glen;
                s += elen;
                continue;
            }
        }
        *d++ = *s++;
    }
    *d = '\0';
    FILE* wf = fopen(desktop_path, "w");
    if (!wf) return;
    fputs(out, wf);
    fclose(wf);
    system("xdg-mime default polyworld.desktop x-scheme-handler/polyworld 2>/dev/null");
    system("update-desktop-database ~/.local/share/applications 2>/dev/null");
    PW_WARN("[VR] Restored polyworld:// handler to %s\n", game);
#endif
}
#endif
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif
    pw_log_parse_args(argc, argv);
    vidactor_parse_args(argc, argv);

#ifndef __EMSCRIPTEN__

    discord_init("1516918398792110130");

#ifdef _WIN32
    {
        char exe_dir[MAX_PATH];
        GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
        char* last = strrchr(exe_dir, '\\');
        if (last) { *last = '\0'; SetCurrentDirectoryA(exe_dir); }
    }
#else
    {
        char exe_path[512] = {0};
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (len > 0) {
            exe_path[len] = '\0';
            char* last = strrchr(exe_path, '/');
            if (last) {
                *last = '\0';
                chdir(exe_path);

                struct stat st;
                if (stat("assets", &st) != 0) {
                    chdir("..");
                }
            }
        }
    }
#endif
#endif

#ifndef __EMSCRIPTEN__

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--force-update") == 0) {
            updater_set_force(true);
        }
    }

#ifdef VR
#ifdef _WIN32
    {
        char vr_exe[MAX_PATH];
        GetModuleFileNameA(NULL, vr_exe, MAX_PATH);
        pw_unsteal_vr_protocol_handler(vr_exe);
    }
#else
    {
        char vr_exe[512] = {0};
        ssize_t el = readlink("/proc/self/exe", vr_exe, sizeof(vr_exe) - 1);
        if (el > 0) {
            vr_exe[el] = '\0';
            pw_unsteal_vr_protocol_handler(vr_exe);
        }
    }
#endif
#elif defined(_WIN32)
    {
        char exe_path[MAX_PATH];
        GetModuleFileNameA(NULL, exe_path, MAX_PATH);
        char cmd_val[MAX_PATH + 16];
        snprintf(cmd_val, sizeof(cmd_val), "\"%s\" \"%%1\"", exe_path);

        bool needs_install = true;
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER,
                          "Software\\Classes\\polyworld\\shell\\open\\command",
                          0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char existing[MAX_PATH + 32];
            DWORD existing_sz = sizeof(existing);
            DWORD type = 0;
            if (RegQueryValueExA(hKey, NULL, NULL, &type, (LPBYTE)existing, &existing_sz) == ERROR_SUCCESS
                && type == REG_SZ && pw_handler_exec_is(existing, exe_path)) {
                needs_install = false;
            }
            RegCloseKey(hKey);
        }

        if (needs_install) {
            char old_dir[1024] = {0};
            char new_dir[1024] = {0};
            platform_find_registered_client_datadir(old_dir, sizeof(old_dir));
            if (!GetCurrentDirectoryA((DWORD)sizeof(new_dir), new_dir)) new_dir[0] = '\0';
            if (old_dir[0] && new_dir[0])
                platform_migrate_userdata_files(old_dir, new_dir);
            if (new_dir[0])
                platform_set_userdata_dir(new_dir);

            RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Classes\\polyworld", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
            const char* desc = "PolyWorld Protocol";
            RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE*)desc, (DWORD)strlen(desc)+1);
            RegSetValueExA(hKey, "URL Protocol", 0, REG_SZ, (const BYTE*)"", 1);
            RegCloseKey(hKey);

            RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Classes\\polyworld\\shell\\open\\command", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
            RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE*)cmd_val, (DWORD)strlen(cmd_val)+1);
            RegCloseKey(hKey);
        }
    }
#else
    {

        char desktop_path[512], exe_path2[512] = {0};
        ssize_t el = readlink("/proc/self/exe", exe_path2, sizeof(exe_path2)-1);
        if (el > 0) {
            exe_path2[el] = '\0';
            snprintf(desktop_path, sizeof(desktop_path), "%s/.local/share/applications/polyworld.desktop", getenv("HOME"));
            FILE* df = fopen(desktop_path, "r");
            bool needs_install = true;
            if (df) {
                char buf[2048]; size_t n = fread(buf, 1, sizeof(buf)-1, df); buf[n] = '\0';
                fclose(df);
                if (pw_handler_exec_is(buf, exe_path2) && strstr(buf, "StartupWMClass=polyworld")
                    && strstr(buf, "Icon=") && strstr(buf, "\nPath=")) needs_install = false;
            }
            if (needs_install) {
                char old_dir[1024] = {0};
                char new_dir[1024] = {0};
                platform_find_registered_client_datadir(old_dir, sizeof(old_dir));
                if (!getcwd(new_dir, sizeof(new_dir))) new_dir[0] = '\0';
                if (old_dir[0] && new_dir[0])
                    platform_migrate_userdata_files(old_dir, new_dir);
                if (new_dir[0])
                    platform_set_userdata_dir(new_dir);

                char mkdir_cmd[512];
                snprintf(mkdir_cmd, sizeof(mkdir_cmd),
                         "mkdir -p \"%s/.local/share/applications\" \"%s/.local/share/polyworld\"",
                         getenv("HOME"), getenv("HOME"));
                system(mkdir_cmd);

                char icon_src[576], icon_dst[576];
                char bindir[512];
                strncpy(bindir, exe_path2, sizeof(bindir)-1);
                bindir[sizeof(bindir)-1] = '\0';
                char* slash = strrchr(bindir, '/');
                if (slash) *slash = '\0';
                snprintf(icon_src, sizeof(icon_src), "%s/../assets/polyworld.png", bindir);
                {
                    FILE* test = fopen(icon_src, "rb");
                    if (!test) snprintf(icon_src, sizeof(icon_src), "%s/assets/polyworld.png", bindir);
                    else fclose(test);
                }
                snprintf(icon_dst, sizeof(icon_dst),
                         "%s/.local/share/polyworld/polyworld.png", getenv("HOME"));
                {
                    char cp_cmd[1200];
                    snprintf(cp_cmd, sizeof(cp_cmd), "cp -f \"%s\" \"%s\" 2>/dev/null", icon_src, icon_dst);
                    system(cp_cmd);
                }

                FILE* wf = fopen(desktop_path, "w");
                if (wf) {
                    fprintf(wf,
                        "[Desktop Entry]\n"
                        "Type=Application\n"
                        "Name=PolyWorld\n"
                        "GenericName=PolyWorld Game Client\n"
                        "Comment=Play games on PolyWorld\n"
                        "Icon=%s\n"
                        "Path=%s\n"
                        "StartupWMClass=polyworld\n"
                        "MimeType=x-scheme-handler/polyworld;\n"
                        "Categories=Game;\n"
                        "Terminal=false\n"
                        "SingleMainWindow=true\n"
                        "Exec=%s -- %%u\n",
                        icon_dst, new_dir[0] ? new_dir : bindir, exe_path2);
                    fclose(wf);
                    system("xdg-mime default polyworld.desktop x-scheme-handler/polyworld 2>/dev/null");
                    system("update-desktop-database ~/.local/share/applications 2>/dev/null");
                }
            }
        }
    }
#endif

    g_game.host = pw_site_origin();
    emote_clip_set_host(g_game.host);
    bool custom_host_provided = false;
    bool local_mode = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0) {
            if (i + 1 < argc) {
                pw_set_site_origin(argv[i + 1]);
                g_game.host = pw_site_origin();
                emote_clip_set_host(g_game.host);
                custom_host_provided = true;
                i++;
            }
        } else if (strcmp(argv[i], "--local") == 0) {
            pw_use_local_site();
            g_game.host = pw_site_origin();
            emote_clip_set_host(g_game.host);
            local_mode = true;
        }
    }
    updater_check();

    bool launch_into_game = argv_launches_into_game(argc, argv);
    if (argv_has_flag(argc, argv, "--studio-playtest")) {
        g_game.studio_playtest = true;
        g_game.menu.studio_playtest = true;
    }
    {
        const char* embed_hp = NULL;
        uint64_t embed_parent = 0;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--studio-embed") == 0 && i + 1 < argc) {
                embed_hp = argv[++i];
            } else if (strcmp(argv[i], "--studio-embed-parent") == 0 && i + 1 < argc) {
                embed_parent = strtoull(argv[++i], NULL, 10);
            }
        }
        if (embed_hp && embed_hp[0])
            pw_embed_client_configure(embed_hp, embed_parent);
    }
    if (launch_into_game) {
        g_game.show_login = false;
        g_game.loading_world = true;
    } else if (!custom_host_provided || local_mode) {
        g_game.show_login = true;
    }

#endif

    int init_w = 1280, init_h = 720;
#ifndef __EMSCRIPTEN__
    if (g_game.show_login) { init_w = 1091; init_h = 711; }
#endif
    if (!platform_init(init_w, init_h,
                       g_game.studio_playtest ? "PolyWorld Playtest" : vidactor_window_title())) {

        PW_ERR(ERR_GENERIC, "Platform init failed\n");
#ifndef __EMSCRIPTEN__
        discord_shutdown();
#endif
        return 1;
    }

#ifndef __EMSCRIPTEN__
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    platform_flush_frame();
#endif

    g_game.multiplayer = false;
#ifdef __EMSCRIPTEN__
    g_game.loading_world = false;
    {
        int game_id = EM_ASM_INT({
            var params = new URLSearchParams(window.location.search);
            var id = params.get('id');
            return id ? parseInt(id) : 0;
        });
        if (game_id > 0) g_game.loading_world = true;
    }
#else

    if (argv_launches_into_game(argc, argv)) g_game.loading_world = true;
#endif

    if (!game_init()) {
        PW_ERR(ERR_GENERIC, "Game init failed\n");
        #ifndef __EMSCRIPTEN__
        discord_shutdown();
        #endif
        return 1;
    }

#ifndef __EMSCRIPTEN__

    if (g_game.studio_playtest)
        g_game.menu.studio_playtest = true;

    if (g_game.loading_world && !g_game.show_login)
        paint_loading_now("Loading");
#endif

    if (updater_server_unreachable()) {
        PW_LOG("Updater: version check unreachable; continuing\n");
    }

#ifndef __EMSCRIPTEN__

    if (g_game.show_login) {
        discord_update_presence("Browsing menus", "Not In-game...", 0, 0, NULL, 0, false);
        login_screen_init(&g_game.login_screen);

        char saved_token[128] = {0};
        if (auth_load_session(saved_token, sizeof(saved_token))) {
            AuthResult vr = auth_validate_token(saved_token);
            if (vr.authenticated) {
                strncpy(g_game.session_token, saved_token, sizeof(g_game.session_token) - 1);
                strncpy(g_game.username, vr.username, sizeof(g_game.username) - 1);
                strncpy(g_game.login_screen.session_token, saved_token,
                        sizeof(g_game.login_screen.session_token) - 1);
                strncpy(g_game.login_screen.username, vr.username,
                        sizeof(g_game.login_screen.username) - 1);
                g_game.login_screen.username_len = (int)strlen(g_game.login_screen.username);
                g_game.login_screen.logged_in = true;
                g_game.login_screen.phase = 1;
                g_game.login_screen.games_fetched = false;
            } else {
                auth_clear_session();
            }
        }
#ifdef VR
        if (g_game.vr.active)
            enter_vr_hub(&g_game.login_screen);
#endif
    }
#endif

#ifdef __EMSCRIPTEN__
    ensure_avatar_bodies_loaded();
    platform_load_file("https://polyworld.games/assets/wasm/guestavatar.png", on_guest_avatar_texture_loaded, NULL);
#else
    ensure_avatar_bodies_loaded();
    platform_load_file("assets/guestavatar.png", on_guest_avatar_texture_loaded, NULL);
    char path_shirt[512];
    char path_pants[512];
    char path_heads[512];

    snprintf(path_shirt, sizeof(path_shirt), "%s/uploads/shirts/guest.png", g_game.host);
    snprintf(path_pants, sizeof(path_pants), "%s/uploads/pants/guest.png", g_game.host);
    snprintf(path_heads, sizeof(path_heads), "%s/uploads/heads/19.png", g_game.host);

    platform_load_file(path_shirt, on_avatar_texture_loaded, (void*)0);
    platform_load_file(path_pants, on_avatar_texture_loaded, (void*)1);
    platform_load_file(path_heads, on_avatar_texture_loaded, (void*)2);

#endif

#ifdef __EMSCRIPTEN__
    if (g_game.loading_world) {
        int game_id = EM_ASM_INT({
            var params = new URLSearchParams(window.location.search);
            var id = params.get('id');
            return id ? parseInt(id) : 0;
        });
        if (game_id > 0) {
            g_game.game_id = game_id;
            char ws_host[128];
            EM_ASM({
                var host = window.location.hostname;
                stringToUTF8(host, $0, 128);
            }, ws_host);
            int ws_port = EM_ASM_INT({
                return parseInt(window.location.port) || 443;
            });
            EM_ASM({
                var el = document.getElementById('pw-username');
                if (el && el.value && el.value.length > 0) {
                    stringToUTF8(el.value, $0, 32);
                } else {
                    var num = Math.floor(Math.random() * 9000) + 1000;
                    stringToUTF8("Guest" + num, $0, 32);
                }
            }, g_game.username);

            EM_ASM({
                var el = document.getElementById('pw-join-ticket');
                if (el && el.value && el.value.length > 0) {
                    stringToUTF8(el.value, $0, 33);
                }
            }, g_game.join_ticket);

            EM_ASM({
                var el = document.getElementById('pw-avatar-color');
                if (el && el.value && el.value.length >= 7) {
                    stringToUTF8(el.value, $0, 8);
                }
            }, g_game.avatar_color);

            {
                char skin_hex[8] = "#eaeaea";
                int eq_shirt = 1, eq_pants = 10, eq_head = 19, eq_package = 0;
                int eq_accessories[PW_MAX_EQUIPPED_ACCESSORIES] = {0};
                EM_ASM({
                    var sc = document.getElementById('pw-skin-color');
                    if (sc && sc.value && sc.value.length >= 7) stringToUTF8(sc.value, $0, 8);
                    setValue($1, parseInt(document.getElementById('pw-equipped-shirt')?.value || '0'), 'i32');
                    setValue($2, parseInt(document.getElementById('pw-equipped-pants')?.value || '0'), 'i32');
                    setValue($3, parseInt(document.getElementById('pw-equipped-head')?.value || '0'), 'i32');
                    setValue($5, parseInt(document.getElementById('pw-equipped-package')?.value || '0'), 'i32');
                    var csv = document.getElementById('pw-equipped-accessories')?.value || '';
                    if (!csv) csv = document.getElementById('pw-equipped-accessory')?.value || '0';
                    var parts = String(csv).split(',').map(function (s) { return parseInt(s, 10) || 0; }).filter(function (n) { return n > 0; });
                    for (var i = 0; i < $6; i++) setValue($4 + i * 4, parts[i] || 0, 'i32');
                }, skin_hex, &eq_shirt, &eq_pants, &eq_head, &eq_accessories[0], &eq_package, PW_MAX_EQUIPPED_ACCESSORIES);

                g_game.local_equipped_package = normalize_mesh_flags(eq_package);
                reload_local_avatar_from_ids(skin_hex, eq_shirt, eq_pants, eq_head, eq_accessories);
            }

            if (net_client_connect(&g_game.net, ws_host, ws_port)) {
                g_game.multiplayer = true;
            }
        }
    }
#else
    if (g_game.loading_world && argc > 1) {
        const char* arg = argv_find_launch_arg(argc, argv);
        if (!arg) arg = argv[1];
        int arg_start = 1;
        for (int i = 1; i < argc; i++) {
            if (argv[i] == arg) { arg_start = i; break; }
        }
        (void)arg_start;

        bool got_identity = false;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--guest") == 0) {

                unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)pw_getpid();
                int num = (int)(seed % 9000) + 1000;
                snprintf(g_game.username, sizeof(g_game.username), "Guest%d", num);
                got_identity = true;
            } else if (strcmp(argv[i], "--session") == 0 && i + 1 < argc) {

                AuthResult auth = auth_validate_token(argv[i + 1]);
                if (auth.authenticated) {
                    strncpy(g_game.username, auth.username, sizeof(g_game.username) - 1);
                        g_game.account_id = (uint32_t)auth.user_id;
                    strncpy(g_game.session_token, auth.token, sizeof(g_game.session_token) - 1);
                } else {
                    PW_ERR(ERR_GENERIC, "Invalid session\n");
                    g_game.show_login = true;
                    got_identity = true;
                    i++;
                    continue;
                }
                i++;
                got_identity = true;
            } else if (strcmp(argv[i], "--login") == 0 && i + 1 < argc) {
                const char* cred = argv[i + 1];
                const char* comma = strchr(cred, ',');
                if (comma) {
                    char user[32] = {0}, pass[64] = {0};
                    size_t ulen = (size_t)(comma - cred);
                    if (ulen > 31) ulen = 31;
                    memcpy(user, cred, ulen);
                    strncpy(pass, comma + 1, 63);

                    AuthResult auth = auth_login(user, pass);
                    if (auth.authenticated) {
                        strncpy(g_game.username, auth.username, sizeof(g_game.username) - 1);
                        g_game.account_id = (uint32_t)auth.user_id;
                        strncpy(g_game.session_token, auth.token, sizeof(g_game.session_token) - 1);
                    } else if (auth.needs_2fa) {
                        PW_ERR(ERR_GENERIC, "This account requires 2FA. Log in from the client.\n");
                        g_game.show_login = true;
                    } else {
                        PW_ERR(ERR_GENERIC, "Invalid session\n");
                        g_game.show_login = true;
                    }
                } else {
                    strncpy(g_game.username, cred, 31);
                    g_game.username[31] = '\0';
                }
                i++;
                got_identity = true;
            }
        }
        if (!got_identity) {
            g_game.username[0] = '\0';
        }

        if (strncmp(arg, "polyworld://", 12) == 0) {
            join_from_polyworld_url(arg);
        } else if (strcmp(arg, "--connect") == 0 && argc > 2) {
            int game_id = atoi(argv[2]);
            const char* host = PW_TCP_HOST;
            int port = PW_TCP_PORT;
            int pos = 3;
            while (pos < argc) {
                if (argv[pos][0] == '-') {
                    pos++;
                    if (pos < argc && argv[pos][0] != '-') pos++;
                } else {
                    if (strcmp(host, PW_TCP_HOST) == 0) {
                        host = argv[pos];
                    } else {
                        port = atoi(argv[pos]);
                    }
                    pos++;
                }
            }
            if (game_id > 0) {
                g_game.game_id = game_id;
                g_game.show_login = false;
                g_game.loading_world = true;
                join_multiplayer_game(game_id, 0, host, port, false);
            } else {
                begin_disconnect("Invalid join link.");
            }
        } else {
            platform_load_file(arg, on_world_loaded, NULL);
        }
    }
#endif

#ifndef __EMSCRIPTEN__
    if (g_game.loading_world && !g_game.show_login && !g_game.world_ready)
        paint_loading_now("Loading");
#endif

    platform_run_loop(game_frame);

    return 0;
}
#endif
