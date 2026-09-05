/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: studio_embed.c                                                                      |
|   Purpose: jam the game window into Studio's 3D view                                        |
\*-------------------------------------------------------------------------------------------*/

#ifndef __EMSCRIPTEN__
#ifndef __ANDROID__

#include "studio_embed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  define GLFW_EXPOSE_NATIVE_WIN32
#else
#  include <unistd.h>
#  include <fcntl.h>
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  define GLFW_EXPOSE_NATIVE_X11
#endif

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#ifndef _WIN32
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

#ifdef _WIN32
static void pw_embed_wsa_once(void) {
    static int started = 0;
    if (started) return;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    started = 1;
}
static int pw_embed_last_wouldblock(void) {
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
static int pw_embed_set_nonblock(intptr_t fd) {
    u_long n = 1;
    return ioctlsocket((SOCKET)fd, FIONBIO, &n) == 0;
}
static int pw_embed_set_nodelay(intptr_t fd) {
    BOOL y = TRUE;
    return setsockopt((SOCKET)fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&y, sizeof(y)) == 0;
}
static void pw_embed_cloexec(intptr_t fd) { (void)fd; }
#else
static int pw_embed_last_wouldblock(void) {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS;
}
static int pw_embed_set_nonblock(intptr_t fd) {
    int fl = fcntl((int)fd, F_GETFL, 0);
    if (fl < 0) return 0;
    return fcntl((int)fd, F_SETFL, fl | O_NONBLOCK) == 0;
}
static int pw_embed_set_nodelay(intptr_t fd) {
    int y = 1;
    return setsockopt((int)fd, IPPROTO_TCP, TCP_NODELAY, &y, sizeof(y)) == 0;
}
static void pw_embed_cloexec(intptr_t fd) {
    int fl = fcntl((int)fd, F_GETFD, 0);
    if (fl >= 0) fcntl((int)fd, F_SETFD, fl | FD_CLOEXEC);
}
#endif

intptr_t pw_embed_listen(uint16_t* out_port) {
    if (out_port) *out_port = 0;
#ifdef _WIN32
    pw_embed_wsa_once();
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return PW_EMBED_INVALID;
    intptr_t fd = (intptr_t)s;
#else
    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) return PW_EMBED_INVALID;
    intptr_t fd = (intptr_t)s;
#endif
    int yes = 1;
#ifdef _WIN32
    setsockopt((SOCKET)fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
#else
    setsockopt((int)fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
#ifdef _WIN32
    if (bind((SOCKET)fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket((SOCKET)fd);
        return PW_EMBED_INVALID;
    }
    if (listen((SOCKET)fd, 1) != 0) {
        closesocket((SOCKET)fd);
        return PW_EMBED_INVALID;
    }
    int alen = (int)sizeof(addr);
    if (getsockname((SOCKET)fd, (struct sockaddr*)&addr, &alen) != 0) {
        closesocket((SOCKET)fd);
        return PW_EMBED_INVALID;
    }
#else
    if (bind((int)fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close((int)fd);
        return PW_EMBED_INVALID;
    }
    if (listen((int)fd, 1) != 0) {
        close((int)fd);
        return PW_EMBED_INVALID;
    }
    socklen_t alen = sizeof(addr);
    if (getsockname((int)fd, (struct sockaddr*)&addr, &alen) != 0) {
        close((int)fd);
        return PW_EMBED_INVALID;
    }
#endif
    pw_embed_set_nonblock(fd);
    pw_embed_cloexec(fd);
    if (out_port) *out_port = ntohs(addr.sin_port);
    return fd;
}

intptr_t pw_embed_try_accept(intptr_t listen_fd) {
    if (listen_fd == PW_EMBED_INVALID) return 0;
    struct sockaddr_in addr;
#ifdef _WIN32
    int alen = (int)sizeof(addr);
    SOCKET c = accept((SOCKET)listen_fd, (struct sockaddr*)&addr, &alen);
    if (c == INVALID_SOCKET) return 0;
    intptr_t fd = (intptr_t)c;
#else
    socklen_t alen = sizeof(addr);
    int c = accept((int)listen_fd, (struct sockaddr*)&addr, &alen);
    if (c < 0) return 0;
    intptr_t fd = (intptr_t)c;
#endif
    pw_embed_set_nonblock(fd);
    pw_embed_set_nodelay(fd);
    pw_embed_cloexec(fd);
    return fd;
}

int pw_embed_send_msg(intptr_t fd, const PwEmbedMsg* msg) {
    if (fd == PW_EMBED_INVALID || !msg) return 0;
    const char* p = (const char*)msg;
    size_t left = sizeof(*msg);
    while (left) {
#ifdef _WIN32
        int n = send((SOCKET)fd, p, (int)left, 0);
#else
        ssize_t n = send((int)fd, p, left, 0);
#endif
        if (n == 0) return 0;
        if (n < 0) {
            if (pw_embed_last_wouldblock()) return 1;
            return 0;
        }
        p += (size_t)n;
        left -= (size_t)n;
    }
    return 1;
}

void pw_embed_close_fd(intptr_t fd) {
    if (fd == PW_EMBED_INVALID) return;
#ifdef _WIN32
    closesocket((SOCKET)fd);
#else
    close((int)fd);
#endif
}

uint64_t pw_embed_native_parent(GLFWwindow* studio_win) {
    if (!studio_win) return 0;
#ifdef _WIN32
    HWND h = glfwGetWin32Window(studio_win);
    return (uint64_t)(uintptr_t)h;
#else
    if (glfwGetPlatform() != GLFW_PLATFORM_X11) return 0;
    return (uint64_t)glfwGetX11Window(studio_win);
#endif
}

static char g_embed_hostport[64];
static uint64_t g_embed_parent = 0;
static intptr_t g_embed_fd = PW_EMBED_INVALID;
static bool g_embed_attached = false;
static bool g_embed_configured = false;
static unsigned char g_embed_rx[sizeof(PwEmbedMsg)];
static int g_embed_rx_n = 0;
static int g_last_x = -1, g_last_y = -1, g_last_w = -1, g_last_h = -1;
static int g_last_vis = -1;

void pw_embed_client_configure(const char* hostport, uint64_t parent_native) {
    g_embed_hostport[0] = '\0';
    g_embed_parent = parent_native;
    g_embed_configured = false;
    if (!hostport || !hostport[0]) return;
    snprintf(g_embed_hostport, sizeof(g_embed_hostport), "%s", hostport);
    g_embed_configured = true;
}

bool pw_embed_client_active(void) {
    return g_embed_configured;
}

static intptr_t pw_embed_connect_hostport(const char* hostport) {
    if (!hostport || !hostport[0]) return PW_EMBED_INVALID;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", hostport);
    char* colon = strrchr(buf, ':');
    if (!colon) return PW_EMBED_INVALID;
    *colon = '\0';
    int port = atoi(colon + 1);
    if (port <= 0 || port > 65535) return PW_EMBED_INVALID;
    const char* host = buf[0] ? buf : "127.0.0.1";

#ifdef _WIN32
    pw_embed_wsa_once();
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return PW_EMBED_INVALID;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        closesocket(s);
        return PW_EMBED_INVALID;
    }
    DWORD ms = 8000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof(ms));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&ms, sizeof(ms));
    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(s);
        return PW_EMBED_INVALID;
    }
    intptr_t fd = (intptr_t)s;
#else
    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) return PW_EMBED_INVALID;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(s);
        return PW_EMBED_INVALID;
    }
    struct timeval tv;
    tv.tv_sec = 8;
    tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(s);
        return PW_EMBED_INVALID;
    }
    intptr_t fd = (intptr_t)s;
