/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: vidactor.c                                                                          |
|   Purpose: record/play movement+chat across a few accounts                                  |
\*-------------------------------------------------------------------------------------------*/

#include "vidactor.h"

#include "auth.h"
#include "platform.h"
#include "avatar_anim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef VIDACTOR

void vidactor_parse_args(int argc, char** argv) { (void)argc; (void)argv; }
int vidactor_slot(void) { return 0; }
const char* vidactor_window_title(void) { return "Polyworld"; }
void vidactor_init(void) {}
void vidactor_shutdown(void) {}
void vidactor_handle_input(bool chat_active,
                           bool key_f6, bool key_f7, bool key_f8, bool key_f9, bool key_f10) {
    (void)chat_active; (void)key_f6; (void)key_f7; (void)key_f8; (void)key_f9; (void)key_f10;
}
bool vidactor_handle_chat(const char* pending) { (void)pending; return false; }
bool vidactor_ui_hidden(void) { return false; }
bool vidactor_is_recording(void) { return false; }
bool vidactor_is_playing(void) { return false; }
bool vidactor_is_armed(void) { return false; }
bool vidactor_is_staging(void) { return false; }
void vidactor_record_move(float x, float y, float z, float yaw, uint8_t anim) {
    (void)x; (void)y; (void)z; (void)yaw; (void)anim;
}
void vidactor_record_chat(const char* text) { (void)text; }
bool vidactor_playback_tick(double dt, VidPose* out_pose, char* out_chat, size_t out_chat_sz) {
    (void)dt; (void)out_pose; (void)out_chat; (void)out_chat_sz;
    return false;
}
int vidactor_stage_tick(double dt, VidPose poses[VIDACTOR_MAX_ACTORS]) {
    (void)dt;
    if (poses) memset(poses, 0, sizeof(VidPose) * VIDACTOR_MAX_ACTORS);
    return 0;
}
const char* vidactor_status_line(void) { return ""; }

#else

#define VID_MAGIC "PWRV"
#define VID_VERSION 1
#define VID_EVT_MOVE 1
#define VID_EVT_CHAT 2
#define VID_MAX_EVENTS 120000
#define VID_CHAT_MAX 128
#define VID_COUNTDOWN_SEC 0.75

typedef struct {
    uint32_t t_ms;
    uint8_t type;
    uint8_t anim;
    uint16_t chat_len;
    float x, y, z, yaw;
    char chat[VID_CHAT_MAX];
} VidEvent;

typedef enum {
    VID_IDLE = 0,
    VID_RECORDING,
    VID_ARMED,
    VID_PLAYING,
    VID_STAGING,
} VidMode;

typedef struct {
    VidEvent* events;
    int count;
    int cap;
    int play_idx;
    bool loaded;
    bool finished;
    VidPose pose;
    bool pose_valid;
    Vec3 prev_pos;
    bool have_prev;
    double prev_t;
} VidTrack;

static int g_slot = 0;
static char g_load_path[512];
static char g_title[64] = "PolyWorld VidActor";
static char g_tape_name[64] = "vidactor.pwrec";
static char g_status[192];
static char g_notice[192];
static double g_notice_until = 0.0;

static VidMode g_mode = VID_IDLE;
static bool g_ui_hidden = false;

static VidEvent* g_events = NULL;
static int g_event_count = 0;
static int g_event_cap = 0;

static double g_rec_t0 = 0.0;
static float g_last_x, g_last_y, g_last_z, g_last_yaw;
static uint8_t g_last_anim;
static bool g_have_last_move = false;
static double g_last_move_sample = 0.0;

static int g_play_idx = 0;
static double g_play_t = 0.0;
static double g_play_start_at = 0.0;
static VidPose g_play_pose;
static bool g_play_pose_valid = false;
static Vec3 g_play_prev_pos;
static bool g_play_have_prev = false;
static double g_play_prev_t = 0.0;

static VidTrack g_stage[VIDACTOR_MAX_ACTORS];
static double g_stage_t = 0.0;
static bool g_stage_done_notice = false;

static double now_sec(void) {
    return platform_get_time();
}

