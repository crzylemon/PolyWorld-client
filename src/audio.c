/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: audio.c                                                                             |
|   Purpose: sfx (Web Audio / stb_vorbis + Pulse)                                             |
\*-------------------------------------------------------------------------------------------*/

#include "audio.h"
#include "log.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static float g_listener_x = 0, g_listener_y = 0, g_listener_z = 0;

static float g_vol_master = 1.0f;
static float g_vol_music = 1.0f;
static float g_vol_sfx = 1.0f;

static float clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

void audio_get_volume_levels(float* master, float* music, float* sfx) {
    if (master) *master = g_vol_master;
    if (music) *music = g_vol_music;
    if (sfx) *sfx = g_vol_sfx;
}

#ifdef __EMSCRIPTEN__
EM_JS(void, audio_apply_volume_js, (float master, float music, float sfx), {
    window._pw_vol_master = master;
    window._pw_vol_music = music;
    window._pw_vol_sfx = sfx;
    try {
        if (window._pw_bgm) {
            var base = (typeof window._pw_bgm._pw_baseVol === 'number')
                ? window._pw_bgm._pw_baseVol : 1.0;
            window._pw_bgm.volume = Math.max(0, Math.min(1, base * music * master));
        }
        var a = window._pw_audio;
        if (a && a.loops) {
            Object.keys(a.loops).forEach(function(k) {
                var L = a.loops[k];
                if (!L || !L.gain) return;
                var base = (typeof L._pw_baseVol === 'number') ? L._pw_baseVol : 0.45;
                L.gain.gain.value = base * sfx * master;
            });
        }
    } catch (e) {}
});
#endif

void audio_set_volume_levels(float master, float music, float sfx) {
    g_vol_master = clamp01(master);
    g_vol_music = clamp01(music);
    g_vol_sfx = clamp01(sfx);
#ifdef __EMSCRIPTEN__
    audio_apply_volume_js(g_vol_master, g_vol_music, g_vol_sfx);
#endif
}

void audio_set_listener(float x, float y, float z) {
    g_listener_x = x; g_listener_y = y; g_listener_z = z;
}

static char g_music_title[96];
static char g_music_author[96];
static unsigned g_music_cred_gen = 0;
static unsigned g_music_cred_req = 0;

static void music_cred_set(const char* title, const char* author) {
    const char* t = title ? title : "";
    const char* a = author ? author : "";
    if (strcmp(g_music_title, t) == 0 && strcmp(g_music_author, a) == 0) return;
    strncpy(g_music_title, t, sizeof(g_music_title) - 1);
    g_music_title[sizeof(g_music_title) - 1] = '\0';
    strncpy(g_music_author, a, sizeof(g_music_author) - 1);
    g_music_author[sizeof(g_music_author) - 1] = '\0';
    g_music_cred_gen++;
}

static void music_cred_clear(void) {
    music_cred_set("", "");
}

void audio_music_cred_get(AudioMusicCred* out) {
    if (!out) return;
    strncpy(out->title, g_music_title, sizeof(out->title) - 1);
    out->title[sizeof(out->title) - 1] = '\0';
    strncpy(out->author, g_music_author, sizeof(out->author) - 1);
    out->author[sizeof(out->author) - 1] = '\0';
    out->gen = g_music_cred_gen;
}

static bool music_cred_url_from_music(const char* url, char* out, size_t cap) {
    if (!url || !url[0] || !out || cap < 8) return false;
    const char* q = strchr(url, '?');
    size_t path_len = q ? (size_t)(q - url) : strlen(url);
    if (path_len + 5 >= cap) return false;
    memcpy(out, url, path_len);
    out[path_len] = '\0';
    char* slash = strrchr(out, '/');
    char* dot = strrchr(out, '.');
    if (dot && (!slash || dot > slash)) *dot = '\0';
    size_t used = strlen(out);
    if (used + 5 >= cap) return false;
    memcpy(out + used, ".cred", 6);
    if (q) {
        size_t qlen = strlen(q);
        if (used + 5 + qlen >= cap) return false;
        memcpy(out + used + 5, q, qlen + 1);
    }
    return true;
}

static void music_cred_trim(char* s) {
    if (!s) return;
    char* start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r') start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

static bool music_cred_looks_html(const char* s) {
    if (!s || !s[0]) return false;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    if (!*s) return false;
    if (s[0] == '<') {
        if ((s[1] == '!' || s[1] == '?' || s[1] == '/')) return true;

        for (const char* p = s + 1; *p; p++) {
            unsigned char c = (unsigned char)*p;
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '>') break;
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                c == '!' || c == '-' || c == ':' ||
                (c >= '0' && c <= '9')) continue;
            return false;
        }
        return true;
    }
    if ((s[0] == 'D' || s[0] == 'd') &&
        (strncmp(s, "DOCTYPE", 7) == 0 || strncmp(s, "doctype", 7) == 0))
        return true;
    return false;
}

