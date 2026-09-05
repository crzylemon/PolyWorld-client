layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord;
layout(location = 3) in vec3 a_color;
uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform mat4 u_light_space;
out vec3 v_normal;
out vec3 v_local_normal;
out vec3 v_local_pos;
out vec3 v_frag_pos;
out vec2 v_texcoord;
out vec3 v_color;
out vec4 v_light_space_pos;
void main() {
    vec4 world_pos = u_model * vec4(a_position, 1.0);
    v_frag_pos = world_pos.xyz;
    v_normal = transpose(inverse(mat3(u_model))) * a_normal;
    v_local_normal = a_normal;
    v_local_pos = a_position;
    v_texcoord = a_texcoord;
    v_color = a_color;
    v_light_space_pos = u_light_space * world_pos;
    gl_Position = u_projection * u_view * world_pos;
}
