in vec2 v_uv;
uniform sampler2D u_depth;
uniform sampler2D u_noise;
uniform mat4 u_projection;
uniform mat4 u_inv_projection;
uniform vec2 u_noise_scale;
uniform vec3 u_samples[24];
uniform float u_radius;
uniform float u_bias;
out vec4 frag_color;

vec3 view_from_depth(vec2 uv) {
    float d = texture(u_depth, uv).r;
    vec4 ndc = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 v = u_inv_projection * ndc;
    return v.xyz / max(v.w, 1e-6);
}

float ign(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main() {
    float depth = texture(u_depth, v_uv).r;
    if (depth > 0.999) {
        frag_color = vec4(1.0);
        return;
    }

    vec3 origin = view_from_depth(v_uv);
    vec3 dx = dFdx(origin);
    vec3 dy = dFdy(origin);
    vec2 ao_size = max(u_noise_scale * 4.0, vec2(8.0));
    float pix = abs(origin.z) * (2.0 / max(u_projection[1][1], 0.2)) / ao_size.y;
    float bias = u_bias + pix * 0.85;

    vec3 nrm = cross(dx, dy);
    if (dot(nrm, nrm) < 1e-12) {
        frag_color = vec4(1.0);
        return;
    }
    vec3 normal = normalize(nrm);
    if (dot(normal, origin) > 0.0) normal = -normal;

    float n = ign(gl_FragCoord.xy);
    float a = n * 6.2831853;
    vec3 rvec = vec3(cos(a), sin(a), 0.0);
    vec3 tangent = normalize(rvec - normal * dot(rvec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 tbn = mat3(tangent, bitangent, normal);

    float occ = 0.0;
    float taps = 0.0;
    float soft = max(u_radius * 0.34, 0.08);
    for (int i = 0; i < 24; i++) {
        vec3 sample_pos = origin + tbn * u_samples[i] * u_radius;
        vec4 clip = u_projection * vec4(sample_pos, 1.0);
        vec3 ndc = clip.xyz / max(clip.w, 1e-6);
        vec2 uv = ndc.xy * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) continue;
        float sample_z = view_from_depth(uv).z;
        float range = smoothstep(0.0, 1.0, u_radius / max(abs(origin.z - sample_z), 1e-4));
        float hit = smoothstep(0.0, soft, sample_z - (sample_pos.z + bias));
        occ += hit * range;
        taps += 1.0;
    }
    float vis = 1.0 - occ / max(taps, 1.0);
    vis = mix(1.0, vis, 0.38);
    vis = mix(1.0, vis, smoothstep(80.0, 25.0, abs(origin.z)));
    float dz = max(abs(dx.z), abs(dy.z));
    vis = mix(vis, 1.0, smoothstep(pix * 6.0, pix * 16.0, dz));
    frag_color = vec4(vis, vis, vis, 1.0);
}
