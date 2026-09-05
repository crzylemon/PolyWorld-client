uniform vec3 u_debug_color;
out vec4 frag_color;
void main() {
    frag_color = vec4(u_debug_color, 1.0);
}
