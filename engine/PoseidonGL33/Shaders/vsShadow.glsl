#version 330 core
layout(std140) uniform VSConstants {
    mat4 proj;          // c0-c3
    mat4 view;          // c4-c7
    mat4 world;         // c8-c11
    vec4 sunDir;        // c12
    vec4 ambient;       // c13
    vec4 diffuse;       // c14
    vec4 emissive;      // c15
    vec4 fogParam;      // c16
    vec4 camPos;        // c17
    vec4 specular;      // c18
    vec4 specEn;        // c19
    vec4 sunEn;         // c20
    vec4 vpScale;       // c21 — VSScreen only, declared for layout parity
    vec4 _pad22;
    vec4 _pad23;
    mat4 texMat0;       // c24-c27
    mat4 texMat1;       // c28-c31
    vec4 texCtrl;       // c32
};

layout(std140) uniform WorldInstances {
    mat4 worldArr[256];
};

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

out vec4 vColor;
out vec4 vSpecColor;
out vec2 vUV0;
out vec2 vUV1;
out float vFogTC;
out vec3 vWorldRel;

void main() {
    vec4 worldPos = worldArr[gl_InstanceID] * vec4(pos, 1.0);
    gl_Position   = proj * view * worldPos;
    vColor        = diffuse;
    vSpecColor    = vec4(0.0);
    vUV0          = (texCtrl.x > 0.5) ? (texMat0 * vec4(uv, 0, 1)).xy : uv;
    vUV1          = vUV0;
    vFogTC        = 1.0;
    vWorldRel     = vec3(0.0);
}