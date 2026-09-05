in vec2 v_uv;
uniform sampler2D u_scene_color;
uniform sampler2D u_scene_depth;
uniform sampler2D u_fog_depth;
uniform vec3 u_clear_color;
uniform float u_near;
uniform float u_far;
uniform float u_fog_start;
uniform float u_fog_end;
out vec4 frag_color;
float linearize_depth(float d) {
    float z = d * 2.0 - 1.0;
    return (2.0 * u_near * u_far) / (u_far + u_near - z * (u_far - u_near));
}
void main() {
    float fd = texture(u_fog_depth, v_uv).r;
    float od = texture(u_scene_depth, v_uv).r;
    vec4 c = texture(u_scene_color, v_uv);
    float d = fd;
    if (od < 0.99999) d = min(d, od);
    bool opaque_hit = (od < 0.99999);
    bool any_depth = (d < 0.99999);
    bool alpha_only = (!opaque_hit && c.a > 0.001 && c.a < 0.99);
    if (!any_depth && c.a >= 0.99) discard;
    if (!any_depth && !alpha_only) discard;
    float fog = 0.0;
    if (any_depth) {
        float dist = linearize_depth(d);
        float t = clamp((dist - u_fog_start) / max(u_fog_end - u_fog_start, 0.001), 0.0, 1.0);
        fog = t * t * (3.0 - 2.0 * t);
        if (dist >= u_fog_end) fog = 1.0;
    }
    if (!opaque_hit) {
        float coverage = clamp(c.a, 0.0, 1.0);
        vec3 straight = c.rgb / max(coverage, 0.001);
        float a = (1.0 - fog) * coverage;
        if (a <= 0.001) discard;
        frag_color = vec4(straight, a);
        return;
    }
    float ao = 1.0;
    float a = 1.0 - fog;
    if (a <= 0.001) discard;
    frag_color = vec4(c.rgb * ao, a);
}
