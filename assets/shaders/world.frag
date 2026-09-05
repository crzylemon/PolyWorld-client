in vec3 v_normal;
in vec3 v_local_normal;
in vec3 v_local_pos;
in vec3 v_frag_pos;
in vec2 v_texcoord;
in vec3 v_color;
in vec4 v_light_space_pos;
uniform vec3 u_color;
uniform vec3 u_light_dir;
uniform vec3 u_light_color;
uniform vec3 u_camera_pos;
uniform vec3 u_fog_color;
uniform float u_fog_start;
uniform float u_fog_end;
uniform sampler2D u_texture;
uniform SHADOW_SAMPLER u_shadow_map;
uniform SHADOW_SAMPLER u_shadow_map_near;
uniform sampler2D u_normal_map;
uniform sampler2D u_inlet_map;
uniform sampler2D u_mat_albedo;
uniform sampler2D u_mat_normal;
uniform sampler2D u_mat_specular;
uniform int u_part_material;
uniform int u_has_texture;
uniform vec4 u_uv_rect;
uniform int u_face_mode;
uniform int u_face_surf[6];
uniform int u_part_shape; // 0 box/wedge, 1 sphere, 2 cylinder
uniform vec3 u_part_size;
uniform int u_shadow_enabled;
uniform int u_shadow_soft;
uniform int u_shadow_face_ids;
uniform int u_shadow_cascades;
uniform float u_shadow_exp;
uniform float u_shadow_depth_bias;
uniform float u_shadow_range;
uniform mat4 u_light_space_near;
uniform VOXEL_SAMPLER u_voxel_map;
uniform int u_voxel_enabled;
uniform vec3 u_voxel_origin;
uniform float u_voxel_size;
uniform int u_voxel_dim;
uniform float u_voxel_range;
uniform bool u_fog_enabled;
uniform float u_glow;
uniform float u_alpha;
uniform float u_contact_shade;
uniform int u_glow_light_count;
uniform vec3 u_glow_light_pos[32];
uniform vec3 u_glow_light_color[32];
uniform float u_glow_light_range[32];
uniform uint u_glow_light_entity[32];
uniform int u_glow_shadow_count;
uniform SHADOW_SAMPLER u_glow_shadow_map0;
uniform SHADOW_SAMPLER u_glow_shadow_map1;
uniform mat4 u_glow_ls[12];
layout(location = 0) out vec4 frag_color;
#ifdef PW_DESKTOP
layout(location = 1) out float frag_fog_depth;
#endif

