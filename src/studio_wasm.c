/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: studio_wasm.c                                                                       |
|   Purpose: web studio viewport (bricks, orbit, pick)                                        |
\*-------------------------------------------------------------------------------------------*/

#include "platform.h"
#include "renderer.h"
#include "scene.h"
#include "mesh_loader.h"
#include "mesh_primitives.h"
#include "math_types.h"
#include "texture.h"
#include "skybox.h"
#include "physics.h"

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#else
#include <GL/glew.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_STUDIO_BRICKS 2048

typedef struct {
    float w, h, l;
    uint8_t shape;
    bool active;
} StudioBrick;

static struct {
    Renderer renderer;
    Scene scene;
    PhysicsWorld* physics;
    Skybox skybox;
    bool initialized;

    float cam_yaw, cam_pitch, cam_dist;
    Vec3 cam_target;

    StudioBrick bricks[MAX_STUDIO_BRICKS];
    int selected;

    struct { float w, h, l; uint8_t shape; GPUMesh mesh; } cache[256];
    int cache_count;

    GPUMesh gizmo_box;
} S;

void input_on_keydown(int k) { (void)k; }
void input_on_keyup(int k) { (void)k; }
void input_on_mousedown(int b) { (void)b; }
void input_on_mouseup(int b) { (void)b; }
void input_on_mousemove(float dx, float dy) { (void)dx; (void)dy; }
void input_on_scroll(float d) { (void)d; }
bool chat_handle_key(int k, bool s) { (void)k; (void)s; return false; }
bool chat_handle_char(unsigned int c) { (void)c; return false; }
bool chat_handle_click(float x, float y) { (void)x; (void)y; return false; }
bool chat_handle_copy(void) { return false; }
bool chat_handle_paste(void) { return false; }

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void resize_canvas(int w, int h) {
    if (S.initialized) renderer_resize(&S.renderer, w, h);
}

