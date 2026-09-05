/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: shader.c                                                                            |
|   Purpose: shaders + uniform cache                                                          |
\*-------------------------------------------------------------------------------------------*/

#include "shader.h"
#include "platform.h"
#include "log.h"
#include "pw_gles.h"

#if PW_USE_GLES
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif

const char* shader_embedded_vert(const char* name);
const char* shader_embedded_frag(const char* name);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif
#ifndef __EMSCRIPTEN__
#include <dirent.h>
#endif

#ifndef GL_PROGRAM_BINARY_RETRIEVABLE_HINT
#define GL_PROGRAM_BINARY_RETRIEVABLE_HINT 0x8257
#endif
#ifndef GL_PROGRAM_BINARY_LENGTH
#define GL_PROGRAM_BINARY_LENGTH 0x8741
#endif
#ifndef GL_NUM_PROGRAM_BINARY_FORMATS
#define GL_NUM_PROGRAM_BINARY_FORMATS 0x87FE
#endif

#define PWPB_MAGIC 0x42505750u

static int shader_exe_dir(char* out, size_t n);
static FILE* shader_fopen_rel(const char* rel, const char* mode);

static uint32_t fnv1a(uint32_t h, const void* data, size_t n) {
    const unsigned char* p = (const unsigned char*)data;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static bool compile_shader_stage(unsigned int type, const char* source, unsigned int* out) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[1024];
        glGetShaderInfoLog(shader, sizeof(info_log), NULL, info_log);
        PW_ERR(ERR_SHADER, "Shader compile failed: %s\n", info_log);
        glDeleteShader(shader);
        return false;
    }

    *out = shader;
    return true;
}