static void set_notice(const char* msg) {
    if (!msg) msg = "";
    snprintf(g_notice, sizeof(g_notice), "%s", msg);
    g_notice_until = now_sec() + 3.5;
}

static void refresh_names(void) {
    if (g_slot >= 1 && g_slot <= 4) {
        snprintf(g_title, sizeof(g_title), "PolyWorld VidActor %d", g_slot);
        snprintf(g_tape_name, sizeof(g_tape_name), "vidactor_%d.pwrec", g_slot);
    } else {
        snprintf(g_title, sizeof(g_title), "PolyWorld VidActor");
        snprintf(g_tape_name, sizeof(g_tape_name), "vidactor.pwrec");
    }
}

static void tape_path_named(const char* name, char* out, size_t out_sz) {
    if (!platform_userdata_path(name, out, out_sz))
        snprintf(out, out_sz, "%s", name);
}

static void tape_path(char* out, size_t out_sz) {
    if (g_load_path[0]) {
        snprintf(out, out_sz, "%s", g_load_path);
        return;
    }
    tape_path_named(g_tape_name, out, out_sz);
}

static void sync_path(char* out, size_t out_sz) {
    const char* home = getenv("HOME");
    if (home && home[0])
        snprintf(out, out_sz, "%s/.cache/polyworld_vidactor_go", home);
    else
        snprintf(out, out_sz, "/tmp/polyworld_vidactor_go");
}

static bool ensure_cap_buf(VidEvent** events, int* cap, int need) {
    if (need <= *cap) return true;
    int ncap = *cap ? *cap * 2 : 4096;
    while (ncap < need) ncap *= 2;
    if (ncap > VID_MAX_EVENTS) ncap = VID_MAX_EVENTS;
    if (need > ncap) return false;
    VidEvent* n = (VidEvent*)realloc(*events, (size_t)ncap * sizeof(VidEvent));
    if (!n) return false;
    *events = n;
    *cap = ncap;
    return true;
}

static bool ensure_cap(int need) {
    return ensure_cap_buf(&g_events, &g_event_cap, need);
}

static uint32_t rec_ms(void) {
    double t = now_sec() - g_rec_t0;
    if (t < 0.0) t = 0.0;
    if (t > 86400.0) t = 86400.0;
    return (uint32_t)(t * 1000.0);
}

static void clear_events(void) {
    g_event_count = 0;
    g_have_last_move = false;
}

static float horiz_speed(Vec3 a, Vec3 b, double dt) {
    if (dt <= 1e-4) return 0.0f;
    float dx = b.x - a.x, dz = b.z - a.z;
    float dist = sqrtf(dx * dx + dz * dz);
    float spd = dist / (float)dt;
    if (spd > 80.0f) spd = 80.0f;
    return spd;
}

static void apply_move_to_pose(VidPose* pose, const VidEvent* e,
                               Vec3* prev_pos, bool* have_prev, double* prev_t, double cur_t) {
    Vec3 p = {e->x, e->y, e->z};
    float spd = 0.0f;
    if (*have_prev) {
        spd = horiz_speed(*prev_pos, p, cur_t - *prev_t);

                if (e->anim == ANIM_STATE_WALKING && spd < 2.0f) spd = 16.0f;
    } else if (e->anim == ANIM_STATE_WALKING) {
        spd = 16.0f;
    }
    pose->pos = p;
    pose->yaw = e->yaw;
    pose->anim = e->anim;
    pose->move_speed = spd;
    pose->active = true;
    *prev_pos = p;
    *have_prev = true;
    *prev_t = cur_t;
}