static bool music_cred_is_valid(const char* title, const char* author) {
    if (!title || !title[0]) return false;
    if (music_cred_looks_html(title)) return false;
    if (author && author[0] && music_cred_looks_html(author)) return false;
    return true;
}

static void music_cred_apply_text(const char* text) {
    if (!text) {
        music_cred_clear();
        return;
    }

    if (music_cred_looks_html(text)) {
        music_cred_clear();
        return;
    }
    char buf[256];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* line1 = buf;
    char* line2 = NULL;
    for (char* p = buf; *p; p++) {
        if (*p == '\n') {
            *p = '\0';
            line2 = p + 1;
            break;
        }
    }
    if (line2) {
        char* nl = strchr(line2, '\n');
        if (nl) *nl = '\0';
    }
    music_cred_trim(line1);
    if (line2) music_cred_trim(line2);
    if (!music_cred_is_valid(line1, line2)) {
        music_cred_clear();
        return;
    }
    music_cred_set(line1, line2 ? line2 : "");
}

static float g_wave_smooth[AUDIO_MUSIC_WAVE_BARS];

static void music_wave_smooth_apply(float* levels, int count, const float* raw) {
    if (count > AUDIO_MUSIC_WAVE_BARS) count = AUDIO_MUSIC_WAVE_BARS;
    for (int i = 0; i < count; i++) {
        float t = raw[i];
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        g_wave_smooth[i] += (t - g_wave_smooth[i]) * 0.42f;
        levels[i] = g_wave_smooth[i];
    }
}

