/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: ui_theme.h                                                                          |
|   Purpose: light/dark for login, home, pause                                                |
\*-------------------------------------------------------------------------------------------*/

#ifndef UI_THEME_H
#define UI_THEME_H

#include <stdbool.h>

typedef struct {
    float bg[3];
    float text[3];
    float muted[3];
    float soft[3];
    float line[3];
    float lime[3];
    float focus[3];
    float playing[3];
    float err[3];
    float on_ink[3];
    float on_lime[3];
    float preview[3];
} UiThemeColors;

void ui_theme_set_dark(bool dark);
bool ui_theme_is_dark(void);
const UiThemeColors* ui_theme_col(void);

#endif
