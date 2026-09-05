/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: net_client.c                                                                        |
|   Purpose: client net (TCP native, websocket wasm)                                          |
\*-------------------------------------------------------------------------------------------*/

#include "net_client.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "platform.h"

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <emscripten/html5.h>

static NetClient* g_ws_client = NULL;

EM_JS(int, js_ws_connect, (const char* url_ptr), {
    var url = UTF8ToString(url_ptr);
    if (window._pw_ws) { try { window._pw_ws.close(); } catch(e){} }
    try {
        window._pw_ws = new WebSocket(url);
        window._pw_ws.binaryType = 'arraybuffer';
        window._pw_ws_connected = false;
        window._pw_ws_queue = [];
        window._pw_ws.onopen = function() {
            console.log('[Net] WebSocket connected');
            window._pw_ws_connected = true;
        };
        window._pw_ws.onmessage = function(e) {
            if (e.data instanceof ArrayBuffer) {
                window._pw_ws_queue.push(new Uint8Array(e.data));
            }
        };
        window._pw_ws.onerror = function(e) {
            console.error('[Net] WebSocket error', e);
            if (window.parent && window.parent !== window) {
                window.parent.postMessage({type: 'connection_error'}, '*');
            }
        };
        window._pw_ws.onclose = function(e) {
            console.log('[Net] WebSocket closed:', e.code, e.reason);
            window._pw_ws_connected = false;
            if (window.parent && window.parent !== window) {
                window.parent.postMessage({type: 'connection_error'}, '*');
            }
        };
        return 1;
    } catch(e) {
        console.error('[Net] Failed to create WebSocket:', e);
        if (window.parent && window.parent !== window) {
            window.parent.postMessage({type: 'connection_error'}, '*');
        }
        return 0;
    }
});

EM_JS(int, js_ws_is_connected, (void), {
    return (window._pw_ws && window._pw_ws_connected) ? 1 : 0;
});

EM_JS(void, js_ws_send, (const uint8_t* data, int len), {
    if (window._pw_ws && window._pw_ws_connected) {
        var buf = new Uint8Array(len);
        for (var i = 0; i < len; i++) buf[i] = HEAPU8[data + i];
        window._pw_ws.send(buf.buffer);
    }
});

EM_JS(int, js_ws_recv, (uint8_t* out_buf, int max_len), {
    if (!window._pw_ws_queue || window._pw_ws_queue.length === 0) return 0;
    var msg = window._pw_ws_queue.shift();
    var len = msg.length < max_len ? msg.length : max_len;
    for (var i = 0; i < len; i++) HEAPU8[out_buf + i] = msg[i];
    return len;
});

EM_JS(void, js_ws_close, (void), {
    if (window._pw_ws) { try { window._pw_ws.close(); } catch(e){} window._pw_ws = null; }
    window._pw_ws_connected = false;
});

bool net_client_connect(NetClient* nc, const char* host, int port) {
    memset(nc, 0, sizeof(NetClient));
    nc->sock_fd = -1;
    nc->state = NET_STATE_CONNECTING;
    g_ws_client = nc;

    static char url[256];
    snprintf(url, sizeof(url), "wss://%s:%d/gameserver", host, port);

    if (js_ws_connect(url)) {

        nc->last_pos_len = 0;
        nc->last_send_time = 0.0;
        return true;
    }
    nc->state = NET_STATE_DISCONNECTED;
    return false;
}

void net_client_disconnect(NetClient* nc) {
    js_ws_close();
    nc->state = NET_STATE_DISCONNECTED;
}

void net_client_poll(NetClient* nc) {
    if (nc->state == NET_STATE_CONNECTING && js_ws_is_connected()) {
        nc->state = NET_STATE_CONNECTED;
    }
    if (nc->state == NET_STATE_CONNECTED && !js_ws_is_connected()) {
        net_client_disconnect(nc);
        return;
    }
    if (nc->state == NET_STATE_CONNECTED) {
        while (nc->recv_len < sizeof(nc->recv_buf) - 1024) {
            int n = js_ws_recv(nc->recv_buf + nc->recv_len,
                              (int)(sizeof(nc->recv_buf) - nc->recv_len));
            if (n <= 0) break;
            nc->recv_len += (size_t)n;
        }
    }
}

