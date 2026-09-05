in vec2 v_uv;
out vec4 frag;
uniform sampler2D u_tex;
uniform vec2 u_size;
uniform float u_radius;
float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}
void main() {
    vec2 p = (v_uv - 0.5) * u_size;
    float d = sdRoundBox(p, u_size * 0.5, u_radius);
    float a = 1.0 - smoothstep(-0.75, 0.75, d);
    if (a < 0.02) discard;
    vec4 c = texture(u_tex, v_uv);
    frag = vec4(c.rgb, c.a * a);
}
