#version 330 core
layout(std140) uniform PSConstants {
    vec4 fogColor;
    vec4 alphaRef;      // {ref, enabled, 0, 0}
    vec4 shadowCtl;   // c2: {enable, bias, darkness, texelSize}
    vec4 constColor; // c3: per-object IsColored tint (white = no-op)
    vec4 _pad4;
    vec4 _pad5;
    vec4 _pad6;
    vec4 rgbEyeCoef;
};

uniform sampler2D tex0;

in vec4 vColor;
in vec2 vUV0;

uniform sampler2DArray shadowMap; // unit 2 — cascade depth-map array (unused unless shadowCtl.x>0.5)
in vec3 vWorldRel;

out vec4 fragColor;

void main() {
    gl_FragDepth = gl_FragCoord.z;

    float a = vColor.a * texture(tex0, vUV0).a;
    if (a - alphaRef.x * alphaRef.y < 0.0) discard;

    fragColor = vec4(0.0, 0.0, 0.0, a);
}