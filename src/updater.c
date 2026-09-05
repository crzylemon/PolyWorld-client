/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: updater.c                                                                           |
|   Purpose: auto-update (native + studio)                                                    |
\*-------------------------------------------------------------------------------------------*/

#include <stdbool.h>
#ifndef __EMSCRIPTEN__

#include "updater.h"
#include "prod_urls.h"
#include "auth.h"
#include "log.h"
#include "shader.h"
#include "client_version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

#include <GL/glew.h>
#include <GLFW/glfw3.h>

static bool g_force_update = false;
static bool g_unreachable = false;
static bool g_studio_channel = false;
static GLFWwindow* g_update_win = NULL;

void updater_set_force(bool force) {
    g_force_update = force;
}

void updater_set_studio_channel(bool studio) {
    g_studio_channel = studio;
}

static const char* updater_check_url(void) {
    return g_studio_channel ? PW_STUDIO_UPDATE_CHECK_URL : PW_UPDATE_CHECK_URL;
}

static const char* updater_pkg_name(void) {
    return g_studio_channel ? "pwstudio" : "polyworld";
}

static const char* updater_window_title(void) {
    return g_studio_channel ? "PolyWorld Studio" : "PolyWorld";
}

static int parse_version(const char* s, int parts[3]) {
    parts[0] = parts[1] = parts[2] = 0;
    if (!s || !s[0]) return 0;
    return sscanf(s, "%d.%d.%d", &parts[0], &parts[1], &parts[2]);
}

static int compare_versions(const char* local, const char* remote) {
    int l_parts[3] = {0, 0, 0};
    int r_parts[3] = {0, 0, 0};
    parse_version(local, l_parts);
    parse_version(remote, r_parts);

    if (r_parts[0] != l_parts[0]) return r_parts[0] - l_parts[0];
    if (r_parts[1] != l_parts[1]) return r_parts[1] - l_parts[1];
    return r_parts[2] - l_parts[2];
}

static void pump_update_ui(void) {
#ifdef _WIN32
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
#endif
    if (g_update_win) {
        glfwPollEvents();

        glfwSwapBuffers(g_update_win);
    }
}

#ifndef _WIN32

static bool updater_detect_wayland(void) {
    const char* session_type = getenv("XDG_SESSION_TYPE");
    if (session_type && strcmp(session_type, "wayland") == 0) {
        return true;
    }
    if (getenv("WAYLAND_DISPLAY") != NULL) {
        return true;
    }
    return false;
}
#endif

static void show_update_window(const char* version) {
    (void)version;
    if (!glfwInit()) return;

#ifndef _WIN32

    if (updater_detect_wayland()) {
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    }
#endif

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* win = glfwCreateWindow(1091, 711, updater_window_title(), NULL, NULL);
    if (!win) { glfwTerminate(); return; }
    g_update_win = win;

    glfwMakeContextCurrent(win);
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    while (glGetError() != GL_NO_ERROR) {}

    extern unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
    extern void stbi_image_free(void*);

    int iw = 0, ih = 0, ic = 0;
    unsigned char* pixels = NULL;
    FILE* splf = fopen("assets/update_splash.png", "rb");
    if (splf) {
        fseek(splf, 0, SEEK_END); long splsz = ftell(splf); fseek(splf, 0, SEEK_SET);
        unsigned char* spldata = (unsigned char*)malloc(splsz);
        fread(spldata, 1, splsz, splf); fclose(splf);
        pixels = stbi_load_from_memory(spldata, (int)splsz, &iw, &ih, &ic, 4);
        free(spldata);
    }
    if (!pixels) {

        glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glfwSwapBuffers(win);
        glfwPollEvents();
        return;
    }

    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, iw, ih, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(pixels);

    unsigned int prog = shader_load_program("ui_splash");

    unsigned int vao;
    glGenVertexArrays(1, &vao);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(prog);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glfwSwapBuffers(win);
    glfwPollEvents();

}

