/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: studio_embed.h                                                                      |
|   Purpose: jam the game window into Studio's 3D view                                        |
\*-------------------------------------------------------------------------------------------*/

#ifndef STUDIO_EMBED_H
#define STUDIO_EMBED_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PW_EMBED_MAGIC 0x4D455750u

#ifdef _WIN32
#define PW_EMBED_INVALID ((intptr_t)-1)
#else
#define PW_EMBED_INVALID ((intptr_t)-1)
#endif

#pragma pack(push, 1)
typedef struct PwEmbedMsg {
    uint32_t magic;
    int32_t x, y, w, h;
    int32_t screen_x, screen_y;
    uint8_t visible;
    uint8_t stop;
    uint8_t pad[2];
} PwEmbedMsg;
#pragma pack(pop)

struct GLFWwindow;

intptr_t pw_embed_listen(uint16_t* out_port);
intptr_t pw_embed_try_accept(intptr_t listen_fd);
int pw_embed_send_msg(intptr_t fd, const PwEmbedMsg* msg);
void pw_embed_close_fd(intptr_t fd);
uint64_t pw_embed_native_parent(struct GLFWwindow* studio_win);

void pw_embed_client_configure(const char* hostport, uint64_t parent_native);
bool pw_embed_client_active(void);
void pw_embed_client_bind(struct GLFWwindow* child);
void pw_embed_client_pump(struct GLFWwindow* child);

#ifdef __cplusplus
}
#endif

#endif
