#version 450
#ifdef POSEIDON_GPU_SCENE
#extension GL_ARB_shader_draw_parameters : enable
#endif

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexcoord;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec2 vTexcoord0;
layout(location = 3) out vec2 vTexcoord1;
layout(location = 4) out float vFogFactor;
layout(location = 5) flat out uint vDrawIndex;

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
    vec4 sunDirection;
    vec4 localLightPosition[8];
    vec4 localLightDiffuse[8];
    vec4 localLightAmbient[8];
    vec4 localLightDirection[8];
    vec4 grassParams;
    vec4 time;
    vec4 camPos;
    vec4 specularColor;
    vec4 specularCtrl;
} frame;

// Per-draw constants uploaded by the host from the backend-neutral frame plan.
// Mirrors the C++ Poseidon::vk::DrawConstantsVK layout (std430, 176 bytes).
struct DrawConstants
{
    mat4 world;
    uint textureIds[4];
    uint meshId;
    uint indexBegin;
    uint indexCount;
    uint pass;
    uint depth;
    uint blend;
    uint fog;
    uint cull;
    uint frontFace;
    uint alpha;
    uint lighting;
    uint texGen;
    uint surface;
    uint samplerFilter;
    uint samplerClamp;
    uint shader;
    uint alphaRef;
    uint stencilExclusion;
    uint reserved[2];
    vec4 tint;
};

layout(set = 0, binding = 1, std430) readonly buffer DrawConstantsBuffer
{
    DrawConstants draws[];
} drawConstants;

layout(push_constant) uniform SceneDraw
{
    mat4 world;
    // Non-zero when the host uploaded at least one DrawConstants entry to the
    // SSBO; the shader then prefers the selected per-draw world over the fallback.
    uint useDrawConstants;
    uint drawIndex;
} draw;

const uint kTexGenNone   = 0u;
const uint kTexGenFixed  = 1u;
const uint kTexGenWater  = 2u;
const uint kTexGenDetail = 3u;
const uint kTexGenGrass  = 4u;

void main()
{
    bool hasDrawConstants = draw.useDrawConstants != 0u && drawConstants.draws.length() > 0u;
    // Indirect scene commands encode their DrawConstants index in
    // firstInstance.  Direct/legacy draws retain the push-constant index.
    uint selectedDrawIndex = draw.drawIndex;
#ifdef POSEIDON_GPU_SCENE
    selectedDrawIndex = gl_BaseInstanceARB != 0u ? gl_BaseInstanceARB : draw.drawIndex;
#endif
    uint drawIndex = hasDrawConstants ? min(selectedDrawIndex, drawConstants.draws.length() - 1u) : 0u;
    mat4 world = hasDrawConstants ? drawConstants.draws[drawIndex].world : draw.world;
    vec4 worldPos = world * vec4(inPosition, 1.0);
    vec3 worldNormal = normalize(mat3(world) * inNormal);

    // Full camera transform mirroring the GL33 vsTransform convention
    // (gl_Position = proj * view * world * pos). frame.projection already
    // carries the engine's D3D-origin row-major projection, which maps to the
    // Vulkan 0..1 depth range the depth attachment uses.
    vec4 viewPos = frame.view * worldPos;
    gl_Position = frame.projection * viewPos;

    vWorldPos = worldPos.xyz;
    vWorldNormal = worldNormal;
    vDrawIndex = drawIndex;

    // Apply UV scaling and animation based on texGen
    uint texGen = hasDrawConstants ? drawConstants.draws[drawIndex].texGen : kTexGenNone;
    if (texGen == kTexGenDetail || texGen == kTexGenGrass)
    {
        vTexcoord0 = inTexcoord;
        vTexcoord1 = inTexcoord * 32.0;
    }
    else if (texGen == kTexGenWater)
    {
        float t = frame.time.x;
        float mw1 = sin(t * 0.04);
        float mw2 = mod(t * 0.3 + sin(t * 0.5) * 0.5, 2.0);
        vTexcoord0 = inTexcoord + vec2(mw1 * 0.5, mw1);
        vTexcoord1 = inTexcoord * 64.0 + vec2(mw2 * 0.5, mw2);
    }
    else
    {
        vTexcoord0 = inTexcoord;
        vTexcoord1 = inTexcoord;
    }

    // Per-draw fog control (mirrors FogMode in RenderPassDescriptor.hpp):
    //   0 = Enabled  — standard distance fog
    //   1 = Disabled — no fog (sky, cockpit, first-person)
    //   2 = AlphaFog — alpha-channel attenuation (treated as enabled for now)
    // When disabled the factor is forced to 1.0 (no fog mixing in the fragment shader).
    uint fogMode = hasDrawConstants ? drawConstants.draws[drawIndex].fog : 0u;
    float dist = length(viewPos.xyz);
    // Exponential (power) fog ramp: replaces linear fog to stay clear across near/mid-field
    // and fade smoothly near the far edge. Matches the visibility boost from the wgpu fork.
    float u = clamp(dist / max(frame.fogParams.y, 1.0), 0.0, 1.0);
    float fogFactor = 1.0 - pow(u, 3.0);
    vFogFactor = (fogMode == 0u && frame.fogParams.w > 0.5) ? fogFactor : 1.0;
}