static void shader_cache_uniforms(Shader* s) {
    unsigned int program = s->program;
    s->u_model       = glGetUniformLocation(program, "u_model");
    s->u_view        = glGetUniformLocation(program, "u_view");
    s->u_projection  = glGetUniformLocation(program, "u_projection");
    s->u_color       = glGetUniformLocation(program, "u_color");
    s->u_light_dir   = glGetUniformLocation(program, "u_light_dir");
    s->u_light_color = glGetUniformLocation(program, "u_light_color");
    s->u_texture     = glGetUniformLocation(program, "u_texture");
    s->u_has_texture = glGetUniformLocation(program, "u_has_texture");
    s->u_uv_rect     = glGetUniformLocation(program, "u_uv_rect");
    s->u_face_mode   = glGetUniformLocation(program, "u_face_mode");
    s->u_face_surf   = glGetUniformLocation(program, "u_face_surf[0]");
    if (s->u_face_surf < 0)
        s->u_face_surf = glGetUniformLocation(program, "u_face_surf");
    s->u_part_shape  = glGetUniformLocation(program, "u_part_shape");
    s->u_part_size   = glGetUniformLocation(program, "u_part_size");
    s->u_inlet_map   = glGetUniformLocation(program, "u_inlet_map");
    s->u_mat_albedo  = glGetUniformLocation(program, "u_mat_albedo");
    s->u_mat_normal  = glGetUniformLocation(program, "u_mat_normal");
    s->u_mat_specular = glGetUniformLocation(program, "u_mat_specular");
    s->u_part_material = glGetUniformLocation(program, "u_part_material");
    s->u_camera_pos  = glGetUniformLocation(program, "u_camera_pos");
    s->u_fog_color   = glGetUniformLocation(program, "u_fog_color");
    s->u_fog_start   = glGetUniformLocation(program, "u_fog_start");
    s->u_fog_end     = glGetUniformLocation(program, "u_fog_end");
    s->u_shadow_map  = glGetUniformLocation(program, "u_shadow_map");
    s->u_shadow_map_near = glGetUniformLocation(program, "u_shadow_map_near");
    s->u_shadow_group = glGetUniformLocation(program, "u_shadow_group");
    s->u_shadow_group_near = glGetUniformLocation(program, "u_shadow_group_near");
    s->u_light_space = glGetUniformLocation(program, "u_light_space");
    s->u_light_space_near = glGetUniformLocation(program, "u_light_space_near");
    s->u_shadow_enabled = glGetUniformLocation(program, "u_shadow_enabled");
    s->u_shadow_soft = glGetUniformLocation(program, "u_shadow_soft");
    s->u_shadow_cascades = glGetUniformLocation(program, "u_shadow_cascades");
    s->u_shadow_id = glGetUniformLocation(program, "u_shadow_id");
    s->u_shadow_face_ids = glGetUniformLocation(program, "u_shadow_face_ids");
    s->u_shadow_id_packed = glGetUniformLocation(program, "u_shadow_id_packed");
    s->u_shadow_depth_bias = glGetUniformLocation(program, "u_shadow_depth_bias");
    s->u_shadow_exp = glGetUniformLocation(program, "u_shadow_exp");
    s->u_shadow_range = glGetUniformLocation(program, "u_shadow_range");
    s->u_fog_enabled = glGetUniformLocation(program, "u_fog_enabled");
    s->u_normal_map = glGetUniformLocation(program, "u_normal_map");
    s->u_glow = glGetUniformLocation(program, "u_glow");
    s->u_alpha = glGetUniformLocation(program, "u_alpha");
    s->u_contact_shade = glGetUniformLocation(program, "u_contact_shade");
    s->u_glow_light_count = glGetUniformLocation(program, "u_glow_light_count");
    s->u_glow_light_pos = glGetUniformLocation(program, "u_glow_light_pos[0]");
    if (s->u_glow_light_pos < 0)
        s->u_glow_light_pos = glGetUniformLocation(program, "u_glow_light_pos");
    s->u_glow_light_color = glGetUniformLocation(program, "u_glow_light_color[0]");
    if (s->u_glow_light_color < 0)
        s->u_glow_light_color = glGetUniformLocation(program, "u_glow_light_color");
    s->u_glow_light_range = glGetUniformLocation(program, "u_glow_light_range[0]");
    if (s->u_glow_light_range < 0)
        s->u_glow_light_range = glGetUniformLocation(program, "u_glow_light_range");
    s->u_glow_light_entity = glGetUniformLocation(program, "u_glow_light_entity[0]");
    if (s->u_glow_light_entity < 0)
        s->u_glow_light_entity = glGetUniformLocation(program, "u_glow_light_entity");
    s->u_glow_shadow_count = glGetUniformLocation(program, "u_glow_shadow_count");
    s->u_glow_shadow_map0 = glGetUniformLocation(program, "u_glow_shadow_map0");
    s->u_glow_shadow_map1 = glGetUniformLocation(program, "u_glow_shadow_map1");
    s->u_glow_ls = glGetUniformLocation(program, "u_glow_ls[0]");
    if (s->u_glow_ls < 0)
        s->u_glow_ls = glGetUniformLocation(program, "u_glow_ls");
    s->u_voxel_map = glGetUniformLocation(program, "u_voxel_map");
    s->u_voxel_enabled = glGetUniformLocation(program, "u_voxel_enabled");
    s->u_voxel_origin = glGetUniformLocation(program, "u_voxel_origin");
    s->u_voxel_size = glGetUniformLocation(program, "u_voxel_size");
    s->u_voxel_dim = glGetUniformLocation(program, "u_voxel_dim");
    s->u_voxel_range = glGetUniformLocation(program, "u_voxel_range");
}

#if PW_USE_GLES
#define pw_glGetProgramBinary glGetProgramBinary
#define pw_glProgramBinary glProgramBinary
#define pw_glProgramParameteri glProgramParameteri
static void shader_bind_binary_procs(void) {}
#else
static PFNGLGETPROGRAMBINARYPROC pw_glGetProgramBinary;
static PFNGLPROGRAMBINARYPROC pw_glProgramBinary;
static PFNGLPROGRAMPARAMETERIPROC pw_glProgramParameteri;

static void shader_bind_binary_procs(void) {
    if (pw_glProgramBinary) return;
    if (glGetProgramBinary) pw_glGetProgramBinary = glGetProgramBinary;
    if (glProgramBinary) pw_glProgramBinary = glProgramBinary;
    if (glProgramParameteri) pw_glProgramParameteri = glProgramParameteri;
    if (glfwGetCurrentContext()) {
        if (!pw_glGetProgramBinary)
            pw_glGetProgramBinary = (PFNGLGETPROGRAMBINARYPROC)glfwGetProcAddress("glGetProgramBinary");
        if (!pw_glProgramBinary)
            pw_glProgramBinary = (PFNGLPROGRAMBINARYPROC)glfwGetProcAddress("glProgramBinary");
        if (!pw_glProgramParameteri)
            pw_glProgramParameteri = (PFNGLPROGRAMPARAMETERIPROC)glfwGetProcAddress("glProgramParameteri");
    }
}
#endif

