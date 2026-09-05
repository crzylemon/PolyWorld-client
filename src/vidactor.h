/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: vidactor.h                                                                          |
|   Purpose: multi-window movement/chat recorder (filming)                                    |
\*-------------------------------------------------------------------------------------------*/

#ifndef VIDACTOR_H
#define VIDACTOR_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "math_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VIDACTOR_MAX_ACTORS 4

typedef struct {
    Vec3 pos;
    float yaw;
    uint8_t anim;
    float move_speed;
    bool active;
} VidPose;

void vidactor_parse_args(int argc, char** argv);

int vidactor_slot(void);

const char* vidactor_window_title(void);

void vidactor_init(void);
void vidactor_shutdown(void);

void vidactor_handle_input(bool chat_active,
                           bool key_f6, bool key_f7, bool key_f8, bool key_f9, bool key_f10);

bool vidactor_handle_chat(const char* pending);

bool vidactor_ui_hidden(void);
bool vidactor_is_recording(void);
bool vidactor_is_playing(void);
bool vidactor_is_armed(void);
bool vidactor_is_staging(void);

void vidactor_record_move(float x, float y, float z, float yaw, uint8_t anim);
void vidactor_record_chat(const char* text);

bool vidactor_playback_tick(double dt, VidPose* out_pose, char* out_chat, size_t out_chat_sz);

int vidactor_stage_tick(double dt, VidPose poses[VIDACTOR_MAX_ACTORS]);

const char* vidactor_status_line(void);

#ifdef __cplusplus
}
#endif

#endif
