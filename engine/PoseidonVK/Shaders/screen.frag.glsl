#version 450
//
// Vulkan screen-space (2D / HUD / text) fragment shader.
//
// Minimal 2D subset of the GL33 psNormal path: modulate the vertex color
// by the sampled texture. Alpha-test (IsAlpha spec) discards transparent
// texels so menu/HUD quads with 1-bit alpha stay crisp. Fog, night-eye,
// and shadow modulation are deferred to a later parity pass.

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexcoord;
layout(location = 2) in float vFogFactor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D tex0;

layout(push_constant) uniform ScreenConstants
{
    vec2 vpScale;
    uint alphaMode;
    float alphaRef;
    vec4 fogColor;
    uint fogMode;
    vec3 _pad;
} pc;

void main()
{
    vec4 c = vColor * texture(tex0, vTexcoord);
    if (pc.alphaMode == 3u)
    {
        c.a = clamp((c.a - pc.alphaRef) / max(fwidth(c.a), 1e-4) + 0.5, 0.0, 1.0);
    }
    else if (pc.alphaMode == 1u && c.a < pc.alphaRef)
    {
        discard;
    }
    if (c.a <= 0.0)
        discard;
    if (pc.fogMode == 0u)
        c.rgb = mix(pc.fogColor.rgb, c.rgb, clamp(vFogFactor, 0.0, 1.0));
    float luma = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));
    c.rgb = mix(vec3(luma), c.rgb, 1.08);
    // Partial gamma boost to compensate for UNORM swapchain.
    c.rgb = pow(c.rgb, vec3(1.0 / 1.5));
    outColor = c;
}
