#version 330 core
layout(std140) uniform PSConstants {
    vec4 fogColor;
    vec4 alphaRef;
    vec4 shadowCtl;
    vec4 constColor;
    vec4 _pad4;
    vec4 _pad5;
    vec4 _pad6;
    vec4 rgbEyeCoef;
    mat4 cascadeVP[4];
    vec4 cascadeSplits;
    vec4 cascadeCtl;
    vec4 camFwd;
};

uniform sampler2D tex0;

in vec4 vColor;
in vec4 vSpecColor;
in vec2 vUV0;
in vec2 vUV1;
in float vFogTC;

uniform sampler2DArray shadowMap;
in vec3 vWorldRel;

out vec4 fragColor;

void main() {
    vec4 r0 = vColor * texture(tex0, vUV0);
    r0 *= constColor;
    r0.rgb += vSpecColor.rgb;

    if (shadowCtl.x > 0.5) {
        int nC = int(cascadeCtl.x);
        int omniN = int(cascadeCtl.w);
        float eyeDepth = dot(vWorldRel, camFwd.xyz);
        float dist3D = length(vWorldRel);
        int ci = nC;
        for (int i = 0; i < 4; ++i) {
            if (i >= nC) break;
            float metric = (i < omniN) ? dist3D : eyeDepth;
            if (metric <= cascadeSplits[i]) { ci = i; break; }
        }
        if (ci < nC) {
            float ts = shadowCtl.w;
            float prevEdge = (ci > 0) ? cascadeSplits[ci - 1] : 0.0;
            float ciMetric = (ci < omniN) ? dist3D : eyeDepth;
            float band = (cascadeSplits[ci] - prevEdge) * 0.15;
            float bw = (ci + 1 < nC) ? clamp((ciMetric - (cascadeSplits[ci] - band)) / max(band, 0.001), 0.0, 1.0) : 0.0;
            float litSum = 0.0;
            float wSum = 0.0;
            for (int p = 0; p < 4; ++p) {
                int c = ci + p;
                if (c >= nC) break;
                float w = (p == 0) ? (1.0 - bw) : ((wSum <= 0.0) ? 1.0 : ((p == 1) ? bw : 0.0));
                if (w <= 0.0) continue;
                vec4 cp = cascadeVP[c] * vec4(vWorldRel, 1.0);
                vec3 sc = cp.xyz / cp.w;
                vec2 suv = sc.xy * 0.5 + 0.5;
                if (suv.x > 0.0 && suv.x < 1.0 && suv.y > 0.0 && suv.y < 1.0 && sc.z > 0.0 && sc.z < 1.0) {
                    float bias = cascadeCtl.z * float(c + 1) * float(c + 1);
                    float lit = 0.0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            lit += (sc.z - bias > texture(shadowMap, vec3(suv + vec2(float(dx), float(dy)) * ts, float(c))).r) ? 0.0 : 1.0;
                    litSum += w * (lit / 9.0);
                    wSum += w;
                }
            }
            if (wSum > 0.0) {
                float lit = litSum / wSum;
                float lastSplit = cascadeSplits[nC - 1];
                float fade = clamp((lastSplit - eyeDepth) / max(cascadeCtl.y, 0.001), 0.0, 1.0);
                float strength = (1.0 - lit) * fade * clamp(vFogTC, 0.0, 1.0);
                r0.rgb *= mix(1.0, shadowCtl.z, strength);
            }
        }
    }

    if (alphaRef.z > 0.5) {
        float cov = clamp((r0.a - alphaRef.x) / max(fwidth(r0.a), 1e-4) + 0.5, 0.0, 1.0);
        if (cov <= 0.0) discard;
        r0.a = cov;
    } else if (r0.a - alphaRef.x * alphaRef.y < 0.0) discard;

    float luminance= clamp(dot(r0.rgb, rgbEyeCoef.rgb), 0.0, 1.0);
    float nightBlend = clamp(luminance + rgbEyeCoef.a, 0.0, 1.0);
    r0.rgb = mix(vec3(luminance), r0.rgb, nightBlend);

    r0.rgb = mix(fogColor.rgb, r0.rgb, vFogTC);
    fragColor = alphaRef.w > 0.5 ? vec4(1.0, 0.0, 0.0, 1.0) : r0;
}