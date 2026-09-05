/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: protocol.h                                                                          |
|   Purpose: shared client/server protocol. KEEP IN SYNC.                                     |
\*-------------------------------------------------------------------------------------------*/

#ifndef PW_PROTOCOL_H
#define PW_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PW_PROTO_LEGACY      1u
#define PW_PROTO_NETOWN      2u
#define PW_PROTO_CURRENT     PW_PROTO_NETOWN
#define PW_PROTO_LEGACY_VER  "26.3.7"

#define PW_MSG_PROTOCOL      0x1B

#define PW_MSG_NET_OWNER     0x1C

#define PW_MSG_CONSTRAINTS   0x1D

#define PW_MSG_PART_STATE    0x1E

#define PW_MSG_PROTOCOL_ACK  0x85

#define PW_MSG_OWNED_POSE    0x86

#define PW_MSG_PART_QUERY    0x87

#define PW_MSG_CONN_BREAK    0x88

#define PW_MSG_WALK_HIT      0x89

#define PW_MSG_VR            0x1F

#define PW_PROTO_ACK_FLAG_NO_NETOWN 0x01u

#define PW_VR_STUD_METERS    0.28f

#define PW_VR_FLAG_ACTIVE    0x01u
#define PW_VR_FLAG_HEAD      0x02u
#define PW_VR_FLAG_LHAND     0x04u
#define PW_VR_FLAG_RHAND     0x08u
#define PW_VR_FLAG_CALIB     0x10u
#define PW_VR_FLAG_IK        0x20u

#define PW_VR_TRACKER_BYTES  28u
#define PW_VR_CALIB_BYTES    8u
#define PW_VR_BODY_MAX       (1u + 3u * PW_VR_TRACKER_BYTES + PW_VR_CALIB_BYTES)

typedef struct {
    float x, y, z;
    float qx, qy, qz, qw;
} PwVrTracker;

typedef struct {
    uint8_t flags;
    PwVrTracker head, lhand, rhand;
    float height_m;
    float arm_span_m;
} PwVrPose;

static inline float pw_metres_to_studs(float metres) {
    return metres / PW_VR_STUD_METERS;
}

static inline float pw_studs_to_metres(float studs) {
    return studs * PW_VR_STUD_METERS;
}

static inline size_t pw_vr_pose_body_len(uint8_t flags) {
    size_t n = 1u;
    if (flags & PW_VR_FLAG_HEAD) n += PW_VR_TRACKER_BYTES;
    if (flags & PW_VR_FLAG_LHAND) n += PW_VR_TRACKER_BYTES;
    if (flags & PW_VR_FLAG_RHAND) n += PW_VR_TRACKER_BYTES;
    if (flags & PW_VR_FLAG_CALIB) n += PW_VR_CALIB_BYTES;
    return n;
}

static inline void pw_vr_write_tracker(uint8_t* dst, const PwVrTracker* t) {
    memcpy(dst, &t->x, 4);
    memcpy(dst + 4, &t->y, 4);
    memcpy(dst + 8, &t->z, 4);
    memcpy(dst + 12, &t->qx, 4);
    memcpy(dst + 16, &t->qy, 4);
    memcpy(dst + 20, &t->qz, 4);
    memcpy(dst + 24, &t->qw, 4);
}

static inline void pw_vr_read_tracker(const uint8_t* src, PwVrTracker* t) {
    memcpy(&t->x, src, 4);
    memcpy(&t->y, src + 4, 4);
    memcpy(&t->z, src + 8, 4);
    memcpy(&t->qx, src + 12, 4);
    memcpy(&t->qy, src + 16, 4);
    memcpy(&t->qz, src + 20, 4);
    memcpy(&t->qw, src + 24, 4);
}

static inline size_t pw_vr_pack_pose(uint8_t* dst, size_t cap, const PwVrPose* p) {
    if (!dst || !p) return 0;
    size_t need = pw_vr_pose_body_len(p->flags);
    if (need > cap || need > PW_VR_BODY_MAX) return 0;
    size_t o = 0;
    dst[o++] = p->flags;
    if (p->flags & PW_VR_FLAG_HEAD) {
        pw_vr_write_tracker(dst + o, &p->head);
        o += PW_VR_TRACKER_BYTES;
    }
    if (p->flags & PW_VR_FLAG_LHAND) {
        pw_vr_write_tracker(dst + o, &p->lhand);
        o += PW_VR_TRACKER_BYTES;
    }
    if (p->flags & PW_VR_FLAG_RHAND) {
        pw_vr_write_tracker(dst + o, &p->rhand);
        o += PW_VR_TRACKER_BYTES;
    }
    if (p->flags & PW_VR_FLAG_CALIB) {
        memcpy(dst + o, &p->height_m, 4);
        memcpy(dst + o + 4, &p->arm_span_m, 4);
        o += PW_VR_CALIB_BYTES;
    }
    return o;
}