#endif
    pw_embed_set_nodelay(fd);
    return fd;
}

static bool pw_embed_attach(GLFWwindow* child, uint64_t parent) {
    if (!child || !parent) return false;
#ifdef _WIN32
    HWND ch = glfwGetWin32Window(child);
    HWND ph = (HWND)(uintptr_t)parent;
    if (!ch || !ph) return false;
    LONG_PTR style = GetWindowLongPtrA(ch, GWL_STYLE);
    style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
               WS_MAXIMIZEBOX | WS_SYSMENU | WS_DLGFRAME);
    style |= WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VISIBLE;
    SetWindowLongPtrA(ch, GWL_STYLE, style);
    SetParent(ch, ph);
    SetWindowPos(ch, HWND_TOP, 0, 0, 64, 64, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    return true;
#else
    if (glfwGetPlatform() != GLFW_PLATFORM_X11) return false;
    Display* dpy = glfwGetX11Display();
    Window cw = glfwGetX11Window(child);
    Window pw = (Window)parent;
    if (!dpy || !cw || !pw) return false;
    XReparentWindow(dpy, cw, pw, 0, 0);
    XMapWindow(dpy, cw);
    {
        Atom skip = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
        Atom state = XInternAtom(dpy, "_NET_WM_STATE", False);
        if (skip && state) {
            XChangeProperty(dpy, cw, state, XA_ATOM, 32, PropModeReplace,
                            (unsigned char*)&skip, 1);
        }
    }
    XFlush(dpy);
    return true;
#endif
}

