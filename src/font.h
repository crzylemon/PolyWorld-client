/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: font.h                                                                              |
|   Purpose: TTF + Twemoji SVG, plus unicode fallbacks                                        |
\*-------------------------------------------------------------------------------------------*/

#ifndef FONT_H
#define FONT_H

#include <stdbool.h>

bool font_init(void);

void font_invalidate_gl(bool context_alive);

float font_draw(const char* text, float x, float y, float r, float g, float b, float a, int screen_w, int screen_h);
float font_draw_small(const char* text, float x, float y, float r, float g, float b, float a, int screen_w, int screen_h);
float font_draw_shadow(const char* text, float x, float y, float r, float g, float b, float a, int screen_w, int screen_h);
float font_draw_scaled(const char* text, float x, float y, float pixel_height, float r, float g, float b, float a, int sw, int sh);
float font_text_width(const char* text);
float font_text_width_scaled(const char* text, float pixel_height);

#endif
