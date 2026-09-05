/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: text_edit.h                                                                         |
|   Purpose: caret, selection, and UTF-8 edits for every text field                           |
\*-------------------------------------------------------------------------------------------*/

#ifndef TEXT_EDIT_H
#define TEXT_EDIT_H

#include <stdbool.h>

typedef struct {
    int caret;
    int sel_anchor;
    bool dragging;
    float last_click_t;
    int last_click_pos;
    int click_count;
} TextEdit;

void te_reset(TextEdit* e, int caret);
void te_clamp(TextEdit* e, int len);
bool te_has_sel(const TextEdit* e);
int te_sel_lo(const TextEdit* e);
int te_sel_hi(const TextEdit* e);
void te_clear_sel(TextEdit* e);
void te_select_all(TextEdit* e, int len);
void te_set_caret(TextEdit* e, int pos, bool extend);

int te_utf8_prev(const char* s, int i);
int te_utf8_next(const char* s, int len, int i);
int te_utf8_encode(unsigned int cp, char* out);
int te_word_prev(const char* s, int i);
int te_word_next(const char* s, int len, int i);
int te_line_start(const char* s, int i);
int te_line_end(const char* s, int len, int i);
int te_line_index(const char* s, int pos);

float te_prefix_width(const char* s, int nbytes, float px_h);
int te_hit_x(const char* s, int start, int end, float local_x, float px_h);

void te_mouse_down(TextEdit* e, const char* buf, int len, int hit, bool shift, float now);
void te_mouse_drag(TextEdit* e, int hit);
void te_mouse_up(TextEdit* e);

void te_move_left(TextEdit* e, const char* buf, int len, bool shift, bool word);
void te_move_right(TextEdit* e, const char* buf, int len, bool shift, bool word);
void te_move_home(TextEdit* e, const char* buf, int len, bool shift, bool doc);
void te_move_end(TextEdit* e, const char* buf, int len, bool shift, bool doc);
void te_move_vert(TextEdit* e, const char* buf, int len, int dir, bool shift, float px_h);

bool te_delete_sel(char* buf, int* len, TextEdit* e);
bool te_insert(char* buf, int* len, int cap, TextEdit* e, const char* bytes, int n);
bool te_backspace(char* buf, int* len, TextEdit* e, bool word);
bool te_delete_fwd(char* buf, int* len, TextEdit* e, bool word);
bool te_insert_cp(char* buf, int* len, int cap, TextEdit* e, unsigned int cp);

int te_filter_paste(const char* src, char* dst, int dst_max, bool multiline);
void te_copy(const char* buf, const TextEdit* e, int len, bool copy_all_if_none);
bool te_cut(char* buf, int* len, TextEdit* e, bool cut_line_if_none);
bool te_paste(char* buf, int* len, int cap, TextEdit* e, const char* src, bool multiline);

#endif
