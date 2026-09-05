in vec2 v_uv;
uniform sampler2D u_ssao;
uniform sampler2D u_depth;
uniform mat4 u_inv_projection;
out vec4 frag_color;

float view_z(vec2 uv) {
    float d = texture(u_depth, uv).r;
    vec4 ndc = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 v = u_inv_projection * ndc;
    return v.z / max(v.w, 1e-6);
}

void main() {
    vec2 ao_size = vec2(textureSize(u_ssao, 0));
    vec2 texel = 1.0 / max(ao_size, vec2(1.0));
    float cz = view_z(v_uv);
    float z_tol = 1.35 + abs(cz) * (3.2 / max(ao_size.y, 1.0));
    if (z_tol < 1.6) z_tol = 1.6;
    float acc = 0.0;
    float wsum = 0.0;
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            vec2 o = vec2(float(x), float(y));
            float g = exp(-0.5 * dot(o, o) / 2.0);
            vec2 uv = v_uv + o * texel;
            float ao = texture(u_ssao, uv).r;
            float w = g * (1.0 - smoothstep(z_tol, z_tol * 4.0, abs(view_z(uv) - cz)));
            acc += ao * w;
            wsum += w;
        }
    }
    float vis = acc / max(wsum, 1e-4);
    frag_color = vec4(vis, vis, vis, 1.0);
}