static float calc_volume(float x, float y, float z) {
    float dx = x - g_listener_x, dy = y - g_listener_y, dz = z - g_listener_z;
    float dist = sqrtf(dx*dx + dy*dy + dz*dz);
    if (dist < 1.0f) return 1.0f;
    if (dist > 100.0f) return 0.0f;
    return 1.0f / (1.0f + dist * 0.1f);
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_JS(void, audio_init_js, (), {
    if (window._pw_audio) return;
    try {
        var ctx = new (window.AudioContext || window.webkitAudioContext)();
        window._pw_audio = { ctx: ctx, buffers: {} };
        var sounds = ['jump', 'land', 'footsteps', 'explosion', 'chat', 'rocket', 'tick', 'dead'];
        sounds.forEach(function(name, idx) {
            var url = 'https://polyworld.games/assets/wasm/sounds/' + name + '.ogg';
            var xhr = new XMLHttpRequest();
            xhr.open('GET', url, true);
            xhr.responseType = 'arraybuffer';
            xhr.onload = function() {
                if (xhr.status === 200) {
                    ctx.decodeAudioData(xhr.response, function(buffer) {
                        window._pw_audio.buffers[idx] = buffer;
                    });
                }
            };
            xhr.send();
        });
    } catch(e) {}
});

EM_JS(void, audio_play_vol_js, (int id, float volume), {
    var a = window._pw_audio;
    if (!a || !a.ctx) return;
    if (a.ctx.state === 'suspended') a.ctx.resume();
    var buf = a.buffers[id];
    if (!buf) return;
    var master = (typeof window._pw_vol_master === 'number') ? window._pw_vol_master : 1.0;
    var sfx = (typeof window._pw_vol_sfx === 'number') ? window._pw_vol_sfx : 1.0;
    var v = volume * 0.5 * sfx * master;
    if (v < 0.01) return;
    var src = a.ctx.createBufferSource();
    src.buffer = buf;
    var gain = a.ctx.createGain();
    gain.gain.value = v;
    src.connect(gain);
    gain.connect(a.ctx.destination);
    src.start(0);
});

EM_JS(void, audio_start_loop_js, (int id, float volume), {
    var a = window._pw_audio;
    if (!a || !a.ctx) return;
    if (a.ctx.state === 'suspended') a.ctx.resume();
    if (!a.loops) a.loops = {};
    if (a.loops[id]) return;
    var buf = a.buffers[id];
    if (!buf) return;
    var master = (typeof window._pw_vol_master === 'number') ? window._pw_vol_master : 1.0;
    var sfx = (typeof window._pw_vol_sfx === 'number') ? window._pw_vol_sfx : 1.0;
    var base = volume * 0.45;
    if (base < 0.01) return;
    var v = base * sfx * master;
    var src = a.ctx.createBufferSource();
    src.buffer = buf;
    src.loop = true;
    var gain = a.ctx.createGain();
    gain.gain.value = v;
    src.connect(gain);
    gain.connect(a.ctx.destination);
    src.start(0);
    a.loops[id] = { src: src, gain: gain, _pw_baseVol: base };
});

EM_JS(void, audio_stop_loop_js, (int id), {
    var a = window._pw_audio;
    if (!a || !a.loops || !a.loops[id]) return;
    try { a.loops[id].src.stop(0); } catch(e) {}
    try { a.loops[id].src.disconnect(); } catch(e) {}
    try { a.loops[id].gain.disconnect(); } catch(e) {}
    delete a.loops[id];
});

void audio_init(void) { audio_init_js(); }
void audio_play(int sfx_id) { audio_play_vol_js(sfx_id, 1.0f); }
void audio_play_at(int sfx_id, float x, float y, float z) {
    float vol = calc_volume(x, y, z);
    if (vol > 0.01f) audio_play_vol_js(sfx_id, vol);
}
void audio_start_loop(int sfx_id, float volume) { audio_start_loop_js(sfx_id, volume); }
void audio_stop_loop(int sfx_id) { audio_stop_loop_js(sfx_id); }

EM_JS(void, audio_play_music_js, (const char* url, float volume), {
    try {
        if (!window._pw_bgm) {
            window._pw_bgm = new Audio();
            window._pw_bgm.loop = true;
            window._pw_bgm.crossOrigin = 'anonymous';
        }
        var bgm = window._pw_bgm;
        bgm.pause();
        bgm.src = UTF8ToString(url);
        var master = (typeof window._pw_vol_master === 'number') ? window._pw_vol_master : 1.0;
        var music = (typeof window._pw_vol_music === 'number') ? window._pw_vol_music : 1.0;
        bgm._pw_baseVol = Math.max(0, Math.min(1, volume));
        bgm.volume = Math.max(0, Math.min(1, bgm._pw_baseVol * music * master));

        try {
            if (!window._pw_bgm_analyser) {
                var AC = window.AudioContext || window.webkitAudioContext;
                if (AC) {
                    if (!window._pw_bgm_ctx) window._pw_bgm_ctx = new AC();
                    var ctx = window._pw_bgm_ctx;
                    var src = ctx.createMediaElementSource(bgm);
                    var analyser = ctx.createAnalyser();
                    analyser.fftSize = 64;
                    analyser.smoothingTimeConstant = 0.55;
                    src.connect(analyser);
                    analyser.connect(ctx.destination);
                    window._pw_bgm_analyser = analyser;
                    window._pw_bgm_src = src;
                }
            }
            if (window._pw_bgm_ctx && window._pw_bgm_ctx.state === 'suspended') {
                window._pw_bgm_ctx.resume();
            }
        } catch (ae) { console.warn('[Audio] analyser setup', ae); }

        var p = bgm.play();
        if (p && p.catch) p.catch(function(e) { console.warn('[Audio] BGM play failed', e); });
    } catch(e) { console.warn('[Audio] BGM error', e); }
});

EM_JS(void, audio_stop_music_js, (), {
    var bgm = window._pw_bgm;
    if (!bgm) return;
    try { bgm.pause(); bgm.removeAttribute('src'); bgm.load(); } catch(e) {}
});

EM_JS(void, audio_music_waveform_js, (float* out, int n), {
    if (n < 1) return;
    var i;
    var analyser = window._pw_bgm_analyser;
    var bgm = window._pw_bgm;
    var playing = bgm && !bgm.paused && bgm.src;
    if (!playing) {
        for (i = 0; i < n; i++) HEAPF32[(out >> 2) + i] = 0;
        return;
    }
    if (analyser) {
        var bins = analyser.frequencyBinCount;
        var data = new Uint8Array(bins);
        analyser.getByteFrequencyData(data);
        for (i = 0; i < n; i++) {
            var a0 = Math.floor(i * bins / n);
            var a1 = Math.floor((i + 1) * bins / n);
            if (a1 <= a0) a1 = a0 + 1;
            var sum = 0, c = 0;
            for (var b = a0; b < a1 && b < bins; b++) { sum += data[b]; c++; }
            var v = c > 0 ? (sum / c) / 255.0 : 0;
            v = Math.min(1, v * 1.55);
            HEAPF32[(out >> 2) + i] = v;
        }
    } else {
        var t = performance.now() * 0.008;
        for (i = 0; i < n; i++) {
            var v = 0.25 + 0.55 * Math.abs(Math.sin(t + i * 0.9));
            HEAPF32[(out >> 2) + i] = v;
        }
    }
});

EM_JS(void, audio_fetch_music_cred_js, (const char* url, int req_id), {
    var musicUrl = UTF8ToString(url);
    var q = musicUrl.indexOf("?");
    var path = q >= 0 ? musicUrl.substring(0, q) : musicUrl;
    var query = q >= 0 ? musicUrl.substring(q) : "";
    var slash = path.lastIndexOf("/");
    var dot = path.lastIndexOf(".");
    if (dot > slash) path = path.substring(0, dot);
    var credUrl = path + ".cred" + query;
    fetch(credUrl, { mode: "cors" }).then(function(r) {
        if (!r.ok) throw new Error("no cred");
        return r.text();
    }).then(function(text) {
        var nl = String.fromCharCode(10);
        var cr = String.fromCharCode(13);
        var normalized = String(text).split(cr + nl).join(nl).split(cr).join(nl);
        var t0 = normalized.replace(/^\s+/, "");
        if (t0.charAt(0) === "<") {
            Module.ccall("audio_music_cred_loaded", null,
                ["number", "string", "string"], [req_id, "", ""]);
            return;
        }
        var lines = normalized.split(nl);
        var title = (lines[0] || "").trim();
        var author = (lines[1] || "").trim();
        Module.ccall("audio_music_cred_loaded", null,
            ["number", "string", "string"], [req_id, title, author]);
    }).catch(function() {
        Module.ccall("audio_music_cred_loaded", null,
            ["number", "string", "string"], [req_id, "", ""]);
    });
});

EMSCRIPTEN_KEEPALIVE
void audio_music_cred_loaded(int req_id, const char* title, const char* author) {
    if ((unsigned)req_id != g_music_cred_req) return;
    if (title && title[0]) {
        char t[96], a[96];
        strncpy(t, title, sizeof(t) - 1); t[sizeof(t) - 1] = '\0';
        strncpy(a, author ? author : "", sizeof(a) - 1); a[sizeof(a) - 1] = '\0';
        music_cred_trim(t);
        music_cred_trim(a);
        if (music_cred_is_valid(t, a)) music_cred_set(t, a);
        else music_cred_clear();
    } else {
        music_cred_clear();
    }
}

void audio_play_music(const char* url, float volume) {
    if (!url || !url[0]) return;
    audio_play_music_js(url, volume);
    g_music_cred_req++;
    audio_fetch_music_cred_js(url, (int)g_music_cred_req);
}
void audio_stop_music(void) {
    audio_stop_music_js();
    g_music_cred_req++;
    music_cred_clear();
    for (int i = 0; i < AUDIO_MUSIC_WAVE_BARS; i++) g_wave_smooth[i] = 0.0f;
}

void audio_music_waveform(float* levels, int count) {
    if (!levels || count < 1) return;
    if (count > AUDIO_MUSIC_WAVE_BARS) count = AUDIO_MUSIC_WAVE_BARS;
    float raw[AUDIO_MUSIC_WAVE_BARS];
    for (int i = 0; i < count; i++) raw[i] = 0.0f;
    audio_music_waveform_js(raw, count);
    music_wave_smooth_apply(levels, count, raw);
}

#else
#include "platform.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "../libs/miniaudio.h"

#define MAX_SFX 8
#define MAX_VOICES 8
#define AUDIO_SAMPLE_RATE 48000

typedef struct {
    short* samples;
    int sample_count;
    int sample_rate;
    int channels;
} SoundBuffer;

static SoundBuffer g_sfx[MAX_SFX];
static bool g_audio_ready = false;

typedef struct {
    int sfx_id;
    int position;
    float volume;
    bool active;
    bool looping;
} Voice;

static Voice g_voices[MAX_VOICES];
static ma_device g_device;
static bool g_device_ready = false;

static SoundBuffer g_music = {0};
static int g_music_pos = 0;
static float g_music_vol = 0.5f;
static volatile bool g_music_playing = false;
static unsigned g_music_load_gen = 0;

static short* resample_interleaved(const short* src, int src_frames, int channels,
                                   int src_rate, int dst_rate, int* out_frames) {
    *out_frames = 0;
    if (!src || src_frames <= 0 || channels <= 0 || src_rate <= 0 || dst_rate <= 0)
        return NULL;

    if (src_rate == dst_rate) {
        size_t n = (size_t)src_frames * (size_t)channels;
        short* copy = (short*)malloc(n * sizeof(short));
        if (!copy) return NULL;
        memcpy(copy, src, n * sizeof(short));
        *out_frames = src_frames;
        return copy;
    }

    int dst_frames = (int)(((double)src_frames * (double)dst_rate) / (double)src_rate + 0.5);
    if (dst_frames < 1) dst_frames = 1;
    short* dst = (short*)malloc((size_t)dst_frames * (size_t)channels * sizeof(short));
    if (!dst) return NULL;

    for (int i = 0; i < dst_frames; i++) {
        double src_pos = ((double)i * (double)src_rate) / (double)dst_rate;
        int i0 = (int)src_pos;
        int i1 = i0 + 1;
        if (i0 >= src_frames) i0 = src_frames - 1;
        if (i1 >= src_frames) i1 = src_frames - 1;
        float t = (float)(src_pos - (double)i0);
        for (int c = 0; c < channels; c++) {
            float a = (float)src[i0 * channels + c];
            float b = (float)src[i1 * channels + c];
            float s = a + (b - a) * t;
            if (s > 32767.0f) s = 32767.0f;
            if (s < -32768.0f) s = -32768.0f;
            dst[i * channels + c] = (short)s;
        }
    }
    *out_frames = dst_frames;
    return dst;
}

static void audio_data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pDevice; (void)pInput;
    float* out = (float*)pOutput;
    memset(out, 0, frameCount * 2 * sizeof(float));

    for (int v = 0; v < MAX_VOICES; v++) {
        if (!g_voices[v].active) continue;
        SoundBuffer* sb = &g_sfx[g_voices[v].sfx_id];
        if (!sb->samples || sb->sample_count <= 0) { g_voices[v].active = false; continue; }

        int pos = g_voices[v].position;
        float vol = g_voices[v].volume * g_vol_sfx * g_vol_master;
        int ch = sb->channels;
        bool looping = g_voices[v].looping;

        for (ma_uint32 f = 0; f < frameCount; f++) {
            if (pos >= sb->sample_count) {
                if (looping) {
                    pos = 0;
                } else {
                    g_voices[v].active = false;
                    break;
                }
            }
            float sample_l = (float)sb->samples[pos] / 32768.0f * vol;
            float sample_r = sample_l;
            if (ch == 2 && pos + 1 < sb->sample_count) {
                sample_r = (float)sb->samples[pos + 1] / 32768.0f * vol;
            }
            out[f * 2 + 0] += sample_l;
            out[f * 2 + 1] += sample_r;
            pos += ch;
        }
        g_voices[v].position = pos;
    }

    if (g_music_playing && g_music.samples && g_music.sample_count > 0) {
        int pos = g_music_pos;
        float vol = g_music_vol * g_vol_music * g_vol_master;
        int ch = g_music.channels;
        if (ch < 1) ch = 1;
        for (ma_uint32 f = 0; f < frameCount; f++) {
            if (pos >= g_music.sample_count) pos = 0;
            float sample_l = (float)g_music.samples[pos] / 32768.0f * vol;
            float sample_r = sample_l;
            if (ch == 2 && pos + 1 < g_music.sample_count) {
                sample_r = (float)g_music.samples[pos + 1] / 32768.0f * vol;
            }
            out[f * 2 + 0] += sample_l;
            out[f * 2 + 1] += sample_r;
            pos += ch;
        }
        g_music_pos = pos;
    }
}