static void close_update_window(void) {
    if (g_update_win) {
        glfwDestroyWindow(g_update_win);
        g_update_win = NULL;
        glfwTerminate();
    }
}

#ifdef _WIN32
static wchar_t* updater_utf8_to_wide(const char* s) {
    if (!s) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t* w = (wchar_t*)malloc((size_t)n * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static bool updater_parse_url(const char* url, char* host, size_t host_sz,
                              char* path, size_t path_sz, int* port, int* is_https) {
    *port = 443;
    *is_https = 0;
    host[0] = path[0] = '\0';
    if (strncmp(url, "https://", 8) == 0) {
        *is_https = 1;
        url += 8;
        *port = INTERNET_DEFAULT_HTTPS_PORT;
    } else if (strncmp(url, "http://", 7) == 0) {
        url += 7;
        *port = INTERNET_DEFAULT_HTTP_PORT;
    } else {
        return false;
    }
    const char* slash = strchr(url, '/');
    const char* colon = strchr(url, ':');
    if (colon && (!slash || colon < slash)) {
        size_t hlen = (size_t)(colon - url);
        if (hlen >= host_sz) hlen = host_sz - 1;
        memcpy(host, url, hlen);
        host[hlen] = '\0';
        *port = atoi(colon + 1);
        strncpy(path, slash ? slash : "/", path_sz - 1);
    } else if (slash) {
        size_t hlen = (size_t)(slash - url);
        if (hlen >= host_sz) hlen = host_sz - 1;
        memcpy(host, url, hlen);
        host[hlen] = '\0';
        strncpy(path, slash, path_sz - 1);
    } else {
        strncpy(host, url, host_sz - 1);
        strcpy(path, "/");
    }
    path[path_sz - 1] = '\0';
    host[host_sz - 1] = '\0';
    return true;
}

static bool winhttp_get_to_file_or_mem(const char* url, const char* dest_file,
                                       char** out_mem, size_t* out_len, size_t max_bytes) {
    if (out_mem) { *out_mem = NULL; }
    if (out_len) { *out_len = 0; }

    char host[256], path[1024];
    int port = 443, is_https = 0;
    if (!updater_parse_url(url, host, sizeof(host), path, sizeof(path), &port, &is_https))
        return false;

    HINTERNET session = WinHttpOpen(L"PolyWorldUpdater/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;

    DWORD timeout = 15000;
    WinHttpSetTimeouts(session, timeout, timeout, timeout, 120000);

    wchar_t* whost = updater_utf8_to_wide(host);
    HINTERNET conn = WinHttpConnect(session, whost, (INTERNET_PORT)port, 0);
    free(whost);
    if (!conn) { WinHttpCloseHandle(session); return false; }

    wchar_t* wpath = updater_utf8_to_wide(path);
    DWORD flags = is_https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", wpath,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    free(wpath);
    if (!req) {
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return false;
    }

    if (flags & WINHTTP_FLAG_SECURE) {
        DWORD sec = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                    SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                    SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                    SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(req, WINHTTP_OPTION_SECURITY_FLAGS, &sec, sizeof(sec));
    }

    BOOL ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    pump_update_ui();
    if (!ok || !WinHttpReceiveResponse(req, NULL)) {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD status = 0, status_sz = sizeof(status);
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_sz,
                             WINHTTP_NO_HEADER_INDEX) || status != 200) {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return false;
    }

    FILE* out = NULL;
    if (dest_file) {
        out = fopen(dest_file, "wb");
        if (!out) {
            WinHttpCloseHandle(req);
            WinHttpCloseHandle(conn);
            WinHttpCloseHandle(session);
            return false;
        }
    }

    char* mem = NULL;
    size_t mem_len = 0, mem_cap = 0;
    bool success = true;
    for (;;) {
        pump_update_ui();
        if (g_update_win && glfwWindowShouldClose(g_update_win)) {
            success = false;
            break;
        }
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) { success = false; break; }
        if (avail == 0) break;
        if (max_bytes && mem_len + avail > max_bytes) { success = false; break; }

        char buf[8192];
        DWORD to_read = avail;
        if (to_read > sizeof(buf)) to_read = (DWORD)sizeof(buf);
        DWORD read = 0;
        if (!WinHttpReadData(req, buf, to_read, &read) || read == 0) {
            success = false;
            break;
        }
        if (out) {
            if (fwrite(buf, 1, read, out) != read) { success = false; break; }
        } else {
            if (mem_len + read + 1 > mem_cap) {
                size_t ncap = mem_len + read + 4096;
                char* nbuf = (char*)realloc(mem, ncap);
                if (!nbuf) { success = false; break; }
                mem = nbuf;
                mem_cap = ncap;
            }
            memcpy(mem + mem_len, buf, read);
            mem_len += read;
            mem[mem_len] = '\0';
        }
    }

    if (out) fclose(out);
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);

    if (!success) {
        free(mem);
        if (dest_file) DeleteFileA(dest_file);
        return false;
    }
    if (out_mem) {
        *out_mem = mem;
        if (out_len) *out_len = mem_len;
    } else {
        free(mem);
    }
    return true;
}

