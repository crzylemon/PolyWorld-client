/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: pw_android_game.h                                                                   |
|   Purpose: Android / iOS hooks into the main.c loop                                         |
\*-------------------------------------------------------------------------------------------*/

#ifndef PW_ANDROID_GAME_H
#define PW_ANDROID_GAME_H

#include <stdbool.h>
#include "login_screen.h"
#include "chat.h"

#ifdef __cplusplus
extern "C" {
#endif

bool pw_game_init(void);
void pw_game_frame(double dt);

void pw_game_restore_gl(void);
LoginScreen* pw_game_login_screen(void);
Chat* pw_game_chat(void);
int pw_android_chat_wants_ime(void);

#ifdef __cplusplus
}
#endif

#endif