static const char* sfx_paths[MAX_SFX] = {
    "assets/sounds/jump.ogg",
    "assets/sounds/land.ogg",
    "assets/sounds/footsteps.ogg",
    "assets/sounds/explosion.ogg",
    "assets/sounds/chat.ogg",
    "assets/sounds/rocket.ogg",
    "assets/sounds/tick.ogg",
    "assets/sounds/dead.ogg",
};

#ifdef __ANDROID__
typedef struct {
    int channels;
    int sample_rate;
    short* samples;
    int frames;
} SfxDecodeResult;

static void sfx_decode_cb(const char* path, const uint8_t* data, size_t len, void* user) {
    (void)path;
    SfxDecodeResult* out = (SfxDecodeResult*)user;
    out->frames = 0;
    out->samples = NULL;
    if (!data || len == 0) return;
    out->frames = stb_vorbis_decode_memory((unsigned char*)data, (int)len,
                                           &out->channels, &out->sample_rate, &out->samples);
}
#endif

static void play_sfx(int sfx_id, float volume) {
    if (!g_audio_ready || !g_device_ready || sfx_id < 0 || sfx_id >= MAX_SFX) return;
    if (!g_sfx[sfx_id].samples || volume < 0.01f) return;

    for (int v = 0; v < MAX_VOICES; v++) {
        if (!g_voices[v].active) {
            g_voices[v].sfx_id = sfx_id;
            g_voices[v].position = 0;
            g_voices[v].volume = volume;
            g_voices[v].active = true;
            g_voices[v].looping = false;
            return;
        }
    }
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!g_voices[v].looping) {
            g_voices[v].sfx_id = sfx_id;
            g_voices[v].position = 0;
            g_voices[v].volume = volume;
            g_voices[v].active = true;
            g_voices[v].looping = false;
            return;
        }
    }
}