static void shader_hint_binary(unsigned int program) {
    shader_bind_binary_procs();
    if (pw_glProgramParameteri)
        pw_glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
}

static bool shader_link_program(unsigned int vert, unsigned int frag, unsigned int* out) {
    unsigned int program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    shader_hint_binary(program);
    glLinkProgram(program);
    glDeleteShader(vert);
    glDeleteShader(frag);

    int success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char info_log[1024];
        glGetProgramInfoLog(program, sizeof(info_log), NULL, info_log);
        PW_ERR(ERR_SHADER, "Shader link failed: %s\n", info_log);
        glDeleteProgram(program);
        return false;
    }
    *out = program;
    return true;
}

static bool shader_binaries_supported(void) {
#ifdef __EMSCRIPTEN__
    return false;
#else
    shader_bind_binary_procs();
    if (!pw_glGetProgramBinary || !pw_glProgramBinary) return false;
    GLint n = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &n);
    return n > 0;
#endif
}

static uint32_t shader_source_crc(const char* vert, const char* frag) {
    uint32_t h = 2166136261u;
    h = fnv1a(h, vert, strlen(vert));
    h = fnv1a(h, frag, strlen(frag));
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* version = (const char*)glGetString(GL_VERSION);
    if (vendor) h = fnv1a(h, vendor, strlen(vendor));
    if (renderer) h = fnv1a(h, renderer, strlen(renderer));
    if (version) h = fnv1a(h, version, strlen(version));
    return h;
}

static void shader_mkdir(const char* path) {
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

static FILE* shader_open_cache(const char* name, uint32_t crc, const char* mode) {
    char rel[256];
    snprintf(rel, sizeof(rel), "assets/shaders/cache/%s.%08x.bin", name, crc);
    if (mode && mode[0] == 'r') {
        FILE* f = shader_fopen_rel(rel, mode);
        if (f) return f;
        char ud[512];
        char udrel[128];
        snprintf(udrel, sizeof(udrel), "shaders/cache/%s.%08x.bin", name, crc);
        if (platform_userdata_path(udrel, ud, sizeof(ud)))
            return fopen(ud, mode);
        return NULL;
    }
    shader_mkdir("assets/shaders/cache");
    shader_mkdir("../assets/shaders/cache");
    FILE* f = fopen(rel, mode);
    if (f) return f;
    char path[512];
    snprintf(path, sizeof(path), "../%s", rel);
    f = fopen(path, mode);
    if (f) return f;
    char ud[512];
    char udrel[128];
    snprintf(udrel, sizeof(udrel), "shaders/cache/%s.%08x.bin", name, crc);
    if (platform_userdata_path(udrel, ud, sizeof(ud)))
        return fopen(ud, mode);
    return NULL;
}

static FILE* shader_open_generic(const char* name, const char* mode) {
    char rel[256];
    snprintf(rel, sizeof(rel), "assets/shaders/cache/%s.bin", name);
    if (mode && mode[0] == 'r') {
        FILE* f = shader_fopen_rel(rel, mode);
        if (f) return f;
        char ud[512];
        char udrel[128];
        snprintf(udrel, sizeof(udrel), "shaders/cache/%s.bin", name);
        if (platform_userdata_path(udrel, ud, sizeof(ud)))
            return fopen(ud, mode);
        return NULL;
    }
    shader_mkdir("assets/shaders/cache");
    shader_mkdir("../assets/shaders/cache");
    FILE* f = fopen(rel, mode);
    if (f) return f;
    char path[512];
    snprintf(path, sizeof(path), "../%s", rel);
    f = fopen(path, mode);
    if (f) return f;
    char ud[512];
    char udrel[128];
    snprintf(udrel, sizeof(udrel), "shaders/cache/%s.bin", name);
    if (platform_userdata_path(udrel, ud, sizeof(ud)))
        return fopen(ud, mode);
    return NULL;
}

static bool shader_try_load_bin_file(Shader* s, FILE* f, const char* name) {
    if (!f) return false;
    uint32_t magic = 0, format = 0, file_crc = 0, size = 0;
    if (fread(&magic, 4, 1, f) != 1 || magic != PWPB_MAGIC ||
        fread(&format, 4, 1, f) != 1 ||
        fread(&file_crc, 4, 1, f) != 1 ||
        fread(&size, 4, 1, f) != 1 || size == 0 || size > 16u * 1024u * 1024u) {
        fclose(f);
        return false;
    }
    unsigned char* bin = (unsigned char*)malloc(size);
    if (!bin || fread(bin, 1, size, f) != size) {
        free(bin);
        fclose(f);
        return false;
    }
    fclose(f);

    unsigned int program = glCreateProgram();
    pw_glProgramBinary(program, format, bin, (GLsizei)size);
    free(bin);
    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glDeleteProgram(program);
        return false;
    }
    s->program = program;
    shader_cache_uniforms(s);
    PW_LOG("[Shader] Loaded program binary %s\n", name);
    return true;
}

