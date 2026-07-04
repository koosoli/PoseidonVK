#version 330 core
// Shared VS UBO; vsScreen reads vpScale at slot 21 (offset 336 bytes).
// The 21-slot prefix is laid out so the byte offsets match what
// vsTransform reads; vsScreen ignores those fields, but std140
// requires the layout to match the shared binding's contents.
layout(std140) uniform VSConstants {
    mat4 _pad_proj;     // slots 0..3 — VSTransform's projection
    mat4 _pad_view;     // slots 4..7
    mat4 _pad_world;    // slots 8..11
    vec4 _pad_sunDir;   // slot 12
    vec4 _pad_ambient;  // 13
    vec4 _pad_diffuse;  // 14
    vec4 _pad_emissive; // 15
    vec4 _pad_fog;      // 16
    vec4 _pad_camPos;   // 17
    vec4 _pad_spec;     // 18
    vec4 _pad_specEn;   // 19
    vec4 _pad_sunEn;    // 20
    vec4 vpScale;       // 21 — {2/width, 2/height, 0, 0}
};

layout(location = 0) in vec3 aPos;
layout(location = 1) in float aRhw;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aSpecular;
layout(location = 4) in vec2 aUV0;
layout(location = 5) in vec2 aUV1;

out vec4 vColor;
out vec4 vSpecColor;
out vec2 vUV0;
out vec2 vUV1;
out float vFogTC;
out vec3 vWorldRel;

void main() {
    float w = 1.0 / aRhw;
    gl_Position.x = (aPos.x * vpScale.x - 1.0) * w;
    gl_Position.y = (1.0 - aPos.y * vpScale.y) * w;
    gl_Position.z = aPos.z * w;
    gl_Position.w = w;
    vColor = aColor;
    vSpecColor = aSpecular;
    vUV0 = aUV0;
    vUV1 = aUV1;
    vFogTC = aSpecular.a;
    vWorldRel = vec3(0.0); // screen draws are never shadow-mapped
}