void net_client_send(NetClient* nc, uint8_t msg_type, const uint8_t* data, size_t len) {
    if (nc->state != NET_STATE_CONNECTED) return;
    size_t total = 3 + len;
    uint8_t* buf = (uint8_t*)malloc(total);
    buf[0] = msg_type;
    buf[1] = (uint8_t)(len & 0xFF);
    buf[2] = (uint8_t)((len >> 8) & 0xFF);
    if (len > 0 && data) memcpy(buf + 3, data, len);
    js_ws_send(buf, (int)total);
    free(buf);

    if (msg_type == MSG_PLAYER_INPUT && len == 16) {
        memcpy(nc->last_pos_buf, data, 16);
        nc->last_pos_len = 16;
        nc->last_send_time = platform_get_time();
    }
}

uint8_t net_client_recv(NetClient* nc, uint8_t* buf, size_t* out_len) {
    if (nc->recv_len < 3) return 0;
    uint8_t msg_type = nc->recv_buf[0];
    uint16_t msg_len = nc->recv_buf[1] | (nc->recv_buf[2] << 8);
    if (nc->recv_len < (size_t)(3 + msg_len)) return 0;
    if (msg_len > 0 && buf) memcpy(buf, nc->recv_buf + 3, msg_len);
    *out_len = msg_len;
    size_t consumed = 3 + msg_len;
    memmove(nc->recv_buf, nc->recv_buf + consumed, nc->recv_len - consumed);
    nc->recv_len -= consumed;
    return msg_type;
}

void net_client_send_auth(NetClient* nc, int game_id, const char* username) {
    size_t name_len = strlen(username);
    uint8_t buf[68];
    memcpy(buf, &game_id, 4);
    memcpy(buf + 4, username, name_len);
    net_client_send(nc, MSG_AUTH, buf, 4 + name_len);
}

void net_client_send_auth_ticket(NetClient* nc, int game_id, const char* ticket) {
    uint8_t buf[37];
    memcpy(buf, &game_id, 4);
    buf[4] = 0x01;
    size_t tlen = strlen(ticket);
    if (tlen > 32) tlen = 32;
    memcpy(buf + 5, ticket, tlen);
    net_client_send(nc, MSG_AUTH, buf, 5 + tlen);
}

void net_client_send_position(NetClient* nc, float x, float y, float z, float yaw) {

    uint8_t buf[16];
    memcpy(buf, &x, 4);
    memcpy(buf + 4, &y, 4);
    memcpy(buf + 8, &z, 4);
    memcpy(buf + 12, &yaw, 4);
    net_client_send(nc, MSG_PLAYER_INPUT, buf, 16);
}

#else

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
typedef long long ssize_t;
#define close closesocket
#define SHUT_RDWR SD_BOTH
static void set_nonblocking(int fd) {
    u_long mode = 1;
    ioctlsocket(fd, FIONBIO, &mode);
}
static void net_init_platform(void) {
    static int done = 0;
    if (!done) { WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa); done = 1; }
}
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#else
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#if !defined(_WIN32)
#include <pthread.h>
#endif

#if !defined(_WIN32)
static void* net_client_keepalive_thread(void* arg) {
    NetClient* nc = (NetClient*)arg;
    while (nc->keepalive_running) {
        struct timespec req = {1, 0};
        nanosleep(&req, NULL);
        double now = platform_get_time();
        if (nc->state == NET_STATE_CONNECTED && nc->last_pos_len == 16) {
            if (now - nc->last_send_time >= 5.0) {

            }
        }
    }
    return NULL;
}
#endif

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
static void net_init_platform(void) {}
#endif

#if !defined(_WIN32)
#include <poll.h>
#endif

static bool net_client_write_all(int fd, const uint8_t* data, size_t len) {
    if (fd < 0 || !data || len == 0) return false;
    size_t sent = 0;
    int spins = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += (size_t)n;
            spins = 0;
            continue;
        }
#ifdef _WIN32
        int err = WSAGetLastError();
        if (n < 0 && err == WSAEINTR) continue;
        if (n < 0 && err == WSAEWOULDBLOCK) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET((SOCKET)fd, &wfds);
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 250000;
            int pr = select(0, NULL, &wfds, NULL, &tv);
            if (++spins > 40) return false;
            if (pr <= 0) continue;
            continue;
        }
        return false;
