#ifdef PW_DESKTOP
in vec3 v_texcoord;
uniform samplerCube u_skybox;
out vec4 frag_color;
void main() {
    frag_color = texture(u_skybox, v_texcoord);
}
#else
in vec2 v_ndc;
uniform samplerCube u_skybox;
uniform mat4 u_inv_view_proj;
uniform mat3 u_sky_yaw;
out vec4 frag_color;
void main() {
    vec4 world = u_inv_view_proj * vec4(v_ndc, 1.0, 1.0);
    vec3 dir = normalize(world.xyz / max(world.w, 1e-6));
    frag_color = texture(u_skybox, u_sky_yaw * dir);
}
#endif
