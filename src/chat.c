/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: chat.c                                                                              |
|   Purpose: chat + nametags                                                                  |
\*-------------------------------------------------------------------------------------------*/

#include "chat.h"
#include "font.h"
#include "emoji_shortcodes.h"
#include "net_client.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "log.h"
#include "shader.h"

#include "pw_gles.h"
#if PW_USE_GLES
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif

#define CHAT_WRAP_LINE_CAP 160
#define CHAT_WRAP_MAX_LINES 96

/**
 * 8x8 monochrome bitmap fonts for rendering
 * Author: Daniel Hepper <daniel@hepper.net>
 *
 * License: Public Domain
 *
 * Based on:
 * // Summary: font8x8.h
 * // 8x8 monochrome bitmap fonts for rendering
 * //
 * // Author:
 * //     Marcel Sondaar
 * //     International Business Machines (public domain VGA fonts)
 * //
 * // License:
 * //     Public Domain
 *
 * Fetched from: http://dimensionalrift.homelinux.net/combuster/mos3/?p=viewsource&file=/modules/gfx/font8_8.asm
 **/

// Constant: font8x8_basic
// Contains an 8x8 font map for unicode points U+0000 - U+007F (basic latin)
char font_8x8[128][8] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0000 (nul)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0001
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0002
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0003
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0004
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0005
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0006
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0007
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0008
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0009
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000A
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000B
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000C
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000D
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000E
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000F
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0010
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0011
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0012
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0013
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0014
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0015
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0016
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0017
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0018
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0019
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001A
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001B
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001C
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001D
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001E
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001F
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0020 (space)
    { 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},   // U+0021 (!)
    { 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0022 (")
    { 0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},   // U+0023 (#)
    { 0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00},   // U+0024 ($)
    { 0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00},   // U+0025 (%)
    { 0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00},   // U+0026 (&)
    { 0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0027 (')
    { 0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00},   // U+0028 (()
    { 0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00},   // U+0029 ())
    { 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},   // U+002A (*)
    { 0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00},   // U+002B (+)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06},   // U+002C (,)
    { 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00},   // U+002D (-)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00},   // U+002E (.)
    { 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00},   // U+002F (/)
    { 0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00},   // U+0030 (0)
    { 0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},   // U+0031 (1)
    { 0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00},   // U+0032 (2)
    { 0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},   // U+0033 (3)
    { 0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00},   // U+0034 (4)
    { 0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},   // U+0035 (5)
    { 0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00},   // U+0036 (6)
    { 0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},   // U+0037 (7)
    { 0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00},   // U+0038 (8)
    { 0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},   // U+0039 (9)
    { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00},   // U+003A (:)
    { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06},   // U+003B (;)
    { 0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00},   // U+003C (<)
    { 0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00},   // U+003D (=)
    { 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00},   // U+003E (>)
    { 0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00},   // U+003F (?)
    { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00},   // U+0040 (@)
    { 0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},   // U+0041 (A)
    { 0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00},   // U+0042 (B)
    { 0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},   // U+0043 (C)
    { 0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00},   // U+0044 (D)
    { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00},   // U+0045 (E)
    { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00},   // U+0046 (F)
    { 0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},   // U+0047 (G)
    { 0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00},   // U+0048 (H)
    { 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0049 (I)
    { 0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00},   // U+004A (J)
    { 0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},   // U+004B (K)
    { 0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00},   // U+004C (L)
    { 0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00},   // U+004D (M)
    { 0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00},   // U+004E (N)
    { 0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},   // U+004F (O)
    { 0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00},   // U+0050 (P)
    { 0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},   // U+0051 (Q)
    { 0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00},   // U+0052 (R)
    { 0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},   // U+0053 (S)
    { 0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0054 (T)
    { 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},   // U+0055 (U)
    { 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},   // U+0056 (V)
    { 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},   // U+0057 (W)
    { 0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00},   // U+0058 (X)
    { 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},   // U+0059 (Y)
    { 0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00},   // U+005A (Z)
    { 0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00},   // U+005B ([)
    { 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00},   // U+005C (\)
    { 0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00},   // U+005D (])
    { 0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00},   // U+005E (^)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},   // U+005F (_)
    { 0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0060 (`)
    { 0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00},   // U+0061 (a)
    { 0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00},   // U+0062 (b)
    { 0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00},   // U+0063 (c)
    { 0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6E, 0x00},   // U+0064 (d)
    { 0x00, 0x00, 0x1E, 0x33, 0x3f, 0x03, 0x1E, 0x00},   // U+0065 (e)
    { 0x1C, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0F, 0x00},   // U+0066 (f)
    { 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F},   // U+0067 (g)
    { 0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00},   // U+0068 (h)
    { 0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0069 (i)
    { 0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E},   // U+006A (j)
    { 0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00},   // U+006B (k)
    { 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+006C (l)
    { 0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00},   // U+006D (m)
    { 0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00},   // U+006E (n)
    { 0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00},   // U+006F (o)
    { 0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F},   // U+0070 (p)
    { 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78},   // U+0071 (q)
    { 0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00},   // U+0072 (r)
    { 0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00},   // U+0073 (s)
    { 0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00},   // U+0074 (t)
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00},   // U+0075 (u)
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},   // U+0076 (v)
    { 0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00},   // U+0077 (w)
    { 0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00},   // U+0078 (x)
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F},   // U+0079 (y)
    { 0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00},   // U+007A (z)
    { 0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00},   // U+007B ({)
    { 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00},   // U+007C (|)
    { 0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00},   // U+007D (})
    { 0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+007E (~)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}    // U+007F
    
};

static char s_pending_send[CHAT_MAX_INPUT];
static bool s_has_pending = false;

static void chat_build_gl_core(Chat* c);

void chat_init(Chat* c) {
    memset(c, 0, sizeof(Chat));
    chat_build_gl_core(c);
    c->fade_timer = 99.0f;
    c->local_username[0] = '\0';
    c->panel_anim = 0.0f;
    c->pl_anim_h = 0.0f;
    c->pl_anim_w = 0.0f;
    c->pl_entry_count = 0;
    memset(c->msg_fade, 0, sizeof(c->msg_fade));
    memset(c->msg_system, 0, sizeof(c->msg_system));
    memset(c->pl_entry_fade, 0, sizeof(c->pl_entry_fade));
    memset(c->pl_entry_names, 0, sizeof(c->pl_entry_names));

    c->bubble_texture = 0;
    c->bubble_bottom_tex = 0;
    c->nineslice_tex = 0;
    c->chat_closed_tex = 0;
    c->chat_open_tex = 0;
    c->chat_unread_tex = 0;
    c->menu_tex = 0;
    c->panel_hit_valid = false;
    c->panel_hit_x = c->panel_hit_y = c->panel_hit_w = c->panel_hit_h = 0.0f;
}

static bool message_is_from_local(Chat* c, const char* msg);

void chat_set_local_username(Chat* c, const char* name) {
    if (!c) return;
    if (!name) { c->local_username[0] = '\0'; return; }
    strncpy(c->local_username, name, sizeof(c->local_username) - 1);
    c->local_username[sizeof(c->local_username) - 1] = '\0';
}

void chat_name_color(const char* name, float* r, float* g, float* b) {

    static const float COLORS[8][3] = {
        {253.0f/255.0f,  41.0f/255.0f,  67.0f/255.0f},
        {  1.0f/255.0f, 162.0f/255.0f, 255.0f/255.0f},
        {  2.0f/255.0f, 184.0f/255.0f,  87.0f/255.0f},
        {107.0f/255.0f,  50.0f/255.0f, 124.0f/255.0f},
        {218.0f/255.0f, 133.0f/255.0f,  65.0f/255.0f},
        {245.0f/255.0f, 205.0f/255.0f,  48.0f/255.0f},
        {232.0f/255.0f, 186.0f/255.0f, 200.0f/255.0f},
        {215.0f/255.0f, 197.0f/255.0f, 154.0f/255.0f},
    };
    int value = 0;
    int len = name ? (int)strlen(name) : 0;
    for (int index = 1; index <= len; index++) {
        int cValue = (unsigned char)name[index - 1];
        int reverseIndex = len - index + 1;
        if (len % 2 == 1) reverseIndex = reverseIndex - 1;
        if (reverseIndex % 4 >= 2) cValue = -cValue;
        value += cValue;
    }
    int idx = value % 8;
    if (idx < 0) idx += 8;
    if (r) *r = COLORS[idx][0];
    if (g) *g = COLORS[idx][1];
    if (b) *b = COLORS[idx][2];
}

void chat_resolve_name_color(Chat* c, const char* name, float* r, float* g, float* b) {
    if (c && name) {
        for (int i = 0; i < CHAT_MAX_NAME_COLORS; i++) {
            if (!c->name_colors[i].active) continue;
            if (strcmp(c->name_colors[i].name, name) == 0) {
                if (r) *r = c->name_colors[i].r;
                if (g) *g = c->name_colors[i].g;
                if (b) *b = c->name_colors[i].b;
                return;
            }
        }
    }
    chat_name_color(name, r, g, b);
}

void chat_set_name_color_override(Chat* c, const char* name, bool enabled, float r, float g, float b) {
    if (!c || !name || !name[0]) return;
    int free_slot = -1;
    for (int i = 0; i < CHAT_MAX_NAME_COLORS; i++) {
        if (c->name_colors[i].active && strcmp(c->name_colors[i].name, name) == 0) {
            if (!enabled) {
                c->name_colors[i].active = false;
                c->name_colors[i].name[0] = '\0';
            } else {
                c->name_colors[i].r = r;
                c->name_colors[i].g = g;
                c->name_colors[i].b = b;
            }
            return;
        }
        if (free_slot < 0 && !c->name_colors[i].active) free_slot = i;
    }
    if (!enabled || free_slot < 0) return;
    strncpy(c->name_colors[free_slot].name, name, 31);
    c->name_colors[free_slot].name[31] = '\0';
    c->name_colors[free_slot].r = r;
    c->name_colors[free_slot].g = g;
    c->name_colors[free_slot].b = b;
    c->name_colors[free_slot].active = true;
}

void chat_clear_name_color_overrides(Chat* c) {
    if (!c) return;
    for (int i = 0; i < CHAT_MAX_NAME_COLORS; i++) {
        c->name_colors[i].active = false;
        c->name_colors[i].name[0] = '\0';
    }
}

static bool message_is_from_local(Chat* c, const char* msg) {
    if (!c || !msg || !c->local_username[0]) return false;
    size_t nlen = strlen(c->local_username);
    if (strncmp(msg, c->local_username, nlen) != 0) return false;
    return msg[nlen] == ':';
}

void chat_shutdown(Chat* c) {
    if (!c->initialized) return;
    glDeleteTextures(1, &c->font_texture);
    if (c->white_tex) glDeleteTextures(1, &c->white_tex);
    if (c->circle_tex) glDeleteTextures(1, &c->circle_tex);
    glDeleteBuffers(1, &c->text_vbo);
    glDeleteVertexArrays(1, &c->text_vao);
    glDeleteProgram(c->text_shader);
    glDeleteProgram(c->quad_shader);
    c->initialized = false;
}

static void chat_build_gl_core(Chat* c) {

    unsigned char atlas[48 * 128];
    memset(atlas, 0, sizeof(atlas));

    for (int i = 0; i < 95; i++) {
        int col = i % 16;
        int row = i / 16;
        for (int y = 0; y < 8; y++) {
            unsigned char bits = (unsigned char)font_8x8[i + 32][y];
            for (int x = 0; x < 8; x++) {
                if (bits & (1 << x)) {
                    atlas[(row * 8 + y) * 128 + col * 8 + x] = 255;
                }
            }
        }
    }

    glGenTextures(1, &c->font_texture);
    glBindTexture(GL_TEXTURE_2D, c->font_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 128, 48, 0, GL_RED, GL_UNSIGNED_BYTE, atlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenVertexArrays(1, &c->text_vao);
    glGenBuffers(1, &c->text_vbo);
    glBindVertexArray(c->text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, c->text_vbo);
    glBufferData(GL_ARRAY_BUFFER, 200 * 6 * 4 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    c->text_shader = shader_load_program("ui_text");
    if (!c->text_shader)
        PW_ERR(ERR_SHADER, "Text shader failed to load\n");

    c->u_projection = glGetUniformLocation(c->text_shader, "u_projection");
    c->u_tex = glGetUniformLocation(c->text_shader, "u_tex");
    c->u_color = glGetUniformLocation(c->text_shader, "u_color");

    {
        unsigned char white[4] = {255, 255, 255, 255};
        glGenTextures(1, &c->white_tex);
        glBindTexture(GL_TEXTURE_2D, c->white_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    {
        const int sz = 64;
        unsigned char* px = (unsigned char*)malloc((size_t)sz * (size_t)sz * 4);
        float cx = (sz - 1) * 0.5f, cy = (sz - 1) * 0.5f, r = (sz - 1) * 0.5f;
        for (int y = 0; y < sz; y++) {
            for (int x = 0; x < sz; x++) {
                float dx = (float)x - cx, dy = (float)y - cy;
                float d = sqrtf(dx * dx + dy * dy);
                float a = 1.0f - (d - (r - 1.0f));
                if (a < 0.0f) a = 0.0f;
                if (a > 1.0f) a = 1.0f;
                int i = (y * sz + x) * 4;
                px[i] = 255; px[i + 1] = 255; px[i + 2] = 255;
                px[i + 3] = (unsigned char)(a * 255.0f + 0.5f);
            }
        }
        glGenTextures(1, &c->circle_tex);
        glBindTexture(GL_TEXTURE_2D, c->circle_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sz, sz, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        free(px);
    }

    {
        c->quad_shader = shader_load_program("ui_quad");
        if (!c->quad_shader)
            PW_ERR(ERR_SHADER, "Quad shader failed to load\n");
        c->quad_u_projection = glGetUniformLocation(c->quad_shader, "u_projection");
        c->quad_u_tex = glGetUniformLocation(c->quad_shader, "u_tex");
        c->quad_u_alpha = glGetUniformLocation(c->quad_shader, "u_alpha");
        c->quad_u_tint = glGetUniformLocation(c->quad_shader, "u_tint");
    }

    c->initialized = true;
}

void chat_recreate_gl(Chat* c, bool context_alive) {
    if (!c) return;
    if (c->initialized && context_alive) {
        if (c->font_texture) glDeleteTextures(1, &c->font_texture);
        if (c->white_tex) glDeleteTextures(1, &c->white_tex);
        if (c->circle_tex) glDeleteTextures(1, &c->circle_tex);
        if (c->text_vbo) glDeleteBuffers(1, &c->text_vbo);
        if (c->text_vao) glDeleteVertexArrays(1, &c->text_vao);
        if (c->text_shader) glDeleteProgram(c->text_shader);
        if (c->quad_shader) glDeleteProgram(c->quad_shader);
    }
    c->font_texture = 0;
    c->white_tex = 0;
    c->circle_tex = 0;
    c->text_vbo = 0;
    c->text_vao = 0;
    c->text_shader = 0;
    c->quad_shader = 0;
    c->initialized = false;

    c->bubble_texture = 0;
    c->bubble_bottom_tex = 0;
    c->nineslice_tex = 0;
    c->chat_closed_tex = 0;
    c->chat_open_tex = 0;
    c->chat_unread_tex = 0;
    c->menu_tex = 0;
    chat_build_gl_core(c);
}

void chat_add_message(Chat* c, const char* msg) {
    chat_add_message_from(c, msg, 0);
}

void chat_add_message_from(Chat* c, const char* msg, uint32_t sender_id) {
    int idx = (c->msg_start + c->msg_count) % CHAT_MAX_MESSAGES;
    if (c->msg_count == CHAT_MAX_MESSAGES) {
        c->msg_start = (c->msg_start + 1) % CHAT_MAX_MESSAGES;
    } else {
        c->msg_count++;
    }
    strncpy(c->messages[idx], msg, CHAT_MSG_LEN - 1);
    c->messages[idx][CHAT_MSG_LEN - 1] = '\0';
    c->msg_sender[idx] = sender_id;
    c->msg_system[idx] = false;
    c->msg_age[idx] = 0.0f;
    c->msg_fade[idx] = 0.0f;
    c->fade_timer = 0.0f;
    if (!c->open && !message_is_from_local(c, msg))
        c->unread = true;
}

void chat_add_system_message(Chat* c, const char* msg) {
    if (!c || !msg || !msg[0]) return;
    int idx = (c->msg_start + c->msg_count) % CHAT_MAX_MESSAGES;
    if (c->msg_count == CHAT_MAX_MESSAGES) {
        c->msg_start = (c->msg_start + 1) % CHAT_MAX_MESSAGES;
    } else {
        c->msg_count++;
    }
    strncpy(c->messages[idx], msg, CHAT_MSG_LEN - 1);
    c->messages[idx][CHAT_MSG_LEN - 1] = '\0';
    c->msg_sender[idx] = 0;
    c->msg_system[idx] = true;
    c->msg_age[idx] = 0.0f;
    c->msg_fade[idx] = 0.0f;
    c->fade_timer = 0.0f;
    if (!c->open)
        c->unread = true;
}

const char* chat_get_latest_bubble(Chat* c, uint32_t* sender_id, float* age) {
    if (c->msg_count == 0) return NULL;
    int idx = (c->msg_start + c->msg_count - 1) % CHAT_MAX_MESSAGES;
    if (sender_id) *sender_id = c->msg_sender[idx];
    if (age) *age = c->msg_age[idx];
    return c->messages[idx];
}

int chat_collect_recent_bubbles(Chat* c, float max_age, ChatBubbleMsg* out, int max_out) {
    if (!c || !out || max_out <= 0) return 0;
    int n = 0;
    for (int i = 0; i < c->msg_count && n < max_out; i++) {
        int idx = (c->msg_start + i) % CHAT_MAX_MESSAGES;
        if (c->msg_age[idx] >= max_age) continue;
        if (c->msg_system[idx]) continue;
        if (!c->messages[idx][0]) continue;
        out[n].text = c->messages[idx];
        out[n].sender_id = c->msg_sender[idx];
        out[n].age = c->msg_age[idx];
        out[n].fade = c->msg_fade[idx];
        n++;
    }
    return n;
}

void chat_blur(Chat* c) {
    if (!c) return;
    c->focused = false;
    c->input_len = 0;
    c->input_buf[0] = '\0';
    c->just_focused = false;
    if (c->opened_by_slash) {
        c->open = false;
        c->opened_by_slash = false;
    }
}

bool chat_on_key(Chat* c, int keycode, bool shift, bool ctrl) {
    (void)shift;
    if (c && c->focused && ctrl) {
        if (keycode == 65 || keycode == 'a' || keycode == 'A')
            return chat_select_all_input(c);
        if (keycode == 88 || keycode == 'x' || keycode == 'X')
            return chat_cut_input(c);
    }

    if (!c->focused && (keycode == 191 || keycode == 47 || keycode == '/')) {
        bool was_open = c->open;
        c->open = true;
        c->focused = true;
        c->just_focused = true;
        c->opened_by_slash = !was_open;
        c->input_len = 0;
        c->input_buf[0] = '\0';
        c->scroll_offset = 0;
        c->unread = false;
        return true;
    }

    if (c->open && !c->focused) {
        if (keycode == 265 || keycode == 33) {
            c->scroll_offset += 3;
            if (c->scroll_offset > CHAT_WRAP_MAX_LINES) c->scroll_offset = CHAT_WRAP_MAX_LINES;
            return true;
        }
        if (keycode == 266 || keycode == 34) {
            c->scroll_offset -= 3;
            if (c->scroll_offset < 0) c->scroll_offset = 0;
            return true;
        }
        return false;
    }

    if (!c->focused) return false;

    {
        EmojiSuggestion sug[EMOJI_SUGGEST_MAX];
        int open = 0, plen = 0;
        int n = 0;
        if (emoji_active_prefix(c->input_buf, c->input_len, &open, &plen) && plen > 0)
            n = emoji_suggest(c->input_buf + open + 1, plen, sug, EMOJI_SUGGEST_MAX);
        if (n > 0) {
            if (keycode == 38 || keycode == 265) {
                c->emoji_sel--;
                if (c->emoji_sel < 0) c->emoji_sel = n - 1;
                return true;
            }
            if (keycode == 40 || keycode == 266) {
                c->emoji_sel++;
                if (c->emoji_sel >= n) c->emoji_sel = 0;
                return true;
            }

            if (keycode == 9) {
                if (c->emoji_sel < 0) c->emoji_sel = 0;
                if (c->emoji_sel >= n) c->emoji_sel = n - 1;
                emoji_apply_suggestion(c->input_buf, &c->input_len, CHAT_MAX_INPUT,
                                      sug[c->emoji_sel].name);
                c->emoji_sel = 0;
                return true;
            }
        }
    }

    if (keycode == 27) {
        chat_blur(c);
        return true;
    }

    if (keycode == 13 || keycode == 10) {
        EmojiSuggestion sug[EMOJI_SUGGEST_MAX];
        int open = 0, plen = 0;
        int n = 0;
        if (emoji_active_prefix(c->input_buf, c->input_len, &open, &plen) && plen > 0)
            n = emoji_suggest(c->input_buf + open + 1, plen, sug, EMOJI_SUGGEST_MAX);
        if (n > 0) {
            if (c->emoji_sel < 0) c->emoji_sel = 0;
            if (c->emoji_sel >= n) c->emoji_sel = n - 1;
            emoji_apply_suggestion(c->input_buf, &c->input_len, CHAT_MAX_INPUT,
                                  sug[c->emoji_sel].name);
            c->emoji_sel = 0;
            return true;
        }
        if (c->input_len > 0) {

            emoji_expand_all(c->input_buf, &c->input_len, CHAT_MAX_INPUT);
            strncpy(s_pending_send, c->input_buf, CHAT_MAX_INPUT);
            s_pending_send[CHAT_MAX_INPUT - 1] = '\0';
            s_has_pending = true;
        }
        chat_blur(c);
        return true;
    }

    if (keycode == 8) {
        if (c->input_len > 0) {
            do {
                c->input_len--;
                unsigned char b = (unsigned char)c->input_buf[c->input_len];
                c->input_buf[c->input_len] = '\0';

                if ((b & 0xC0) != 0x80) break;
            } while (c->input_len > 0);
        }
        c->emoji_sel = 0;
        return true;
    }

    if (keycode == 265 || keycode == 33) {
        c->scroll_offset += 3;
        if (c->scroll_offset > CHAT_WRAP_MAX_LINES) c->scroll_offset = CHAT_WRAP_MAX_LINES;
        return true;
    }
    if (keycode == 266 || keycode == 34) {
        c->scroll_offset -= 3;
        if (c->scroll_offset < 0) c->scroll_offset = 0;
        return true;
    }

    return true;
}

bool chat_on_char(Chat* c, unsigned int codepoint) {
    if (!c->focused) return false;

    if (c->just_focused) {
        c->just_focused = false;
        if (codepoint == '/') return true;
    }

    if (codepoint < 32 || codepoint == 0x7F) return true;
    if (codepoint > 0x10FFFF) return true;

    char tmp[5];
    int nb = 0;
    if (codepoint < 0x80) {
        tmp[0] = (char)codepoint; nb = 1;
    } else if (codepoint < 0x800) {
        tmp[0] = (char)(0xC0 | (codepoint >> 6));
        tmp[1] = (char)(0x80 | (codepoint & 0x3F));
        nb = 2;
    } else if (codepoint < 0x10000) {
        tmp[0] = (char)(0xE0 | (codepoint >> 12));
        tmp[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        tmp[2] = (char)(0x80 | (codepoint & 0x3F));
        nb = 3;
    } else {
        tmp[0] = (char)(0xF0 | (codepoint >> 18));
        tmp[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        tmp[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        tmp[3] = (char)(0x80 | (codepoint & 0x3F));
        nb = 4;
    }
    if (c->input_len + nb >= CHAT_MAX_INPUT) return true;
    memcpy(c->input_buf + c->input_len, tmp, (size_t)nb);
    c->input_len += nb;
    c->input_buf[c->input_len] = '\0';

    if (codepoint == ':') {
        emoji_expand_trailing(c->input_buf, &c->input_len, CHAT_MAX_INPUT);
    }
    c->emoji_sel = 0;
    return true;
}

void chat_set_input_text(Chat* c, const char* utf8) {
    if (!c) return;
    c->just_focused = false;
    if (!utf8) utf8 = "";
    size_t n = 0;
    for (size_t i = 0; utf8[i] && n < (size_t)(CHAT_MAX_INPUT - 1); i++) {
        unsigned char ch = (unsigned char)utf8[i];

        if (ch < 32) continue;
        c->input_buf[n++] = (char)ch;
    }
    c->input_buf[n] = '\0';
    c->input_len = (int)n;
    emoji_expand_trailing(c->input_buf, &c->input_len, CHAT_MAX_INPUT);
    c->emoji_sel = 0;
}

bool chat_copy_input(Chat* c) {
    if (!c || !c->focused) return false;
    platform_clipboard_set(c->input_buf);
    return true;
}

bool chat_cut_input(Chat* c) {
    if (!c || !c->focused) return false;
    platform_clipboard_set(c->input_buf);
    c->input_len = 0;
    c->input_buf[0] = '\0';
    te_reset(&c->edit, 0);
    return true;
}

bool chat_select_all_input(Chat* c) {
    if (!c || !c->focused) return false;
    te_select_all(&c->edit, c->input_len);
    return true;
}

bool chat_paste_text(Chat* c, const char* utf8) {
    if (!c || !c->focused) return false;
    if (!utf8 || !utf8[0]) return true;
    for (size_t i = 0; utf8[i] && c->input_len < CHAT_MAX_INPUT - 1; i++) {
        unsigned char ch = (unsigned char)utf8[i];
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            if (c->input_len < CHAT_MAX_INPUT - 1)
                c->input_buf[c->input_len++] = ' ';
            continue;
        }
        if (ch < 32) continue;
        c->input_buf[c->input_len++] = (char)ch;
    }
    c->input_buf[c->input_len] = '\0';
    emoji_expand_trailing(c->input_buf, &c->input_len, CHAT_MAX_INPUT);
    c->emoji_sel = 0;
    return true;
}

bool chat_on_click(Chat* c, float x, float y, int screen_width, int screen_height) {
    (void)screen_width;
    (void)screen_height;
    float uis = c->ui_scale > 0.1f ? c->ui_scale : 1.0f;
    float margin = 10.0f * uis;
    float btn_size = 40.0f * uis;
    float btn_gap = 6.0f * uis;
    float scale = 2.0f * uis;
    float line_h = 8.0f * scale + 4.0f * uis;
    float pad = 8.0f * uis;

    if (c->focused && c->emoji_hit_count > 0 && c->emoji_hit_row_h > 0.5f) {
        if (x >= c->emoji_hit_x && x <= c->emoji_hit_x + c->emoji_hit_w &&
            y >= c->emoji_hit_y &&
            y <= c->emoji_hit_y + c->emoji_hit_row_h * (float)c->emoji_hit_count) {
            int row = (int)((y - c->emoji_hit_y) / c->emoji_hit_row_h);
            if (row < 0) row = 0;
            if (row >= c->emoji_hit_count) row = c->emoji_hit_count - 1;
            EmojiSuggestion sug[EMOJI_SUGGEST_MAX];
            int open = 0, plen = 0;
            int n = 0;
            if (emoji_active_prefix(c->input_buf, c->input_len, &open, &plen) && plen > 0)
                n = emoji_suggest(c->input_buf + open + 1, plen, sug, EMOJI_SUGGEST_MAX);
            if (row < n) {
                emoji_apply_suggestion(c->input_buf, &c->input_len, CHAT_MAX_INPUT, sug[row].name);
                c->emoji_sel = 0;
            }
            return true;
        }
    }

    float chat_btn_x = margin + btn_size + btn_gap;
    float chat_btn_y = margin;
    if (x >= chat_btn_x && x <= chat_btn_x + btn_size &&
        y >= chat_btn_y && y <= chat_btn_y + btn_size) {
        if (c->open) {
            c->open = false;
            c->focused = false;
            c->opened_by_slash = false;
            c->input_len = 0;
            c->input_buf[0] = '\0';
        } else {
            c->open = true;
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)

            c->focused = true;
            c->just_focused = true;
#else
            c->focused = false;
#endif
            c->opened_by_slash = false;
            c->unread = false;
            c->scroll_offset = 0;
        }
        return true;
    }

    if (c->open) {
        float panel_w = 350.0f * uis;
        float panel_h = 220.0f * uis;
        float panel_x = margin;
        float panel_y = margin + btn_size + 8.0f * uis;
        float max_input_h = 4.0f * line_h + pad * 2.0f;
        float input_box_y = panel_y + panel_h - max_input_h;
        if (x >= panel_x && x <= panel_x + panel_w &&
            y >= input_box_y && y <= panel_y + panel_h) {
            if (!c->focused) {
                c->focused = true;
                c->just_focused = false;
                c->opened_by_slash = false;
                c->unread = false;
            }
            return true;
        }

        if (x >= panel_x && x <= panel_x + panel_w &&
            y >= panel_y && y <= panel_y + panel_h) {
            return true;
        }
    }

    return false;
}

bool chat_on_scroll(Chat* c, float x, float y, float delta,
                    int screen_width, int screen_height) {
    (void)screen_width;
    (void)screen_height;
    if (!c || delta == 0.0f) return false;
    if (!c->open && c->panel_anim < 0.5f) return false;

    float px, py, pw, ph;
    if (c->panel_hit_valid) {
        px = c->panel_hit_x;
        py = c->panel_hit_y;
        pw = c->panel_hit_w;
        ph = c->panel_hit_h;
    } else {
        float uis = c->ui_scale > 0.1f ? c->ui_scale : 1.0f;
        float margin = 10.0f * uis;
        float btn_size = 40.0f * uis;
        px = margin;
        py = margin + btn_size + 8.0f * uis;
        pw = 350.0f * uis;
        ph = 220.0f * uis;
    }

    if (x < px || x > px + pw || y < py || y > py + ph)
        return false;

    int steps = (int)(fabsf(delta) * 3.0f + 0.5f);
    if (steps < 1) steps = 1;
    if (delta < 0.0f) {
        c->scroll_offset += steps;
        if (c->scroll_offset > CHAT_WRAP_MAX_LINES) c->scroll_offset = CHAT_WRAP_MAX_LINES;
    } else {
        c->scroll_offset -= steps;
        if (c->scroll_offset < 0) c->scroll_offset = 0;
    }
    return true;
}

void chat_update(Chat* c, float dt) {
    for (int i = 0; i < c->msg_count; i++) {
        int idx = (c->msg_start + i) % CHAT_MAX_MESSAGES;
        c->msg_age[idx] += dt;
        if (c->msg_fade[idx] < 1.0f) {
            c->msg_fade[idx] += dt / 0.28f;
            if (c->msg_fade[idx] > 1.0f) c->msg_fade[idx] = 1.0f;
        }
    }
    float panel_target = c->open ? 1.0f : 0.0f;
    float panel_spd = 10.0f;
    if (c->panel_anim < panel_target) {
        c->panel_anim += dt * panel_spd;
        if (c->panel_anim > panel_target) c->panel_anim = panel_target;
    } else if (c->panel_anim > panel_target) {
        c->panel_anim -= dt * panel_spd;
        if (c->panel_anim < panel_target) c->panel_anim = panel_target;
    }

    for (int i = 0; i < c->pl_entry_count; i++) {
        if (c->pl_entry_fade[i] < 1.0f) {
            c->pl_entry_fade[i] += dt / 0.25f;
            if (c->pl_entry_fade[i] > 1.0f) c->pl_entry_fade[i] = 1.0f;
        }
    }
    if (!c->open) {
        c->fade_timer += dt;
    } else {
        c->fade_timer = 0.0f;
    }
}

const char* chat_get_pending_send(Chat* c) {
    (void)c;
    if (s_has_pending) return s_pending_send;
    return NULL;
}

void chat_clear_pending(Chat* c) {
    (void)c;
    s_has_pending = false;
}

static void draw_quad(Chat* c, unsigned int tex, float x, float y, float w, float h,
                      int screen_width, int screen_height);
static void draw_quad_tinted(Chat* c, unsigned int tex, float x, float y, float w, float h,
                             float r, float g, float b, float a,
                             int screen_width, int screen_height);
static void draw_nineslice(Chat* c, unsigned int tex, float x, float y, float w, float h,
                           float border, float alpha, float uv_border,
                           int screen_width, int screen_height);

static void draw_pill_fill(Chat* c, float x, float y, float w, float h,
                           float r, float g, float b, float a,
                           int screen_width, int screen_height);
static int draw_text(Chat* c, const char* text, float x, float y, float scale,
                     float r, float g, float b, float a, int screen_w, int screen_h);
static void draw_chat_line(Chat* c, const char* line, float x, float y, float scale,
                           float alpha, bool is_system, bool color_name,
                           int screen_width, int screen_height);

static float chat_ease(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static int collect_badges(Chat* c, uint8_t badges, unsigned int* tex_out) {
    int n = 0;
    if ((badges & BADGE_STAFF) && c->badge_shield)
        tex_out[n++] = c->badge_shield;
    if ((badges & BADGE_TESTER) && c->badge_tester)
        tex_out[n++] = c->badge_tester;
    if ((badges & BADGE_CREATOR) && c->badge_creator)
        tex_out[n++] = c->badge_creator;
    if ((badges & BADGE_VERIFIED) && c->badge_verified)
        tex_out[n++] = c->badge_verified;
    return n;
}

#define BADGE_GOLD_R 0.910f
#define BADGE_GOLD_G 0.627f
#define BADGE_GOLD_B 0.000f

static void draw_badge_icon(Chat* c, unsigned int tex, float x, float y, float size,
                            int screen_width, int screen_height) {
    if (!tex) return;
    draw_quad_tinted(c, tex, x + 1.5f, y + 1.5f, size, size,
                     0.0f, 0.0f, 0.0f, 0.75f, screen_width, screen_height);
    draw_quad_tinted(c, tex, x, y, size, size,
                     BADGE_GOLD_R, BADGE_GOLD_G, BADGE_GOLD_B, 1.0f,
                     screen_width, screen_height);
}

static float measure_prefix_width(const char* text, int len, float pixel_h) {
    if (!text || len <= 0) return 0.0f;
    char tmp[CHAT_WRAP_LINE_CAP];
    if (len >= (int)sizeof(tmp)) len = (int)sizeof(tmp) - 1;
    memcpy(tmp, text, (size_t)len);
    tmp[len] = '\0';
    return font_text_width_scaled(tmp, pixel_h);
}

static int wrap_text_lines(const char* text, float max_width, float pixel_h,
                           char lines[][CHAT_WRAP_LINE_CAP], int max_lines) {
    if (!text || !text[0] || max_lines <= 0 || !lines) return 0;
    if (max_width < 4.0f) max_width = 4.0f;

    int count = 0;
    const char* p = text;
    while (*p && count < max_lines) {
        while (*p == ' ') p++;
        if (!*p) break;

        int fit = 0;
        int last_space = -1;
        for (int i = 0; p[i] && i < CHAT_WRAP_LINE_CAP - 1; i++) {
            if (p[i] == '\n') {
                fit = i;
                last_space = i;
                break;
            }
            float w = measure_prefix_width(p, i + 1, pixel_h);
            if (w > max_width) break;
            fit = i + 1;
            if (p[i] == ' ') last_space = i;
        }

        if (fit == 0) {
            if (p[0] == '\n') { p++; continue; }
            fit = 1;
        } else if (p[fit] != '\0' && p[fit] != '\n' && last_space > 0) {
            fit = last_space;
        }

        int copy_len = fit;
        while (copy_len > 0 && p[copy_len - 1] == ' ') copy_len--;
        if (copy_len <= 0) {
            p += (fit > 0 ? fit : 1);
            if (*p == '\n') p++;
            continue;
        }

        memcpy(lines[count], p, (size_t)copy_len);
        lines[count][copy_len] = '\0';
        count++;

        p += fit;
        if (*p == '\n') p++;
    }

    if (count == 0 && max_lines > 0) {
        lines[0][0] = '\0';
        return 1;
    }
    return count;
}

static int draw_text(Chat* c, const char* text, float x, float y, float scale,
              float r, float g, float b, float a, int screen_w, int screen_h) {
    if (!text || !text[0]) return 0;
    (void)c;
    float pixel_h = 8.0f * scale;
    return (int)font_draw_scaled(text, x, y, pixel_h, r, g, b, a, screen_w, screen_h);
}

void chat_render(Chat* c, int screen_width, int screen_height) {
    if (!c->initialized) return;

    glViewport(0, 0, screen_width, screen_height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float uis = c->ui_scale > 0.1f ? c->ui_scale : 1.0f;
    float scale = 2.0f * uis;
    float line_h = 8.0f * scale + 4.0f * uis;
    float margin = 10.0f * uis;
    float btn_size = 40.0f * uis;
    float btn_gap = 6.0f * uis;
    const float uv_panel = 50.0f / 550.0f;

    if (c->menu_tex) {
        draw_quad(c, c->menu_tex, margin, margin, btn_size, btn_size, screen_width, screen_height);
    }

    float chat_btn_x = margin + btn_size + btn_gap;
    unsigned int chat_icon = c->chat_closed_tex;
    if (c->open && c->chat_open_tex) {
        chat_icon = c->chat_open_tex;
    } else if (c->unread && c->chat_unread_tex) {
        chat_icon = c->chat_unread_tex;
    }
    if (chat_icon) {
        draw_quad(c, chat_icon, chat_btn_x, margin, btn_size, btn_size, screen_width, screen_height);
    }

    float chat_top_y = margin + btn_size + 8.0f * uis;
    float panel_alpha = c->panel_anim;
    float hud_alpha = 1.0f - c->panel_anim;
    if (hud_alpha < 0.0f) hud_alpha = 0.0f;

    c->emoji_hit_count = 0;
    c->panel_hit_valid = false;
    if (panel_alpha > 0.01f) {
        float panel_w = 350.0f * uis;
        float panel_h = 220.0f * uis;
        float panel_x = margin;
        float panel_y = chat_top_y + (1.0f - panel_alpha) * (-12.0f * uis);

        float ime_lift = 0.0f;
        if (c->focused && platform_ime_visible()) {
            float inset = (float)platform_get_ime_bottom_inset();
            float panel_bottom = panel_y + panel_h;
            float keep_above = (float)screen_height - inset - 12.0f * uis;
            if (panel_bottom > keep_above)
                ime_lift = panel_bottom - keep_above;
            if (ime_lift > panel_y - margin) ime_lift = panel_y - margin;
            if (ime_lift < 0.0f) ime_lift = 0.0f;
            panel_y -= ime_lift;
        }
        c->panel_hit_x = panel_x;
        c->panel_hit_y = panel_y;
        c->panel_hit_w = panel_w;
        c->panel_hit_h = panel_h;
        c->panel_hit_valid = true;
        float border = 8.0f * uis;
        float pad = 8.0f * uis;
        float max_text_w = panel_w - pad * 2.0f;
        float pixel_h = 8.0f * scale;

        static const char* placeholder = "Click here or press / to chat";
        char display[CHAT_MAX_INPUT + 4];
        bool show_placeholder = (c->input_len == 0 && !c->focused);
        if (c->focused) {
            snprintf(display, sizeof(display), "%s|", c->input_buf);
        } else if (show_placeholder) {
            snprintf(display, sizeof(display), "%s", placeholder);
        } else {
            snprintf(display, sizeof(display), "%s", c->input_buf);
        }
        char input_lines[8][CHAT_WRAP_LINE_CAP];
        int input_n = wrap_text_lines(display, max_text_w, pixel_h, input_lines, 8);
        if (input_n < 1) input_n = 1;
        int input_vis = input_n;
        if (input_vis > 4) input_vis = 4;
        float input_h = (float)input_vis * line_h + pad * 2.0f;

        if (c->nineslice_tex) {
            draw_nineslice(c, c->nineslice_tex, panel_x, panel_y, panel_w, panel_h - input_h - 4.0f * uis,
                           border, panel_alpha, uv_panel, screen_width, screen_height);
        }

        float msg_area_y = panel_y + pad;
        float msg_area_h = panel_h - input_h - 4.0f * uis - pad * 2.0f;

        char vis[CHAT_WRAP_MAX_LINES][CHAT_WRAP_LINE_CAP];
        float vis_fade[CHAT_WRAP_MAX_LINES];
        bool vis_system[CHAT_WRAP_MAX_LINES];
        bool vis_color_name[CHAT_WRAP_MAX_LINES];
        int vis_count = 0;
        for (int i = 0; i < c->msg_count && vis_count < CHAT_WRAP_MAX_LINES; i++) {
            int idx = (c->msg_start + i) % CHAT_MAX_MESSAGES;
            int before = vis_count;
            vis_count += wrap_text_lines(c->messages[idx], max_text_w, pixel_h,
                                         &vis[vis_count], CHAT_WRAP_MAX_LINES - vis_count);
            for (int li = before; li < vis_count; li++) {
                vis_fade[li] = c->msg_fade[idx];
                vis_system[li] = c->msg_system[idx];

                vis_color_name[li] = (!c->msg_system[idx] && li == before);
            }
        }

        int visible_lines = (int)(msg_area_h / line_h);
        if (visible_lines < 1) visible_lines = 1;
        if (visible_lines > vis_count) visible_lines = vis_count;

        int max_scroll = vis_count > visible_lines ? vis_count - visible_lines : 0;
        if (c->scroll_offset > max_scroll) c->scroll_offset = max_scroll;

        int start_line = vis_count - visible_lines - c->scroll_offset;
        if (start_line < 0) start_line = 0;
        int end_line = start_line + visible_lines;
        if (end_line > vis_count) end_line = vis_count;

        float total_h = 0.0f;
        for (int i = start_line; i < end_line; i++)
            total_h += line_h * chat_ease(vis_fade[i]);

        float y = msg_area_y + msg_area_h - total_h;
        for (int i = start_line; i < end_line; i++) {
            float ease = chat_ease(vis_fade[i]);
            float occupy = line_h * ease;
            float fade = vis_fade[i] * panel_alpha;
            if (occupy > 0.35f * uis && fade > 0.01f &&
                y + occupy > msg_area_y - 2.0f && y < msg_area_y + msg_area_h + 2.0f) {
                draw_chat_line(c, vis[i], panel_x + pad, y, scale, fade,
                               vis_system[i], vis_color_name[i],
                               screen_width, screen_height);
            }
            y += occupy;
        }

        float input_box_y = panel_y + panel_h - input_h;

        if (c->focused) {
            EmojiSuggestion sug[EMOJI_SUGGEST_MAX];
            int open = 0, plen = 0;
            int n = 0;
            if (emoji_active_prefix(c->input_buf, c->input_len, &open, &plen) && plen > 0)
                n = emoji_suggest(c->input_buf + open + 1, plen, sug, EMOJI_SUGGEST_MAX);
            if (n > 0) {
                if (c->emoji_sel < 0) c->emoji_sel = 0;
                if (c->emoji_sel >= n) c->emoji_sel = n - 1;
                float row_h = line_h + 4.0f * uis;
                float box_h = row_h * (float)n + pad;
                float box_y = input_box_y - box_h - 4.0f * uis;
                if (box_y < margin) box_y = margin;

                if (c->nineslice_tex) {
                    draw_nineslice(c, c->nineslice_tex, panel_x, box_y, panel_w, box_h,
                                   border, panel_alpha, uv_panel, screen_width, screen_height);
                } else if (c->white_tex) {
                    draw_quad_tinted(c, c->white_tex, panel_x, box_y, panel_w, box_h,
                                     0.10f, 0.10f, 0.12f, 0.94f * panel_alpha,
                                     screen_width, screen_height);
                }
                c->emoji_hit_x = panel_x;
                c->emoji_hit_y = box_y;
                c->emoji_hit_w = panel_w;
                c->emoji_hit_row_h = row_h;
                c->emoji_hit_count = n;
                for (int i = 0; i < n; i++) {
                    float ry = box_y + pad * 0.5f + (float)i * row_h;
                    if (i == c->emoji_sel && c->white_tex) {
                        draw_quad_tinted(c, c->white_tex, panel_x + 2.0f * uis, ry,
                                         panel_w - 4.0f * uis, row_h - 2.0f * uis,
                                         0.22f, 0.24f, 0.28f, 0.90f * panel_alpha,
                                         screen_width, screen_height);

                        draw_quad_tinted(c, c->white_tex, panel_x + 2.0f * uis, ry,
                                         3.0f * uis, row_h - 2.0f * uis,
                                         0.35f, 0.78f, 0.40f, 0.95f * panel_alpha,
                                         screen_width, screen_height);
                    }
                    char line[96];
                    snprintf(line, sizeof(line), "%s  :%s:", sug[i].utf8, sug[i].name);
                    draw_text(c, line, panel_x + pad + 4.0f * uis, ry + 2.0f * uis, scale,
                              1.0f, 1.0f, 1.0f, panel_alpha, screen_width, screen_height);
                }
            }
        }

        if (c->nineslice_tex) {
            draw_nineslice(c, c->nineslice_tex, panel_x, input_box_y, panel_w, input_h,
                           border, panel_alpha, uv_panel, screen_width, screen_height);
        }

        int input_start = input_n - input_vis;
        if (input_start < 0) input_start = 0;
        float input_alpha = (show_placeholder ? 0.45f : (c->focused ? 1.0f : 0.55f)) * panel_alpha;
        for (int i = 0; i < input_vis; i++) {
            float iy = input_box_y + pad + (float)i * line_h;
            draw_text(c, input_lines[input_start + i], panel_x + pad, iy, scale,
                      1.0f, 1.0f, 1.0f, input_alpha, screen_width, screen_height);
        }

        if (c->focused && platform_ime_visible() && c->white_tex) {
            float inset = (float)platform_get_ime_bottom_inset();
            float strip_h = 44.0f * uis;
            float strip_y = (float)screen_height - inset - strip_h - 8.0f * uis;
            if (strip_y < margin) strip_y = margin;
            float strip_w = (float)screen_width - margin * 2.0f;
            draw_quad_tinted(c, c->white_tex, margin, strip_y, strip_w, strip_h,
                             0.08f, 0.08f, 0.10f, 0.96f, screen_width, screen_height);
            char strip[CHAT_MAX_INPUT + 16];
            snprintf(strip, sizeof(strip), "Chat: %s|", c->input_buf);
            draw_text(c, strip, margin + 10.0f * uis, strip_y + 12.0f * uis, scale,
                      1.0f, 1.0f, 1.0f, 1.0f, screen_width, screen_height);
        }
    }

    if (hud_alpha > 0.01f) {
        float panel_w = 350.0f * uis;
        float pad = 8.0f * uis;
        float max_text_w = panel_w - pad * 2.0f;
        if (max_text_w > (float)screen_width - margin * 2.0f - pad * 2.0f)
            max_text_w = (float)screen_width - margin * 2.0f - pad * 2.0f;
        float pixel_h = 8.0f * scale;
        float text_x = margin + pad;
        float y = chat_top_y + pad;
        int drawn_lines = 0;
        const int max_hud_lines = 14;

        for (int i = 0; i < c->msg_count && drawn_lines < max_hud_lines; i++) {
            int idx = (c->msg_start + i) % CHAT_MAX_MESSAGES;
            float age = c->msg_age[idx];
            if (age >= CHAT_BUBBLE_MAX_AGE) continue;
            if (!c->messages[idx][0]) continue;

            float t = c->msg_fade[idx];
            if (age > CHAT_BUBBLE_MAX_AGE - 1.0f) {
                float out = CHAT_BUBBLE_MAX_AGE - age;
                if (out < 0.0f) out = 0.0f;
                t *= out;
            }
            t *= hud_alpha;
            if (t <= 0.01f) continue;

            float ease = chat_ease(t > 1.0f ? 1.0f : t);
            char preview[8][CHAT_WRAP_LINE_CAP];
            int nlines = wrap_text_lines(c->messages[idx], max_text_w, pixel_h, preview, 8);
            for (int li = 0; li < nlines && drawn_lines < max_hud_lines; li++) {
                float occupy = line_h * ease;
                if (occupy > 0.35f * uis) {
                    draw_chat_line(c, preview[li], text_x, y, scale, t,
                                   c->msg_system[idx],
                                   !c->msg_system[idx] && li == 0,
                                   screen_width, screen_height);
                }
                y += occupy;
                drawn_lines++;
            }
        }
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}

void chat_draw_billboard_colored(Chat* c, const char* text, uint8_t badges,
                                 float world_x, float world_y, float world_z,
                                 const float* view, const float* projection,
                                 int screen_width, int screen_height,
                                 float nr, float ng, float nb) {
    if (!c->initialized || !text || !text[0]) return;

    float vx = view[0]*world_x + view[4]*world_y + view[8]*world_z + view[12];
    float vy = view[1]*world_x + view[5]*world_y + view[9]*world_z + view[13];
    float vz = view[2]*world_x + view[6]*world_y + view[10]*world_z + view[14];
    float vw = view[3]*world_x + view[7]*world_y + view[11]*world_z + view[15];

    float cx = projection[0]*vx + projection[4]*vy + projection[8]*vz + projection[12]*vw;
    float cy = projection[1]*vx + projection[5]*vy + projection[9]*vz + projection[13]*vw;
    float cw = projection[3]*vx + projection[7]*vy + projection[11]*vz + projection[15]*vw;

    if (cw <= 0.0f) return;

    float ndc_x = cx / cw;
    float ndc_y = cy / cw;

    if (ndc_x < -1.2f || ndc_x > 1.2f || ndc_y < -1.2f || ndc_y > 1.2f) return;

    float screen_x = (ndc_x + 1.0f) * 0.5f * (float)screen_width;
    float screen_y = (1.0f - ndc_y) * 0.5f * (float)screen_height;

    float dist = cw;
    if (dist > 20.0f) return;
    float dist_scale = 1.0f;
    if (dist > 5.0f) dist_scale = 5.0f / dist;
    float uis = c->ui_scale > 0.1f ? c->ui_scale : 1.0f;
    float scale = 3.0f * dist_scale * uis;
    float text_width = font_text_width_scaled(text, 8.0f * scale);
    float y = screen_y - 8.0f * scale;

    unsigned int btex[4];
    int nbadges = collect_badges(c, badges, btex);
    float badge_size = 10.0f * scale;
    float badge_gap = 2.5f * scale;
    float badges_w = 0.0f;
    if (nbadges > 0) {
        badges_w = (float)nbadges * badge_size + (float)(nbadges - 1) * badge_gap + 4.0f * scale;
    }

    float total_w = badges_w + text_width;
    float x = screen_x - total_w * 0.5f;
    float icon_x = x;
    float text_x = x + badges_w;
    float h = 10.0f * scale;
    float pad = 4.0f * scale;
    if (c->nt_hit_count < CHAT_PL_MAX && text[0]) {
        int hi = c->nt_hit_count++;
        strncpy(c->nt_names[hi], text, sizeof(c->nt_names[hi]) - 1);
        c->nt_names[hi][sizeof(c->nt_names[hi]) - 1] = '\0';
        c->nt_x0[hi] = x - pad;
        c->nt_y0[hi] = y - pad;
        c->nt_x1[hi] = x + total_w + pad;
        c->nt_y1[hi] = y + h + pad;
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (int i = 0; i < nbadges; i++) {
        float iy = y + (8.0f * scale - badge_size) * 0.5f;
        draw_badge_icon(c, btex[i], icon_x, iy, badge_size, screen_width, screen_height);
        icon_x += badge_size + badge_gap;
    }

    static const float outline_ox[] = {
        -1.0f, -1.0f, -1.0f,  0.0f, 0.0f,  1.0f, 1.0f, 1.0f
    };
    static const float outline_oy[] = {
        -1.0f,  0.0f,  1.0f, -1.0f, 1.0f, -1.0f, 0.0f, 1.0f
    };
    for (int oi = 0; oi < 8; oi++) {
        draw_text(c, text, text_x + outline_ox[oi], y + outline_oy[oi], scale,
                  0.0f, 0.0f, 0.0f, 0.92f, screen_width, screen_height);
    }
    draw_text(c, text, text_x, y, scale,
              nr, ng, nb, 1.0f, screen_width, screen_height);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}

void chat_draw_billboard(Chat* c, const char* text, uint8_t badges,
                         float world_x, float world_y, float world_z,
                         const float* view, const float* projection,
                         int screen_width, int screen_height) {
    float nr, ng, nb;
    chat_resolve_name_color(c, text, &nr, &ng, &nb);
    chat_draw_billboard_colored(c, text, badges, world_x, world_y, world_z,
                                view, projection, screen_width, screen_height,
                                nr, ng, nb);
}

bool chat_billboard_hit_test(Chat* c, const char* text, uint8_t badges,
                             float world_x, float world_y, float world_z,
                             const float* view, const float* projection,
                             int screen_width, int screen_height,
                             float mx, float my,
                             float* out_screen_x, float* out_screen_y) {
    if (!c || !c->initialized || !text || !text[0] || !view || !projection) return false;

    float vx = view[0]*world_x + view[4]*world_y + view[8]*world_z + view[12];
    float vy = view[1]*world_x + view[5]*world_y + view[9]*world_z + view[13];
    float vz = view[2]*world_x + view[6]*world_y + view[10]*world_z + view[14];
    float vw = view[3]*world_x + view[7]*world_y + view[11]*world_z + view[15];

    float cx = projection[0]*vx + projection[4]*vy + projection[8]*vz + projection[12]*vw;
    float cy = projection[1]*vx + projection[5]*vy + projection[9]*vz + projection[13]*vw;
    float cw = projection[3]*vx + projection[7]*vy + projection[11]*vz + projection[15]*vw;
    if (cw <= 0.0f) return false;

    float ndc_x = cx / cw;
    float ndc_y = cy / cw;
    if (ndc_x < -1.2f || ndc_x > 1.2f || ndc_y < -1.2f || ndc_y > 1.2f) return false;

    float screen_x = (ndc_x + 1.0f) * 0.5f * (float)screen_width;
    float screen_y = (1.0f - ndc_y) * 0.5f * (float)screen_height;

    float dist = cw;
    if (dist > 20.0f) return false;
    float dist_scale = 1.0f;
    if (dist > 5.0f) dist_scale = 5.0f / dist;
    float uis = c->ui_scale > 0.1f ? c->ui_scale : 1.0f;
    float scale = 3.0f * dist_scale * uis;
    float text_width = font_text_width_scaled(text, 8.0f * scale);
    float y = screen_y - 8.0f * scale;

    unsigned int btex[4];
    int nbadges = collect_badges(c, badges, btex);
    float badge_size = 10.0f * scale;
    float badge_gap = 2.5f * scale;
    float badges_w = 0.0f;
    if (nbadges > 0) {
        badges_w = (float)nbadges * badge_size + (float)(nbadges - 1) * badge_gap + 4.0f * scale;
    }
    float total_w = badges_w + text_width;
    float x = screen_x - total_w * 0.5f;
    float h = 10.0f * scale;
    float pad = 4.0f * scale;
    if (mx < x - pad || mx > x + total_w + pad || my < y - pad || my > y + h + pad)
        return false;
    if (out_screen_x) *out_screen_x = screen_x;
    if (out_screen_y) *out_screen_y = y + h + 4.0f * scale;
    return true;
}

void chat_nametag_hits_clear(Chat* c) {
    if (c) c->nt_hit_count = 0;
}

int chat_nametag_hit_test(Chat* c, float mx, float my, char* out_name, size_t name_cap) {
    if (!c || c->nt_hit_count <= 0) return -1;
    for (int i = c->nt_hit_count - 1; i >= 0; i--) {
        if (mx < c->nt_x0[i] || mx > c->nt_x1[i] || my < c->nt_y0[i] || my > c->nt_y1[i])
            continue;
        if (out_name && name_cap) {
            strncpy(out_name, c->nt_names[i], name_cap - 1);
            out_name[name_cap - 1] = '\0';
        }
        return i;
    }
    return -1;
}

static void draw_quad_tinted(Chat* c, unsigned int tex, float x, float y, float w, float h,
                             float r, float g, float b, float a,
                             int screen_width, int screen_height) {
    if (!tex) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(c->quad_shader);
    float proj[16];
    memset(proj, 0, sizeof(proj));
    proj[0] = 2.0f / (float)screen_width;
    proj[5] = -2.0f / (float)screen_height;
    proj[10] = 1.0f;
    proj[12] = -1.0f;
    proj[13] = 1.0f;
    proj[15] = 1.0f;
    glUniformMatrix4fv(c->quad_u_projection, 1, GL_FALSE, proj);
    glUniform1f(c->quad_u_alpha, a);
    glUniform4f(c->quad_u_tint, r, g, b, 1.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(c->quad_u_tex, 0);

    float verts[] = {
        x,     y,     0.0f, 1.0f,
        x + w, y,     1.0f, 1.0f,
        x + w, y + h, 1.0f, 0.0f,
        x,     y,     0.0f, 1.0f,
        x + w, y + h, 1.0f, 0.0f,
        x,     y + h, 0.0f, 0.0f,
    };

    glBindVertexArray(c->text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, c->text_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

static void draw_quad(Chat* c, unsigned int tex, float x, float y, float w, float h,
                      int screen_width, int screen_height) {
    draw_quad_tinted(c, tex, x, y, w, h, 1.0f, 1.0f, 1.0f, 1.0f, screen_width, screen_height);
}

void chat_draw_tex_uv(Chat* c, unsigned int tex,
                      float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1,
                      float r, float g, float b, float a,
                      int screen_width, int screen_height) {
    if (!c || !tex || a <= 0.01f) return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(c->quad_shader);
    float proj[16];
    memset(proj, 0, sizeof(proj));
    proj[0] = 2.0f / (float)screen_width;
    proj[5] = -2.0f / (float)screen_height;
    proj[10] = 1.0f;
    proj[12] = -1.0f;
    proj[13] = 1.0f;
    proj[15] = 1.0f;
    glUniformMatrix4fv(c->quad_u_projection, 1, GL_FALSE, proj);
    glUniform1f(c->quad_u_alpha, a);
    glUniform4f(c->quad_u_tint, r, g, b, 1.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(c->quad_u_tex, 0);
    float verts[] = {
        x,     y,     u0, v1,
        x + w, y,     u1, v1,
        x + w, y + h, u1, v0,
        x,     y,     u0, v1,
        x + w, y + h, u1, v0,
        x,     y + h, u0, v0,
    };
    glBindVertexArray(c->text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, c->text_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void chat_draw_tex_wedge(Chat* c, unsigned int tex,
                         float cx, float cy, float radius,
                         float ang0, float ang1,
                         float r, float g, float b, float a,
                         int screen_width, int screen_height) {
    if (!c || !tex || a <= 0.01f || radius < 1.0f) return;
    float half = 0.5f * fabsf(ang1 - ang0);
    float cos_h = cosf(half);
    if (cos_h < 0.2f) cos_h = 0.2f;
    float tri_r = radius / cos_h;
    float diam = radius * 2.0f;
    float x0 = cx - radius;
    float y0 = cy - radius;
    float x1 = cx + sinf(ang0) * tri_r;
    float y1 = cy - cosf(ang0) * tri_r;
    float x2 = cx + sinf(ang1) * tri_r;
    float y2 = cy - cosf(ang1) * tri_r;
    float u1 = (x1 - x0) / diam;
    float v1 = 1.0f - (y1 - y0) / diam;
    float u2 = (x2 - x0) / diam;
    float v2 = 1.0f - (y2 - y0) / diam;
    float verts[] = {
        cx, cy, 0.5f, 0.5f,
        x1, y1, u1,   v1,
        x2, y2, u2,   v2,
    };

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(c->quad_shader);
    float proj[16];
    memset(proj, 0, sizeof(proj));
    proj[0] = 2.0f / (float)screen_width;
    proj[5] = -2.0f / (float)screen_height;
    proj[10] = 1.0f;
    proj[12] = -1.0f;
    proj[13] = 1.0f;
    proj[15] = 1.0f;
    glUniformMatrix4fv(c->quad_u_projection, 1, GL_FALSE, proj);
    glUniform1f(c->quad_u_alpha, a);
    glUniform4f(c->quad_u_tint, r, g, b, 1.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(c->quad_u_tex, 0);

    glBindVertexArray(c->text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, c->text_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

static void draw_pill_fill(Chat* c, float x, float y, float w, float h,
                           float r, float g, float b, float a,
                           int screen_width, int screen_height) {
    if (!c || !c->circle_tex || !c->white_tex || w < 0.5f || h < 0.5f || a <= 0.01f) return;

    if (w <= h) {

        float d = w;
        float cy = y + (h - d) * 0.5f;
        draw_quad_tinted(c, c->circle_tex, x, cy, d, d, r, g, b, a, screen_width, screen_height);
        return;
    }

    float rad = h * 0.5f;
    draw_quad_tinted(c, c->circle_tex, x, y, h, h, r, g, b, a, screen_width, screen_height);
    float mid_w = w - h;
    if (mid_w > 0.5f) {
        draw_quad_tinted(c, c->white_tex, x + rad, y, mid_w, h, r, g, b, a,
                         screen_width, screen_height);
    }
    draw_quad_tinted(c, c->circle_tex, x + w - h, y, h, h, r, g, b, a,
                     screen_width, screen_height);
}

static void draw_nineslice(Chat* c, unsigned int tex, float x, float y, float w, float h,
                           float border, float alpha, float uv_border,
                           int screen_width, int screen_height) {
    if (!tex || alpha <= 0.01f) return;
    if (w < border * 2.0f) w = border * 2.0f;
    if (h < border * 2.0f) h = border * 2.0f;
    if (uv_border <= 0.0f) uv_border = 50.0f / 550.0f;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(c->quad_shader);
    float proj[16];
    memset(proj, 0, sizeof(proj));
    proj[0] = 2.0f / (float)screen_width;
    proj[5] = -2.0f / (float)screen_height;
    proj[10] = 1.0f;
    proj[12] = -1.0f;
    proj[13] = 1.0f;
    proj[15] = 1.0f;
    glUniformMatrix4fv(c->quad_u_projection, 1, GL_FALSE, proj);
    glUniform1f(c->quad_u_alpha, alpha);
    glUniform4f(c->quad_u_tint, 1.0f, 1.0f, 1.0f, 1.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(c->quad_u_tex, 0);

    const float uv_b = uv_border;

    float sx[4] = { x, x + border, x + w - border, x + w };
    float sy[4] = { y, y + border, y + h - border, y + h };

    float su[4] = { 0.0f, uv_b, 1.0f - uv_b, 1.0f };
    float sv[4] = { 1.0f, 1.0f - uv_b, uv_b, 0.0f };

    float verts[9 * 6 * 4];
    int vi = 0;

    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            float x0 = sx[col],   x1 = sx[col + 1];
            float y0 = sy[row],   y1 = sy[row + 1];
            float u0 = su[col],   u1 = su[col + 1];
            float v0 = sv[row],   v1 = sv[row + 1];

            verts[vi++] = x0; verts[vi++] = y0; verts[vi++] = u0; verts[vi++] = v0;
            verts[vi++] = x1; verts[vi++] = y0; verts[vi++] = u1; verts[vi++] = v0;
            verts[vi++] = x1; verts[vi++] = y1; verts[vi++] = u1; verts[vi++] = v1;

            verts[vi++] = x0; verts[vi++] = y0; verts[vi++] = u0; verts[vi++] = v0;
            verts[vi++] = x1; verts[vi++] = y1; verts[vi++] = u1; verts[vi++] = v1;
            verts[vi++] = x0; verts[vi++] = y1; verts[vi++] = u0; verts[vi++] = v1;
        }
    }

    glBindVertexArray(c->text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, c->text_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (size_t)vi * sizeof(float), verts);
    glDrawArrays(GL_TRIANGLES, 0, 9 * 6);
    glBindVertexArray(0);
}

static void draw_chat_line(Chat* c, const char* line, float x, float y, float scale,
                           float alpha, bool is_system, bool color_name,
                           int screen_width, int screen_height) {
    if (!line || !line[0] || alpha <= 0.01f) return;
    if (is_system) {
        draw_text(c, line, x + 1, y + 1, scale, 0.0f, 0.0f, 0.0f, alpha * 0.55f,
                  screen_width, screen_height);
        draw_text(c, line, x, y, scale, 0.62f, 0.62f, 0.62f, alpha,
                  screen_width, screen_height);
        return;
    }
    const char* colon = color_name ? strchr(line, ':') : NULL;
    int name_len = colon ? (int)(colon - line) : 0;
    if (colon && name_len > 0 && name_len < 32 && line[0] != ' ') {
        char name[32];
        memcpy(name, line, (size_t)name_len);
        name[name_len] = '\0';
        float nr, ng, nb;
        chat_resolve_name_color(c, name, &nr, &ng, &nb);
        draw_text(c, name, x + 1, y + 1, scale, 0.0f, 0.0f, 0.0f, alpha * 0.55f,
                  screen_width, screen_height);
        int nw = draw_text(c, name, x, y, scale, nr, ng, nb, alpha,
                           screen_width, screen_height);
        draw_text(c, colon, x + (float)nw + 1, y + 1, scale,
                  0.0f, 0.0f, 0.0f, alpha * 0.55f, screen_width, screen_height);
        draw_text(c, colon, x + (float)nw, y, scale,
                  1.0f, 1.0f, 1.0f, alpha, screen_width, screen_height);
    } else {
        draw_text(c, line, x + 1, y + 1, scale, 0.0f, 0.0f, 0.0f, alpha * 0.55f,
                  screen_width, screen_height);
        draw_text(c, line, x, y, scale, 1.0f, 1.0f, 1.0f, alpha,
                  screen_width, screen_height);
    }
}

float chat_draw_bubble(Chat* c, const char* text, float world_x, float world_y, float world_z,
                       const float* view, const float* projection,
                       int screen_width, int screen_height,
                       bool show_pointer, float screen_lift,
                       float appear_fade, float age) {
    if (!c->initialized || !text || !text[0]) return 0.0f;
    if (!c->bubble_texture) return 0.0f;

    float t = appear_fade;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float fade_out = 1.0f;
    if (age > CHAT_BUBBLE_MAX_AGE - 1.0f) {
        fade_out = CHAT_BUBBLE_MAX_AGE - age;
        if (fade_out < 0.0f) fade_out = 0.0f;
        if (fade_out > 1.0f) fade_out = 1.0f;
        t *= fade_out;
    }
    if (t <= 0.01f) return 0.0f;
    float ease = chat_ease(t);

    float vx = view[0]*world_x + view[4]*world_y + view[8]*world_z + view[12];
    float vy = view[1]*world_x + view[5]*world_y + view[9]*world_z + view[13];
    float vz = view[2]*world_x + view[6]*world_y + view[10]*world_z + view[14];
    float vw = view[3]*world_x + view[7]*world_y + view[11]*world_z + view[15];

    float clipx = projection[0]*vx + projection[4]*vy + projection[8]*vz + projection[12]*vw;
    float clipy = projection[1]*vx + projection[5]*vy + projection[9]*vz + projection[13]*vw;
    float cw = projection[3]*vx + projection[7]*vy + projection[11]*vz + projection[15]*vw;

    if (cw <= 0.0f) return 0.0f;

    float ndc_x = clipx / cw;
    float ndc_y = clipy / cw;
    if (ndc_x < -1.2f || ndc_x > 1.2f || ndc_y < -1.2f || ndc_y > 1.2f) return 0.0f;

    float screen_x = (ndc_x + 1.0f) * 0.5f * (float)screen_width;
    float screen_y = (1.0f - ndc_y) * 0.5f * (float)screen_height - screen_lift;

    float dist = cw;
    if (dist > 25.0f) return 0.0f;
    float dist_scale = 1.0f;
    if (dist > 5.0f) dist_scale = 5.0f / dist;

    float uis = c->ui_scale > 0.1f ? c->ui_scale : 1.0f;
    float scale = 5.0f * dist_scale * uis;
    float pixel_h = 8.0f * scale;
    float text_height = pixel_h;
    float line_gap = 2.0f * dist_scale * uis;
    float pad_h = 6.0f * dist_scale * uis;
    float pad_w = 14.0f * dist_scale * uis;
    float stack_gap = 4.0f * dist_scale * uis;

    float slide = (1.0f - ease) * 14.0f * dist_scale * uis;
    if (fade_out < 1.0f)
        slide -= (1.0f - fade_out) * 12.0f * dist_scale * uis;
    screen_y += slide;

    float max_text_w = 18.0f * pixel_h;
    if (max_text_w > (float)screen_width * 0.45f)
        max_text_w = (float)screen_width * 0.45f;
    if (max_text_w < pixel_h * 4.0f) max_text_w = pixel_h * 4.0f;

    char lines[16][CHAT_WRAP_LINE_CAP];
    int nlines = wrap_text_lines(text, max_text_w, pixel_h, lines, 16);
    if (nlines < 1) return 0.0f;

    float text_width = 0.0f;
    for (int i = 0; i < nlines; i++) {
        float w = font_text_width_scaled(lines[i], pixel_h);
        if (w > text_width) text_width = w;
    }

    float bw = text_width + pad_w * 2.0f;
    float bh = (float)nlines * text_height + (float)(nlines - 1) * line_gap + pad_h * 2.0f;
    float bx = screen_x - bw * 0.5f;
    float by = screen_y - bh - 5.0f * dist_scale;
    float border = 12.0f * dist_scale * uis;
    if (border > bw * 0.35f) border = bw * 0.35f;
    if (border > bh * 0.35f) border = bh * 0.35f;
    if (border < 4.0f) border = 4.0f;

    float tx = bx + pad_w;
    float ty = by + pad_h - 4.0f * dist_scale;

    float ptr_h = 0.0f;
    if (show_pointer && c->bubble_bottom_tex) {
        ptr_h = 25.0f * dist_scale * uis;
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    draw_nineslice(c, c->bubble_texture, bx, by, bw, bh,
                   border, t, 50.0f / 560.0f, screen_width, screen_height);

    if (show_pointer && c->bubble_bottom_tex) {
        float ptr_w = 50.0f * dist_scale * uis;
        float ptr_x = screen_x - ptr_w * 0.5f;
        float ptr_y = by + bh - 5.0f * dist_scale;
        draw_quad_tinted(c, c->bubble_bottom_tex, ptr_x, ptr_y, ptr_w, ptr_h,
                         1.0f, 1.0f, 1.0f, t, screen_width, screen_height);
    }

    for (int i = 0; i < nlines; i++) {
        float ly = ty + (float)i * (text_height + line_gap);
        draw_text(c, lines[i], tx, ly, scale,
                  0.0f, 0.0f, 0.0f, t, screen_width, screen_height);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    float full_h = bh + (show_pointer ? ptr_h * 0.55f : 0.0f) + stack_gap;
    return full_h * ease;
}

float chat_render_player_list(Chat* c, const char** names, const uint8_t* badges, int count,
                              int screen_width, int screen_height) {
    if (!c || count <= 0) return -1.0f;
    if (count > CHAT_PL_MAX) count = CHAT_PL_MAX;
    ChatPlayerListEntry entries[CHAT_PL_MAX];
    memset(entries, 0, sizeof(entries));
    for (int i = 0; i < count; i++) {
        entries[i].name = names ? names[i] : NULL;
        entries[i].badges = badges ? badges[i] : 0;
        entries[i].team_idx = -1;
        entries[i].stat_count = 0;
        entries[i].has_name_color = false;
    }
    return chat_render_player_list_ex(c, entries, count, NULL, 0, screen_width, screen_height);
}

float chat_render_player_list_ex(Chat* c, const ChatPlayerListEntry* entries, int count,
                                 const char* const* stat_names, int stat_name_count,
                                 int screen_width, int screen_height) {
    if (!c || !c->initialized || !entries || count <= 0) return -1.0f;
    if (count > CHAT_PL_MAX) count = CHAT_PL_MAX;
    if (stat_name_count < 0) stat_name_count = 0;
    if (stat_name_count > CHAT_PL_STAT_MAX) stat_name_count = CHAT_PL_STAT_MAX;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float uis = c->ui_scale > 0.1f ? c->ui_scale : 1.0f;
    float scale = 1.5f * uis;
    float line_h = 8.0f * scale + 3.0f * uis;
    float margin = 10.0f * uis;
    float pad = 8.0f * uis;
    float header_h = line_h + 2.0f * uis;
    float border = 8.0f * uis;
    float badge_size = 8.0f * scale;
    float badge_gap = 2.0f * scale;
    float stat_col_w = 0.0f;
    float stat_gap = 10.0f * uis;
    const float uv_panel = 50.0f / 550.0f;

    if (stat_name_count > 0) {
        for (int s = 0; s < stat_name_count; s++) {
            const char* sn = (stat_names && stat_names[s]) ? stat_names[s] : "";
            float tw = font_text_width_scaled(sn, 8.0f * scale);
            if (tw < 20.0f * scale) tw = 20.0f * scale;
            if (tw > stat_col_w) stat_col_w = tw;
        }
        float zero_w = font_text_width_scaled("0", 8.0f * scale);
        if (zero_w > stat_col_w) stat_col_w = zero_w;
        for (int i = 0; i < count; i++) {
            if (entries[i].team_idx == -2) continue;
            for (int s = 0; s < stat_name_count; s++) {
                char num[32];
                float v = (s < entries[i].stat_count) ? entries[i].stats[s] : 0.0f;
                if (fabsf(v - (float)(int)v) < 0.001f)
                    snprintf(num, sizeof(num), "%d", (int)v);
                else
                    snprintf(num, sizeof(num), "%.1f", v);
                float tw = font_text_width_scaled(num, 8.0f * scale);
                if (tw > stat_col_w) stat_col_w = tw;
            }
        }
        stat_col_w += 8.0f * uis;
    }

    {
        float old_fade[CHAT_PL_MAX];
        char old_names[CHAT_PL_MAX][32];
        int old_count = c->pl_entry_count;
        memcpy(old_fade, c->pl_entry_fade, sizeof(old_fade));
        memcpy(old_names, c->pl_entry_names, sizeof(old_names));
        c->pl_entry_count = count;
        for (int i = 0; i < count; i++) {
            const char* n = entries[i].name ? entries[i].name : "";
            strncpy(c->pl_entry_names[i], n, 31);
            c->pl_entry_names[i][31] = '\0';
            c->pl_entry_header[i] = (entries[i].team_idx == -2);
            float found = 0.0f;
            for (int j = 0; j < old_count; j++) {
                if (strcmp(old_names[j], n) == 0) { found = old_fade[j]; break; }
            }
            c->pl_entry_fade[i] = found;
            if (found <= 0.0f) c->pl_entry_fade[i] = 0.01f;
        }
    }

    float names_w = font_text_width_scaled("Players", 8.0f * scale);
    for (int i = 0; i < count; i++) {
        if (!entries[i].name) continue;
        float tw = font_text_width_scaled(entries[i].name, 8.0f * scale);
        if (entries[i].team_idx == -2) {
            if (tw > names_w) names_w = tw;
            continue;
        }
        uint8_t b = entries[i].badges;
        unsigned int btex[4];
        int nb = collect_badges(c, b, btex);
        if (nb > 0) {
            tw += (float)nb * badge_size + (float)(nb - 1) * badge_gap + 3.0f * scale;
        }
        if (tw > names_w) names_w = tw;
    }

    float stats_block_w = (stat_name_count > 0)
        ? (stat_gap + (float)stat_name_count * stat_col_w)
        : 0.0f;
    float max_w = names_w + stats_block_w;

    float target_w = max_w + pad * 2.0f;
    float target_h = header_h + (line_h * (float)count) + pad * 2.0f;

    if (c->pl_anim_w < 1.0f) c->pl_anim_w = target_w;
    if (c->pl_anim_h < 1.0f) c->pl_anim_h = target_h;
    if (target_w > c->pl_anim_w + 1.0f)
        c->pl_anim_w += (target_w - c->pl_anim_w) * 0.45f;
    else
        c->pl_anim_w += (target_w - c->pl_anim_w) * 0.18f;
    c->pl_anim_h += (target_h - c->pl_anim_h) * 0.18f;

    float panel_w = c->pl_anim_w;
    float panel_h = c->pl_anim_h;
    float panel_x = (float)screen_width - margin - panel_w;
    float panel_y = margin;

    c->pl_hit_x = panel_x;
    c->pl_hit_y = panel_y;
    c->pl_hit_w = panel_w;
    c->pl_hit_h = panel_h;
    c->pl_hit_pad = pad;
    c->pl_hit_header_h = header_h;
    c->pl_hit_line_h = line_h;
    c->pl_hit_count = count;

    if (c->nineslice_tex) {
        draw_nineslice(c, c->nineslice_tex, panel_x, panel_y, panel_w, panel_h,
                       border, 1.0f, uv_panel, screen_width, screen_height);
    }

    float text_x = panel_x + pad;
    float text_y = panel_y + pad;
    draw_text(c, "Players", text_x + 1, text_y + 1, scale,
              0.0f, 0.0f, 0.0f, 0.6f, screen_width, screen_height);
    draw_text(c, "Players", text_x, text_y, scale,
              1.0f, 1.0f, 1.0f, 1.0f, screen_width, screen_height);

    if (stat_name_count > 0) {
        float col_right = panel_x + panel_w - pad;
        for (int s = stat_name_count - 1; s >= 0; s--) {
            const char* sn = (stat_names && stat_names[s]) ? stat_names[s] : "";
            float tw = font_text_width_scaled(sn, 8.0f * scale);
            float sx = col_right - tw;
            draw_text(c, sn, sx + 1, text_y + 1, scale,
                      0.0f, 0.0f, 0.0f, 0.5f, screen_width, screen_height);
            draw_text(c, sn, sx, text_y, scale,
                      0.9f, 0.9f, 0.95f, 1.0f, screen_width, screen_height);
            col_right -= stat_col_w;
        }
    }

    {
        float sep_y = text_y + line_h + 1.0f * uis;
        float sep_h = 1.0f * uis;
        float sep_x = panel_x + pad;
        float sep_w = panel_w - pad * 2.0f;
        if (c->nineslice_tex || c->white_tex) {
            glUseProgram(c->quad_shader);
            float proj[16];
            memset(proj, 0, sizeof(proj));
            proj[0] = 2.0f / (float)screen_width;
            proj[5] = -2.0f / (float)screen_height;
            proj[10] = 1.0f;
            proj[12] = -1.0f;
            proj[13] = 1.0f;
            proj[15] = 1.0f;
            glUniformMatrix4fv(c->quad_u_projection, 1, GL_FALSE, proj);
            glUniform1f(c->quad_u_alpha, 0.85f);
            glUniform4f(c->quad_u_tint, 0.05f, 0.05f, 0.05f, 1.0f);
            glActiveTexture(GL_TEXTURE0);
            if (c->white_tex) {
                glBindTexture(GL_TEXTURE_2D, c->white_tex);
            } else {
                glBindTexture(GL_TEXTURE_2D, c->nineslice_tex);
            }
            glUniform1i(c->quad_u_tex, 0);
            float verts[] = {
                sep_x,         sep_y,         0.5f, 0.5f,
                sep_x + sep_w, sep_y,         0.5f, 0.5f,
                sep_x + sep_w, sep_y + sep_h, 0.5f, 0.5f,
                sep_x,         sep_y,         0.5f, 0.5f,
                sep_x + sep_w, sep_y + sep_h, 0.5f, 0.5f,
                sep_x,         sep_y + sep_h, 0.5f, 0.5f,
            };
            glBindVertexArray(c->text_vao);
            glBindBuffer(GL_ARRAY_BUFFER, c->text_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
        }
    }

    float name_y = text_y + header_h;

    for (int i = 0; i < count; i++) {
        if (!entries[i].name) { name_y += line_h; continue; }
        float entry_a = c->pl_entry_fade[i];
        if (entry_a < 0.01f) entry_a = 0.01f;
        float slide = (1.0f - entry_a) * 10.0f * uis;
        float nx = text_x;
        float ny = name_y + slide;

        if (entries[i].team_idx == -2) {
            float br = entries[i].has_name_color ? entries[i].name_r : 0.72f;
            float bg = entries[i].has_name_color ? entries[i].name_g : 0.72f;
            float bb = entries[i].has_name_color ? entries[i].name_b : 0.76f;
            float bar_h = line_h;
            float bar_y = name_y;
            if (c->white_tex) {
                draw_quad_tinted(c, c->white_tex, panel_x, bar_y, panel_w, bar_h,
                                 br, bg, bb, 0.95f * entry_a,
                                 screen_width, screen_height);
            }
            float text_h = 8.0f * scale;
            float label_x = panel_x + pad;
            float label_y = bar_y + (bar_h - text_h) * 0.5f;
            draw_text(c, entries[i].name, label_x + 1, label_y + 1, scale,
                      0.0f, 0.0f, 0.0f, 0.45f * entry_a, screen_width, screen_height);
            draw_text(c, entries[i].name, label_x, label_y, scale,
                      1.0f, 1.0f, 1.0f, 0.98f * entry_a, screen_width, screen_height);
            name_y += line_h;
            continue;
        }

        uint8_t b = entries[i].badges;
        unsigned int btex[4];
        int nb = collect_badges(c, b, btex);
        for (int bi = 0; bi < nb; bi++) {
            float iy = ny + (8.0f * scale - badge_size) * 0.5f;
            draw_badge_icon(c, btex[bi], nx, iy, badge_size, screen_width, screen_height);
            nx += badge_size + badge_gap;
        }
        if (nb > 0) nx += 1.0f * scale;

        float nr, ng, nb_col;
        if (entries[i].has_name_color) {
            nr = entries[i].name_r; ng = entries[i].name_g; nb_col = entries[i].name_b;
        } else {
            chat_resolve_name_color(c, entries[i].name, &nr, &ng, &nb_col);
        }
        draw_text(c, entries[i].name, nx + 1, ny + 1, scale,
                  0.0f, 0.0f, 0.0f, 0.5f * entry_a, screen_width, screen_height);
        draw_text(c, entries[i].name, nx, ny, scale,
                  nr, ng, nb_col, 0.95f * entry_a, screen_width, screen_height);

        if (stat_name_count > 0) {
            float col_right = panel_x + panel_w - pad;
            for (int s = stat_name_count - 1; s >= 0; s--) {
                char num[32];
                float v = (s < entries[i].stat_count) ? entries[i].stats[s] : 0.0f;
                if (fabsf(v - (float)(int)v) < 0.001f)
                    snprintf(num, sizeof(num), "%d", (int)v);
                else
                    snprintf(num, sizeof(num), "%.1f", v);
                float tw = font_text_width_scaled(num, 8.0f * scale);
                float sx = col_right - tw;
                draw_text(c, num, sx + 1, ny + 1, scale,
                          0.0f, 0.0f, 0.0f, 0.45f * entry_a, screen_width, screen_height);
                draw_text(c, num, sx, ny, scale,
                          1.0f, 1.0f, 1.0f, 0.95f * entry_a, screen_width, screen_height);
                col_right -= stat_col_w;
            }
        }

        name_y += line_h;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    return panel_x;
}

int chat_player_list_hit_test(Chat* c, float x, float y,
                              float* out_card_x, float* out_card_y) {
    if (!c || !c->initialized || c->pl_hit_count <= 0) return -1;
    if (x < c->pl_hit_x || x > c->pl_hit_x + c->pl_hit_w ||
        y < c->pl_hit_y || y > c->pl_hit_y + c->pl_hit_h) {
        return -1;
    }
    float name_top = c->pl_hit_y + c->pl_hit_pad + c->pl_hit_header_h;
    if (y < name_top || c->pl_hit_line_h < 0.5f) return -1;
    int idx = (int)((y - name_top) / c->pl_hit_line_h);
    if (idx < 0 || idx >= c->pl_hit_count) return -1;
    if (c->pl_entry_header[idx]) return -1;
    if (out_card_x) *out_card_x = c->pl_hit_x - 12.0f;
    if (out_card_y) *out_card_y = name_top + (float)idx * c->pl_hit_line_h;
    return idx;
}

void chat_render_toolbar(Chat* c, const ToolSlotInfo* tools, int count,
                         float bottom_lift,
                         int screen_width, int screen_height) {
    if (!c->initialized || count <= 0) return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float uis = c->ui_scale > 0.1f ? c->ui_scale : 1.0f;
    float scale = 1.2f * uis;
    float slot_size = 64.0f * uis;
    float slot_gap = 6.0f * uis;
    float icon_size = 40.0f * uis;
    float border = 6.0f * uis;
    float label_h = 8.0f * scale;
    float slot_total_h = slot_size + label_h + 4.0f * uis;
    float margin_bottom = 10.0f * uis;
    if (bottom_lift < 0.0f) bottom_lift = 0.0f;

    float total_w = (float)count * slot_size + (float)(count - 1) * slot_gap;
    float start_x = ((float)screen_width - total_w) * 0.5f;
    float start_y = (float)screen_height - margin_bottom - slot_total_h - bottom_lift;

    for (int i = 0; i < count; i++) {
        float sx = start_x + (float)i * (slot_size + slot_gap);
        float sy = start_y;

        if (c->nineslice_tex) {
            draw_nineslice(c, c->nineslice_tex, sx, sy, slot_size, slot_size,
                           border, 1.0f, 50.0f / 550.0f, screen_width, screen_height);
        }

        if (tools[i].icon_tex) {
            float draw_w = icon_size;
            float draw_h = icon_size;
            if (tools[i].icon_w > 0 && tools[i].icon_h > 0) {
                float aspect = (float)tools[i].icon_w / (float)tools[i].icon_h;
                if (aspect > 1.0f) {
                    draw_h = icon_size / aspect;
                } else {
                    draw_w = icon_size * aspect;
                }
            }
            float icon_x = sx + (slot_size - draw_w) * 0.5f;
            float icon_y = sy + (slot_size - draw_h) * 0.5f;
            draw_quad(c, tools[i].icon_tex, icon_x, icon_y, draw_w, draw_h,
                      screen_width, screen_height);
        }

        if (tools[i].name) {
            float tw = font_text_width_scaled(tools[i].name, 8.0f * scale);
            float lx = sx + (slot_size - tw) * 0.5f;
            float ly = sy + slot_size + 2.0f * uis;

            draw_text(c, tools[i].name, lx + 1, ly + 1, scale,
                      0.0f, 0.0f, 0.0f, 0.6f, screen_width, screen_height);

            float brightness = tools[i].selected ? 1.0f : 0.7f;
            draw_text(c, tools[i].name, lx, ly, scale,
                      brightness, brightness, brightness, 1.0f, screen_width, screen_height);
        }
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}

void chat_render_tool_cooldown(Chat* c, float ready_frac, int tool_count,
                               float bottom_lift,
                               int screen_width, int screen_height) {
    if (!c->initialized || tool_count <= 0) return;
    if (ready_frac < 0.0f) ready_frac = 0.0f;
    if (ready_frac > 1.0f) ready_frac = 1.0f;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float uis = c->ui_scale > 0.1f ? c->ui_scale : 1.0f;
    float scale = 1.2f * uis;
    float slot_size = 64.0f * uis;
    float slot_gap = 6.0f * uis;
    float label_h = 8.0f * scale;
    float slot_total_h = slot_size + label_h + 4.0f * uis;
    float margin_bottom = 10.0f * uis;
    if (bottom_lift < 0.0f) bottom_lift = 0.0f;

    float total_w = (float)tool_count * slot_size + (float)(tool_count - 1) * slot_gap;
    float toolbar_y = (float)screen_height - margin_bottom - slot_total_h - bottom_lift;

    float bar_w = total_w;
    if (bar_w > 220.0f * uis) bar_w = 220.0f * uis;
    if (bar_w < 100.0f * uis) bar_w = 100.0f * uis;
    float bar_h = 12.0f * uis;
    float gap = 8.0f * uis;
    float border = 5.0f * uis;
    float pad = 3.0f * uis;

    float bar_x = ((float)screen_width - bar_w) * 0.5f;
    float bar_y = toolbar_y - gap - bar_h;

    if (c->nineslice_tex) {
        draw_nineslice(c, c->nineslice_tex, bar_x, bar_y, bar_w, bar_h,
                       border, 1.0f, 50.0f / 550.0f, screen_width, screen_height);
    }

    float inner_x = bar_x + pad;
    float inner_y = bar_y + pad;
    float inner_w = (bar_w - pad * 2.0f) * ready_frac;
    float inner_h = bar_h - pad * 2.0f;

    if (inner_w > 0.5f) {
        float r = 0.95f - ready_frac * 0.35f;
        float g = 0.55f + ready_frac * 0.35f;
        float b = 0.12f + ready_frac * 0.15f;
        draw_pill_fill(c, inner_x, inner_y, inner_w, inner_h,
                       r, g, b, 0.95f, screen_width, screen_height);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}

int chat_toolbar_hit_test(Chat* c, float x, float y, int tool_count,
                          float bottom_lift,
                          int screen_width, int screen_height) {
    if (!c || tool_count <= 0) return 0;
    (void)screen_height;

    float uis = c->ui_scale > 0.1f ? c->ui_scale : 1.0f;
    float scale = 1.2f * uis;
    float slot_size = 64.0f * uis;
    float slot_gap = 6.0f * uis;
    float label_h = 8.0f * scale;
    float slot_total_h = slot_size + label_h + 4.0f * uis;
    float margin_bottom = 10.0f * uis;
    if (bottom_lift < 0.0f) bottom_lift = 0.0f;

    float total_w = (float)tool_count * slot_size + (float)(tool_count - 1) * slot_gap;
    float start_x = ((float)screen_width - total_w) * 0.5f;
    float start_y = (float)screen_height - margin_bottom - slot_total_h - bottom_lift;

    float pad = 10.0f * uis;
    if (y < start_y - pad || y > start_y + slot_size + pad) return 0;

    for (int i = 0; i < tool_count; i++) {
        float sx = start_x + (float)i * (slot_size + slot_gap);
        if (x >= sx - pad * 0.35f && x <= sx + slot_size + pad * 0.35f) {
            return i + 1;
        }
    }
    return 0;
}

void chat_render_health_bar(Chat* c, int health, int max_health,
                            float list_left_x, float list_top_y,
                            int screen_width, int screen_height) {
    if (!c->initialized) return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float uis = c->ui_scale > 0.1f ? c->ui_scale : 1.0f;
    float margin = 10.0f * uis;
    float gap = 8.0f * uis;
    float bar_w = 120.0f * uis;
    float bar_h = 16.0f * uis;
    float border = 6.0f * uis;
    float pad = 4.0f * uis;
    float text_scale = 0.95f * uis;

    float bar_x, bar_y;
    if (list_left_x >= 0.0f) {

        bar_x = list_left_x - gap - bar_w;
        bar_y = list_top_y >= 0.0f ? list_top_y : margin;
        if (bar_x < margin) bar_x = margin;
    } else {

        bar_x = (float)screen_width - margin - bar_w;
        bar_y = margin;
    }

    if (c->nineslice_tex) {
        draw_nineslice(c, c->nineslice_tex, bar_x, bar_y, bar_w, bar_h,
                       border, 1.0f, 50.0f / 550.0f, screen_width, screen_height);
    }

    float fill_frac = (float)health / (float)max_health;
    if (fill_frac < 0.0f) fill_frac = 0.0f;
    if (fill_frac > 1.0f) fill_frac = 1.0f;
    float inner_x = bar_x + pad;
    float inner_y = bar_y + pad;
    float inner_w = (bar_w - pad * 2.0f) * fill_frac;
    float inner_h = bar_h - pad * 2.0f;

    if (inner_w > 0.5f) {
        float r = 0.2f, g = 0.8f, b = 0.2f;
        if (health < 30) { r = 0.9f; g = 0.2f; b = 0.1f; }
        else if (health < 60) { r = 0.9f; g = 0.7f; b = 0.1f; }
        draw_pill_fill(c, inner_x, inner_y, inner_w, inner_h,
                       r, g, b, 0.95f, screen_width, screen_height);
    }

    char hp_buf[16];
    snprintf(hp_buf, sizeof(hp_buf), "%d/%d", health, max_health);
    float glyph_h = 8.0f * text_scale;
    float tw = font_text_width_scaled(hp_buf, glyph_h);
    float tx = bar_x + (bar_w - tw) * 0.5f;
    float ty = bar_y + (bar_h - glyph_h) * 0.5f;
    draw_text(c, hp_buf, tx + 1, ty + 1, text_scale,
              0.0f, 0.0f, 0.0f, 0.6f, screen_width, screen_height);
    draw_text(c, hp_buf, tx, ty, text_scale,
              1.0f, 1.0f, 1.0f, 1.0f, screen_width, screen_height);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}

void chat_render_hud_text(Chat* c, const char* text, float x, float y, float scale,
                          int screen_width, int screen_height) {
    if (!c->initialized || !text || !text[0]) return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    draw_text(c, text, x + 1, y + 1, scale,
              0.0f, 0.0f, 0.0f, 0.6f, screen_width, screen_height);
    draw_text(c, text, x, y, scale,
              1.0f, 1.0f, 1.0f, 0.9f, screen_width, screen_height);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}

void chat_render_banner(Chat* c, const char* text, float cx, float cy, float scale,
                        float alpha, float y_offset,
                        int screen_width, int screen_height) {
    if (!c->initialized || !text || !text[0] || alpha <= 0.01f) return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float uis = c->ui_scale > 0.1f ? c->ui_scale : 1.0f;
    float pixel_h = 8.0f * scale;
    float pad_x = 18.0f * uis;
    float pad_y = 10.0f * uis;
    float border = 10.0f * uis;

    float max_text_w = (float)screen_width * 0.7f;
    char lines[6][CHAT_WRAP_LINE_CAP];
    int nlines = wrap_text_lines(text, max_text_w, pixel_h, lines, 6);
    if (nlines < 1) nlines = 1;

    float text_w = 0.0f;
    for (int i = 0; i < nlines; i++) {
        float w = font_text_width_scaled(lines[i], pixel_h);
        if (w > text_w) text_w = w;
    }
    float line_h = pixel_h + 4.0f * uis;
    float bw = text_w + pad_x * 2.0f;
    float bh = (float)nlines * line_h + pad_y * 2.0f - 4.0f * uis;
    float bx = cx - bw * 0.5f;
    float by = cy + y_offset - bh * 0.5f;

    if (c->nineslice_tex) {
        draw_nineslice(c, c->nineslice_tex, bx, by, bw, bh,
                       border, alpha * 0.92f, 50.0f / 550.0f,
                       screen_width, screen_height);
    }

    for (int i = 0; i < nlines; i++) {
        float tw = font_text_width_scaled(lines[i], pixel_h);
        float tx = cx - tw * 0.5f;
        float ty = by + pad_y + (float)i * line_h;
        draw_text(c, lines[i], tx + 1, ty + 1, scale,
                  0.0f, 0.0f, 0.0f, alpha * 0.55f, screen_width, screen_height);
        draw_text(c, lines[i], tx, ty, scale,
                  1.0f, 1.0f, 1.0f, alpha, screen_width, screen_height);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}

float chat_music_credit_stack_height(Chat* c, float size_scale) {
    float uis = (c && c->ui_scale > 0.1f) ? c->ui_scale : 1.0f;
    if (size_scale < 0.4f) size_scale = 0.4f;
    if (size_scale > 1.5f) size_scale = 1.5f;
    float s = uis * size_scale;
    float title_h = 15.0f * s;
    float author_h = 12.0f * s;
    float pad_y = 10.0f * s;
    float gap = 3.0f * s;
    float margin = 16.0f * uis;
    float text_block_h = title_h + gap + author_h;
    float icon_size = text_block_h;
    if (icon_size < 22.0f * s) icon_size = 22.0f * s;
    float bh = pad_y * 2.0f + (text_block_h > icon_size ? text_block_h : icon_size);
    return bh + margin;
}

void chat_render_music_credit(Chat* c, const char* title, const char* author,
                              float alpha, float y_offset, float size_scale,
                              const float* wave_levels, int wave_count,
                              int screen_width, int screen_height) {
    if (!c->initialized || !title || !title[0] || alpha <= 0.01f) return;
    if (size_scale < 0.4f) size_scale = 0.4f;
    if (size_scale > 1.5f) size_scale = 1.5f;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float uis = c->ui_scale > 0.1f ? c->ui_scale : 1.0f;
    float s = uis * size_scale;
    float title_h = 15.0f * s;
    float author_h = 12.0f * s;
    float pad_x = 14.0f * s;
    float pad_y = 10.0f * s;
    float gap = 3.0f * s;
    float border = 8.0f * s;
    float margin = 16.0f * uis;
    float icon_gap = 10.0f * s;
    float wave_gap = 12.0f * s;

    float text_block_h = title_h;
    if (author && author[0]) text_block_h += gap + author_h;
    float icon_size = text_block_h;
    if (icon_size < 22.0f * s) icon_size = 22.0f * s;
    bool has_icon = c->music_icon_tex != 0;

    int nbars = (wave_levels && wave_count > 0) ? wave_count : 0;
    if (nbars > 8) nbars = 8;
    float wave_w = 0.0f;
    float bar_gap = 2.0f * s;
    float bar_w = 3.5f * s;
    if (nbars > 0) {
        wave_w = (float)nbars * bar_w + (float)(nbars - 1) * bar_gap;
    }

    float title_w = font_text_width_scaled(title, title_h);
    float author_w = (author && author[0]) ? font_text_width_scaled(author, author_h) : 0.0f;
    float text_w = title_w > author_w ? title_w : author_w;
    float content_w = text_w;
    if (has_icon) content_w += icon_size + icon_gap;
    if (nbars > 0) content_w += wave_gap + wave_w;
    float bw = content_w + pad_x * 2.0f;
    float bh = pad_y * 2.0f + (text_block_h > icon_size ? text_block_h : icon_size);
    if (bw < 140.0f * s) bw = 140.0f * s;

    float bx = ((float)screen_width - bw) * 0.5f;
    float by = (float)screen_height - bh - margin + y_offset;

    if (c->nineslice_tex) {
        draw_nineslice(c, c->nineslice_tex, bx, by, bw, bh,
                       border, alpha * 0.92f, 50.0f / 550.0f,
                       screen_width, screen_height);
    }

    float content_x = bx + pad_x;
    float content_y = by + (bh - (text_block_h > icon_size ? text_block_h : icon_size)) * 0.5f;

    if (has_icon) {
        float iy = by + (bh - icon_size) * 0.5f;
        draw_quad_tinted(c, c->music_icon_tex, content_x, iy, icon_size, icon_size,
                         1.0f, 1.0f, 1.0f, alpha, screen_width, screen_height);
        content_x += icon_size + icon_gap;
    }

    float ty = content_y;
    if (!author || !author[0]) {
        ty = by + (bh - title_h) * 0.5f;
    }
    draw_text(c, title, content_x + 1, ty + 1, title_h / 8.0f,
              0.0f, 0.0f, 0.0f, alpha * 0.55f, screen_width, screen_height);
    draw_text(c, title, content_x, ty, title_h / 8.0f,
              1.0f, 1.0f, 1.0f, alpha, screen_width, screen_height);
    if (author && author[0]) {
        float ay = ty + title_h + gap;
        draw_text(c, author, content_x + 1, ay + 1, author_h / 8.0f,
                  0.0f, 0.0f, 0.0f, alpha * 0.55f, screen_width, screen_height);
        draw_text(c, author, content_x, ay, author_h / 8.0f,
                  0.75f, 0.75f, 0.78f, alpha, screen_width, screen_height);
    }

    if (nbars > 0 && c->white_tex) {
        float wave_h = bh - pad_y * 2.0f;
        if (wave_h < 12.0f * s) wave_h = 12.0f * s;
        float wx = bx + bw - pad_x - wave_w;
        float mid_y = by + bh * 0.5f;
        float min_h = 3.0f * s;
        for (int i = 0; i < nbars; i++) {
            float lvl = wave_levels[i];
            if (lvl < 0.0f) lvl = 0.0f;
            if (lvl > 1.0f) lvl = 1.0f;
            float h = min_h + lvl * (wave_h - min_h);
            float x = wx + (float)i * (bar_w + bar_gap);
            float y = mid_y - h * 0.5f;
            draw_quad_tinted(c, c->white_tex, x, y, bar_w, h,
                             1.0f, 1.0f, 1.0f, alpha * 0.95f,
                             screen_width, screen_height);
        }
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}