static bool save_tape(void) {
    char path[512];
    tape_path(path, sizeof(path));
    FILE* f = fopen(path, "wb");
    if (!f) {
        set_notice("VidActor: save failed");
        return false;
    }
    fwrite(VID_MAGIC, 1, 4, f);
    uint16_t ver = VID_VERSION, zero = 0;
    fwrite(&ver, 2, 1, f);
    fwrite(&zero, 2, 1, f);
    for (int i = 0; i < g_event_count; i++) {
        VidEvent* e = &g_events[i];
        fwrite(&e->t_ms, 4, 1, f);
        fwrite(&e->type, 1, 1, f);
        if (e->type == VID_EVT_MOVE) {
            fwrite(&e->x, 4, 1, f);
            fwrite(&e->y, 4, 1, f);
            fwrite(&e->z, 4, 1, f);
            fwrite(&e->yaw, 4, 1, f);
            fwrite(&e->anim, 1, 1, f);
        } else if (e->type == VID_EVT_CHAT) {
            uint16_t len = e->chat_len;
            if (len > VID_CHAT_MAX - 1) len = VID_CHAT_MAX - 1;
            fwrite(&len, 2, 1, f);
            if (len) fwrite(e->chat, 1, len, f);
        }
    }
    fclose(f);
    char msg[192];
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    snprintf(msg, sizeof(msg), "VidActor: saved %d events -> %s", g_event_count, base);
    set_notice(msg);
    return true;
}

static bool load_tape_into(VidEvent** events, int* count, int* cap, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, VID_MAGIC, 4) != 0) {
        fclose(f);
        return false;
    }
    uint16_t ver = 0, pad = 0;
    if (fread(&ver, 2, 1, f) != 1 || fread(&pad, 2, 1, f) != 1 || ver != VID_VERSION) {
        fclose(f);
        return false;
    }
    *count = 0;
    while (*count < VID_MAX_EVENTS) {
        VidEvent e;
        memset(&e, 0, sizeof(e));
        if (fread(&e.t_ms, 4, 1, f) != 1) break;
        if (fread(&e.type, 1, 1, f) != 1) break;
        if (e.type == VID_EVT_MOVE) {
            if (fread(&e.x, 4, 1, f) != 1) break;
            if (fread(&e.y, 4, 1, f) != 1) break;
            if (fread(&e.z, 4, 1, f) != 1) break;
            if (fread(&e.yaw, 4, 1, f) != 1) break;
            if (fread(&e.anim, 1, 1, f) != 1) break;
        } else if (e.type == VID_EVT_CHAT) {
            if (fread(&e.chat_len, 2, 1, f) != 1) break;
            if (e.chat_len >= VID_CHAT_MAX) e.chat_len = VID_CHAT_MAX - 1;
            if (e.chat_len && fread(e.chat, 1, e.chat_len, f) != e.chat_len) break;
            e.chat[e.chat_len] = '\0';
        } else {
            break;
        }
        if (!ensure_cap_buf(events, cap, *count + 1)) break;
        (*events)[(*count)++] = e;
    }
    fclose(f);
    return *count > 0;
}

static bool load_tape(void) {
    char path[512];
    tape_path(path, sizeof(path));
    clear_events();
    if (!load_tape_into(&g_events, &g_event_count, &g_event_cap, path)) {
        set_notice("VidActor: no tape file");
        return false;
    }
    char msg[192];
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    snprintf(msg, sizeof(msg), "VidActor: loaded %d events from %s", g_event_count, base);
    set_notice(msg);
    return true;
}

static void free_stage(void) {
    for (int i = 0; i < VIDACTOR_MAX_ACTORS; i++) {
        free(g_stage[i].events);
        memset(&g_stage[i], 0, sizeof(g_stage[i]));
    }
    g_stage_t = 0.0;
}

static void stop_record(bool save) {
    if (g_mode != VID_RECORDING) return;
    g_mode = VID_IDLE;
    if (save) save_tape();
    else set_notice("VidActor: recording discarded");
}

static void stop_play(void) {
    if (g_mode == VID_STAGING) {
        free_stage();
        g_mode = VID_IDLE;
        g_ui_hidden = false;
        set_notice("VidActor: stage stopped");
        return;
    }
    g_mode = VID_IDLE;
    g_play_start_at = 0.0;
    g_play_pose_valid = false;
    g_play_have_prev = false;
    set_notice("VidActor: playback stopped");
}

static void start_record(void) {
    if (g_mode == VID_PLAYING || g_mode == VID_ARMED || g_mode == VID_STAGING) stop_play();
    clear_events();
    g_rec_t0 = now_sec();
    g_mode = VID_RECORDING;
    g_have_last_move = false;
    set_notice("VidActor: REC -- F7 stop & save");
}

