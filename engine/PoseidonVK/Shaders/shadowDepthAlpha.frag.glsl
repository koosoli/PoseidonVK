#version 450

layout(location = 0) in vec2 vUV;

layout(push_constant) uniform ShadowPC
{
    mat4 lightVP;
    vec4 worldColumns[3];
    vec3 translation;
    float alphaCutoff;
} pc;

layout(set = 0, binding = 0) uniform sampler2D uTex;

void main()
{
    if (texture(uTex, vUV).a < pc.alphaCutoff)
    {
        discard;
    }
}