void audio_init(void) {
    memset(g_voices, 0, sizeof(g_voices));

    int loaded = 0;
    for (int i = 0; i < MAX_SFX; i++) {
        int channels = 0, sample_rate = 0;
        short* samples = NULL;
        int frames = 0;
#ifdef __ANDROID__
        SfxDecodeResult dec = {0};
        platform_load_file(sfx_paths[i], sfx_decode_cb, &dec);
        frames = dec.frames;
        samples = dec.samples;
        channels = dec.channels;
        sample_rate = dec.sample_rate;
#else
        frames = stb_vorbis_decode_filename(sfx_paths[i], &channels, &sample_rate, &samples);
#endif
        if (frames > 0 && samples) {
            int out_frames = 0;
            short* resampled = resample_interleaved(samples, frames, channels,
                                                    sample_rate, AUDIO_SAMPLE_RATE, &out_frames);
            free(samples);
            if (resampled && out_frames > 0) {
                g_sfx[i].samples = resampled;
                g_sfx[i].sample_count = out_frames * channels;
                g_sfx[i].sample_rate = AUDIO_SAMPLE_RATE;
                g_sfx[i].channels = channels;
                loaded++;
            } else {
                PW_ERR(ERR_AUDIO, "Resampling failed for %s\n", sfx_paths[i]);
            }
        } else {
            PW_ERR(ERR_AUDIO, "Decoding failed for %s\n", sfx_paths[i]);
        }
    }
    g_audio_ready = (loaded > 0);

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = AUDIO_SAMPLE_RATE;
    config.dataCallback = audio_data_callback;
#ifdef __ANDROID__
    config.periodSizeInFrames = 1024;
    config.periods = 4;
#endif

    if (ma_device_init(NULL, &config, &g_device) == MA_SUCCESS) {
        if (ma_device_start(&g_device) == MA_SUCCESS) {
            g_device_ready = true;
#ifdef __ANDROID__
            ma_device_set_master_volume(&g_device, 1.0f);
#endif
        } else {
            ma_device_uninit(&g_device);
            PW_ERR(ERR_AUDIO, "Failed to start device\n");
        }
    } else {
        PW_ERR(ERR_AUDIO, "Failed to init device\n");
    }
}