static char* download_string(const char* url) {
    char* data = NULL;
    size_t len = 0;
    if (!winhttp_get_to_file_or_mem(url, NULL, &data, &len, 64))
        return NULL;
    if (!data || len == 0) { free(data); return NULL; }
    while (len > 0 && (data[len-1] == '\n' || data[len-1] == '\r' || data[len-1] == ' '))
        data[--len] = '\0';
    return data;
}

static bool download_file(const char* url, const char* dest) {
    return winhttp_get_to_file_or_mem(url, dest, NULL, NULL, 0);
}

static bool run_cmd_pumped(const char* cmd, DWORD timeout_ms) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    memset(&pi, 0, sizeof(pi));

    char* cmdline = (char*)malloc(strlen(cmd) + 1);
    if (!cmdline) return false;
    memcpy(cmdline, cmd, strlen(cmd) + 1);
    BOOL created = CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                                  CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(cmdline);
    if (!created) return false;

    DWORD start = GetTickCount();
    for (;;) {
        pump_update_ui();
        DWORD wait = WaitForSingleObject(pi.hProcess, 50);
        if (wait == WAIT_OBJECT_0) break;
        if (timeout_ms && (GetTickCount() - start) > timeout_ms) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return false;
        }
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return true;
}

static void updater_fail_msg(const char* detail) {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "Update failed: %s\n\nYou can keep using this version, or download manually from polyworld.games",
             detail ? detail : "unknown error");
    MessageBoxA(NULL, buf, g_studio_channel ? "PolyWorld Studio Update" : "PolyWorld Update",
                MB_OK | MB_ICONWARNING);
}

