/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: net_client.h                                                                        |
|   Purpose: client net                                                                       |
\*-------------------------------------------------------------------------------------------*/

#ifndef NET_CLIENT_H
#define NET_CLIENT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "protocol.h"

#define MSG_WORLD_INIT      0x01
#define MSG_OBJECT_CREATE   0x02
#define MSG_OBJECT_UPDATE   0x03
#define MSG_OBJECT_DESTROY  0x04
#define MSG_CONNECTORS      0x05
#define MSG_EXPLOSION       0x06
#define MSG_OBJECT_UNANCHOR 0x07
#define MSG_ROCKET          0x08
#define MSG_STARTER_TOOLS   0x09
#define MSG_PLAYER_UNSTUCK  0x0A
#define MSG_PLAY_SOUND      0x0B
#define MSG_MUSIC           0x0C
#define MSG_UI              0x0D
#define MSG_PLAYER_JOIN     0x10
#define MSG_PLAYER_UPDATE   0x11
#define MSG_PLAYER_LEAVE    0x12
#define MSG_PLAYER_KICK     0x13
#define MSG_SCRIPT_RUN      0x20
#define MSG_CHAT            0x30
#define MSG_FRIEND          0x40
#define MSG_AUTH            0x80
#define MSG_PLAYER_INPUT    0x81
#define MSG_REMOTE_EVENT    0x82
#define MSG_KEEPALIVE       0x83
#define MSG_DAMAGE          0x21
#define MSG_YOUR_SPAWN      0x22
#define MSG_CLIENT_CAPS     0x23
#define MSG_CAMERA_MODE     0x24
#define MSG_PLAYER_BADGES   0x14
#define MSG_PLAYER_APPEARANCE 0x15
#define MSG_PLAYER_VISUAL   0x16
#define MSG_OBJECT_COLLIDE  0x17
#define MSG_SCOREBOARD      0x18
#define MSG_OBJECT_CLICKABLE 0x19
#define MSG_OBJECT_MATERIAL  0x1A
#define MSG_OBJECT_MESH      0x25
#define MSG_OBJECT_DECAL     0x26
#define MSG_PROTOCOL         PW_MSG_PROTOCOL
#define MSG_NET_OWNER        PW_MSG_NET_OWNER
#define MSG_CONSTRAINTS      PW_MSG_CONSTRAINTS
#define MSG_PART_STATE       PW_MSG_PART_STATE
#define MSG_PROTOCOL_ACK     PW_MSG_PROTOCOL_ACK
#define MSG_OWNED_POSE       PW_MSG_OWNED_POSE
#define MSG_PART_QUERY       PW_MSG_PART_QUERY
#define MSG_CONN_BREAK       PW_MSG_CONN_BREAK
#define MSG_WALK_HIT         PW_MSG_WALK_HIT
#define MSG_VR               PW_MSG_VR
#define MSG_APPEARANCE_UPDATE 0x84

#define BADGE_VERIFIED  0x01
#define BADGE_CREATOR   0x02
#define BADGE_STAFF     0x04
#define BADGE_TESTER    0x08

#define CAM_MODE_NORMAL  0
#define CAM_MODE_FREECAM 1
#define CAM_MODE_FREEFLY 2
#define CLIENT_CAP_FREECAM 0x01
#define CLIENT_CAP_RESET_DISABLED 0x02

typedef enum {
    NET_STATE_DISCONNECTED,
    NET_STATE_CONNECTING,
    NET_STATE_CONNECTED,
} NetClientState;

typedef struct {
    NetClientState state;
    uint8_t recv_buf[524288];
    size_t recv_len;
    int sock_fd;

    uint8_t last_pos_buf[16];
    size_t last_pos_len;
    double last_send_time;

#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
    void* keepalive_thread;
    int keepalive_running;
#endif
} NetClient;

bool net_client_connect(NetClient* nc, const char* host, int port);
void net_client_disconnect(NetClient* nc);

void net_client_poll(NetClient* nc);

void net_client_send(NetClient* nc, uint8_t msg_type, const uint8_t* data, size_t len);

uint8_t net_client_recv(NetClient* nc, uint8_t* buf, size_t* out_len);

void net_client_send_auth(NetClient* nc, int game_id, const char* username);

void net_client_send_auth_ticket(NetClient* nc, int game_id, const char* ticket);

void net_client_send_position(NetClient* nc, float x, float y, float z, float yaw);

#endif
