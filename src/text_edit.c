/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: text_edit.c                                                                         |
|   Purpose: caret, selection, and UTF-8 edits for every text field                           |
\*-------------------------------------------------------------------------------------------*/

#include "text_edit.h"
#include "font.h"
#include "platform.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define TE_DBLCLICK 0.40f

void te_reset(TextEdit* e, int caret) {
    if (!e) return;
    e->caret = caret;
    e->sel_anchor = caret;
    e->dragging = false;
    e->last_click_t = -10.0f;
    e->last_click_pos = -1;
    e->click_count = 0;
}

void te_clamp(TextEdit* e, int len) {
    if (!e) return;
    if (len < 0) len = 0;
    if (e->caret < 0) e->caret = 0;
    if (e->caret > len) e->caret = len;
    if (e->sel_anchor < 0) e->sel_anchor = 0;
    if (e->sel_anchor > len) e->sel_anchor = len;
}

bool te_has_sel(const TextEdit* e) {
    return e && e->caret != e->sel_anchor;
}

int te_sel_lo(const TextEdit* e) {
    if (!e) return 0;
    return e->caret < e->sel_anchor ? e->caret : e->sel_anchor;
}

int te_sel_hi(const TextEdit* e) {
    if (!e) return 0;
    return e->caret > e->sel_anchor ? e->caret : e->sel_anchor;
}

void te_clear_sel(TextEdit* e) {
    if (!e) return;
    e->sel_anchor = e->caret;
}

void te_select_all(TextEdit* e, int len) {
    if (!e) return;
    if (len < 0) len = 0;
    e->sel_anchor = 0;
    e->caret = len;
}

void te_set_caret(TextEdit* e, int pos, bool extend) {
    if (!e) return;
    if (pos < 0) pos = 0;
    e->caret = pos;
    if (!extend) e->sel_anchor = pos;
}

int te_utf8_prev(const char* s, int i) {
    if (!s || i <= 0) return 0;
    i--;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
    return i;
}

int te_utf8_next(const char* s, int len, int i) {
    if (!s || i >= len) return len;
    i++;
    while (i < len && ((unsigned char)s[i] & 0xC0) == 0x80) i++;
    return i;
}

int te_utf8_encode(unsigned int cp, char* out) {
    if (!out) return 0;
    if (cp < 0x80u) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800u) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    out[0] = (char)(0xF0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4;
}

static bool te_is_word(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c >= 0x80;
}

int te_word_prev(const char* s, int i) {
    if (!s || i <= 0) return 0;
    i = te_utf8_prev(s, i);
    while (i > 0 && s[i] != '\n' && !te_is_word((unsigned char)s[i]))
        i = te_utf8_prev(s, i);
    if (s[i] == '\n') return i + 1;
    while (i > 0 && te_is_word((unsigned char)s[te_utf8_prev(s, i)]))
        i = te_utf8_prev(s, i);
    return i;
}

int te_word_next(const char* s, int len, int i) {
    if (!s || i >= len) return len;
    if (s[i] == '\n') return i + 1;
    if (te_is_word((unsigned char)s[i])) {
        while (i < len && te_is_word((unsigned char)s[i]))
            i = te_utf8_next(s, len, i);
    } else {
        while (i < len && !te_is_word((unsigned char)s[i]) && s[i] != '\n')
            i = te_utf8_next(s, len, i);
    }
    return i;
}

int te_line_start(const char* s, int i) {
    if (!s || i <= 0) return 0;
    while (i > 0 && s[i - 1] != '\n') i--;
    return i;
}

int te_line_end(const char* s, int len, int i) {
    if (!s) return 0;
    if (i < 0) i = 0;
    while (i < len && s[i] != '\n') i++;
    return i;
}

int te_line_index(const char* s, int pos) {
    if (!s || pos <= 0) return 0;
    int n = 0;
    for (int i = 0; i < pos; i++)
        if (s[i] == '\n') n++;
    return n;
}

float te_prefix_width(const char* s, int nbytes, float px_h) {
    if (!s || nbytes <= 0) return 0.0f;
    char tmp[768];
    if (nbytes < (int)sizeof(tmp)) {
        memcpy(tmp, s, (size_t)nbytes);
        tmp[nbytes] = '\0';
        return font_text_width_scaled(tmp, px_h);
    }
    char* heap = (char*)malloc((size_t)nbytes + 1);
    if (!heap) return 0.0f;
    memcpy(heap, s, (size_t)nbytes);
    heap[nbytes] = '\0';
    float w = font_text_width_scaled(heap, px_h);
    free(heap);
    return w;
}

int te_hit_x(const char* s, int start, int end, float local_x, float px_h) {
    if (!s) return 0;
    if (start < 0) start = 0;
    if (end < start) end = start;
    if (local_x <= 0.0f) return start;
    int i = start;
    while (i < end) {
        int n = te_utf8_next(s, end, i);
        float w = te_prefix_width(s + start, n - start, px_h);
        if (w >= local_x) {
            float prev = te_prefix_width(s + start, i - start, px_h);
            return (local_x - prev <= w - local_x) ? i : n;
        }
        i = n;
    }
    return end;
}