static bool shader_try_load_binary(Shader* s, const char* name, uint32_t crc) {
    if (!shader_binaries_supported()) return false;
    FILE* f = shader_open_cache(name, crc, "rb");
    if (!f) return false;
    return shader_try_load_bin_file(s, f, name);
}

static bool shader_try_load_binary_named(Shader* s, const char* name) {
    if (!shader_binaries_supported() || !name) return false;
    FILE* f = shader_open_generic(name, "rb");
    if (f && shader_try_load_bin_file(s, f, name)) return true;
#ifndef __EMSCRIPTEN__
    const char* dirs[6];
    char exe_a[768], exe_b[768], exe[512];
    int nd = 0;
    dirs[nd++] = "assets/shaders/cache";
    dirs[nd++] = "../assets/shaders/cache";
    if (shader_exe_dir(exe, sizeof(exe))) {
        snprintf(exe_a, sizeof(exe_a), "%s/assets/shaders/cache", exe);
        snprintf(exe_b, sizeof(exe_b), "%s/../assets/shaders/cache", exe);
        dirs[nd++] = exe_a;
        dirs[nd++] = exe_b;
    }
    char prefix[128];
    snprintf(prefix, sizeof(prefix), "%s.", name);
    size_t plen = strlen(prefix);
    for (int di = 0; di < nd; di++) {
        DIR* d = opendir(dirs[di]);
        if (!d) continue;
        struct dirent* ent;
        while ((ent = readdir(d)) != NULL) {
            const char* n = ent->d_name;
            size_t nl = strlen(n);
            if (nl < plen + 4) continue;
            if (strncmp(n, prefix, plen) != 0) continue;
            if (strcmp(n + nl - 4, ".bin") != 0) continue;
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", dirs[di], n);
            FILE* sf = fopen(path, "rb");
            if (sf && shader_try_load_bin_file(s, sf, name)) {
                closedir(d);
                return true;
            }
        }
        closedir(d);
    }
#endif
    return false;
}

static void shader_save_binary(unsigned int program, const char* name, uint32_t crc) {
    if (!shader_binaries_supported() || !name) return;
    GLint len = 0;
    glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &len);
    if (len <= 0) return;
    unsigned char* bin = (unsigned char*)malloc((size_t)len);
    if (!bin) return;
    GLenum format = 0;
    GLsizei out_len = 0;
    pw_glGetProgramBinary(program, len, &out_len, &format, bin);
    if (out_len <= 0) { free(bin); return; }
    FILE* f = shader_open_cache(name, crc, "wb");
    if (f) {
        uint32_t magic = PWPB_MAGIC;
        uint32_t fmt = (uint32_t)format;
        uint32_t sz = (uint32_t)out_len;
        fwrite(&magic, 4, 1, f);
        fwrite(&fmt, 4, 1, f);
        fwrite(&crc, 4, 1, f);
        fwrite(&sz, 4, 1, f);
        fwrite(bin, 1, (size_t)out_len, f);
        fclose(f);
        PW_LOG("[Shader] Saved program binary %s (%d bytes)\n", name, (int)out_len);
    }
    FILE* g = shader_open_generic(name, "wb");
    if (g) {
        uint32_t magic = PWPB_MAGIC;
        uint32_t fmt = (uint32_t)format;
        uint32_t sz = (uint32_t)out_len;
        fwrite(&magic, 4, 1, g);
        fwrite(&fmt, 4, 1, g);
        fwrite(&crc, 4, 1, g);
        fwrite(&sz, 4, 1, g);
        fwrite(bin, 1, (size_t)out_len, g);
        fclose(g);
    }
    free(bin);
}

