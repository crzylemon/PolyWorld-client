in vec2 v_uv;
uniform sampler2D u_tex;
uniform float u_alpha;
uniform vec4 u_tint;
uniform int u_use_tex;
out vec4 frag;
void main() {
    vec4 c = (u_use_tex != 0) ? texture(u_tex, v_uv) : vec4(1.0);
    frag = vec4(c.rgb * u_tint.rgb, c.a * u_alpha * u_tint.a);
}