static void te_select_word(TextEdit* e, const char* buf, int len, int pos) {
    if (!buf) {
        te_set_caret(e, pos, false);
        return;
    }
    te_clamp(e, len);
    if (pos < 0) pos = 0;
    if (pos > len) pos = len;
    int a = pos;
    int b = pos;
    if (pos < len && te_is_word((unsigned char)buf[pos])) {
        a = pos;
        while (a > 0 && te_is_word((unsigned char)buf[te_utf8_prev(buf, a)]))
            a = te_utf8_prev(buf, a);
        b = pos;
        while (b < len && te_is_word((unsigned char)buf[b]))
            b = te_utf8_next(buf, len, b);
    } else if (pos > 0 && te_is_word((unsigned char)buf[te_utf8_prev(buf, pos)])) {
        a = te_word_prev(buf, pos);
        b = pos;
    } else {
        a = pos > 0 ? te_utf8_prev(buf, pos) : 0;
        b = pos < len ? te_utf8_next(buf, len, pos) : len;
        if (a == b) {
            a = te_line_start(buf, pos);
            b = te_line_end(buf, len, pos);
        }
    }
    e->sel_anchor = a;
    e->caret = b;
}

static void te_select_line(TextEdit* e, const char* buf, int len, int pos) {
    int a = te_line_start(buf, pos);
    int b = te_line_end(buf, len, pos);
    if (b < len && buf[b] == '\n') b++;
    e->sel_anchor = a;
    e->caret = b;
}

void te_mouse_down(TextEdit* e, const char* buf, int len, int hit, bool shift, float now) {
    if (!e) return;
    if (hit < 0) hit = 0;
    if (hit > len) hit = len;
    e->dragging = true;
    if (shift) {
        e->caret = hit;
        e->click_count = 1;
        e->last_click_pos = hit;
        e->last_click_t = now;
        return;
    }
    int clicks = 1;
    if (now - e->last_click_t <= TE_DBLCLICK && abs(hit - e->last_click_pos) <= 2) {
        clicks = e->click_count + 1;
        if (clicks > 3) clicks = 1;
    }
    e->click_count = clicks;
    e->last_click_t = now;
    e->last_click_pos = hit;
    if (clicks == 2) te_select_word(e, buf, len, hit);
    else if (clicks >= 3) te_select_line(e, buf, len, hit);
    else te_set_caret(e, hit, false);
}

void te_mouse_drag(TextEdit* e, int hit) {
    if (!e || !e->dragging) return;
    if (hit < 0) hit = 0;
    e->caret = hit;
}

void te_mouse_up(TextEdit* e) {
    if (!e) return;
    e->dragging = false;
}

void te_move_left(TextEdit* e, const char* buf, int len, bool shift, bool word) {
    if (!e) return;
    te_clamp(e, len);
    if (!shift && te_has_sel(e) && !word) {
        e->caret = te_sel_lo(e);
        te_clear_sel(e);
        return;
    }
    int p = e->caret;
    if (word) p = te_word_prev(buf, p);
    else p = te_utf8_prev(buf, p);
    te_set_caret(e, p, shift);
}

void te_move_right(TextEdit* e, const char* buf, int len, bool shift, bool word) {
    if (!e) return;
    te_clamp(e, len);
    if (!shift && te_has_sel(e) && !word) {
        e->caret = te_sel_hi(e);
        te_clear_sel(e);
        return;
    }
    int p = e->caret;
    if (word) p = te_word_next(buf, len, p);
    else p = te_utf8_next(buf, len, p);
    te_set_caret(e, p, shift);
}

void te_move_home(TextEdit* e, const char* buf, int len, bool shift, bool doc) {
    if (!e) return;
    te_clamp(e, len);
    int p = doc ? 0 : te_line_start(buf, e->caret);
    te_set_caret(e, p, shift);
}

void te_move_end(TextEdit* e, const char* buf, int len, bool shift, bool doc) {
    if (!e) return;
    te_clamp(e, len);
    int p = doc ? len : te_line_end(buf, len, e->caret);
    te_set_caret(e, p, shift);
}

void te_move_vert(TextEdit* e, const char* buf, int len, int dir, bool shift, float px_h) {
    if (!e || !buf) return;
    te_clamp(e, len);
    int ls = te_line_start(buf, e->caret);
    int le = te_line_end(buf, len, e->caret);
    float want = te_prefix_width(buf + ls, e->caret - ls, px_h);
    int dest_s, dest_e;
    if (dir < 0) {
        if (ls <= 0) {
            te_set_caret(e, 0, shift);
            return;
        }
        dest_e = ls - 1;
        dest_s = te_line_start(buf, dest_e);
    } else {
        if (le >= len) {
            te_set_caret(e, len, shift);
            return;
        }
        dest_s = le + 1;
        dest_e = te_line_end(buf, len, dest_s);
    }
    int hit = te_hit_x(buf, dest_s, dest_e, want, px_h);
    te_set_caret(e, hit, shift);
}