static void do_update(const char* remote_ver) {
    show_update_window(remote_ver);
    pump_update_ui();

    const char* pkg = updater_pkg_name();
    char url[512];
    if (g_studio_channel) {
        snprintf(url, sizeof(url),
                 PW_SITE_ORIGIN "/build/%s/polyworld-studio-windows-x86-64-%s.zip",
                 remote_ver, remote_ver);
    } else {
        snprintf(url, sizeof(url),
                 PW_SITE_ORIGIN "/build/%s/polyworld-windows-x86-64-%s.zip",
                 remote_ver, remote_ver);
    }

    char tmp_zip[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp_zip);
    strcat(tmp_zip, g_studio_channel ? "pwstudio_update.zip" : "polyworld_update.zip");
    DeleteFileA(tmp_zip);

    PW_LOG("Updater: downloading %s\n", url);
    if (!download_file(url, tmp_zip)) {
        updater_fail_msg("could not download update (network/timeout)");
        close_update_window();
        return;
    }

    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);

    char old_path[MAX_PATH];
    snprintf(old_path, sizeof(old_path), "%s.old", exe_path);
    DeleteFileA(old_path);
    if (!MoveFileA(exe_path, old_path)) {
        updater_fail_msg("could not replace running exe (file in use?)");
        DeleteFileA(tmp_zip);
        close_update_window();
        return;
    }

    char extract_dir[MAX_PATH];
    GetTempPathA(MAX_PATH, extract_dir);
    strcat(extract_dir, g_studio_channel ? "pwstudio_update_extract" : "pw_update_extract");

    char cmd[1400];
    snprintf(cmd, sizeof(cmd),
             "cmd.exe /c rmdir /s /q \"%s\" & mkdir \"%s\" & tar.exe -xf \"%s\" -C \"%s\"",
             extract_dir, extract_dir, tmp_zip, extract_dir);
    if (!run_cmd_pumped(cmd, 180000)) {

        snprintf(cmd, sizeof(cmd),
                 "powershell -nologo -noprofile -ExecutionPolicy Bypass -command "
                 "\"Remove-Item -Recurse -Force '%s' -ErrorAction SilentlyContinue; "
                 "Expand-Archive -Force '%s' '%s'\"",
                 extract_dir, tmp_zip, extract_dir);
        if (!run_cmd_pumped(cmd, 180000)) {
            MoveFileA(old_path, exe_path);
            updater_fail_msg("could not extract update archive");
            DeleteFileA(tmp_zip);
            close_update_window();
            return;
        }
    }

    char new_exe[MAX_PATH];
    const char* bin_name = g_studio_channel ? "polystudio.exe" : "polyworld.exe";
    snprintf(new_exe, sizeof(new_exe), "%s\\%s\\%s", extract_dir, pkg, bin_name);
    if (!CopyFileA(new_exe, exe_path, FALSE)) {
        snprintf(new_exe, sizeof(new_exe), "%s\\%s", extract_dir, bin_name);
        if (!CopyFileA(new_exe, exe_path, FALSE)) {
            MoveFileA(old_path, exe_path);
            updater_fail_msg(g_studio_channel ? "could not install new polystudio.exe"
                                              : "could not install new polyworld.exe");
            DeleteFileA(tmp_zip);
            close_update_window();
            return;
        }
    }

    char exe_dir[MAX_PATH];
    strncpy(exe_dir, exe_path, MAX_PATH - 1);
    exe_dir[MAX_PATH - 1] = '\0';
    char* last_bs = strrchr(exe_dir, '\\');
    if (last_bs) *last_bs = '\0';

    if (g_studio_channel) {
        char src[MAX_PATH], dst[MAX_PATH];
        snprintf(src, sizeof(src), "%s\\%s\\polyworld.exe", extract_dir, pkg);
        snprintf(dst, sizeof(dst), "%s\\polyworld.exe", exe_dir);
        CopyFileA(src, dst, FALSE);

        snprintf(cmd, sizeof(cmd),
                 "cmd.exe /c copy /Y \"%s\\%s\\*.dll\" \"%s\\\" >nul 2>nul",
                 extract_dir, pkg, exe_dir);
        run_cmd_pumped(cmd, 60000);
    }

    char assets_src[MAX_PATH], assets_dst[MAX_PATH];
    snprintf(assets_src, sizeof(assets_src), "%s\\%s\\assets", extract_dir, pkg);
    snprintf(assets_dst, sizeof(assets_dst), "%s\\assets", exe_dir);
    snprintf(cmd, sizeof(cmd),
             "robocopy \"%s\" \"%s\" /MIR /NJH /NJS /NP /R:1 /W:1",
             assets_src, assets_dst);
    run_cmd_pumped(cmd, 180000);

    DeleteFileA(tmp_zip);
    close_update_window();

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    char launch[MAX_PATH + 64];
    snprintf(launch, sizeof(launch), "\"%s\"", exe_path);
    if (CreateProcessA(NULL, launch, NULL, NULL, FALSE, 0, NULL, exe_dir, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    ExitProcess(0);
}

#else

static void updater_fail_msg(const char* detail) {
    const char* title = g_studio_channel ? "PolyWorld Studio" : "PolyWorld";
    PW_LOG("Updater: failed: %s\n", detail ? detail : "unknown");
    fprintf(stderr, "%s update failed: %s\n", title, detail ? detail : "unknown");
    char cmd[768];
    snprintf(cmd, sizeof(cmd),
             "notify-send -u critical '%s' 'Update failed: %s. You can keep using this version.' >/dev/null 2>&1 || true",
             title, detail ? detail : "unknown error");
    system(cmd);
}

static char* curl_download_string(const char* url, const char* extra_flags) {
    char cmd[640];
    const char* curl = (access("/usr/bin/curl", X_OK) == 0) ? "/usr/bin/curl" : "curl";
    snprintf(cmd, sizeof(cmd),
             "%s -fsS --max-time 10 --connect-timeout 5 -L %s '%s' 2>/dev/null",
             curl, extra_flags ? extra_flags : "", url);
    FILE* fp = popen(cmd, "r");
    if (!fp) return NULL;
    char buf[128] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    int st = pclose(fp);
    if (n == 0 || st != 0) return NULL;
    buf[n] = '\0';
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' ')) buf[--n] = '\0';
    return n > 0 ? strdup(buf) : NULL;
}

