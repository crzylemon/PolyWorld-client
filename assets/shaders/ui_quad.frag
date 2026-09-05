in vec2 v_uv;
uniform sampler2D u_tex;
uniform float u_alpha;
uniform vec4 u_tint;
out vec4 frag_color;
void main() {
    vec4 col = texture(u_tex, v_uv);
    frag_color = vec4(col.rgb * u_tint.rgb, col.a * u_alpha * u_tint.a);
}
