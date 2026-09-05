/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: world_loader.c                                                                      |
|   Purpose: place XML -> entities                                                            |
\*-------------------------------------------------------------------------------------------*/

#include "world_loader.h"
#include "part_material.h"
#include "log.h"
#include "mesh_primitives.h"
#include "renderer.h"
#include "texture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char* find_tag(const char* xml, const char* tag, const char* end) {
    char open[64];
    snprintf(open, sizeof(open), "<%s>", tag);
    const char* p = strstr(xml, open);
    if (!p || (end && p >= end)) return NULL;
    return p + strlen(open);
}

static const char* find_tag_end(const char* start, const char* tag) {
    char close[64];
    snprintf(close, sizeof(close), "</%s>", tag);
    return strstr(start, close);
}

static uint8_t parse_part_material_tag(const char* content, const char* obj_end) {
    const char* m = find_tag(content, "material", obj_end);
    return m ? part_material_from_name(m) : PART_MATERIAL_PLASTIC;
}

static float parse_float_tag(const char* xml, const char* tag, const char* end) {
    const char* val = find_tag(xml, tag, end);
    if (!val) return 0.0f;
    return (float)atof(val);
}

static int parse_int_tag(const char* xml, const char* tag, const char* end) {
    const char* val = find_tag(xml, tag, end);
    if (!val) return 0;
    return atoi(val);
}

static SurfaceType parse_surface_type(const char* val) {
    if (!val) return SURFACE_SMOOTH;
    if (strncmp(val, "stud", 4) == 0) return SURFACE_STUD;
    if (strncmp(val, "inlet", 5) == 0) return SURFACE_INLET;
    return SURFACE_SMOOTH;
}

extern bool create_box_mesh(MeshData* out, float hx, float hy, float hz);

static GPUMesh g_unit_box;
static bool g_unit_box_ready = false;
static EntityID g_display_ui_id = ENTITY_INVALID;

static void parse_name_tag(const char* content, const char* end, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    const char* n = find_tag(content, "name", end);
    if (!n) return;
    const char* e = find_tag_end(n, "name");
    if (!e || e <= n) return;
    size_t len = (size_t)(e - n);
    while (len > 0 && (n[len - 1] == ' ' || n[len - 1] == '\n' || n[len - 1] == '\r' || n[len - 1] == '\t'))
        len--;
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, n, len);
    out[len] = '\0';
}

EntityID world_loader_display_ui_id(void) {
    return g_display_ui_id;
}

static GPUMesh* get_unit_box_mesh(void) {
    if (g_unit_box_ready) return &g_unit_box;
    MeshData md;
    memset(&g_unit_box, 0, sizeof(g_unit_box));
    if (!create_box_mesh(&md, 0.5f, 0.5f, 0.5f)) return NULL;
    if (!mesh_upload(&md, &g_unit_box)) {
        mesh_data_free(&md);
        return NULL;
    }
    mesh_data_free(&md);
    g_unit_box_ready = true;
    return &g_unit_box;
}

void world_loader_invalidate_unit_meshes(void) {

    memset(&g_unit_box, 0, sizeof(g_unit_box));
    g_unit_box_ready = false;
    g_display_ui_id = ENTITY_INVALID;
}