static char* download_string(const char* url) {
    char* data = curl_download_string(url, "");
    if (data) return data;

    return curl_download_string(url, "-4");
}

static void do_update(const char* remote_ver) {
    show_update_window(remote_ver);

    const char* pkg = updater_pkg_name();
    char url[512];
    if (g_studio_channel) {
        snprintf(url, sizeof(url),
                 PW_SITE_ORIGIN "/build/%s/polyworld-studio-linux-x64-%s.tar.xz",
                 remote_ver, remote_ver);
    } else {
        snprintf(url, sizeof(url),
                 PW_SITE_ORIGIN "/build/%s/polyworld-linux-x64-%s.tar.xz",
                 remote_ver, remote_ver);
    }

    char self_path[512] = {0};
    ssize_t len = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (len <= 0) { close_update_window(); return; }
    self_path[len] = '\0';

    char tmp_tar[256];
    snprintf(tmp_tar, sizeof(tmp_tar),
             g_studio_channel ? "/tmp/pwstudio_update.tar.xz" : "/tmp/polyworld_update.tar.xz");
    char dl_cmd[1024];
    const char* curl = (access("/usr/bin/curl", X_OK) == 0) ? "/usr/bin/curl" : "curl";
    snprintf(dl_cmd, sizeof(dl_cmd),
             "%s -fL --retry 2 --max-time 120 --connect-timeout 15 -o '%s' '%s'",
             curl, tmp_tar, url);
    PW_LOG("Updater: downloading %s\n", url);
    if (system(dl_cmd) != 0) {
        updater_fail_msg("could not download update");
        close_update_window();
        return;
    }

    char extract_dir[64];
    snprintf(extract_dir, sizeof(extract_dir),
             g_studio_channel ? "/tmp/pwstudio_update_extract" : "/tmp/pw_update_extract");
    char ext_cmd[512];
    snprintf(ext_cmd, sizeof(ext_cmd), "rm -rf %s && mkdir -p %s && tar -xf '%s' -C %s",
             extract_dir, extract_dir, tmp_tar, extract_dir);
    if (system(ext_cmd) != 0) {
        updater_fail_msg("could not extract update archive");
        close_update_window();
        return;
    }

    const char* bin_name = g_studio_channel ? "polystudio" : "polyworld";
    char new_bin[512];
    snprintf(new_bin, sizeof(new_bin), "%s/%s/%s", extract_dir, pkg, bin_name);

    struct stat st;
    if (stat(new_bin, &st) != 0) {

        snprintf(new_bin, sizeof(new_bin), "%s/%s", extract_dir, bin_name);
        if (stat(new_bin, &st) != 0) {
            updater_fail_msg(g_studio_channel
                ? "could not find new polystudio in update"
                : "could not find new polyworld in update");
            close_update_window();
            return;
        }
    }

    char backup[512];
    snprintf(backup, sizeof(backup), "%s.old", self_path);
    rename(self_path, backup);
    char cp_cmd[1024];
    snprintf(cp_cmd, sizeof(cp_cmd), "cp '%s' '%s' && chmod +x '%s'", new_bin, self_path, self_path);
    if (system(cp_cmd) != 0) {
        rename(backup, self_path);
        updater_fail_msg("could not replace running binary");
        close_update_window();
        return;
    }

    char bin_dir[512];
    strncpy(bin_dir, self_path, sizeof(bin_dir));
    bin_dir[sizeof(bin_dir) - 1] = '\0';
    char* last_slash = strrchr(bin_dir, '/');
    if (last_slash) *last_slash = '\0';
    else strcpy(bin_dir, ".");

    if (g_studio_channel) {
        char play_src[512];
        snprintf(play_src, sizeof(play_src), "%s/%s/polyworld", extract_dir, pkg);
        if (stat(play_src, &st) != 0)
            snprintf(play_src, sizeof(play_src), "%s/polyworld", extract_dir);
        if (stat(play_src, &st) == 0) {
            char play_dst[512];
            snprintf(play_dst, sizeof(play_dst), "%s/polyworld", bin_dir);
            snprintf(cp_cmd, sizeof(cp_cmd), "cp '%s' '%s' && chmod +x '%s'",
                     play_src, play_dst, play_dst);
            system(cp_cmd);
        }
    }

    char new_assets[512];
    snprintf(new_assets, sizeof(new_assets), "%s/%s/assets", extract_dir, pkg);
    struct stat ast;
    if (stat(new_assets, &ast) != 0)
        snprintf(new_assets, sizeof(new_assets), "%s/assets", extract_dir);
    if (stat(new_assets, &ast) == 0) {
        char assets_cmd[1024];
        snprintf(assets_cmd, sizeof(assets_cmd),
                 "rm -rf '%s/assets' && cp -r '%s' '%s/assets'", bin_dir, new_assets, bin_dir);
        if (system(assets_cmd) != 0)
            PW_LOG("Updater: warning: could not copy updated assets\n");
    } else {
        PW_LOG("Updater: warning: update archive had no assets/\n");
    }

    remove(tmp_tar);
    close_update_window();
    execl(self_path, self_path, (char*)NULL);
    updater_fail_msg("could not relaunch after update");
}
#endif