static inline size_t pw_vr_unpack_pose(const uint8_t* src, size_t len, PwVrPose* p) {
    if (!src || !p || len < 1u) return 0;
    memset(p, 0, sizeof(*p));
    p->head.qw = p->lhand.qw = p->rhand.qw = 1.0f;
    p->flags = src[0];
    size_t need = pw_vr_pose_body_len(p->flags);
    if (need < 1u || need > len) return 0;
    size_t o = 1;
    if (p->flags & PW_VR_FLAG_HEAD) {
        pw_vr_read_tracker(src + o, &p->head);
        o += PW_VR_TRACKER_BYTES;
    }
    if (p->flags & PW_VR_FLAG_LHAND) {
        pw_vr_read_tracker(src + o, &p->lhand);
        o += PW_VR_TRACKER_BYTES;
    }
    if (p->flags & PW_VR_FLAG_RHAND) {
        pw_vr_read_tracker(src + o, &p->rhand);
        o += PW_VR_TRACKER_BYTES;
    }
    if (p->flags & PW_VR_FLAG_CALIB) {
        memcpy(&p->height_m, src + o, 4);
        memcpy(&p->arm_span_m, src + o + 4, 4);
        o += PW_VR_CALIB_BYTES;
    }
    return o;
}

static inline size_t pw_vr_pack_sc(uint8_t* dst, size_t cap, uint32_t pid, const PwVrPose* p) {
    if (!dst || cap < 5u) return 0;
    memcpy(dst, &pid, 4);
    size_t n = pw_vr_pack_pose(dst + 4, cap - 4, p);
    return n ? (4 + n) : 0;
}

static inline size_t pw_vr_unpack_sc(const uint8_t* src, size_t len, uint32_t* pid, PwVrPose* p) {
    if (!src || !pid || !p || len < 5u) return 0;
    memcpy(pid, src, 4);
    size_t n = pw_vr_unpack_pose(src + 4, len - 4, p);
    return n ? (4 + n) : 0;
}

#define PW_PART_STATUS_DELETED  0u
#define PW_PART_STATUS_ALIVE    1u

#define PW_PART_STATUS_ALIVE_VEL 2u

#define PW_PART_STATUS_CORRECT  3u
#define PW_PART_QUERY_MAX       24u
#define PW_CONN_BREAK_MAX       32u

#define PW_PART_STATE_ALIVE_BYTES 84u

#define PW_CONSTRAINT_WIRE_SIZE 58
#define PW_OWNED_POSE_WIRE      52

#define PW_CONSTRAINT_MSG_MAX   ((65535u - 2u) / PW_CONSTRAINT_WIRE_SIZE)
#define PW_NETOWN_MSG_MAX       ((65535u - 2u) / 8u)

#define PW_NETOWN_RADIUS    32.0f
#define PW_NETOWN_KEEP      48.0f
#define PW_NETOWN_MAX_SPEED 80.0f
#define PW_NETOWN_MAX_WARP  80.0f
#define PW_NETOWN_MAX_VEL   120.0f
#define PW_NETOWN_SCRIPT_LOCK_MS 500u

#define PW_NETOWN_MAX_ASM_PARTS 20
#define PW_NETOWN_MAX_ASM_SPAN  48.0f
#define PW_NETOWN_MAX_ASM_VOL   800.0f

#define PW_FALL_WIND          24.0f
#define PW_FALL_TERMINAL      42.0f

static inline float pw_part_mass(float sx, float sy, float sz) {
    float v = fabsf(sx * sy * sz);
    if (v < 0.25f) v = 0.25f;
    if (v > 250.0f) v = 250.0f;
    return v;
}

static inline void pw_fall_params(float mass, float* wind, float* term) {
    if (mass < 0.25f) mass = 0.25f;
    if (mass > 250.0f) mass = 250.0f;
    float mk = powf(mass, 1.0f / 3.0f);
    float w = PW_FALL_WIND / mk;
    float t = PW_FALL_TERMINAL * sqrtf(mk);
    if (w < 8.0f) w = 8.0f;
    if (w > 52.0f) w = 52.0f;
    if (t < 20.0f) t = 20.0f;
    if (t > 95.0f) t = 95.0f;
    if (wind) *wind = w;
    if (term) *term = t;
}

static inline void pw_clamp_fall_vel(float mass, float* vx, float* vy, float* vz) {
    float term = 42.0f;
    pw_fall_params(mass, NULL, &term);
    if (vy && *vy < -term) *vy = -term;
    if (!vx || !vz) return;
    float hs2 = (*vx) * (*vx) + (*vz) * (*vz);
    float hmax = term * 1.35f;
    if (hs2 > hmax * hmax && hs2 > 0.0f) {
        float s = hmax / sqrtf(hs2);
        *vx *= s;
        *vz *= s;
    }
}

#ifdef __cplusplus
}
#endif

#endif
