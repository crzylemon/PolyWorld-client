in vec2 v_uv;
out vec4 frag;
uniform sampler2D u_tex;
uniform vec4 u_color;
void main() {
    vec4 t = texture(u_tex, v_uv);
    frag = vec4(t.rgb, t.a * u_color.a);
}
