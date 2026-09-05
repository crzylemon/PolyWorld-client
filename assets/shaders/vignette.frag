in vec2 v_uv;
uniform float u_strength;
out vec4 frag_color;
void main() {
    vec2 d = v_uv * 2.0 - 1.0;
    d.x *= 1.35;
    float r = length(d);
    float a = smoothstep(0.28, 1.12, r) * u_strength;
    frag_color = vec4(0.0, 0.0, 0.0, a);
}