void audio_play(int sfx_id) { play_sfx(sfx_id, 1.0f); }
void audio_play_at(int sfx_id, float x, float y, float z) {
    play_sfx(sfx_id, calc_volume(x, y, z));
}

void audio_start_loop(int sfx_id, float volume) {
    if (!g_audio_ready || !g_device_ready || sfx_id < 0 || sfx_id >= MAX_SFX) return;
    if (!g_sfx[sfx_id].samples || volume < 0.01f) return;

    for (int v = 0; v < MAX_VOICES; v++) {
        if (g_voices[v].active && g_voices[v].looping && g_voices[v].sfx_id == sfx_id) {
            g_voices[v].volume = volume;
            return;
        }
    }

    for (int v = 0; v < MAX_VOICES; v++) {
        if (!g_voices[v].active) {
            g_voices[v].sfx_id = sfx_id;
            g_voices[v].position = 0;
            g_voices[v].volume = volume;
            g_voices[v].active = true;
            g_voices[v].looping = true;
            return;
        }
    }
}

void audio_stop_loop(int sfx_id) {
    if (sfx_id < 0 || sfx_id >= MAX_SFX) return;
    for (int v = 0; v < MAX_VOICES; v++) {
        if (g_voices[v].active && g_voices[v].looping && g_voices[v].sfx_id == sfx_id) {
            g_voices[v].active = false;
            g_voices[v].looping = false;
        }
    }
}

void audio_stop_music(void) {
    g_music_playing = false;
    g_music_pos = 0;
    if (g_music.samples) {
        free(g_music.samples);
        g_music.samples = NULL;
    }
    g_music.sample_count = 0;
    g_music.channels = 0;
    g_music_cred_req++;
    g_music_load_gen++;
    music_cred_clear();
    for (int i = 0; i < AUDIO_MUSIC_WAVE_BARS; i++) g_wave_smooth[i] = 0.0f;
}

typedef struct {
    unsigned gen;
    float volume;
    char url[512];
} MusicLoadCtx;

typedef struct {
    unsigned gen;
} MusicCredLoadCtx;

static void music_start_from_pcm(short* resampled, int out_frames, int channels, float volume,
                                 const char* url_for_log) {
    if (!resampled || out_frames <= 0) return;
    if (g_music.samples) {
        free(g_music.samples);
        g_music.samples = NULL;
    }
    g_music.samples = resampled;
    g_music.sample_count = out_frames * channels;
    g_music.sample_rate = AUDIO_SAMPLE_RATE;
    g_music.channels = channels;
    g_music_pos = 0;
    g_music_vol = volume;
    g_music_playing = true;
    PW_LOG("[Audio] playMusic: looping %s (vol=%.2f, %d frames)\n",
           url_for_log ? url_for_log : "?", volume, out_frames);
}

static void music_cred_on_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    MusicCredLoadCtx* ctx = (MusicCredLoadCtx*)user;
    if (!ctx) return;
    unsigned gen = ctx->gen;
    free(ctx);
    (void)path;
    if (gen != g_music_cred_req) return;
    if (!data || len == 0) {
        music_cred_clear();
        return;
    }
    char* text = (char*)malloc(len + 1);
    if (!text) {
        music_cred_clear();
        return;
    }
    memcpy(text, data, len);
    text[len] = '\0';
    music_cred_apply_text(text);
    free(text);
}

