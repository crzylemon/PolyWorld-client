uniform float u_caster_group;
uniform float u_evsm_c;
out vec4 frag_color;
void main() {
    float z = gl_FragCoord.z;
    float c = u_evsm_c;
    if (c > 0.5) {
        float e = exp(c * z);
        frag_color = vec4(e, e * e, u_caster_group, 1.0);
    } else {
        frag_color = vec4(z, z * z, u_caster_group, 1.0);
    }
}