void updater_check(void) {

    if (!pw_site_is_production()) {
        return;
    }

    if (strstr(CLIENT_VERSION, "demo") != NULL) {
        return;
    }

#ifdef _WIN32

    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    char old_path[MAX_PATH];
    snprintf(old_path, sizeof(old_path), "%s.old", exe_path);
    DeleteFileA(old_path);
#endif

    char* remote = download_string(updater_check_url());
    if (!remote) {

        PW_LOG("Updater: could not reach %s; continuing with %s\n",
               updater_check_url(), CLIENT_VERSION);
        g_unreachable = true;
        return;
    }

    if (!g_force_update && strcmp(CLIENT_VERSION, remote) == 0) {
        free(remote);
        return;
    }

    int rparts[3];
    if (parse_version(remote, rparts) < 2) {
        PW_LOG("Updater: ignoring malformed remote version '%s'\n", remote);
        free(remote);
        return;
    }

    PW_LOG("Updater: local=%s remote=%s\n", CLIENT_VERSION, remote);
    if (g_force_update || compare_versions(CLIENT_VERSION, remote) > 0) {
        do_update(remote);

        close_update_window();
    }

    free(remote);
}

bool updater_server_unreachable(void) {
    return g_unreachable;
}

const char* updater_get_version(void) {
    return CLIENT_VERSION;
}
#else

void updater_check(void) {}
void updater_set_force(bool force) { (void)force; }
void updater_set_studio_channel(bool studio) { (void)studio; }
bool updater_server_unreachable(void) { return false; }
const char* updater_get_version(void) { return "web"; }
#endif