static void pw_embed_apply_rect(GLFWwindow* child, const PwEmbedMsg* m) {
    if (!child || !m) return;
    int w = m->w > 16 ? m->w : 16;
    int h = m->h > 16 ? m->h : 16;
    int vis = m->visible ? 1 : 0;

    if (g_embed_attached) {
#ifdef _WIN32
        HWND ch = glfwGetWin32Window(child);
        if (ch) {
            UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
            if (vis != g_last_vis) {
                flags |= vis ? SWP_SHOWWINDOW : SWP_HIDEWINDOW;
            }
            SetWindowPos(ch, NULL, m->x, m->y, w, h, flags);
        }
#else
        if (glfwGetPlatform() == GLFW_PLATFORM_X11) {
            Display* dpy = glfwGetX11Display();
            Window cw = glfwGetX11Window(child);
            if (dpy && cw) {
                if (vis) {
                    XMoveResizeWindow(dpy, cw, m->x, m->y, (unsigned)w, (unsigned)h);
                    XMapWindow(dpy, cw);
                } else {
                    XUnmapWindow(dpy, cw);
                }
                XFlush(dpy);
            }
        }
#endif
    } else {

        if (vis) {
            glfwShowWindow(child);
            glfwSetWindowSize(child, w, h);
            if (m->screen_x != 0 || m->screen_y != 0)
                glfwSetWindowPos(child, m->screen_x, m->screen_y);
        } else {
            glfwHideWindow(child);
        }
    }
    g_last_x = m->x;
    g_last_y = m->y;
    g_last_w = w;
    g_last_h = h;
    g_last_vis = vis;
}

static int pw_embed_recv_one(intptr_t fd, PwEmbedMsg* out) {
    if (fd == PW_EMBED_INVALID) return -1;
    while (g_embed_rx_n < (int)sizeof(PwEmbedMsg)) {
#ifdef _WIN32
        int n = recv((SOCKET)fd, (char*)g_embed_rx + g_embed_rx_n,
                     (int)sizeof(PwEmbedMsg) - g_embed_rx_n, 0);
#else
        ssize_t n = recv((int)fd, g_embed_rx + g_embed_rx_n,
                         sizeof(PwEmbedMsg) - (size_t)g_embed_rx_n, 0);
#endif
        if (n == 0) return -1;
        if (n < 0) {
            if (pw_embed_last_wouldblock()) return 0;
            return -1;
        }
        g_embed_rx_n += (int)n;
    }
    memcpy(out, g_embed_rx, sizeof(*out));
    g_embed_rx_n = 0;
    if (out->magic != PW_EMBED_MAGIC) return -1;
    return 1;
}

void pw_embed_client_bind(GLFWwindow* child) {
    if (!g_embed_configured || !child) return;
    g_embed_fd = pw_embed_connect_hostport(g_embed_hostport);
    if (g_embed_fd == PW_EMBED_INVALID) {
        fprintf(stderr, "[playtest] embed connect failed (%s)\n", g_embed_hostport);
        glfwShowWindow(child);
        return;
    }
    g_embed_attached = pw_embed_attach(child, g_embed_parent);
    if (!g_embed_attached)
        glfwShowWindow(child);

    PwEmbedMsg msg;
    memset(&msg, 0, sizeof(msg));
    int spins = 0;
    while (spins++ < 200) {
        int r = pw_embed_recv_one(g_embed_fd, &msg);
        if (r < 0) {
            pw_embed_close_fd(g_embed_fd);
            g_embed_fd = PW_EMBED_INVALID;
            glfwShowWindow(child);
            return;
        }
        if (r > 0) {
            if (msg.stop) {
                glfwSetWindowShouldClose(child, GLFW_TRUE);
                return;
            }
            pw_embed_apply_rect(child, &msg);
            pw_embed_set_nonblock(g_embed_fd);
            return;
        }
#ifdef _WIN32
        Sleep(10);
#else
        usleep(10000);
#endif
    }
    pw_embed_set_nonblock(g_embed_fd);
}

void pw_embed_client_pump(GLFWwindow* child) {
    if (!child || g_embed_fd == PW_EMBED_INVALID) return;
    for (;;) {
        PwEmbedMsg msg;
        int r = pw_embed_recv_one(g_embed_fd, &msg);
        if (r == 0) break;
        if (r < 0) {
            pw_embed_close_fd(g_embed_fd);
            g_embed_fd = PW_EMBED_INVALID;
            glfwSetWindowShouldClose(child, GLFW_TRUE);
            break;
        }
        if (msg.stop) {
            glfwSetWindowShouldClose(child, GLFW_TRUE);
            break;
        }
        if (msg.x != g_last_x || msg.y != g_last_y || msg.w != g_last_w ||
            msg.h != g_last_h || (int)msg.visible != g_last_vis) {
            pw_embed_apply_rect(child, &msg);
        }
    }
}

#endif
#endif