#else
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd pfd = { .fd = fd, .events = POLLOUT, .revents = 0 };
            int pr = poll(&pfd, 1, 250);

            if (++spins > 40) return false;
            if (pr < 0 && errno == EINTR) continue;
            if (pr <= 0) continue;
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return false;
            continue;
        }
        return false;
#endif
    }
    return true;
}

static bool net_client_write_nb(int fd, const uint8_t* data, size_t len) {
    if (fd < 0 || !data || len == 0) return false;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
#ifdef _WIN32
        int err = WSAGetLastError();
        if (n < 0 && err == WSAEINTR) continue;
        if (n < 0 && err == WSAEWOULDBLOCK) return sent > 0;
        return false;
#else
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return sent > 0;
        return false;
#endif
    }
    return true;
}

bool net_client_connect(NetClient* nc, const char* host, int port) {
    net_init_platform();

    if (nc->sock_fd >= 0
#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
        || nc->keepalive_running
#endif
        )
        net_client_disconnect(nc);
    memset(nc, 0, sizeof(NetClient));
    nc->sock_fd = -1;
    nc->state = NET_STATE_DISCONNECTED;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* res = NULL;
    int gai = getaddrinfo(host, port_str, &hints, &res);
    if (gai != 0 || !res) {
        PW_ERR(ERR_CONN, "Failed to resolve hostname\n");
        return false;
    }

    int fd = -1;
    for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
        fd = (int)socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;

        int flag = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&flag, sizeof(flag));
        set_nonblocking(fd);

        int cr = connect(fd, ai->ai_addr, (socklen_t)ai->ai_addrlen);
#ifdef _WIN32
        int in_progress = (cr < 0 && WSAGetLastError() == WSAEWOULDBLOCK);
#else
        int in_progress = (cr < 0 && (errno == EINPROGRESS || errno == EWOULDBLOCK));
#endif
        if (cr == 0) {

            break;
        }
        if (!in_progress) {
            close(fd);
            fd = -1;
            continue;
        }

        double t0 = platform_get_time();
        int ok = 0;
        while (platform_get_time() - t0 < 5.0) {
#ifdef _WIN32
            fd_set wfds, efds;
            FD_ZERO(&wfds);
            FD_ZERO(&efds);
            FD_SET((SOCKET)fd, &wfds);
            FD_SET((SOCKET)fd, &efds);
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 100000;
            int pr = select(0, NULL, &wfds, &efds, &tv);
            if (pr > 0 && (FD_ISSET((SOCKET)fd, &wfds) || FD_ISSET((SOCKET)fd, &efds))) {
                int soerr = 0;
                socklen_t sl = sizeof(soerr);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&soerr, &sl);
                if (soerr == 0) ok = 1;
                break;
            }
#else
            struct pollfd pfd = { .fd = fd, .events = POLLOUT, .revents = 0 };
            int pr = poll(&pfd, 1, 100);
            if (pr > 0) {
                int soerr = 0;
                socklen_t sl = sizeof(soerr);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
                if (soerr == 0) ok = 1;
                break;
            }
#endif

            platform_flush_frame();
        }
        if (ok) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) {
        PW_ERR(ERR_CONN, "connect timed out or failed\n");
        return false;
    }

    nc->sock_fd = fd;
    nc->state = NET_STATE_CONNECTED;

    uint8_t ident = 0x50;
    if (!net_client_write_all(nc->sock_fd, &ident, 1)) {
        PW_ERR(ERR_CONN, "Failed to send protocol ident\n");
        close(nc->sock_fd);
        nc->sock_fd = -1;
        nc->state = NET_STATE_DISCONNECTED;
        return false;
    }

    nc->last_pos_len = 0;
    nc->last_send_time = 0.0;

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)

    nc->keepalive_running = 1;
    pthread_t thr;
    if (pthread_create(&thr, NULL, net_client_keepalive_thread, nc) == 0) {
        nc->keepalive_thread = (void*)(intptr_t)thr;
    } else {
        nc->keepalive_thread = NULL;
    }
#endif
    return true;
}

