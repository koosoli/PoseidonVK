#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 0) out vec3 vColor;

layout(push_constant) uniform BootstrapConstants
{
    vec4 viewport;
    vec4 clearColor;
} pc;

layout(set = 0, binding = 0, std140) uniform FrameConstants
{
    mat4 view;
    mat4 projection;
    mat4 sunMatrix;
    vec4 viewport;
    vec4 clipPlanes;
    vec4 worldRect;
    vec4 fogParams;
    vec4 fogColor;
    vec4 lightingParams;
} frame;

void main()
{
    vec4 viewport = frame.viewport.z > 0.0 ? frame.viewport : pc.viewport;
    float aspectComp = viewport.w > 0.0 ? viewport.w / max(viewport.z, 1.0) : 1.0;
    vec2 pos = vec2(inPosition.x * aspectComp, inPosition.y);
    gl_Position = vec4(pos, 0.0, 1.0);
    vColor = inColor;
}