bool shader_compile(Shader* s, const char* vert_src, const char* frag_src) {
    s->program = 0;
    unsigned int vert, frag;
    if (!compile_shader_stage(GL_VERTEX_SHADER, vert_src, &vert)) {
        return false;
    }
    if (!compile_shader_stage(GL_FRAGMENT_SHADER, frag_src, &frag)) {
        glDeleteShader(vert);
        return false;
    }
    unsigned int program;
    if (!shader_link_program(vert, frag, &program)) {
        return false;
    }
    s->program = program;
    shader_cache_uniforms(s);
    return true;
}

static char* shader_join2(const char* a, const char* b) {
    size_t na = strlen(a), nb = strlen(b);
    char* out = (char*)malloc(na + nb + 1);
    if (!out) return NULL;
    memcpy(out, a, na);
    memcpy(out + na, b, nb);
    out[na + nb] = '\0';
    return out;
}

static int shader_exe_dir(char* out, size_t n) {
    if (!out || n < 4) return 0;
    out[0] = '\0';
#ifdef _WIN32
    DWORD k = GetModuleFileNameA(NULL, out, (DWORD)n);
    if (k == 0 || k >= n) return 0;
    char* last = strrchr(out, '\\');
    if (!last) last = strrchr(out, '/');
    if (!last) return 0;
    *last = '\0';
    return 1;
#else
    ssize_t k = readlink("/proc/self/exe", out, n - 1);
    if (k <= 0) return 0;
    out[k] = '\0';
    char* last = strrchr(out, '/');
    if (!last) return 0;
    *last = '\0';
    return 1;
#endif
}

static FILE* shader_fopen_rel(const char* rel, const char* mode) {
    if (!rel || !rel[0]) return NULL;
    FILE* f = fopen(rel, mode);
    if (f) return f;
    char buf[768];
    snprintf(buf, sizeof(buf), "../%s", rel);
    f = fopen(buf, mode);
    if (f) return f;
    char exe[512];
    if (shader_exe_dir(exe, sizeof(exe))) {
        snprintf(buf, sizeof(buf), "%s/%s", exe, rel);
        f = fopen(buf, mode);
        if (f) return f;
        snprintf(buf, sizeof(buf), "%s/../%s", exe, rel);
        f = fopen(buf, mode);
        if (f) return f;
    }
    return NULL;
}

static FILE* shader_fopen_asset(const char* rel) {
    return shader_fopen_rel(rel, "rb");
}

static char* shader_slurp(FILE* f) {
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char* data = (char*)malloc((size_t)size + 1);
    if (!data) { fclose(f); return NULL; }
    size_t n = fread(data, 1, (size_t)size, f);
    fclose(f);
    data[n] = '\0';
    return data;
}

static char* shader_read_asset_file(const char* rel) {
    char* data = platform_read_text_file(rel, NULL);
    if (data) return data;
    return shader_slurp(shader_fopen_asset(rel));
}