static void music_cred_load_for_url_async(const char* url) {
    char cred_url[512];
    if (!music_cred_url_from_music(url, cred_url, sizeof(cred_url))) {
        music_cred_clear();
        return;
    }
    MusicCredLoadCtx* ctx = (MusicCredLoadCtx*)malloc(sizeof(MusicCredLoadCtx));
    if (!ctx) {
        music_cred_clear();
        return;
    }
    ctx->gen = g_music_cred_req;
    if (strncmp(cred_url, "http://", 7) == 0 || strncmp(cred_url, "https://", 8) == 0) {
        platform_load_file(cred_url, music_cred_on_loaded, ctx);
        return;
    }
    FILE* f = fopen(cred_url, "rb");
    if (!f) {
        free(ctx);
        music_cred_clear();
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* data = NULL;
    size_t data_len = 0;
    if (sz > 0 && sz < 4096) {
        data = (unsigned char*)malloc((size_t)sz + 1);
        if (data) {
            data_len = fread(data, 1, (size_t)sz, f);
            data[data_len] = '\0';
        }
    }
    fclose(f);
    music_cred_on_loaded(cred_url, data, data_len, ctx);
    free(data);
}

static int music_decode_memory(const uint8_t* data, size_t len,
                               int* out_channels, int* out_rate, short** out_samples) {
    if (out_channels) *out_channels = 0;
    if (out_rate) *out_rate = 0;
    if (out_samples) *out_samples = NULL;
    if (!data || len < 4 || !out_samples) return 0;

    ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, 0, 0);
    ma_decoder dec;
    if (ma_decoder_init_memory(data, len, &cfg, &dec) == MA_SUCCESS) {
        int ch = (int)dec.outputChannels;
        int rate = (int)dec.outputSampleRate;
        if (ch < 1) ch = 1;
        if (rate < 1) rate = AUDIO_SAMPLE_RATE;

        ma_uint64 frame_count = 0;
        ma_bool32 have_len = (ma_decoder_get_length_in_pcm_frames(&dec, &frame_count) == MA_SUCCESS
                              && frame_count > 0);

        if (have_len) {
            short* buf = (short*)malloc((size_t)frame_count * (size_t)ch * sizeof(short));
            if (!buf) {
                ma_decoder_uninit(&dec);
            } else {
                ma_uint64 frames_read = 0;
                ma_result rr = ma_decoder_read_pcm_frames(&dec, buf, frame_count, &frames_read);
                ma_decoder_uninit(&dec);
                if (rr == MA_SUCCESS && frames_read > 0) {
                    if (out_channels) *out_channels = ch;
                    if (out_rate) *out_rate = rate;
                    *out_samples = buf;
                    return (int)frames_read;
                }
                free(buf);
            }
        } else {
            ma_uint64 cap = 48000ull * 120ull;
            short* buf = (short*)malloc((size_t)cap * (size_t)ch * sizeof(short));
            ma_uint64 total = 0;
            if (buf) {
                for (;;) {
                    if (total + 4096 > cap) {
                        cap *= 2;
                        short* nb = (short*)realloc(buf, (size_t)cap * (size_t)ch * sizeof(short));
                        if (!nb) { free(buf); buf = NULL; break; }
                        buf = nb;
                    }
                    ma_uint64 got = 0;
                    ma_result rr = ma_decoder_read_pcm_frames(&dec, buf + total * (ma_uint64)ch, 4096, &got);
                    if (got == 0) break;
                    total += got;
                    if (rr != MA_SUCCESS) break;
                }
            }
            ma_decoder_uninit(&dec);
            if (buf && total > 0) {
                if (out_channels) *out_channels = ch;
                if (out_rate) *out_rate = rate;
                *out_samples = buf;
                return (int)total;
            }
            free(buf);
        }
    }

    {
        int channels = 0, sample_rate = 0;
        short* samples = NULL;
        int frames = stb_vorbis_decode_memory((unsigned char*)data, (int)len,
                                              &channels, &sample_rate, &samples);
        if (frames <= 0 || !samples) return 0;
        if (out_channels) *out_channels = channels;
        if (out_rate) *out_rate = sample_rate;
        *out_samples = samples;
        return frames;
    }
}

static void music_http_on_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    MusicLoadCtx* ctx = (MusicLoadCtx*)user;
    if (!ctx) return;
    unsigned gen = ctx->gen;
    float volume = ctx->volume;
    char url_copy[512];
    snprintf(url_copy, sizeof(url_copy), "%s", ctx->url);
    free(ctx);
    if (gen != g_music_load_gen) return;
    if (!data || len == 0) {
        PW_ERR(ERR_AUDIO, "Failed to download %s\n", path ? path : url_copy);
        return;
    }
    int channels = 0, sample_rate = 0;
    short* samples = NULL;
    int frames = music_decode_memory(data, len, &channels, &sample_rate, &samples);
    if (frames <= 0 || !samples) {
        PW_ERR(ERR_AUDIO, "Failed to decode %s\n", url_copy);
        return;
    }
    int out_frames = 0;
    short* resampled = resample_interleaved(samples, frames, channels,
                                            sample_rate, AUDIO_SAMPLE_RATE, &out_frames);
    free(samples);
    if (!resampled || out_frames <= 0) {
        PW_ERR(ERR_AUDIO, "Failed to resample %s\n", url_copy);
        return;
    }
    if (gen != g_music_load_gen) {
        free(resampled);
        return;
    }
    music_start_from_pcm(resampled, out_frames, channels, volume, url_copy);
    g_music_cred_req++;
    music_cred_load_for_url_async(url_copy);
}