uniform mat4 u_light_space;
uniform uint u_shadow_id;
float vsm_vis(vec4 m, float z) {
    if (u_shadow_face_ids == 0 && m.b > 0.02) return 1.0;
    float c = u_shadow_exp;
    float rz = (c > 0.5) ? exp(c * z) : z;
    float mean = m.r;
    if (rz <= mean) return 1.0;
    float var = max(m.g - mean * mean, 1e-4 * max(mean * mean, 1e-6));
    float d = rz - mean;
    return var / (var + d * d);
}
float tap_occ(SHADOW_SAMPLER sm, vec2 uv, float z) {
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 0.0;
    return 1.0 - vsm_vis(texture(sm, uv), z);
}
float cascade_uv(mat4 ls, out vec3 p) {
    vec4 clip = ls * vec4(v_frag_pos, 1.0);
    p = clip.xyz / max(clip.w, 1e-6);
    p = p * 0.5 + 0.5;
    if (p.z > 1.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return -1.0;
    return 1.0;
}
float shadow_dist_fade() {
    float fade_end = max(u_shadow_range, 1.0);
    float dist = length(v_frag_pos.xz - u_camera_pos.xz);
    return 1.0 - smoothstep(fade_end * 0.62, fade_end, dist);
}
float cascade_occl(SHADOW_SAMPLER sm, mat4 ls) {
    vec3 p;
    if (cascade_uv(ls, p) < 0.0) return -1.0;
    if (u_shadow_soft < 1)
        return tap_occ(sm, p.xy, p.z);
    vec2 texel = 1.0 / vec2(textureSize(sm, 0));
    float acc = 0.0;
    float wsum = 0.0;
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            vec2 o = vec2(float(x), float(y));
            float g = exp(-0.5 * dot(o, o) / 1.8225);
            acc += tap_occ(sm, p.xy + o * texel, p.z) * g;
            wsum += g;
        }
    }
    return acc / max(wsum, 1e-6);
}
float map_occ(vec4 lsp) {
    if (u_shadow_enabled == 0) return 0.0;
    if (lsp.w <= 0.0) return 0.0;
    float occ_far = cascade_occl(u_shadow_map, u_light_space);
    if (occ_far < 0.0) occ_far = 0.0;
    if (u_shadow_cascades < 1)
        return occ_far;
    float occ_near = cascade_occl(u_shadow_map_near, u_light_space_near);
    if (occ_near < 0.0)
        return occ_far;
    float near_r = max(u_shadow_depth_bias, 1.0);
    float dist = length(v_frag_pos.xz - u_camera_pos.xz);
    float w = 1.0 - smoothstep(near_r * 0.48, near_r * 0.78, dist);
    return mix(occ_far, occ_near, w);
}
float voxel_occ() {
    if (u_voxel_enabled != 1) return 0.0;
    vec3 ld = normalize(u_light_dir);
    vec3 p = v_frag_pos - ld * (u_voxel_size * 0.35);
    float extent = u_voxel_size * max(float(u_voxel_dim), 1.0);
    vec3 uvw = (p - u_voxel_origin) / extent;
    float edge = min(min(uvw.x, 1.0 - uvw.x), min(min(uvw.y, 1.0 - uvw.y), min(uvw.z, 1.0 - uvw.z)));
    float edge_fade = smoothstep(0.0, 0.10, edge);
    if (edge_fade <= 0.0) return 0.0;
    float dist = length(v_frag_pos.xz - u_camera_pos.xz);
    float fade_end = max(u_voxel_range, 1.0);
    float dist_fade = 1.0 - smoothstep(fade_end * 0.62, fade_end, dist);
    float fade = edge_fade * dist_fade;
    if (fade <= 0.001) return 0.0;
    float vis = texture(u_voxel_map, uvw).r;
    return (1.0 - vis) * fade;
}
float calc_shadow(vec4 lsp) {
    if (u_voxel_enabled == 1) return voxel_occ();
    return map_occ(lsp) * shadow_dist_fade();
}
int glow_cube_face(vec3 d) {
    vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) return d.x >= 0.0 ? 0 : 1;
    if (a.y >= a.z) return d.y >= 0.0 ? 2 : 3;
    return d.z >= 0.0 ? 4 : 5;
}
vec2 glow_atlas_uv(int face, vec2 p) {
    float col = float(face - (face / 3) * 3);
    float row = float(face / 3);
    p = clamp(p, 0.002, 0.998);
    return vec2((col + p.x) / 3.0, (row + p.y) / 2.0);
}
float glow_evsm_vis(int light_i, vec3 frag, vec3 lamp) {
    if (light_i >= u_glow_shadow_count) return 1.0;
    uint lamp_id = u_glow_light_entity[light_i];
    // Accessory lamps light the wearer but do not EVSM-shadow avatars or hats.
    if ((lamp_id & 0x80000000u) != 0u && u_shadow_id >= 16000000u) return 1.0;
    if (u_shadow_id != 0u && u_shadow_id == (lamp_id & 0x7fffffffu)) return 1.0;
    vec3 dir = frag - lamp;
    int face = glow_cube_face(dir);
    vec4 clip = u_glow_ls[light_i * 6 + face] * vec4(frag, 1.0);
    if (clip.w <= 1e-5) return 1.0;
    vec3 p = clip.xyz / clip.w;
    p = p * 0.5 + 0.5;
    if (p.z > 1.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 1.0;
    vec2 uv = glow_atlas_uv(face, p.xy);
    float occ = (light_i == 0)
        ? tap_occ(u_glow_shadow_map0, uv, p.z)
        : tap_occ(u_glow_shadow_map1, uv, p.z);
    return 1.0 - occ;
}

float pw_surf_detail(int st, vec2 uv) {
    if (st == 1) return (texture(u_texture, uv).r - 0.9) * 1.5;
    if (st == 2) return (texture(u_inlet_map, uv).r - 0.9) * 1.5;
    return 0.0;
}
float pw_surf_detail_g(int st, vec2 uv, vec2 ddx, vec2 ddy) {
    if (st == 1) return (textureGrad(u_texture, uv, ddx, ddy).r - 0.9) * 1.5;
    if (st == 2) return (textureGrad(u_inlet_map, uv, ddx, ddy).r - 0.9) * 1.5;
    return 0.0;
}
vec2 pw_cube_uv_y(vec3 lp, vec3 sz) {
    return vec2(lp.x * sz.x, (lp.y >= 0.0 ? -lp.z : lp.z) * sz.z);
}
/* Cube-face studs on a sphere. Gradients from local pos so mipmaps
 * don't see the UV jump at cube edges (that was the black blotching). */
void pw_sphere_stud(vec3 lp, vec3 sz, out vec2 uv, out vec2 ddx, out vec2 ddy) {
    vec3 a = abs(lp);
    vec3 px = dFdx(lp);
    vec3 py = dFdy(lp);
    vec2 t, jx, jy, jz, scale;
    if (a.y >= a.x && a.y >= a.z) {
        float iy = 1.0 / (lp.y >= 0.0 ? max(lp.y, 1e-5) : min(lp.y, -1e-5));
        if (lp.y >= 0.0) {
            t = vec2(lp.x * iy, -lp.z * iy);
            jx = vec2(iy, 0.0);
            jy = vec2(-lp.x * iy * iy, lp.z * iy * iy);
            jz = vec2(0.0, -iy);
        } else {
            t = vec2(lp.x * iy, lp.z * iy);
            jx = vec2(iy, 0.0);
            jy = vec2(-lp.x * iy * iy, -lp.z * iy * iy);
            jz = vec2(0.0, iy);
        }
        scale = 0.5 * vec2(sz.x, sz.z);
    } else if (a.x >= a.z) {
        float ix = 1.0 / (lp.x >= 0.0 ? max(lp.x, 1e-5) : min(lp.x, -1e-5));
        if (lp.x >= 0.0) {
            t = vec2(-lp.z * ix, lp.y * ix);
            jx = vec2(lp.z * ix * ix, -lp.y * ix * ix);
            jy = vec2(0.0, ix);
            jz = vec2(-ix, 0.0);
        } else {
            t = vec2(lp.z * ix, lp.y * ix);
            jx = vec2(-lp.z * ix * ix, -lp.y * ix * ix);
            jy = vec2(0.0, ix);
            jz = vec2(ix, 0.0);
        }
        scale = 0.5 * vec2(sz.z, sz.y);
    } else {
        float iz = 1.0 / (lp.z >= 0.0 ? max(lp.z, 1e-5) : min(lp.z, -1e-5));
        if (lp.z >= 0.0) {
            t = vec2(lp.x * iz, lp.y * iz);
            jx = vec2(iz, 0.0);
            jy = vec2(0.0, iz);
            jz = vec2(-lp.x * iz * iz, -lp.y * iz * iz);
        } else {
            t = vec2(-lp.x * iz, lp.y * iz);
            jx = vec2(-iz, 0.0);
            jy = vec2(0.0, iz);
            jz = vec2(lp.x * iz * iz, -lp.y * iz * iz);
        }
        scale = 0.5 * vec2(sz.x, sz.y);
    }
    uv = t * scale;
    ddx = (jx * px.x + jy * px.y + jz * px.z) * scale;
    ddy = (jx * py.x + jy * py.y + jz * py.z) * scale;
}
float pw_curve_detail(vec3 lp, vec3 ln, vec3 sz, int shape) {
    int stx = (lp.x >= 0.0) ? u_face_surf[4] : u_face_surf[5];
    int sty = (lp.y >= 0.0) ? u_face_surf[2] : u_face_surf[3];
    int stz = (lp.z >= 0.0) ? u_face_surf[0] : u_face_surf[1];
    vec3 an = abs(normalize(lp));

    if (shape == 2) {
        if (abs(ln.y) > 0.45) {
            vec2 uv = pw_cube_uv_y(lp, sz);
            vec3 px = dFdx(lp), py = dFdy(lp);
            float s = (lp.y >= 0.0) ? -1.0 : 1.0;
            vec2 ddx = vec2(px.x * sz.x, s * px.z * sz.z);
            vec2 ddy = vec2(py.x * sz.x, s * py.z * sz.z);
            return pw_surf_detail_g(sty, uv, ddx, ddy);
        }
        /* Mesh U is 0..pi (arc on the unit cylinder). Snap the wrap to a
         * whole number of stud tiles so the seam meets instead of leaving
         * a fractional leftover. */
        float circ = 3.14159265 * max(sz.x, sz.z);
        float tiles = max(floor(circ + 0.5), 1.0);
        vec2 cuv = vec2((v_texcoord.x / 3.14159265) * tiles, v_texcoord.y * sz.y);
        if (stx == stz) return pw_surf_detail(stx, cuv);
        float t = an.x / max(an.x + an.z, 1e-6);
        float b = smoothstep(0.38, 0.62, t);
        return mix(pw_surf_detail(stz, cuv), pw_surf_detail(stx, cuv), b);
    }

    vec2 uv, ddx, ddy;
    pw_sphere_stud(lp, sz, uv, ddx, ddy);
    vec3 ap = abs(lp);
    int axis = (ap.y >= ap.x && ap.y >= ap.z) ? 1 : ((ap.x >= ap.z) ? 0 : 2);
    int st = (axis == 0) ? stx : ((axis == 1) ? sty : stz);
    float d = pw_surf_detail_g(st, uv, ddx, ddy);
    int axis2 = (axis == 1) ? ((ap.x >= ap.z) ? 0 : 2)
              : (axis == 0) ? ((ap.y >= ap.z) ? 1 : 2)
                            : ((ap.y >= ap.x) ? 1 : 0);
    int st2 = (axis2 == 0) ? stx : ((axis2 == 1) ? sty : stz);
    if (st2 == st) return d;
    float a1 = (axis == 0) ? ap.x : ((axis == 1) ? ap.y : ap.z);
    float a2 = (axis2 == 0) ? ap.x : ((axis2 == 1) ? ap.y : ap.z);
    float w = smoothstep(0.0, 0.10, a1 - a2);
    if (w > 0.999) return d;
    return mix(pw_surf_detail_g(st2, uv, ddx, ddy), d, w);
}

void main() {
    vec3 normal = normalize(v_normal);
    vec3 light_d = normalize(u_light_dir);

    vec2 stud_uv = v_texcoord;
    vec2 stud_ddx = vec2(0.0);
    vec2 stud_ddy = vec2(0.0);
    bool stud_grad = false;
    {
        vec3 ln = v_local_normal;
        vec3 lp = v_local_pos;
        vec3 sz = u_part_size;
        if (u_part_shape == 2) {
            if (abs(ln.y) > 0.45) {
                stud_uv = pw_cube_uv_y(lp, sz);
                vec3 px = dFdx(lp), py = dFdy(lp);
                float s = (lp.y >= 0.0) ? -1.0 : 1.0;
                stud_ddx = vec2(px.x * sz.x, s * px.z * sz.z);
                stud_ddy = vec2(py.x * sz.x, s * py.z * sz.z);
                stud_grad = true;
            } else {
                float circ = 3.14159265 * max(sz.x, sz.z);
                float tiles = max(floor(circ + 0.5), 1.0);
                stud_uv = vec2((v_texcoord.x / 3.14159265) * tiles, v_texcoord.y * sz.y);
            }
        } else if (u_part_shape == 1) {
            pw_sphere_stud(lp, sz, stud_uv, stud_ddx, stud_ddy);
            stud_grad = true;
        } else {
            vec3 aln = abs(ln);
            float fu = 1.0, fv = 1.0;
            if (ln.y > 0.2 && aln.y >= aln.x * 0.55) {
                fu = u_part_size.x;
                if (aln.z > 0.15)
                    fv = length(vec2(u_part_size.y, u_part_size.z)) / sqrt(2.0);
                else
                    fv = u_part_size.z;
            } else if (ln.y < -0.2 && aln.y >= aln.x * 0.55) {
                fu = u_part_size.x; fv = u_part_size.z;
            } else if (aln.y >= aln.x && aln.y >= aln.z) {
                fu = u_part_size.x; fv = u_part_size.z;
            } else if (aln.x >= aln.z) {
                fu = u_part_size.z; fv = u_part_size.y;
            } else {
                fu = u_part_size.x; fv = u_part_size.y;
            }
            stud_uv = v_texcoord * vec2(fu, fv);
        }
    }
    vec2 mat_uv = stud_uv / 4.0;

    vec3 mapped_normal = normal;
    bool plastic = (u_part_material == 0);
    bool use_mat_n = !plastic;
    if ((use_mat_n || (plastic && u_has_texture == 1)) && u_shadow_enabled == 1) {
        vec3 nm_val = use_mat_n
            ? (stud_grad ? textureGrad(u_mat_normal, mat_uv, stud_ddx / 4.0, stud_ddy / 4.0).rgb
                         : texture(u_mat_normal, mat_uv).rgb)
            : (stud_grad ? textureGrad(u_normal_map, stud_uv, stud_ddx, stud_ddy).rgb
                         : texture(u_normal_map, stud_uv).rgb);
        vec3 T = abs(normal.y) > 0.9 ? vec3(1,0,0) : vec3(0,1,0);
        T = normalize(T - normal * dot(T, normal));
        vec3 B = cross(T, normal);
        mat3 TBN = mat3(T, B, normal);
        vec3 nm = nm_val * 2.0 - 1.0;
        nm.y = -nm.y;
        mapped_normal = normalize(TBN * nm);
    }

    vec3 sky_color = vec3(0.72, 0.76, 0.82);
    vec3 ground_color = vec3(0.42, 0.40, 0.38);
    float hemi = mapped_normal.y * 0.5 + 0.5;
    vec3 ambient = mix(ground_color, sky_color, hemi) * 0.92;

    float nd_raw = dot(mapped_normal, -light_d);
    float ndl = max(nd_raw, 0.0);
    float wrap = max(nd_raw * 0.5 + 0.5, 0.0);
    float diff = mix(ndl, wrap * wrap, 0.4);
    if (u_contact_shade > 0.001) {
        diff *= mix(0.55, 1.0, smoothstep(0.0, 0.65, ndl));
        float top = max(mapped_normal.y, 0.0);
        diff *= (1.0 - u_contact_shade * top * top * 0.85);
        ambient *= (1.0 - u_contact_shade * top * 0.35);
    }
    vec3 diffuse = diff * u_light_color * 0.95;

    float shadow = calc_shadow(v_light_space_pos);
    float sh_d = 0.64;
    if (u_voxel_enabled == 1 && u_shadow_enabled == 0)
        sh_d = 0.68;
    diffuse *= (1.0 - shadow * sh_d);
    vec3 shadow_sky = vec3(0.48, 0.62, 0.88);
    vec3 shadow_ground = vec3(0.36, 0.39, 0.46);
    vec3 shadow_amb = mix(shadow_ground, shadow_sky, hemi);
    ambient = mix(ambient, shadow_amb, shadow * 0.58);

    vec3 glow_rgb = vec3(0.0);
    vec3 glow_s = vec3(0.0);
    if (u_glow_light_count > 0) {
        vec3 Vglow = normalize(u_camera_pos - v_frag_pos);
        float spec_g = plastic ? 0.16 : texture(u_mat_specular, mat_uv).r;
        for (int i = 0; i < 32; i++) {
            if (i < u_glow_light_count) {
                vec3 to_l = u_glow_light_pos[i] - v_frag_pos;
                float dist = length(to_l);
                float range = u_glow_light_range[i];
                if (range > 0.01 && dist < range) {
                    float t = 1.0 - dist / range;
                    float att = t * t * t;
                    vec3 L = to_l / max(dist, 0.001);
                    float ndotl = max(dot(mapped_normal, L), 0.0);
                    float fill = max(dot(mapped_normal, L) * 0.35 + 0.65, 0.0);
                    float vis = glow_evsm_vis(i, v_frag_pos, u_glow_light_pos[i]);
                    vec3 col = u_glow_light_color[i] * att * vis;
                    glow_rgb += col * (ndotl * 0.88 + fill * fill * 0.30);
                    if (spec_g > 0.004) {
                        vec3 H = normalize(L + Vglow);
                        float ndh = max(dot(mapped_normal, H), 0.0);
                        glow_s += col * pow(ndh, mix(14.0, 80.0, spec_g)) * spec_g * 0.50;
                    }
                }
            }
        }
    }

    vec3 base_color = u_color * v_color;
    float out_alpha = u_alpha;
    if (u_part_material != 0)
        base_color *= texture(u_mat_albedo, mat_uv).rgb;
    if (plastic && u_face_mode == 1) {
        if (u_part_shape == 1 || u_part_shape == 2) {
            float detail = pw_curve_detail(v_local_pos, v_local_normal, u_part_size, u_part_shape);
            base_color = base_color + base_color * detail;
        } else {
            vec3 ln = v_local_normal;
            vec3 aln = abs(ln);
            int face;
            if (ln.y > 0.2 && aln.y >= aln.x * 0.55) face = 2;
            else if (ln.y < -0.2 && aln.y >= aln.x * 0.55) face = 3;
            else if (aln.y >= aln.x && aln.y >= aln.z) face = (ln.y >= 0.0) ? 2 : 3;
            else if (aln.x >= aln.z) face = (ln.x >= 0.0) ? 4 : 5;
            else face = (ln.z >= 0.0) ? 0 : 1;
            int st = u_face_surf[face];
            if (st == 1 || st == 2) {
                vec4 tex_color = (st == 1) ? texture(u_texture, stud_uv)
                                          : texture(u_inlet_map, stud_uv);
                float detail = (tex_color.r - 0.9) * 1.5;
                base_color = base_color + base_color * detail;
            }
        }
    } else if (plastic && u_has_texture == 1) {
        vec4 tex_color = stud_grad ? textureGrad(u_texture, stud_uv, stud_ddx, stud_ddy)
                                   : texture(u_texture, stud_uv);
        float detail = (tex_color.r - 0.9) * 1.5;
        base_color = base_color + base_color * detail;
    } else if (u_has_texture == 2) {
        vec4 tex = texture(u_texture, v_texcoord);
        float r = tex.r; float g = tex.g; float b = tex.b;
        float is_red = step(0.3, r - g) * step(0.3, r - b);
        vec3 tinted = u_color * r;
        base_color = mix(tex.rgb, tinted, is_red);
    } else if (u_has_texture == 3) {
        vec4 tex = texture(u_texture, v_texcoord);
        float a = clamp(tex.a, 0.0, 1.0);
        vec3 cloth = tex.rgb / max(a, 0.001);
        base_color = mix(u_color, cloth, a);
    } else if (u_has_texture == 4) {
        vec2 uv = v_texcoord * u_uv_rect.zw + u_uv_rect.xy;
        vec4 tex = texture(u_texture, uv);
        if (tex.a * u_alpha < 0.02) discard;
        vec3 emit = tex.rgb * u_color * (1.0 + u_glow * 0.9);
        frag_color = vec4(emit, u_alpha * tex.a);
#ifdef PW_DESKTOP
        frag_fog_depth = gl_FragCoord.z;
#endif
        return;
    } else if (u_has_texture == 5) {
        vec4 tex = texture(u_texture, v_texcoord);
        if (tex.a * u_alpha < 0.02) discard;
        float a = clamp(tex.a, 0.0, 1.0);
        base_color = (tex.rgb / max(a, 0.001)) * u_color;
        out_alpha = u_alpha * a;
    }

    float lit = mix(1.0, 0.7, clamp(u_glow, 0.0, 1.0));
    vec3 result = min((ambient + diffuse) * lit, vec3(1.22)) * base_color;
    result += base_color * u_glow * 0.85;
    result += base_color * glow_rgb;

    float spec_m = plastic ? 0.16 : texture(u_mat_specular, mat_uv).r;
    if (spec_m > 0.004) {
        vec3 V = normalize(u_camera_pos - v_frag_pos);
        vec3 L = normalize(-light_d);
        vec3 H = normalize(L + V);
        float ndh = max(dot(mapped_normal, H), 0.0);
        float spec = pow(ndh, mix(14.0, 80.0, spec_m)) * spec_m;
        spec *= (1.0 - shadow * 0.82);
        result += u_light_color * spec;
        float fres = pow(1.0 - max(dot(mapped_normal, V), 0.0), 3.0);
        result += base_color * fres * spec_m * 0.28;
    }
    result += glow_s;

    frag_color = vec4(result, out_alpha);
#ifdef PW_DESKTOP
    frag_fog_depth = gl_FragCoord.z;
#endif
}
