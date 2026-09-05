/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: audio.h                                                                             |
|   Purpose: sfx ids                                                                          |
\*-------------------------------------------------------------------------------------------*/

#ifndef AUDIO_H
#define AUDIO_H

#define SFX_JUMP       0
#define SFX_LAND       1
#define SFX_FOOTSTEP   2
#define SFX_EXPLOSION  3
#define SFX_CHAT       4
#define SFX_ROCKET     5
#define SFX_TICK       6
#define SFX_DEAD       7

void audio_init(void);
void audio_play(int sfx_id);
void audio_play_at(int sfx_id, float x, float y, float z);
void audio_start_loop(int sfx_id, float volume);
void audio_stop_loop(int sfx_id);
void audio_set_listener(float x, float y, float z);

void audio_play_music(const char* url, float volume);
void audio_stop_music(void);

typedef struct {
    char title[96];
    char author[96];
    unsigned gen;
} AudioMusicCred;
void audio_music_cred_get(AudioMusicCred* out);

#define AUDIO_MUSIC_WAVE_BARS 5
void audio_music_waveform(float* levels, int count);

void audio_set_volume_levels(float master, float music, float sfx);
void audio_get_volume_levels(float* master, float* music, float* sfx);

#endif
