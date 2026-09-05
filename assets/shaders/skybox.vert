#ifdef PW_DESKTOP
layout(location = 0) in vec3 a_position;
uniform mat4 u_view;
uniform mat4 u_projection;
out vec3 v_texcoord;
void main() {
    v_texcoord = a_position;
    vec4 pos = u_projection * u_view * vec4(a_position, 1.0);
    gl_Position = pos.xyww;
}
#else
layout(location = 0) in vec2 a_pos;
out vec2 v_ndc;
void main() {
    v_ndc = a_pos;
    gl_Position = vec4(a_pos, 1.0, 1.0);
}
#endif