static GPUMesh* get_mesh(float w, float h, float l, uint8_t shape) {
    for (int i = 0; i < S.cache_count; i++) {
        if (S.cache[i].w == w && S.cache[i].h == h && S.cache[i].l == l && S.cache[i].shape == shape)
            return &S.cache[i].mesh;
    }
    if (S.cache_count >= 256) return &S.cache[0].mesh;
    MeshData md;
    bool ok = false;
    float hw = w*0.5f, hh = h*0.5f, hl = l*0.5f;
    if (shape == 1) ok = create_sphere_mesh(&md, hw, 32, 24);
    else if (shape == 2) ok = create_cylinder_mesh(&md, hw, h, 20);
    else if (shape == 3) ok = create_wedge_mesh(&md, hw, hh, hl);
    else ok = create_box_mesh(&md, hw, hh, hl);
    if (!ok) return NULL;
    int idx = S.cache_count++;
    S.cache[idx].w = w; S.cache[idx].h = h; S.cache[idx].l = l; S.cache[idx].shape = shape;
    mesh_upload(&md, &S.cache[idx].mesh);
    mesh_data_free(&md);
    return &S.cache[idx].mesh;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void studio_orbit(float dx, float dy) {
    S.cam_yaw -= dx * 0.4f;
    S.cam_pitch += dy * 0.4f;
    if (S.cam_pitch > 89.0f) S.cam_pitch = 89.0f;
    if (S.cam_pitch < -89.0f) S.cam_pitch = -89.0f;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void studio_zoom(float delta) {
    S.cam_dist += delta * 2.0f;
    if (S.cam_dist < 3.0f) S.cam_dist = 3.0f;
    if (S.cam_dist > 200.0f) S.cam_dist = 200.0f;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void studio_pan(float dx, float dy) {
    float yaw_rad = S.cam_yaw * (float)M_PI / 180.0f;
    float right_x = -cosf(yaw_rad);
    float right_z = sinf(yaw_rad);
    float speed = S.cam_dist * 0.003f;
    S.cam_target.x += right_x * dx * speed;
    S.cam_target.z += right_z * dx * speed;
    S.cam_target.y += dy * speed;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int studio_insert(float x, float y, float z, float w, float h, float l,
                  int r, int g, int b, int shape) {
    GPUMesh* mesh = get_mesh(w, h, l, (uint8_t)shape);
    if (!mesh) return -1;

    EntityID eid = scene_create_entity(&S.scene);
    Entity* ent = scene_get_entity(&S.scene, eid);
    if (!ent) return -1;

    ent->transform.position = (Vec3){x, y, z};
    ent->transform.scale = (Vec3){1, 1, 1};
    ent->material.color = (Vec3){r/255.0f, g/255.0f, b/255.0f};
    ent->material.surfaces[0] = SURFACE_STUD;
    ent->material.surfaces[1] = SURFACE_INLET;
    ent->mesh = mesh;

    BodyDesc desc = {
        .type = BODY_STATIC,
        .collider = (shape == 1) ? COLLIDER_SPHERE : (shape == 2) ? COLLIDER_CYLINDER : COLLIDER_BOX,
        .position = {x, y, z},
        .half_extents = {w*0.5f, h*0.5f, l*0.5f},
        .radius = w*0.5f,
    };
    if (shape == 3) {
        float hx = w * 0.5f, hy = h * 0.5f, hz = l * 0.5f;
        desc.collider = COLLIDER_HULL;
        desc.hull_point_count = 6;
        desc.hull_points[0] = (Vec3){ -hx, -hy, -hz };
        desc.hull_points[1] = (Vec3){  hx, -hy, -hz };
        desc.hull_points[2] = (Vec3){ -hx, -hy,  hz };
        desc.hull_points[3] = (Vec3){  hx, -hy,  hz };
        desc.hull_points[4] = (Vec3){ -hx,  hy,  hz };
        desc.hull_points[5] = (Vec3){  hx,  hy,  hz };
    }
    ent->physics_body = physics_create_body(S.physics, &desc);

    S.bricks[eid].w = w; S.bricks[eid].h = h; S.bricks[eid].l = l;
    S.bricks[eid].shape = (uint8_t)shape;
    S.bricks[eid].active = true;

    return (int)eid;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void studio_delete(int id) {
    if (id < 0) return;
    scene_destroy_entity(&S.scene, (EntityID)id);
    S.bricks[id].active = false;
    if (S.selected == id) S.selected = -1;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void studio_set_position(int id, float x, float y, float z) {
    Entity* ent = scene_get_entity(&S.scene, (EntityID)id);
    if (!ent) return;
    ent->transform.position = (Vec3){x, y, z};
    if (ent->physics_body) physics_set_position(S.physics, ent->physics_body, (Vec3){x,y,z});
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void studio_set_size(int id, float w, float h, float l) {
    Entity* ent = scene_get_entity(&S.scene, (EntityID)id);
    if (!ent) return;
    S.bricks[id].w = w; S.bricks[id].h = h; S.bricks[id].l = l;
    ent->mesh = get_mesh(w, h, l, S.bricks[id].shape);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void studio_set_color(int id, int r, int g, int b) {
    Entity* ent = scene_get_entity(&S.scene, (EntityID)id);
    if (!ent) return;
    ent->material.color = (Vec3){r/255.0f, g/255.0f, b/255.0f};
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void studio_set_rotation(int id, float ry) {
    Entity* ent = scene_get_entity(&S.scene, (EntityID)id);
    if (!ent) return;
    ent->transform.rotation.y = ry;
    if (ent->physics_body) physics_set_rotation_euler(S.physics, ent->physics_body, ent->transform.rotation);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void studio_select(int id) {
    S.selected = id;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void studio_drag_move(float dx, float dy, int axis) {

    if (S.selected < 0) return;
    Entity* ent = scene_get_entity(&S.scene, (EntityID)S.selected);
    if (!ent) return;

    float speed = S.cam_dist * 0.005f;
    float yaw_rad = S.cam_yaw * (float)M_PI / 180.0f;

    if (axis == 1) {

        ent->transform.position.y -= dy * speed;
    } else {

        float right_x = -cosf(yaw_rad);
        float right_z = sinf(yaw_rad);
        float fwd_x = -sinf(yaw_rad);
        float fwd_z = -cosf(yaw_rad);
        ent->transform.position.x += right_x * dx * speed + fwd_x * dy * speed;
        ent->transform.position.z += right_z * dx * speed + fwd_z * dy * speed;
    }

    if (ent->physics_body)
        physics_set_position(S.physics, ent->physics_body, ent->transform.position);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
float studio_get_pos_x(int id) { Entity* e = scene_get_entity(&S.scene, (EntityID)id); return e ? e->transform.position.x : 0; }
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
float studio_get_pos_y(int id) { Entity* e = scene_get_entity(&S.scene, (EntityID)id); return e ? e->transform.position.y : 0; }
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
float studio_get_pos_z(int id) { Entity* e = scene_get_entity(&S.scene, (EntityID)id); return e ? e->transform.position.z : 0; }

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int studio_pick(float mx, float my) {
    int w = S.renderer.canvas_width;
    int h = S.renderer.canvas_height;
    if (w <= 0 || h <= 0) return -1;

    float ndc_x = (2.0f * mx / (float)w) - 1.0f;
    float ndc_y = 1.0f - (2.0f * my / (float)h);

    float yaw_rad = S.cam_yaw * (float)M_PI / 180.0f;
    float pitch_rad = S.cam_pitch * (float)M_PI / 180.0f;
    float fov_rad = 60.0f * (float)M_PI / 180.0f;
    float aspect = (float)w / (float)h;
    float half_h = tanf(fov_rad * 0.5f);
    float half_w = half_h * aspect;

    float cx = S.cam_target.x + S.cam_dist * cosf(pitch_rad) * sinf(yaw_rad);
    float cy = S.cam_target.y + S.cam_dist * sinf(pitch_rad);
    float cz = S.cam_target.z + S.cam_dist * cosf(pitch_rad) * cosf(yaw_rad);
    Vec3 cam_pos = {cx, cy, cz};

    Vec3 forward = {
        S.cam_target.x - cx,
        S.cam_target.y - cy,
        S.cam_target.z - cz
    };
    float flen = sqrtf(forward.x*forward.x + forward.y*forward.y + forward.z*forward.z);
    forward.x /= flen; forward.y /= flen; forward.z /= flen;

    Vec3 right = { forward.z, 0, -forward.x };
    float rlen = sqrtf(right.x*right.x + right.z*right.z);
    if (rlen > 0.001f) { right.x /= rlen; right.z /= rlen; }

    Vec3 up = {
        right.y * forward.z - right.z * forward.y,
        right.z * forward.x - right.x * forward.z,
        right.x * forward.y - right.y * forward.x
    };

    Vec3 ray_dir = {
        forward.x + right.x * ndc_x * half_w + up.x * ndc_y * half_h,
        forward.y + right.y * ndc_x * half_w + up.y * ndc_y * half_h,
        forward.z + right.z * ndc_x * half_w + up.z * ndc_y * half_h,
    };
    float len = sqrtf(ray_dir.x*ray_dir.x + ray_dir.y*ray_dir.y + ray_dir.z*ray_dir.z);
    ray_dir.x /= len; ray_dir.y /= len; ray_dir.z /= len;

    RaycastHit hit = physics_raycast(S.physics, cam_pos, ray_dir, 500.0f);
    if (hit.hit && hit.body > 0) {
        for (uint32_t i = 0; i < S.scene.count; i++) {
            Entity* e = &S.scene.entities[i];
            if (e->active && e->physics_body == hit.body) {
                S.selected = (int)e->id;
                return (int)e->id;
            }
        }
    }
    S.selected = -1;
    return -1;
}

static void frame(double dt) {
    (void)dt;
    if (!S.initialized) return;

    float yaw_rad = S.cam_yaw * (float)M_PI / 180.0f;
    float pitch_rad = S.cam_pitch * (float)M_PI / 180.0f;
    float cx = S.cam_target.x + S.cam_dist * cosf(pitch_rad) * sinf(yaw_rad);
    float cy = S.cam_target.y + S.cam_dist * sinf(pitch_rad);
    float cz = S.cam_target.z + S.cam_dist * cosf(pitch_rad) * cosf(yaw_rad);
    Vec3 cam_pos = {cx, cy, cz};

    Mat4 view = mat4_look_at(cam_pos, S.cam_target, (Vec3){0, 1, 0});
    float aspect = (float)S.renderer.canvas_width / (float)S.renderer.canvas_height;
    Mat4 projection = mat4_perspective(60.0f, aspect, 0.1f, 1000.0f);

    renderer_begin_frame(&S.renderer);
    skybox_render(&S.skybox, &view, &projection);
    renderer_render_scene(&S.renderer, &S.scene, &view, &projection);

    if (S.selected >= 0) {
        Entity* ent = scene_get_entity(&S.scene, (EntityID)S.selected);
        if (ent && ent->active) {
            Mat4 model = scene_get_world_matrix(&S.scene, (EntityID)S.selected);
            Vec3 sel_color = {1.0f, 0.9f, 0.0f};
            renderer_debug_box_matrix(&S.renderer, model.m, sel_color, &view, &projection);

            glDisable(GL_DEPTH_TEST);
            Vec3 pos = ent->transform.position;
            float arrow_len = 3.0f;
            float arrow_thick = 0.15f;

            {
                Mat4 m = mat4_multiply(mat4_translate((Vec3){pos.x + arrow_len*0.5f, pos.y, pos.z}),
                                       mat4_scale((Vec3){arrow_len, arrow_thick, arrow_thick}));
                renderer_draw_mesh(&S.renderer, &S.gizmo_box, &m, (Vec3){1,0.2f,0.2f}, 0, 0, &view, &projection);
            }

            {
                Mat4 m = mat4_multiply(mat4_translate((Vec3){pos.x, pos.y + arrow_len*0.5f, pos.z}),
                                       mat4_scale((Vec3){arrow_thick, arrow_len, arrow_thick}));
                renderer_draw_mesh(&S.renderer, &S.gizmo_box, &m, (Vec3){0.2f,1,0.2f}, 0, 0, &view, &projection);
            }

            {
                Mat4 m = mat4_multiply(mat4_translate((Vec3){pos.x, pos.y, pos.z + arrow_len*0.5f}),
                                       mat4_scale((Vec3){arrow_thick, arrow_thick, arrow_len}));
                renderer_draw_mesh(&S.renderer, &S.gizmo_box, &m, (Vec3){0.3f,0.3f,1}, 0, 0, &view, &projection);
            }
            glEnable(GL_DEPTH_TEST);
        }
    }

    {
        float grid_size = 0.5f;
        Vec3 grid_color = {0.4f, 0.4f, 0.4f};
        for (int i = -20; i <= 20; i += 2) {

            Mat4 m = mat4_multiply(mat4_translate((Vec3){0, -0.01f, (float)i}),
                                   mat4_scale((Vec3){40.0f, 0.02f, 0.04f}));
            renderer_draw_mesh(&S.renderer, &S.gizmo_box, &m, grid_color, 0, 0, &view, &projection);

            m = mat4_multiply(mat4_translate((Vec3){(float)i, -0.01f, 0}),
                             mat4_scale((Vec3){0.04f, 0.02f, 40.0f}));
            renderer_draw_mesh(&S.renderer, &S.gizmo_box, &m, grid_color, 0, 0, &view, &projection);
        }
    }

    renderer_end_frame(&S.renderer);
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    memset(&S, 0, sizeof(S));
    S.selected = -1;
    S.cam_yaw = 45.0f;
    S.cam_pitch = 25.0f;
    S.cam_dist = 30.0f;
    S.cam_target = (Vec3){0, 2, 0};

    int w = 800, h = 600;
#ifdef __EMSCRIPTEN__
    {
        double css_w, css_h;
        emscripten_get_element_css_size("#canvas", &css_w, &css_h);
        w = (int)css_w; h = (int)css_h;
    }
#endif

    if (!platform_init(w, h, "Studio")) return 1;
    if (!renderer_init(&S.renderer, w, h)) return 1;

    S.physics = physics_create((Vec3){0, -39.24f, 0});
    memset(&S.scene, 0, sizeof(Scene));
    skybox_init(&S.skybox);

    S.initialized = true;

    {
        MeshData md;
        create_box_mesh(&md, 0.5f, 0.5f, 0.5f);
        mesh_upload(&md, &S.gizmo_box);
        mesh_data_free(&md);
    }

    printf("[Studio WASM] Ready (%dx%d)\n", w, h);

    platform_run_loop(frame);
    return 0;
}
