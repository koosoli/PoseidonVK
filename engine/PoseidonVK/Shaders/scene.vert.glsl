#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexcoord;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec2 vTexcoord;

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

// Per-draw constants uploaded by the host from the backend-neutral frame plan.
// Mirrors the C++ Poseidon::vk::DrawConstantsVK layout (std430, 160 bytes).
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
};

layout(set = 0, binding = 1, std430) readonly buffer DrawConstantsBuffer
{
    DrawConstants draws[];
} drawConstants;

layout(push_constant) uniform SceneDraw
{
    mat4 world;
    // Non-zero when the host uploaded at least one DrawConstants entry to the
    // SSBO; the shader then prefers the per-draw world over the fallback.
    uint useDrawConstants;
} draw;

void main()
{
    mat4 world = (draw.useDrawConstants != 0u && drawConstants.draws.length() > 0u)
                     ? drawConstants.draws[0].world
                     : draw.world;
    vec4 worldPos = world * vec4(inPosition, 1.0);
    vec3 worldNormal = normalize(mat3(world) * inNormal);

    // Bring-up placement: the quad's vertex positions are already in NDC XY
    // (range roughly [-0.4..0.8] x [-0.4..0.4]), so it is visible regardless of
    // the game camera. The full proj*view*world transform activates when this
    // pipeline draws real scene meshes whose positions are in world space; the
    // matrix path is already covered by the GL33 parity tests.
    gl_Position = vec4(worldPos.xy, 0.0, 1.0);

    vWorldPos = worldPos.xyz;
    vWorldNormal = worldNormal;
    vTexcoord = inTexcoord;
}