bool te_delete_sel(char* buf, int* len, TextEdit* e) {
    if (!buf || !len || !e) return false;
    te_clamp(e, *len);
    int lo = te_sel_lo(e);
    int hi = te_sel_hi(e);
    if (hi <= lo) return false;
    memmove(buf + lo, buf + hi, (size_t)(*len - hi + 1));
    *len -= (hi - lo);
    e->caret = lo;
    e->sel_anchor = lo;
    return true;
}

bool te_insert(char* buf, int* len, int cap, TextEdit* e, const char* bytes, int n) {
    if (!buf || !len || !e || !bytes || n <= 0 || cap <= 1) return false;
    te_clamp(e, *len);
    te_delete_sel(buf, len, e);
    if (*len < 0) *len = 0;
    if (*len + n >= cap) n = cap - 1 - *len;
    if (n <= 0) return false;
    memmove(buf + e->caret + n, buf + e->caret, (size_t)(*len - e->caret + 1));
    memcpy(buf + e->caret, bytes, (size_t)n);
    *len += n;
    buf[*len] = '\0';
    e->caret += n;
    e->sel_anchor = e->caret;
    return true;
}

bool te_backspace(char* buf, int* len, TextEdit* e, bool word) {
    if (!buf || !len || !e) return false;
    te_clamp(e, *len);
    if (te_has_sel(e)) return te_delete_sel(buf, len, e);
    if (e->caret <= 0) return false;
    int from = word ? te_word_prev(buf, e->caret) : te_utf8_prev(buf, e->caret);
    e->sel_anchor = from;
    return te_delete_sel(buf, len, e);
}

bool te_delete_fwd(char* buf, int* len, TextEdit* e, bool word) {
    if (!buf || !len || !e) return false;
    te_clamp(e, *len);
    if (te_has_sel(e)) return te_delete_sel(buf, len, e);
    if (e->caret >= *len) return false;
    int to = word ? te_word_next(buf, *len, e->caret) : te_utf8_next(buf, *len, e->caret);
    e->sel_anchor = to;
    return te_delete_sel(buf, len, e);
}

bool te_insert_cp(char* buf, int* len, int cap, TextEdit* e, unsigned int cp) {
    if (cp < 32 || cp == 0x7F || cp > 0x10FFFF) return false;
    char tmp[5];
    int n = te_utf8_encode(cp, tmp);
    return te_insert(buf, len, cap, e, tmp, n);
}

int te_filter_paste(const char* src, char* dst, int dst_max, bool multiline) {
    if (!src || !dst || dst_max <= 1) return 0;
    int n = 0;
    for (int i = 0; src[i] && n < dst_max - 1; i++) {
        unsigned char ch = (unsigned char)src[i];
        if (ch == '\r') continue;
        if (ch == '\n') {
            dst[n++] = multiline ? '\n' : ' ';
            continue;
        }
        if (ch == '\t') {
            if (multiline) {
                if (n + 2 >= dst_max) break;
                dst[n++] = ' ';
                dst[n++] = ' ';
            } else {
                dst[n++] = ' ';
            }
            continue;
        }
        if (ch < 32) continue;
        dst[n++] = (char)ch;
    }
    dst[n] = '\0';
    return n;
}

void te_copy(const char* buf, const TextEdit* e, int len, bool copy_all_if_none) {
    if (!buf || !e) return;
    int lo = te_sel_lo(e);
    int hi = te_sel_hi(e);
    if (hi > lo) {
        int n = hi - lo;
        char* tmp = (char*)malloc((size_t)n + 1);
        if (!tmp) return;
        memcpy(tmp, buf + lo, (size_t)n);
        tmp[n] = '\0';
        platform_clipboard_set(tmp);
        free(tmp);
        return;
    }
    if (copy_all_if_none && len > 0)
        platform_clipboard_set(buf);
}

bool te_cut(char* buf, int* len, TextEdit* e, bool cut_line_if_none) {
    if (!buf || !len || !e) return false;
    te_clamp(e, *len);
    if (!te_has_sel(e) && cut_line_if_none) {
        int a = te_line_start(buf, e->caret);
        int b = te_line_end(buf, *len, e->caret);
        if (b < *len && buf[b] == '\n') b++;
        e->sel_anchor = a;
        e->caret = b;
    }
    if (!te_has_sel(e)) return false;
    te_copy(buf, e, *len, false);
    return te_delete_sel(buf, len, e);
}

bool te_paste(char* buf, int* len, int cap, TextEdit* e, const char* src, bool multiline) {
    char tmp[4096];
    int n = te_filter_paste(src, tmp, (int)sizeof(tmp), multiline);
    if (n <= 0) return false;
    return te_insert(buf, len, cap, e, tmp, n);
}