static void seed_pose_from_events(VidEvent* events, int count, VidPose* pose, bool* valid) {
    *valid = false;
    memset(pose, 0, sizeof(*pose));
    for (int i = 0; i < count; i++) {
        if (events[i].type == VID_EVT_MOVE) {
            pose->pos = (Vec3){events[i].x, events[i].y, events[i].z};
            pose->yaw = events[i].yaw;
            pose->anim = events[i].anim;
            pose->move_speed = (events[i].anim == ANIM_STATE_WALKING) ? 16.0f : 0.0f;
            pose->active = true;
            *valid = true;
            break;
        }
    }
}

static void arm_play(void) {
    if (g_mode == VID_RECORDING) stop_record(true);
    if (g_mode == VID_STAGING) stop_play();
    if (!load_tape()) {
        g_mode = VID_IDLE;
        return;
    }
    g_mode = VID_ARMED;
    g_play_idx = 0;
    g_play_t = 0.0;
    g_play_start_at = 0.0;
    g_play_have_prev = false;
    seed_pose_from_events(g_events, g_event_count, &g_play_pose, &g_play_pose_valid);
    set_notice("VidActor: ARMED -- press F9 on any window to GO");
}

static bool start_stage(void) {
    if (g_mode == VID_RECORDING) stop_record(true);
    free_stage();

    int loaded = 0;
    for (int i = 0; i < VIDACTOR_MAX_ACTORS; i++) {
        char name[64], path[512];
        snprintf(name, sizeof(name), "vidactor_%d.pwrec", i + 1);
        tape_path_named(name, path, sizeof(path));
        if (load_tape_into(&g_stage[i].events, &g_stage[i].count, &g_stage[i].cap, path)) {
            g_stage[i].loaded = true;
            g_stage[i].play_idx = 0;
            g_stage[i].finished = false;
            seed_pose_from_events(g_stage[i].events, g_stage[i].count,
                                  &g_stage[i].pose, &g_stage[i].pose_valid);
            loaded++;
        }
    }

    if (loaded == 0) {
        char path[512];
        tape_path(path, sizeof(path));
        if (load_tape_into(&g_stage[0].events, &g_stage[0].count, &g_stage[0].cap, path)) {
            g_stage[0].loaded = true;
            seed_pose_from_events(g_stage[0].events, g_stage[0].count,
                                  &g_stage[0].pose, &g_stage[0].pose_valid);
            loaded = 1;
        }
    }
    if (loaded == 0) {
        set_notice("VidActor: no tapes (need vidactor_1.pwrec ...)");
        return false;
    }
    g_mode = VID_STAGING;
    g_stage_t = 0.0;
    g_stage_done_notice = false;
    g_ui_hidden = true;
    char msg[128];
    snprintf(msg, sizeof(msg), "VidActor: STAGE %d tape(s) -- freecam, UI hidden  [F10/F8 stop]", loaded);
    set_notice(msg);

    return true;
}

static void write_go_cue(void) {
    char path[512];
    sync_path(path, sizeof(path));
    {
        char dir[512];
        snprintf(dir, sizeof(dir), "%s", path);
        char* slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            char cmd[600];
            snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", dir);
            (void)system(cmd);
        }
    }
    double go_at = now_sec() + VID_COUNTDOWN_SEC;
    FILE* f = fopen(path, "w");
    if (!f) {
        set_notice("VidActor: sync write failed");
        return;
    }
    fprintf(f, "%.6f\n", go_at);
    fclose(f);
    set_notice("VidActor: GO sent -- starting soon");
}

static void poll_go_cue(void) {
    if (g_mode != VID_ARMED) return;
    char path[512];
    sync_path(path, sizeof(path));
    FILE* f = fopen(path, "r");
    if (!f) return;
    double go_at = 0.0;
    if (fscanf(f, "%lf", &go_at) != 1) {
        fclose(f);
        return;
    }
    fclose(f);
    if (go_at <= 0.0) return;
    if (g_play_start_at <= 0.0 || go_at > g_play_start_at)
        g_play_start_at = go_at;
    if (now_sec() >= g_play_start_at) {
        g_mode = VID_PLAYING;
        g_play_idx = 0;
        g_play_t = 0.0;
        g_play_start_at = 0.0;
        g_play_have_prev = false;
        set_notice("VidActor: PLAY");
    }
}

