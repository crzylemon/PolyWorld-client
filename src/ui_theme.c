/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: ui_theme.c                                                                          |
|   Purpose: same palette as the website CSS (hopefully)                                      |
\*-------------------------------------------------------------------------------------------*/

#include "ui_theme.h"

static bool s_dark = false;

static const UiThemeColors s_pal_light = {
    { 1.000f, 1.000f, 1.000f },
    { 0.102f, 0.102f, 0.102f },
    { 0.533f, 0.533f, 0.533f },
    { 0.953f, 0.953f, 0.953f },
    { 0.902f, 0.902f, 0.902f },
    { 0.561f, 0.808f, 0.000f },
    { 0.102f, 0.102f, 0.102f },
    { 0.204f, 0.596f, 0.859f },
    { 0.753f, 0.224f, 0.169f },
    { 1.000f, 1.000f, 1.000f },
    { 0.102f, 0.102f, 0.102f },
    { 0.910f, 0.910f, 0.910f },
};

static const UiThemeColors s_pal_dark = {
    { 0.082f, 0.094f, 0.071f },
    { 0.910f, 0.910f, 0.918f },
    { 0.604f, 0.604f, 0.639f },
    { 0.153f, 0.165f, 0.118f },
    { 0.200f, 0.239f, 0.180f },
    { 0.604f, 0.851f, 0.078f },
    { 0.910f, 0.910f, 0.918f },
    { 0.204f, 0.596f, 0.859f },
    { 1.000f, 0.420f, 0.353f },
    { 0.082f, 0.094f, 0.071f },
    { 0.082f, 0.094f, 0.071f },
    { 0.165f, 0.196f, 0.141f },
};

void ui_theme_set_dark(bool dark) {
    s_dark = dark;
}

bool ui_theme_is_dark(void) {
    return s_dark;
}

const UiThemeColors* ui_theme_col(void) {
    return s_dark ? &s_pal_dark : &s_pal_light;
}
