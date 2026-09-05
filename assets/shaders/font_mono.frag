in vec2 v_uv;
out vec4 frag;
uniform sampler2D u_tex;
uniform vec4 u_color;
void main() {
    float a = texture(u_tex, v_uv).r;
    frag = vec4(u_color.rgb, u_color.a * a);
}