void vidactor_parse_args(int argc, char** argv) {
    g_slot = 0;
    g_load_path[0] = '\0';
    for (int i = 1; i < argc; i++) {
        if (!argv[i]) continue;
        if (strcmp(argv[i], "--vid-slot") == 0 && i + 1 < argc) {
            int s = atoi(argv[++i]);
            if (s < 0) s = 0;
            if (s > 4) s = 4;
            g_slot = s;
        } else if (strncmp(argv[i], "--vid-slot=", 11) == 0) {
            int s = atoi(argv[i] + 11);
            if (s < 0) s = 0;
            if (s > 4) s = 4;
            g_slot = s;
        } else if (strcmp(argv[i], "--vid-load") == 0 && i + 1 < argc) {
            snprintf(g_load_path, sizeof(g_load_path), "%s", argv[++i]);
        } else if (strncmp(argv[i], "--vid-load=", 11) == 0) {
            snprintf(g_load_path, sizeof(g_load_path), "%s", argv[i] + 11);
        }
    }
    refresh_names();
    if (g_slot >= 1 && g_slot <= 4) {
        char sess[64];
        snprintf(sess, sizeof(sess), "polyworld_session_vid%d.dat", g_slot);
        auth_set_session_filename(sess);
    }
}

int vidactor_slot(void) { return g_slot; }
const char* vidactor_window_title(void) { return g_title; }

void vidactor_init(void) {
    refresh_names();
    g_mode = VID_IDLE;
    g_ui_hidden = false;
    g_status[0] = '\0';
    g_notice[0] = '\0';
    memset(g_stage, 0, sizeof(g_stage));
    set_notice("VidActor: F6 UI  F7 REC  F8 PLAY  F9 GO  F10 STAGE  -- /vid help");
}

void vidactor_shutdown(void) {
    if (g_mode == VID_RECORDING) stop_record(true);
    free_stage();
    free(g_events);
    g_events = NULL;
    g_event_cap = 0;
    g_event_count = 0;
}

void vidactor_handle_input(bool chat_active,
                           bool key_f6, bool key_f7, bool key_f8, bool key_f9, bool key_f10) {
    poll_go_cue();
    if (chat_active) return;
    if (key_f6) {
        g_ui_hidden = !g_ui_hidden;
        set_notice(g_ui_hidden ? "VidActor: UI hidden (F6)" : "VidActor: UI shown (F6)");
    }
    if (key_f7) {
        if (g_mode == VID_RECORDING) stop_record(true);
        else start_record();
    }
    if (key_f8) {
        if (g_mode == VID_PLAYING || g_mode == VID_ARMED || g_mode == VID_STAGING) stop_play();
        else arm_play();
    }
    if (key_f9) {
        if (g_mode == VID_STAGING) return;
        if (g_mode == VID_ARMED || g_mode == VID_IDLE) {
            if (g_mode == VID_IDLE) arm_play();
            if (g_mode == VID_ARMED) write_go_cue();
        }
    }
    if (key_f10) {
        if (g_mode == VID_STAGING) stop_play();
        else start_stage();
    }
}

