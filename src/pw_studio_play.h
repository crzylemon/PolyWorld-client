/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: pw_studio_play.h                                                                    |
|   Purpose: run the game inside Studio's viewport                                            |
\*-------------------------------------------------------------------------------------------*/

#ifndef PW_STUDIO_PLAY_H
#define PW_STUDIO_PLAY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool pw_studio_play_start(const char* polyworld_url, int view_w, int view_h);
void pw_studio_play_stop(void);
void pw_studio_play_set_view(int view_w, int view_h);
void pw_studio_play_frame(double dt);
unsigned int pw_studio_play_fbo(void);
bool pw_studio_play_wants_stop(void);
bool pw_studio_host_intercept_close(void);
bool pw_studio_play_blocks_look(void);
#ifdef PW_STUDIO_HOST
void pw_studio_host_busy_redraw(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
