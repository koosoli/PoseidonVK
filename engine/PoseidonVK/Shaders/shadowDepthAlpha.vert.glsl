#version 450

layout(push_constant) uniform ShadowPC
{
    mat4 lightVP;
    vec4 worldColumns[3];
    vec3 translation;
    float alphaCutoff;
} pc;

layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aUV;

layout(location = 0) out vec2 vUV;

void main()
{
    vUV = aUV;
    mat4 world = mat4(pc.worldColumns[0], pc.worldColumns[1], pc.worldColumns[2], vec4(pc.translation, 1.0));
    gl_Position = pc.lightVP * world * vec4(aPos, 1.0f);
}
