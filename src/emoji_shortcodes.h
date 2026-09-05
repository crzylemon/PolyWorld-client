/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: emoji_shortcodes.h                                                                  |
|   Purpose: :name: -> emoji (chat typing)                                                    |
\*-------------------------------------------------------------------------------------------*/

#ifndef EMOJI_SHORTCODES_H
#define EMOJI_SHORTCODES_H

#include <stdbool.h>
#include <stddef.h>

#define EMOJI_SUGGEST_MAX 8

typedef struct {
    const char* name;
    const char* utf8;
} EmojiSuggestion;

bool emoji_expand_trailing(char* buf, int* inout_len, int max_len);

int emoji_expand_all(char* buf, int* inout_len, int max_len);

bool emoji_active_prefix(const char* buf, int len, int* out_start, int* out_prefix_len);

int emoji_suggest(const char* prefix, int prefix_len, EmojiSuggestion* out, int max_out);

bool emoji_apply_suggestion(char* buf, int* inout_len, int max_len, const char* selected_name);

#endif