bool vidactor_handle_chat(const char* pending) {
    if (!pending || strncmp(pending, "/vid", 4) != 0) return false;
    if (pending[4] != '\0' && pending[4] != ' ') return false;
    const char* arg = pending + 4;
    while (*arg == ' ') arg++;

    if (*arg == '\0' || strcmp(arg, "help") == 0) {
        set_notice("F6 UI | F7 REC | F8 play | F9 GO | F10 STAGE all tapes | /vid stage");
        return true;
    }
    if (strcmp(arg, "ui") == 0) {
        g_ui_hidden = !g_ui_hidden;
        set_notice(g_ui_hidden ? "UI hidden" : "UI shown");
        return true;
    }
    if (strcmp(arg, "rec") == 0) {
        if (g_mode == VID_RECORDING) stop_record(true);
        else start_record();
        return true;
    }
    if (strcmp(arg, "play") == 0) {
        if (g_mode == VID_PLAYING || g_mode == VID_ARMED || g_mode == VID_STAGING) stop_play();
        else arm_play();
        return true;
    }
    if (strcmp(arg, "stage") == 0 || strcmp(arg, "all") == 0) {
        if (g_mode == VID_STAGING) stop_play();
        else start_stage();
        return true;
    }
    if (strcmp(arg, "go") == 0) {
        if (g_mode != VID_ARMED) arm_play();
        if (g_mode == VID_ARMED) write_go_cue();
        return true;
    }
    if (strcmp(arg, "stop") == 0) {
        if (g_mode == VID_RECORDING) stop_record(true);
        else if (g_mode == VID_PLAYING || g_mode == VID_ARMED || g_mode == VID_STAGING) stop_play();
        else set_notice("VidActor: idle");
        return true;
    }
    if (strcmp(arg, "save") == 0) {
        save_tape();
        return true;
    }
    if (strcmp(arg, "load") == 0) {
        load_tape();
        return true;
    }
    set_notice("VidActor: unknown /vid command -- try /vid help");
    return true;
}

bool vidactor_ui_hidden(void) { return g_ui_hidden; }
bool vidactor_is_recording(void) { return g_mode == VID_RECORDING; }
bool vidactor_is_playing(void) { return g_mode == VID_PLAYING; }
bool vidactor_is_armed(void) { return g_mode == VID_ARMED; }
bool vidactor_is_staging(void) { return g_mode == VID_STAGING; }

void vidactor_record_move(float x, float y, float z, float yaw, uint8_t anim) {
    if (g_mode != VID_RECORDING) return;
    double t = now_sec();
    bool changed = !g_have_last_move;
    if (!changed) {
        float dx = x - g_last_x, dy = y - g_last_y, dz = z - g_last_z;
        float dyaw = yaw - g_last_yaw;
        if (dyaw < 0.0f) dyaw = -dyaw;
        if (dx * dx + dy * dy + dz * dz > 0.0001f || dyaw > 0.25f || anim != g_last_anim)
            changed = true;
        else if (t - g_last_move_sample >= 0.1)
            changed = true;
    }
    if (!changed) return;
    if (!ensure_cap(g_event_count + 1)) {
        stop_record(true);
        set_notice("VidActor: tape full -- saved");
        return;
    }
    VidEvent* e = &g_events[g_event_count++];
    memset(e, 0, sizeof(*e));
    e->t_ms = rec_ms();
    e->type = VID_EVT_MOVE;
    e->x = x; e->y = y; e->z = z; e->yaw = yaw; e->anim = anim;
    g_last_x = x; g_last_y = y; g_last_z = z; g_last_yaw = yaw; g_last_anim = anim;
    g_have_last_move = true;
    g_last_move_sample = t;
}

void vidactor_record_chat(const char* text) {
    if (g_mode != VID_RECORDING || !text || !text[0]) return;
    if (!ensure_cap(g_event_count + 1)) return;
    VidEvent* e = &g_events[g_event_count++];
    memset(e, 0, sizeof(*e));
    e->t_ms = rec_ms();
    e->type = VID_EVT_CHAT;
    snprintf(e->chat, sizeof(e->chat), "%s", text);
    e->chat_len = (uint16_t)strlen(e->chat);
}

bool vidactor_playback_tick(double dt, VidPose* out_pose, char* out_chat, size_t out_chat_sz) {
    poll_go_cue();
    if (out_chat && out_chat_sz) out_chat[0] = '\0';
    if (g_mode == VID_STAGING) return false;
    if (g_mode != VID_PLAYING) {
        if (g_mode == VID_ARMED && g_play_pose_valid && out_pose) {
            *out_pose = g_play_pose;
            return true;
        }
        return false;
    }
    g_play_t += dt;
    uint32_t t_ms = (uint32_t)(g_play_t * 1000.0);
    while (g_play_idx < g_event_count && g_events[g_play_idx].t_ms <= t_ms) {
        VidEvent* e = &g_events[g_play_idx++];
        if (e->type == VID_EVT_MOVE) {
            apply_move_to_pose(&g_play_pose, e, &g_play_prev_pos, &g_play_have_prev,
                               &g_play_prev_t, g_play_t);
            g_play_pose_valid = true;
        } else if (e->type == VID_EVT_CHAT && out_chat && out_chat_sz > 1) {
            snprintf(out_chat, out_chat_sz, "%s", e->chat);
        }
    }
    if (g_play_idx >= g_event_count && g_event_count > 0 &&
        t_ms >= g_events[g_event_count - 1].t_ms) {
        g_mode = VID_IDLE;
        set_notice("VidActor: playback finished");
    }
    if (out_pose && g_play_pose_valid) *out_pose = g_play_pose;
    return g_play_pose_valid;
}