int world_load_from_xml(const char* xml_data, size_t len,
                        Scene* scene, PhysicsWorld* physics,
                        void* renderer) {
    (void)renderer;
    if (!xml_data || len == 0 || !scene || !physics) return -1;

    uint32_t entity_start = scene->count;
    g_display_ui_id = ENTITY_INVALID;

    int brick_count = 0;
    const char* end_of_xml = xml_data + len;

    const char* cursor = xml_data;
    while (cursor && cursor < end_of_xml) {
        const char* brick_start = strstr(cursor, "<object type=\"brick\">");
        if (!brick_start || brick_start >= end_of_xml) break;

        const char* brick_content = brick_start + strlen("<object type=\"brick\">");
        const char* brick_end = strstr(brick_content, "</object>");
        if (!brick_end) break;

        float size_w = parse_float_tag(brick_content, "w", brick_end);
        float size_h = parse_float_tag(brick_content, "h", brick_end);
        float size_l = parse_float_tag(brick_content, "l", brick_end);

        float pos_x = parse_float_tag(brick_content, "x", brick_end);
        float pos_y = parse_float_tag(brick_content, "y", brick_end);
        float pos_z = parse_float_tag(brick_content, "z", brick_end);

        int color_r = parse_int_tag(brick_content, "r", brick_end);
        int color_g = parse_int_tag(brick_content, "g", brick_end);
        int color_b = parse_int_tag(brick_content, "b", brick_end);

        bool anchored = false;
        const char* anch = find_tag(brick_content, "anchored", brick_end);
        if (anch && strncmp(anch, "true", 4) == 0) anchored = true;

        bool invisible = false;
        const char* invis = find_tag(brick_content, "invisible", brick_end);
        if (invis && strncmp(invis, "true", 4) == 0) invisible = true;

        bool can_collide = !invisible;
        const char* cc = find_tag(brick_content, "cancollide", brick_end);
        if (cc) {
            if (strncmp(cc, "true", 4) == 0) can_collide = true;
            else if (strncmp(cc, "false", 5) == 0) can_collide = false;
        }

        if (invisible && !can_collide) {
            cursor = brick_end + strlen("</object>");
            continue;
        }

        SurfaceType surfaces[6] = { SURFACE_SMOOTH };
        const char* surf_start = find_tag(brick_content, "surface", brick_end);
        if (surf_start) {
            const char* surf_end = find_tag_end(surf_start, "surface");
            const char* top_val = find_tag(surf_start, "top", surf_end);
            const char* bot_val = find_tag(surf_start, "bottom", surf_end);
            const char* front_val = find_tag(surf_start, "front", surf_end);
            const char* back_val = find_tag(surf_start, "back", surf_end);
            const char* left_val = find_tag(surf_start, "left", surf_end);
            const char* right_val = find_tag(surf_start, "right", surf_end);
            surfaces[0] = parse_surface_type(top_val);
            surfaces[1] = parse_surface_type(bot_val);
            surfaces[2] = parse_surface_type(front_val);
            surfaces[3] = parse_surface_type(back_val);
            surfaces[4] = parse_surface_type(left_val);
            surfaces[5] = parse_surface_type(right_val);
        }

        float hx = size_w * 0.5f;
        float hy = size_h * 0.5f;
        float hz = size_l * 0.5f;

        if (hx < 0.01f || hy < 0.01f || hz < 0.01f) {
            cursor = brick_end + strlen("</object>");
            continue;
        }

        EntityID eid = scene_create_entity(scene);
        Entity* ent = scene_get_entity(scene, eid);
        if (!ent) {
            cursor = brick_end + strlen("</object>");
            continue;
        }

        ent->transform.position = (Vec3){ pos_x, pos_y, pos_z };
        ent->transform.scale = (Vec3){ size_w, size_h, size_l };

        const char* rot_start = find_tag(brick_content, "rotation", brick_end);
        if (rot_start) {
            const char* rot_end = find_tag_end(rot_start, "rotation");
            if (rot_end) {
                ent->transform.rotation.x = parse_float_tag(rot_start, "x", rot_end);
                ent->transform.rotation.y = parse_float_tag(rot_start, "y", rot_end);
                ent->transform.rotation.z = parse_float_tag(rot_start, "z", rot_end);
            }
        }

        ent->material.color = (Vec3){
            color_r / 255.0f,
            color_g / 255.0f,
            color_b / 255.0f
        };
        {
            const char* glow_tag = find_tag(brick_content, "glow", brick_end);
            if (glow_tag) {
                float g = (float)atof(glow_tag);
                if (g < 0.0f) g = 0.0f;
                if (g > 1.0f) g = 1.0f;
                ent->material.glow = g;
            }
            ent->material.alpha = 1.0f;
            const char* alpha_tag = find_tag(brick_content, "alpha", brick_end);
            if (alpha_tag) {
                float a = (float)atof(alpha_tag);
                if (a < 0.0f) a = 0.0f;
                if (a > 1.0f) a = 1.0f;
                ent->material.alpha = a;
            }
        }
        ent->material.part_material = parse_part_material_tag(brick_content, brick_end);

        for (int i = 0; i < 6; i++) {
            ent->material.surfaces[i] = surfaces[i];
        }

        if (!invisible) {
            GPUMesh* mesh = get_unit_box_mesh();
            if (mesh) ent->mesh = mesh;
        }
        ent->static_batch = anchored && !invisible;
        {
            char bname[64];
            parse_name_tag(brick_content, brick_end, bname, sizeof(bname));
            if (strcmp(bname, "DISPLAY_UI") == 0) {
                g_display_ui_id = eid;
                ent->static_batch = false;
            }
        }

        if (can_collide) {
            BodyDesc desc = {
                .type = anchored ? BODY_STATIC : BODY_DYNAMIC,
                .collider = COLLIDER_BOX,
                .position = { pos_x, pos_y, pos_z },
                .half_extents = { hx, hy, hz },
                .mass = anchored ? 0.0f : (size_w * size_h * size_l * 2.0f),
                .restitution = 0.3f,
                .friction = 0.8f
            };
            ent->physics_body = physics_create_body(physics, &desc);

            if (ent->transform.rotation.x != 0.0f || ent->transform.rotation.y != 0.0f || ent->transform.rotation.z != 0.0f) {
                physics_set_rotation_euler(physics, ent->physics_body, ent->transform.rotation);
            }
        }

        brick_count++;
        cursor = brick_end + strlen("</object>");
    }

    cursor = xml_data;
    while (cursor && cursor < end_of_xml) {
        const char* start = strstr(cursor, "<object type=\"sphere-brick\">");
        if (!start || start >= end_of_xml) break;
        const char* content = start + strlen("<object type=\"sphere-brick\">");
        const char* obj_end = strstr(content, "</object>");
        if (!obj_end) break;

        float radius = parse_float_tag(content, "r", obj_end);
        float pos_x = parse_float_tag(content, "x", obj_end);
        float pos_y = parse_float_tag(content, "y", obj_end);
        float pos_z = parse_float_tag(content, "z", obj_end);
        int cr = parse_int_tag(content, "r", obj_end);
        int cg = parse_int_tag(content, "g", obj_end);
        int cb = parse_int_tag(content, "b", obj_end);

        const char* size_tag = find_tag(content, "size", obj_end);
        if (size_tag) {
            const char* size_end = strstr(size_tag, "</size>");
            if (size_end) radius = parse_float_tag(size_tag, "r", size_end);
        }
        const char* color_tag = find_tag(content, "color", obj_end);
        if (color_tag) {
            const char* color_end = strstr(color_tag, "</color>");
            if (color_end) {
                cr = parse_int_tag(color_tag, "r", color_end);
                cg = parse_int_tag(color_tag, "g", color_end);
                cb = parse_int_tag(color_tag, "b", color_end);
            }
        }
        const char* pos_tag = find_tag(content, "position", obj_end);
        if (pos_tag) {
            const char* pos_end = strstr(pos_tag, "</position>");
            if (pos_end) {
                pos_x = parse_float_tag(pos_tag, "x", pos_end);
                pos_y = parse_float_tag(pos_tag, "y", pos_end);
                pos_z = parse_float_tag(pos_tag, "z", pos_end);
            }
        }

        if (radius > 0.01f) {
            EntityID eid = scene_create_entity(scene);
            Entity* ent = scene_get_entity(scene, eid);
            if (ent) {
                ent->transform.position = (Vec3){ pos_x, pos_y, pos_z };
                ent->transform.scale = (Vec3){ radius * 2.0f, radius * 2.0f, radius * 2.0f };
                ent->material.color = (Vec3){ cr/255.0f, cg/255.0f, cb/255.0f };
                {
                    const char* glow_tag = find_tag(content, "glow", obj_end);
                    if (glow_tag) {
                        float g = (float)atof(glow_tag);
                        if (g < 0.0f) g = 0.0f;
                        if (g > 1.0f) g = 1.0f;
                        ent->material.glow = g;
                    }
                    ent->material.alpha = 1.0f;
                    const char* alpha_tag = find_tag(content, "alpha", obj_end);
                    if (alpha_tag) {
                        float a = (float)atof(alpha_tag);
                        if (a < 0.0f) a = 0.0f;
                        if (a > 1.0f) a = 1.0f;
                        ent->material.alpha = a;
                    }
                }
                ent->material.part_material = parse_part_material_tag(content, obj_end);

                ent->mesh = renderer_unit_curve_mesh(1, 2);

                BodyDesc desc = {
                    .type = BODY_DYNAMIC, .collider = COLLIDER_SPHERE,
                    .position = {pos_x, pos_y, pos_z}, .radius = radius,
                    .mass = (4.0f/3.0f) * 3.14159f * radius*radius*radius * 2.0f,
                    .restitution = 0.5f, .friction = 0.8f };

                const char* anch_tag = find_tag(content, "anchored", obj_end);
                bool is_anchored = (anch_tag && strncmp(anch_tag, "true", 4) == 0);
                if (is_anchored) {
                    desc.type = BODY_STATIC;
                    desc.mass = 0.0f;
                }

                ent->physics_body = physics_create_body(physics, &desc);
                brick_count++;
            }
        }
        cursor = obj_end + strlen("</object>");
    }

    cursor = xml_data;
    while (cursor && cursor < end_of_xml) {
        const char* start = strstr(cursor, "<object type=\"cylinder-brick\">");
        if (!start || start >= end_of_xml) break;
        const char* content = start + strlen("<object type=\"cylinder-brick\">");
        const char* obj_end = strstr(content, "</object>");
        if (!obj_end) break;

        float cyl_radius = 1.0f, cyl_length = 2.0f;
        float pos_x = 0, pos_y = 0, pos_z = 0;
        int cr = 128, cg = 128, cb = 128;

        const char* size_tag = find_tag(content, "size", obj_end);
        if (size_tag) {
            const char* size_end = strstr(size_tag, "</size>");
            if (size_end) {
                cyl_radius = parse_float_tag(size_tag, "r", size_end);
                cyl_length = parse_float_tag(size_tag, "l", size_end);
            }
        }
        const char* pos_tag = find_tag(content, "position", obj_end);
        if (pos_tag) {
            const char* pos_end = strstr(pos_tag, "</position>");
            if (pos_end) {
                pos_x = parse_float_tag(pos_tag, "x", pos_end);
                pos_y = parse_float_tag(pos_tag, "y", pos_end);
                pos_z = parse_float_tag(pos_tag, "z", pos_end);
            }
        }
        const char* color_tag = find_tag(content, "color", obj_end);
        if (color_tag) {
            const char* color_end = strstr(color_tag, "</color>");
            if (color_end) {
                cr = parse_int_tag(color_tag, "r", color_end);
                cg = parse_int_tag(color_tag, "g", color_end);
                cb = parse_int_tag(color_tag, "b", color_end);
            }
        }

        if (cyl_radius > 0.01f && cyl_length > 0.01f) {
            EntityID eid = scene_create_entity(scene);
            Entity* ent = scene_get_entity(scene, eid);
            if (ent) {
                ent->transform.position = (Vec3){ pos_x, pos_y, pos_z };
                ent->transform.scale = (Vec3){ cyl_radius * 2.0f, cyl_length, cyl_radius * 2.0f };
                ent->material.color = (Vec3){ cr/255.0f, cg/255.0f, cb/255.0f };
                {
                    const char* glow_tag = find_tag(content, "glow", obj_end);
                    if (glow_tag) {
                        float g = (float)atof(glow_tag);
                        if (g < 0.0f) g = 0.0f;
                        if (g > 1.0f) g = 1.0f;
                        ent->material.glow = g;
                    }
                    ent->material.alpha = 1.0f;
                    const char* alpha_tag = find_tag(content, "alpha", obj_end);
                    if (alpha_tag) {
                        float a = (float)atof(alpha_tag);
                        if (a < 0.0f) a = 0.0f;
                        if (a > 1.0f) a = 1.0f;
                        ent->material.alpha = a;
                    }
                }
                ent->material.part_material = parse_part_material_tag(content, obj_end);

                ent->mesh = renderer_unit_curve_mesh(2, 2);

                BodyDesc desc = { .type = BODY_DYNAMIC, .collider = COLLIDER_CYLINDER,
                    .position = {pos_x, pos_y, pos_z},
                    .half_extents = {cyl_radius, cyl_length*0.5f, cyl_radius},
                    .radius = cyl_radius,
                    .mass = 3.14159f * cyl_radius*cyl_radius * cyl_length * 2.0f,
                    .restitution = 0.3f, .friction = 0.8f };

                const char* anch_tag2 = find_tag(content, "anchored", obj_end);
                bool is_anchored2 = (anch_tag2 && strncmp(anch_tag2, "true", 4) == 0);
                if (is_anchored2) { desc.type = BODY_STATIC; desc.mass = 0.0f; }

                ent->physics_body = physics_create_body(physics, &desc);
                brick_count++;
            }
        }
        cursor = obj_end + strlen("</object>");
    }

    cursor = xml_data;
    while (cursor && cursor < end_of_xml) {
        const char* start = strstr(cursor, "<object type=\"wedge-brick\">");
        if (!start || start >= end_of_xml) break;
        const char* content = start + strlen("<object type=\"wedge-brick\">");
        const char* obj_end = strstr(content, "</object>");
        if (!obj_end) break;

        float sw = 4.0f, sh = 1.0f, sl = 2.0f;
        float pos_x = 0, pos_y = 0, pos_z = 0;
        float rot_x = 0, rot_y = 0, rot_z = 0;
        int cr = 128, cg = 128, cb = 128;

        const char* size_tag = find_tag(content, "size", obj_end);
        if (size_tag) {
            const char* size_end = strstr(size_tag, "</size>");
            if (size_end) {
                sw = parse_float_tag(size_tag, "w", size_end);
                sh = parse_float_tag(size_tag, "h", size_end);
                sl = parse_float_tag(size_tag, "l", size_end);
            }
        }
        const char* pos_tag = find_tag(content, "position", obj_end);
        if (pos_tag) {
            const char* pos_end = strstr(pos_tag, "</position>");
            if (pos_end) {
                pos_x = parse_float_tag(pos_tag, "x", pos_end);
                pos_y = parse_float_tag(pos_tag, "y", pos_end);
                pos_z = parse_float_tag(pos_tag, "z", pos_end);
            }
        }
        const char* rot_tag = find_tag(content, "rotation", obj_end);
        if (rot_tag) {
            const char* rot_end = strstr(rot_tag, "</rotation>");
            if (rot_end) {
                rot_x = parse_float_tag(rot_tag, "x", rot_end);
                rot_y = parse_float_tag(rot_tag, "y", rot_end);
                rot_z = parse_float_tag(rot_tag, "z", rot_end);
            }
        }
        const char* color_tag = find_tag(content, "color", obj_end);
        if (color_tag) {
            const char* color_end = strstr(color_tag, "</color>");
            if (color_end) {
                cr = parse_int_tag(color_tag, "r", color_end);
                cg = parse_int_tag(color_tag, "g", color_end);
                cb = parse_int_tag(color_tag, "b", color_end);
            }
        }

        if (sw > 0.01f && sh > 0.01f && sl > 0.01f) {
            EntityID eid = scene_create_entity(scene);
            Entity* ent = scene_get_entity(scene, eid);
            if (ent) {
                float hx = sw * 0.5f, hy = sh * 0.5f, hz = sl * 0.5f;
                ent->transform.position = (Vec3){ pos_x, pos_y, pos_z };
                ent->transform.rotation = (Vec3){ rot_x, rot_y, rot_z };
                ent->transform.scale = (Vec3){ 1, 1, 1 };
                ent->material.color = (Vec3){ cr/255.0f, cg/255.0f, cb/255.0f };
                ent->material.alpha = 1.0f;
                ent->material.part_material = parse_part_material_tag(content, obj_end);

                MeshData md;
                if (create_wedge_mesh(&md, hx, hy, hz)) {
                    static GPUMesh wedge_meshes[512];
                    static int wedge_mesh_count = 0;
                    if (wedge_mesh_count < 512) {
                        if (mesh_upload(&md, &wedge_meshes[wedge_mesh_count])) {
                            ent->mesh = &wedge_meshes[wedge_mesh_count];
                            wedge_mesh_count++;
                        }
                    }
                    mesh_data_free(&md);
                }

                BodyDesc desc = {
                    .type = BODY_DYNAMIC,
                    .collider = COLLIDER_HULL,
                    .position = {pos_x, pos_y, pos_z},
                    .half_extents = {hx, hy, hz},
                    .mass = sw * sh * sl,
                    .restitution = 0.3f,
                    .friction = 0.8f,
                    .hull_point_count = 6
                };
                desc.hull_points[0] = (Vec3){ -hx, -hy, -hz };
                desc.hull_points[1] = (Vec3){  hx, -hy, -hz };
                desc.hull_points[2] = (Vec3){ -hx, -hy,  hz };
                desc.hull_points[3] = (Vec3){  hx, -hy,  hz };
                desc.hull_points[4] = (Vec3){ -hx,  hy,  hz };
                desc.hull_points[5] = (Vec3){  hx,  hy,  hz };

                const char* anch_tag = find_tag(content, "anchored", obj_end);
                bool is_anchored = (anch_tag && strncmp(anch_tag, "true", 4) == 0);
                if (is_anchored) { desc.type = BODY_STATIC; desc.mass = 0.0f; }

                ent->physics_body = physics_create_body(physics, &desc);
                if (ent->physics_body && (rot_x != 0.0f || rot_y != 0.0f || rot_z != 0.0f))
                    physics_set_rotation_euler(physics, ent->physics_body, ent->transform.rotation);
                brick_count++;
            }
        }
        cursor = obj_end + strlen("</object>");
    }

    static PhysicsBodyID body_map[MAX_ENTITIES];
    int body_map_count = 0;
    {
        for (uint32_t i = entity_start; i < scene->count && body_map_count < MAX_ENTITIES; i++) {
            Entity* e = &scene->entities[i];
            if (e->active && e->physics_body != 0) {
                body_map[body_map_count++] = e->physics_body;
            }
        }
    }

    #define MAX_CONN_EDGES 8192
    static int conn_edges[MAX_CONN_EDGES][2];
    static ConstraintDesc conn_desc[MAX_CONN_EDGES];
    int edge_count = 0;

    cursor = xml_data;
    int connector_count = 0;
    while (cursor && cursor < end_of_xml) {
        const char* conn_start = strstr(cursor, "<connector");
        if (!conn_start || conn_start >= end_of_xml) break;
        if (conn_start[10] != '>' && conn_start[10] != ' ' &&
            conn_start[10] != '\t' && conn_start[10] != '\n') {
            cursor = conn_start + 10;
            continue;
        }
        const char* gt = strchr(conn_start, '>');
        const char* conn_end = strstr(conn_start, "</connector>");
        if (!gt || !conn_end || conn_end >= end_of_xml) break;
        const char* content = gt + 1;

        int idx_a = parse_int_tag(content, "a", conn_end);
        int idx_b = parse_int_tag(content, "b", conn_end);

        if (idx_a >= 0 && idx_a < body_map_count && idx_b >= 0 && idx_b < body_map_count) {
            if (edge_count < MAX_CONN_EDGES) {
                conn_edges[edge_count][0] = idx_a;
                conn_edges[edge_count][1] = idx_b;
                memset(&conn_desc[edge_count], 0, sizeof(conn_desc[edge_count]));
                conn_desc[edge_count].type = constraint_type_from_open_tag(conn_start, gt);
                const char* ax = find_tag(content, "axis", conn_end);
                if (ax) {
                    const char* axe = strstr(ax, "</axis>");
                    if (axe && axe < conn_end) {
                        conn_desc[edge_count].axis.x = parse_float_tag(ax, "x", axe);
                        conn_desc[edge_count].axis.y = parse_float_tag(ax, "y", axe);
                        conn_desc[edge_count].axis.z = parse_float_tag(ax, "z", axe);
                    }
                }
                const char* pt = find_tag(content, "point", conn_end);
                if (pt) {
                    const char* pte = strstr(pt, "</point>");
                    if (pte && pte < conn_end) {
                        conn_desc[edge_count].point.x = parse_float_tag(pt, "x", pte);
                        conn_desc[edge_count].point.y = parse_float_tag(pt, "y", pte);
                        conn_desc[edge_count].point.z = parse_float_tag(pt, "z", pte);
                        conn_desc[edge_count].point_set = 1;
                    }
                }
                conn_desc[edge_count].limits_min = parse_float_tag(content, "limitsMin", conn_end);
                conn_desc[edge_count].limits_max = parse_float_tag(content, "limitsMax", conn_end);
                conn_desc[edge_count].stiffness = parse_float_tag(content, "stiffness", conn_end);
                conn_desc[edge_count].damping = parse_float_tag(content, "damping", conn_end);
                conn_desc[edge_count].motor = parse_float_tag(content, "motor", conn_end);
                conn_desc[edge_count].torque = parse_float_tag(content, "torque", conn_end);
                edge_count++;
            }
            connector_count++;
        }
        cursor = conn_end + strlen("</connector>");
    }

    if (connector_count > 0 && body_map_count > 0) {

        static int uf[MAX_ENTITIES];
        for (int i = 0; i < body_map_count; i++) uf[i] = i;
        for (int e = 0; e < edge_count; e++) {
            if (!constraint_is_weld(conn_desc[e].type)) continue;
            int a = conn_edges[e][0], b = conn_edges[e][1];
            while (uf[a] != a) { uf[a] = uf[uf[a]]; a = uf[a]; }
            while (uf[b] != b) { uf[b] = uf[uf[b]]; b = uf[b]; }
            if (a != b) uf[a] = b;
        }

        static bool anch_map[MAX_ENTITIES];
        memset(anch_map, 0, sizeof(bool) * body_map_count);
        {
            int bi = 0;
            const char* c2 = xml_data;
            while (c2 && c2 < end_of_xml && bi < body_map_count) {
                const char* next_brick = strstr(c2, "<object type=\"brick\">");
                const char* next_sphere = strstr(c2, "<object type=\"sphere-brick\">");
                const char* next_cyl = strstr(c2, "<object type=\"cylinder-brick\">");
                const char* best = NULL;
                if (next_brick && (!best || next_brick < best)) best = next_brick;
                if (next_sphere && (!best || next_sphere < best)) best = next_sphere;
                if (next_cyl && (!best || next_cyl < best)) best = next_cyl;
                if (!best || best >= end_of_xml) break;
                const char* content2 = strchr(best + 1, '>');
                if (!content2) break;
                content2++;
                const char* obj_end2 = strstr(content2, "</object>");
                if (!obj_end2) break;
                const char* anch = find_tag(content2, "anchored", obj_end2);
                if (anch && strncmp(anch, "true", 4) == 0) anch_map[bi] = true;
                bi++;
                c2 = obj_end2 + strlen("</object>");
            }
        }

        static bool root_has_anchor[MAX_ENTITIES];
        memset(root_has_anchor, 0, sizeof(bool) * body_map_count);
        for (int i = 0; i < body_map_count; i++) {
            if (anch_map[i]) {
                int r = i; while (uf[r] != r) { uf[r] = uf[uf[r]]; r = uf[r]; }
                root_has_anchor[r] = true;
            }
        }

        int made_static = 0;
        for (int i = 0; i < body_map_count; i++) {
            if (anch_map[i]) continue;
            int r = i; while (uf[r] != r) { uf[r] = uf[uf[r]]; r = uf[r]; }
            if (!root_has_anchor[r]) continue;

            PhysicsBodyInfo info = physics_get_body_info(physics, body_map[i]);
            if (!info.active) continue;

            uint32_t ent_idx = entity_start + (uint32_t)i;
            Entity* ent = &scene->entities[ent_idx];
            Vec3 rot = ent->transform.rotation;

            physics_destroy_body(physics, body_map[i]);

            BodyDesc desc = {
                .type = BODY_STATIC,
                .collider = info.collider,
                .position = info.position,
                .half_extents = info.half_extents,
                .radius = info.radius,
                .mass = 0.0f,
                .restitution = 0.3f,
                .friction = 0.8f
            };
            PhysicsBodyID new_body = physics_create_body(physics, &desc);
            if (new_body != PHYSICS_BODY_INVALID) {
                if (rot.x != 0.0f || rot.y != 0.0f || rot.z != 0.0f) {
                    physics_set_rotation_euler(physics, new_body, rot);
                }
                ent->physics_body = new_body;
                body_map[i] = new_body;
            }
            made_static++;
        }

        int joints_created = 0;
        physics_weld_batch_begin(physics);
        for (int e = 0; e < edge_count; e++) {
            int a = conn_edges[e][0], b = conn_edges[e][1];
            int ra = a; while (uf[ra] != ra) { uf[ra] = uf[uf[ra]]; ra = uf[ra]; }
            if (root_has_anchor[ra] && constraint_is_weld(conn_desc[e].type)) continue;
            PhysicsBodyID ba = body_map[a], bb = body_map[b];
            if (ba != PHYSICS_BODY_INVALID && bb != PHYSICS_BODY_INVALID) {
                physics_create_constraint(physics, ba, bb, &conn_desc[e]);
                joints_created++;
            }
        }
        physics_weld_batch_end(physics);

    }

    return brick_count;
}