bool shader_compile_asset(Shader* s, const char* name) {
    s->program = 0;
    shader_bind_binary_procs();

#if PW_USE_GLES
    const char* header =
        "#version 300 es\n"
        "precision highp float;\n"
        "precision highp sampler3D;\n"
        "#define SHADOW_SAMPLER highp sampler2D\n"
        "#define VOXEL_SAMPLER highp sampler3D\n";
#else
    const char* header =
        "#version 330 core\n"
        "#define PW_DESKTOP 1\n"
        "#define SHADOW_SAMPLER sampler2D\n"
        "#define VOXEL_SAMPLER sampler3D\n";
#endif

    char* heap_v = NULL;
    char* heap_f = NULL;
    const char* body_v = NULL;
    const char* body_f = NULL;
    if (name) {
        char path[256];
        snprintf(path, sizeof(path), "assets/shaders/%s.vert", name);
        heap_v = shader_read_asset_file(path);
        if (!heap_v) {
            snprintf(path, sizeof(path), "assets/shaders/NO_PROD1/%s.vert", name);
            heap_v = shader_read_asset_file(path);
        }
        snprintf(path, sizeof(path), "assets/shaders/%s.frag", name);
        heap_f = shader_read_asset_file(path);
        if (!heap_f) {
            snprintf(path, sizeof(path), "assets/shaders/NO_PROD1/%s.frag", name);
            heap_f = shader_read_asset_file(path);
        }
        body_v = heap_v ? heap_v : shader_embedded_vert(name);
        body_f = heap_f ? heap_f : shader_embedded_frag(name);
    }

    if (!body_v || !body_f) {
        free(heap_v);
        free(heap_f);
        if (name && shader_try_load_binary_named(s, name))
            return true;
        PW_ERR(ERR_SHADER, "Missing shader '%s' (no GLSL, no embed, no program binary)\n",
               name ? name : "?");
        return false;
    }

    char* vert = shader_join2(header, body_v);
    char* frag = shader_join2(header, body_f);
    free(heap_v);
    free(heap_f);
    if (!vert || !frag) {
        free(vert);
        free(frag);
        return false;
    }

    uint32_t crc = shader_source_crc(vert, frag);
    if (name && shader_try_load_binary(s, name, crc)) {
        free(vert);
        free(frag);
        return true;
    }

    unsigned int vs = 0, fs = 0;
    if (!compile_shader_stage(GL_VERTEX_SHADER, vert, &vs) ||
        !compile_shader_stage(GL_FRAGMENT_SHADER, frag, &fs)) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        free(vert);
        free(frag);
        if (name && shader_try_load_binary_named(s, name))
            return true;
        return false;
    }

    unsigned int program;
    if (!shader_link_program(vs, fs, &program)) {
        free(vert);
        free(frag);
        if (name && shader_try_load_binary_named(s, name))
            return true;
        return false;
    }

    if (name) shader_save_binary(program, name, crc);
    free(vert);
    free(frag);

    s->program = program;
    shader_cache_uniforms(s);
    return true;
}

unsigned int shader_load_program(const char* name) {
    Shader s;
    memset(&s, 0, sizeof(s));
    if (!shader_compile_asset(&s, name))
        return 0;
    return s.program;
}

bool shader_warmup_all(void) {
    static const char* names[] = {
        "world", "ssao", "ssao_blur", "shadow", "fog", "debug_line", "skybox",
        "ui_color", "ui_tex", "ui_round", "ui_round_rgb", "ui_round_tex",
        "ui_text", "ui_quad", "ui_touch", "font_mono", "font_color", "ui_splash", "vignette",
        NULL
    };
    bool ok = true;
    for (int i = 0; names[i]; i++) {
        Shader s;
        memset(&s, 0, sizeof(s));
        if (!shader_compile_asset(&s, names[i])) {
            PW_ERR(ERR_SHADER, "Warmup failed: %s\n", names[i]);
            ok = false;
            continue;
        }
        shader_destroy(&s);
    }
    return ok;
}

void shader_use(const Shader* s) {
    glUseProgram(s->program);
}

void shader_destroy(Shader* s) {
    if (s->program) {
        glDeleteProgram(s->program);
    }
    s->program = 0;
    s->u_model = -1;
    s->u_view = -1;
    s->u_projection = -1;
    s->u_color = -1;
    s->u_light_dir = -1;
    s->u_light_color = -1;
    s->u_glow_light_count = -1;
    s->u_glow_light_pos = -1;
    s->u_glow_light_color = -1;
    s->u_glow_light_range = -1;
    s->u_glow_light_entity = -1;
    s->u_glow_shadow_count = -1;
    s->u_glow_shadow_map0 = -1;
    s->u_glow_shadow_map1 = -1;
    s->u_glow_ls = -1;
}
