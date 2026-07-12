#version 450
//
// Vulkan screen-space (2D / HUD / text) vertex shader.
//
// Mirrors the GL33 vsScreen convention: vertices are pre-transformed
// (D3DFVF_XYZRHW). pos.xy is in screen pixel coordinates; a 2-vector
// vpScale = {2/width, 2/height} maps pixels to [-1,+1] clip range.
// In Vulkan NDC y=-1 is the TOP of the framebuffer, so pixel y=0 (top)
// maps to y_ndc=-1 and pixel y=height (bottom) maps to y_ndc=+1.
// rhw is the reciprocal-w; for typical 2D draws rhw = 1.0.
//
// Vertex layout matches the shared TLVertex struct (pos.xyz, rhw, color,
// uv0). specular and the second texcoord are not consumed by the minimal
// 2D path; they remain in the struct for ABI parity with GL33.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in float inRhw;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inTexcoord;
layout(location = 4) in vec4 inSpecular;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vTexcoord;
layout(location = 2) out float vFogFactor;

layout(push_constant) uniform ScreenConstants
{
    vec2 vpScale; // {2/width, 2/height}
    uint alphaMode;
    float alphaRef;
    vec4 fogColor;
    uint fogMode;
    vec3 _pad;
} pc;

void main()
{
    float w = 1.0 / inRhw;
    gl_Position.x = (inPosition.x * pc.vpScale.x - 1.0) * w;
    gl_Position.y = (inPosition.y * pc.vpScale.y - 1.0) * w;
    gl_Position.z = inPosition.z * w;
    gl_Position.w = w;

    vColor = inColor.zyxw;
    vTexcoord = inTexcoord;
    vFogFactor = inSpecular.a;
}
