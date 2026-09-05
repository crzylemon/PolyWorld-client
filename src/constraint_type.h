/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: constraint_type.h                                                                   |
|   Purpose: welds + joints between parts                                                     |
\*-------------------------------------------------------------------------------------------*/

#ifndef CONSTRAINT_TYPE_H
#define CONSTRAINT_TYPE_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CONSTRAINT_WELD = 0,
    CONSTRAINT_HINGE = 1,
    CONSTRAINT_BALL = 2,
    CONSTRAINT_SLIDER = 3,
    CONSTRAINT_ROD = 4,
    CONSTRAINT_SPRING = 5,
    CONSTRAINT_ROPE = 6,
    CONSTRAINT_NOCOLLIDE = 7,
    CONSTRAINT_TYPE_COUNT
} ConstraintType;

static inline int constraint_is_weld(uint8_t t) {
    return t == (uint8_t)CONSTRAINT_WELD;
}

static inline int constraint_is_nocollide(uint8_t t) {
    return t == (uint8_t)CONSTRAINT_NOCOLLIDE;
}

static inline int constraint_has_joint(uint8_t t) {
    return t != (uint8_t)CONSTRAINT_NOCOLLIDE;
}

static inline int constraint_uses_pivot(uint8_t t) {
    return t == (uint8_t)CONSTRAINT_HINGE ||
           t == (uint8_t)CONSTRAINT_BALL ||
           t == (uint8_t)CONSTRAINT_SLIDER;
}

static inline int constraint_disables_collision(uint8_t t) {
    return t == (uint8_t)CONSTRAINT_NOCOLLIDE ||
           t == (uint8_t)CONSTRAINT_HINGE ||
           t == (uint8_t)CONSTRAINT_BALL ||
           t == (uint8_t)CONSTRAINT_SLIDER;
}

static inline const char* constraint_type_name(uint8_t t) {
    switch (t) {
        case CONSTRAINT_HINGE:     return "Hinge";
        case CONSTRAINT_BALL:      return "Ball";
        case CONSTRAINT_SLIDER:    return "Slider";
        case CONSTRAINT_ROD:       return "Rod";
        case CONSTRAINT_SPRING:    return "Spring";
        case CONSTRAINT_ROPE:      return "Rope";
        case CONSTRAINT_NOCOLLIDE: return "NoCollide";
        default:                   return "Weld";
    }
}

static inline const char* constraint_type_xml(uint8_t t) {
    switch (t) {
        case CONSTRAINT_HINGE:     return "hinge";
        case CONSTRAINT_BALL:      return "ball";
        case CONSTRAINT_SLIDER:    return "slider";
        case CONSTRAINT_ROD:       return "rod";
        case CONSTRAINT_SPRING:    return "spring";
        case CONSTRAINT_ROPE:      return "rope";
        case CONSTRAINT_NOCOLLIDE: return "nocollide";
        default:                   return "weld";
    }
}

static inline uint8_t constraint_type_from_xml(const char* s) {
    if (!s) return CONSTRAINT_WELD;
    if (strncmp(s, "hinge", 5) == 0) return CONSTRAINT_HINGE;
    if (strncmp(s, "ball", 4) == 0) return CONSTRAINT_BALL;
    if (strncmp(s, "slider", 6) == 0) return CONSTRAINT_SLIDER;
    if (strncmp(s, "rod", 3) == 0) return CONSTRAINT_ROD;
    if (strncmp(s, "spring", 6) == 0) return CONSTRAINT_SPRING;
    if (strncmp(s, "rope", 4) == 0) return CONSTRAINT_ROPE;
    if (strncmp(s, "nocollide", 9) == 0 || strncmp(s, "nocollision", 11) == 0)
        return CONSTRAINT_NOCOLLIDE;
    return CONSTRAINT_WELD;
}

static inline uint8_t constraint_type_from_open_tag(const char* open, const char* gt) {
    if (!open || !gt || gt <= open) return CONSTRAINT_WELD;
    for (const char* t = open; t + 6 < gt; t++) {
        if (strncmp(t, "type=\"", 6) == 0)
            return constraint_type_from_xml(t + 6);
    }
    return CONSTRAINT_WELD;
}

static inline void constraint_type_color255(uint8_t t, float* r, float* g, float* b) {
    switch (t) {
        case CONSTRAINT_HINGE:     *r = 255.f; *g = 140.f; *b = 20.f;  break;
        case CONSTRAINT_BALL:      *r = 40.f;  *g = 220.f; *b = 255.f; break;
        case CONSTRAINT_SLIDER:    *r = 70.f;  *g = 120.f; *b = 255.f; break;
        case CONSTRAINT_ROD:       *r = 255.f; *g = 220.f; *b = 40.f;  break;
        case CONSTRAINT_SPRING:    *r = 220.f; *g = 80.f;  *b = 255.f; break;
        case CONSTRAINT_ROPE:      *r = 230.f; *g = 230.f; *b = 230.f; break;
        case CONSTRAINT_NOCOLLIDE: *r = 255.f; *g = 70.f;  *b = 70.f;  break;
        default:                   *r = 0.f;   *g = 255.f; *b = 0.f;   break;
    }
}

#ifdef __cplusplus
}
#endif

#endif