int vidactor_stage_tick(double dt, VidPose poses[VIDACTOR_MAX_ACTORS]) {
    if (poses) memset(poses, 0, sizeof(VidPose) * VIDACTOR_MAX_ACTORS);
    if (g_mode != VID_STAGING) return 0;

    g_stage_t += dt;
    uint32_t t_ms = (uint32_t)(g_stage_t * 1000.0);
    int active = 0;
    int still_playing = 0;

    for (int i = 0; i < VIDACTOR_MAX_ACTORS; i++) {
        VidTrack* tr = &g_stage[i];
        if (!tr->loaded) continue;

        while (!tr->finished && tr->play_idx < tr->count &&
               tr->events[tr->play_idx].t_ms <= t_ms) {
            VidEvent* e = &tr->events[tr->play_idx++];
            if (e->type == VID_EVT_MOVE) {
                apply_move_to_pose(&tr->pose, e, &tr->prev_pos, &tr->have_prev,
                                   &tr->prev_t, g_stage_t);
                tr->pose_valid = true;
            }
        }
        if (tr->play_idx >= tr->count && tr->count > 0 &&
            t_ms >= tr->events[tr->count - 1].t_ms) {
            tr->finished = true;
        }
        if (!tr->finished) still_playing++;

        if (tr->pose_valid) {
            if (poses) poses[i] = tr->pose;
            active++;
        }
    }

    if (still_playing == 0 && active > 0) {
        if (!g_stage_done_notice) {
            set_notice("VidActor: stage finished -- F10 to exit");
            g_stage_done_notice = true;
        }
    }

    return active;
}

const char* vidactor_status_line(void) {
    double t = now_sec();
    if (g_ui_hidden) {
        g_status[0] = '\0';
        return g_status;
    }
    if (g_notice[0] && t < g_notice_until) {
        snprintf(g_status, sizeof(g_status), "%s", g_notice);
        return g_status;
    }
    switch (g_mode) {
        case VID_RECORDING:
            snprintf(g_status, sizeof(g_status), "VID REC  %d events  %.1fs  [F7 stop]",
                     g_event_count, now_sec() - g_rec_t0);
            break;
        case VID_ARMED:
            if (g_play_start_at > 0.0) {
                double left = g_play_start_at - now_sec();
                if (left < 0.0) left = 0.0;
                snprintf(g_status, sizeof(g_status), "VID ARMED  GO in %.2fs", left);
            } else {
                snprintf(g_status, sizeof(g_status), "VID ARMED  %d events  [F9 GO]", g_event_count);
            }
            break;
        case VID_PLAYING:
            snprintf(g_status, sizeof(g_status), "VID PLAY  %.1fs  [%d/%d]  [F8 stop]",
                     g_play_t, g_play_idx, g_event_count);
            break;
        case VID_STAGING: {
            int n = 0;
            for (int i = 0; i < VIDACTOR_MAX_ACTORS; i++)
                if (g_stage[i].loaded) n++;
            snprintf(g_status, sizeof(g_status), "VID STAGE  %d actors  %.1fs  [F10 stop]",
                     n, g_stage_t);
            break;
        }
        default:
            snprintf(g_status, sizeof(g_status),
                     "VidActor slot %d  F6 UI  F7 REC  F8 PLAY  F9 GO  F10 STAGE",
                     g_slot > 0 ? g_slot : 0);
            break;
    }
    return g_status;
}

#endif