void net_client_disconnect(NetClient* nc) {
    if (nc->sock_fd >= 0) { close(nc->sock_fd); nc->sock_fd = -1; }
    nc->state = NET_STATE_DISCONNECTED;
#if !defined(_WIN32)
    if (nc->keepalive_running) {
        nc->keepalive_running = 0;
        if (nc->keepalive_thread) {
            pthread_t thr = (pthread_t)(intptr_t)nc->keepalive_thread;
            pthread_join(thr, NULL);
            nc->keepalive_thread = NULL;
        }
    }
#endif
}

void net_client_poll(NetClient* nc) {
    if (nc->state != NET_STATE_CONNECTED || nc->sock_fd < 0) return;
    size_t space = sizeof(nc->recv_buf) - nc->recv_len;

    if (space < 3) return;
    ssize_t n = recv(nc->sock_fd, nc->recv_buf + nc->recv_len, space, 0);
    if (n > 0) {
        nc->recv_len += (size_t)n;
    } else if (n == 0) {
        net_client_disconnect(nc);
    } else {
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINTR)
            net_client_disconnect(nc);
#else
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            net_client_disconnect(nc);
#endif
    }
}

void net_client_send(NetClient* nc, uint8_t msg_type, const uint8_t* data, size_t len) {
    if (nc->state != NET_STATE_CONNECTED || nc->sock_fd < 0) return;

    bool reliable = (msg_type == MSG_AUTH);
    uint8_t header[3];
    header[0] = msg_type;
    header[1] = (uint8_t)(len & 0xFF);
    header[2] = (uint8_t)((len >> 8) & 0xFF);
    if (reliable) {
        if (!net_client_write_all(nc->sock_fd, header, 3) ||
            (len > 0 && data && !net_client_write_all(nc->sock_fd, data, len))) {
            PW_ERR(ERR_CONN, "Reliable send failed. disconnecting\n");
            net_client_disconnect(nc);
            return;
        }
    } else {

        uint8_t stack[512];
        uint8_t* buf = stack;
        size_t total = 3 + len;
        bool heap = false;
        if (total > sizeof(stack)) {
            buf = (uint8_t*)malloc(total);
            if (!buf) return;
            heap = true;
        }
        buf[0] = header[0];
        buf[1] = header[1];
        buf[2] = header[2];
        if (len > 0 && data) memcpy(buf + 3, data, len);
        if (!net_client_write_nb(nc->sock_fd, buf, total)) {

        }
        if (heap) free(buf);
    }

    if (msg_type == MSG_PLAYER_INPUT && len == 16) {
        memcpy(nc->last_pos_buf, data, 16);
        nc->last_pos_len = 16;
        nc->last_send_time = platform_get_time();
    }
}

uint8_t net_client_recv(NetClient* nc, uint8_t* buf, size_t* out_len) {
    if (nc->recv_len < 3) return 0;
    uint8_t msg_type = nc->recv_buf[0];
    uint16_t msg_len = nc->recv_buf[1] | (nc->recv_buf[2] << 8);
    if (nc->recv_len < (size_t)(3 + msg_len)) return 0;
    if (msg_len > 0 && buf) memcpy(buf, nc->recv_buf + 3, msg_len);
    *out_len = msg_len;
    size_t consumed = 3 + msg_len;
    memmove(nc->recv_buf, nc->recv_buf + consumed, nc->recv_len - consumed);
    nc->recv_len -= consumed;
    return msg_type;
}

void net_client_send_auth(NetClient* nc, int game_id, const char* username) {
    size_t name_len = strlen(username);
    uint8_t buf[68];
    memcpy(buf, &game_id, 4);
    memcpy(buf + 4, username, name_len);
    net_client_send(nc, MSG_AUTH, buf, 4 + name_len);
}

void net_client_send_auth_ticket(NetClient* nc, int game_id, const char* ticket) {
    uint8_t buf[37];
    memcpy(buf, &game_id, 4);
    buf[4] = 0x01;
    size_t tlen = strlen(ticket);
    if (tlen > 32) tlen = 32;
    memcpy(buf + 5, ticket, tlen);
    net_client_send(nc, MSG_AUTH, buf, 5 + tlen);
}

void net_client_send_position(NetClient* nc, float x, float y, float z, float yaw) {
    uint8_t buf[16];
    memcpy(buf, &x, 4);
    memcpy(buf + 4, &y, 4);
    memcpy(buf + 8, &z, 4);
    memcpy(buf + 12, &yaw, 4);
    net_client_send(nc, MSG_PLAYER_INPUT, buf, 16);
}

#endif