void audio_play_music(const char* url, float volume) {
    if (!url || !url[0]) return;
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
#ifdef __ANDROID__
    volume = 1.0f;
#endif

    audio_stop_music();

    if (!g_audio_ready || !g_device_ready) {
        PW_ERR(ERR_AUDIO, "Device not ready!\n");
        return;
    }

    if (g_device_ready) {
        ma_device_start(&g_device);
#ifdef __ANDROID__
        ma_device_set_master_volume(&g_device, 1.0f);
#endif
    }

    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
        MusicLoadCtx* ctx = (MusicLoadCtx*)malloc(sizeof(MusicLoadCtx));
        if (!ctx) {
            PW_ERR(ERR_AUDIO, "Out of memory for music load\n");
            return;
        }
        ctx->gen = g_music_load_gen;
        ctx->volume = volume;
        snprintf(ctx->url, sizeof(ctx->url), "%s", url);
        platform_load_file(url, music_http_on_loaded, ctx);
        return;
    }

    FILE* f = fopen(url, "rb");
    if (!f) {
        PW_ERR(ERR_AUDIO, "Failed to open %s\n", url);
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 64 * 1024 * 1024) {
        fclose(f);
        PW_ERR(ERR_AUDIO, "Invalid music file size for %s\n", url);
        return;
    }
    uint8_t* data = (uint8_t*)malloc((size_t)sz);
    if (!data) {
        fclose(f);
        PW_ERR(ERR_AUDIO, "Out of memory for %s\n", url);
        return;
    }
    size_t nread = fread(data, 1, (size_t)sz, f);
    fclose(f);
    int channels = 0, sample_rate = 0;
    short* samples = NULL;
    int frames = music_decode_memory(data, nread, &channels, &sample_rate, &samples);
    free(data);
    if (frames <= 0 || !samples) {
        PW_ERR(ERR_AUDIO, "Failed to decode %s\n", url);
        return;
    }
    int out_frames = 0;
    short* resampled = resample_interleaved(samples, frames, channels,
                                            sample_rate, AUDIO_SAMPLE_RATE, &out_frames);
    free(samples);
    if (!resampled || out_frames <= 0) {
        PW_ERR(ERR_AUDIO, "Failed to resample %s\n", url);
        return;
    }
    music_start_from_pcm(resampled, out_frames, channels, volume, url);
    g_music_cred_req++;
    music_cred_load_for_url_async(url);
}

void audio_music_waveform(float* levels, int count) {
    if (!levels || count < 1) return;
    if (count > AUDIO_MUSIC_WAVE_BARS) count = AUDIO_MUSIC_WAVE_BARS;
    float raw[AUDIO_MUSIC_WAVE_BARS];
    for (int i = 0; i < count; i++) raw[i] = 0.0f;

    if (g_music_playing && g_music.samples && g_music.sample_count > 0) {
        int ch = g_music.channels;
        if (ch < 1) ch = 1;
        int pos = g_music_pos;
        if (pos < 0) pos = 0;
        if (pos >= g_music.sample_count) pos = 0;

        const int win_frames = 1024;
        int frames_per_bar = win_frames / count;
        if (frames_per_bar < 16) frames_per_bar = 16;

        for (int i = 0; i < count; i++) {
            double acc = 0.0;
            int n = 0;
            int start = pos + i * frames_per_bar * ch;
            for (int f = 0; f < frames_per_bar; f++) {
                int idx = start + f * ch;
                while (idx >= g_music.sample_count) idx -= g_music.sample_count;
                if (idx < 0) idx = 0;
                float s = (float)g_music.samples[idx] / 32768.0f;
                if (ch == 2 && idx + 1 < g_music.sample_count) {
                    float sr = (float)g_music.samples[idx + 1] / 32768.0f;
                    s = 0.5f * (s + sr);
                }
                acc += (double)(s * s);
                n++;
            }
            float rms = (n > 0) ? sqrtf((float)(acc / (double)n)) : 0.0f;
            raw[i] = rms * 3.2f;
            if (raw[i] > 1.0f) raw[i] = 1.0f;
        }
    }

    music_wave_smooth_apply(levels, count, raw);
}

#ifdef __ANDROID__
#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#endif

#endif
