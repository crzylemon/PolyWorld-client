in vec2 v_uv;
uniform sampler2D u_tex;
uniform vec4 u_color;
out vec4 frag_color;
void main() {
    float a = texture(u_tex, v_uv).r;
    if (a < 0.5) discard;
    frag_color = vec4(u_color.rgb, u_color.a);
}
