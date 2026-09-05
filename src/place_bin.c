/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: place_bin.c                                                                         |
|   Purpose: PWBF encoder/decoder                                                             |
\*-------------------------------------------------------------------------------------------*/

#include "place_bin.h"
#include <stdlib.h>
#include <string.h>

#define FOURCC(a,b,c,d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

static int grow(uint8_t** buf, size_t* off, size_t* cap, size_t add) {
    size_t need = *off + add;
    if (need <= *cap) return 0;
    size_t ncap = *cap ? *cap * 2 : 256;
    while (ncap < need) ncap *= 2;
    uint8_t* n = (uint8_t*)realloc(*buf, ncap);
    if (!n) return -1;
    *buf = n;
    *cap = ncap;
    return 0;
}

static int write_u16(uint8_t** buf, size_t* off, size_t* cap, uint16_t v) {
    if (grow(buf, off, cap, 2)) return -1;
    memcpy(*buf + *off, &v, 2);
    *off += 2;
    return 0;
}

static int write_u32(uint8_t** buf, size_t* off, size_t* cap, uint32_t v) {
    if (grow(buf, off, cap, 4)) return -1;
    memcpy(*buf + *off, &v, 4);
    *off += 4;
    return 0;
}

static int write_bytes(uint8_t** buf, size_t* off, size_t* cap, const void* src, size_t n) {
    if (grow(buf, off, cap, n)) return -1;
    if (n) memcpy(*buf + *off, src, n);
    *off += n;
    return 0;
}

static void pack_part(uint8_t rec[PLACE_BIN_PART_REC], const PlaceBinPart* p) {
    memset(rec, 0, PLACE_BIN_PART_REC);
    uint32_t flags = (uint32_t)(p->shape & 7u)
        | ((uint32_t)(p->material & 31u) << 3)
        | ((uint32_t)(p->anchored ? 1u : 0u) << 8)
        | ((uint32_t)(p->can_collide ? 1u : 0u) << 9);
    memcpy(rec + 0, &flags, 4);
    memcpy(rec + 4, p->pos, 12);
    memcpy(rec + 16, p->rot, 12);
    memcpy(rec + 28, p->size, 12);
    memcpy(rec + 40, p->color, 12);
    memcpy(rec + 52, &p->glow, 4);
    memcpy(rec + 56, &p->alpha, 4);
    memcpy(rec + 60, p->surfaces, 6);
    memcpy(rec + 72, p->name, 32);
    rec[103] = '\0';
}

static void unpack_part(const uint8_t rec[PLACE_BIN_PART_REC], PlaceBinPart* p) {
    memset(p, 0, sizeof(*p));
    uint32_t flags = 0;
    memcpy(&flags, rec, 4);
    p->shape = (uint8_t)(flags & 7u);
    p->material = (uint8_t)((flags >> 3) & 31u);
    p->anchored = (uint8_t)((flags >> 8) & 1u);
    p->can_collide = (uint8_t)((flags >> 9) & 1u);
    memcpy(p->pos, rec + 4, 12);
    memcpy(p->rot, rec + 16, 12);
    memcpy(p->size, rec + 28, 12);
    memcpy(p->color, rec + 40, 12);
    memcpy(&p->glow, rec + 52, 4);
    memcpy(&p->alpha, rec + 56, 4);
    memcpy(p->surfaces, rec + 60, 6);
    memcpy(p->name, rec + 72, 32);
    p->name[31] = '\0';
}

int place_bin_encode(const PlaceBin* src, uint8_t** out, size_t* out_len) {
    if (!src || !out || !out_len) return -1;
    uint8_t* buf = NULL;
    size_t off = 0, cap = 0;
    if (write_u32(&buf, &off, &cap, PLACE_BIN_MAGIC) ||
        write_u16(&buf, &off, &cap, PLACE_BIN_VERSION) ||
        write_u16(&buf, &off, &cap, 0)) {
        free(buf);
        return -1;
    }

    uint8_t head[72];
    memset(head, 0, sizeof(head));
    memcpy(head, src->title, 63);
    memcpy(head + 64, &src->flags, 4);
    if (write_u32(&buf, &off, &cap, FOURCC('H','E','A','D')) ||
        write_u32(&buf, &off, &cap, 72) ||
        write_bytes(&buf, &off, &cap, head, 72)) {
        free(buf);
        return -1;
    }

    uint32_t n = src->part_count;
    uint32_t part_bytes = 4u + n * PLACE_BIN_PART_REC;
    if (write_u32(&buf, &off, &cap, FOURCC('P','A','R','T')) ||
        write_u32(&buf, &off, &cap, part_bytes) ||
        write_u32(&buf, &off, &cap, n)) {
        free(buf);
        return -1;
    }
    for (uint32_t i = 0; i < n; i++) {
        uint8_t rec[PLACE_BIN_PART_REC];
        pack_part(rec, &src->parts[i]);
        if (write_bytes(&buf, &off, &cap, rec, PLACE_BIN_PART_REC)) {
            free(buf);
            return -1;
        }
    }

    if (write_u32(&buf, &off, &cap, FOURCC('E','N','D','!')) ||
        write_u32(&buf, &off, &cap, 0)) {
        free(buf);
        return -1;
    }
    *out = buf;
    *out_len = off;
    return 0;
}

int place_bin_decode(const uint8_t* data, size_t len, PlaceBin* dst) {
    if (!data || !dst || len < 8) return -1;
    memset(dst, 0, sizeof(*dst));
    uint32_t magic = 0;
    memcpy(&magic, data, 4);
    if (magic != PLACE_BIN_MAGIC) return -1;
    uint16_t ver = 0;
    memcpy(&ver, data + 4, 2);
    if (ver != PLACE_BIN_VERSION) return -1;

    size_t off = 8;
    while (off + 8 <= len) {
        uint32_t four = 0, sz = 0;
        memcpy(&four, data + off, 4);
        memcpy(&sz, data + off + 4, 4);
        off += 8;
        if (off + sz > len) {
            place_bin_free(dst);
            return -1;
        }
        if (four == FOURCC('E','N','D','!'))
            return 0;
        if (four == FOURCC('H','E','A','D') && sz >= 72) {
            memcpy(dst->title, data + off, 63);
            dst->title[63] = '\0';
            memcpy(&dst->flags, data + off + 64, 4);
        } else if (four == FOURCC('P','A','R','T') && sz >= 4) {
            uint32_t n = 0;
            memcpy(&n, data + off, 4);
            size_t need = (size_t)n * PLACE_BIN_PART_REC;
            if (4 + need > sz) {
                place_bin_free(dst);
                return -1;
            }
            PlaceBinPart* parts = (PlaceBinPart*)calloc(n ? n : 1, sizeof(PlaceBinPart));
            if (!parts) {
                place_bin_free(dst);
                return -1;
            }
            const uint8_t* recs = data + off + 4;
            for (uint32_t i = 0; i < n; i++)
                unpack_part(recs + i * PLACE_BIN_PART_REC, &parts[i]);
            free(dst->parts);
            dst->parts = parts;
            dst->part_count = n;
        }
        off += sz;
    }
    return 0;
}

void place_bin_free(PlaceBin* p) {
    if (!p) return;
    free(p->parts);
    memset(p, 0, sizeof(*p));
}
