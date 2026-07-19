#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec2 vLandUv;
layout(location = 3) flat in uvec2 vLandCell;
layout(location = 4) flat in uint vLod;
layout(location = 0) out vec4 outColor;

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
    vec4 shadowCtl;
    mat4 cascadeVP[4];
    vec4 cascadeSplits;
    vec4 cascadeCtl;
    vec4 camFwd;
    vec4 camPos;
    vec4 specularColor;
    vec4 specularCtrl;
    vec4 cloudOrigin;
    vec4 wind;
    vec4 windOffset;
} frame;
layout(set = 0, binding = 2) uniform sampler2DArrayShadow shadowMap;

// TerrainVK descriptor set. Index high bit is the captured ClampU|ClampV
// transition bit; the lower 15 bits are the native TextureVK layer index.
layout(set = 1, binding = 0) uniform sampler2D heightmap;
layout(set = 1, binding = 1) uniform usampler2D indexMap;
layout(set = 1, binding = 2) uniform sampler2D jitterMap;
layout(set = 1, binding = 3, std140) uniform TerrainParams
{
    vec2 worldOrigin;
    float landGrid;
    float terrainGrid;
    uvec4 dimensions;
    vec4 water;
    vec4 wetness;
} terrain;
// WGPU's material contract owns these two samplers at terrain scope. Layer
// descriptors are sampled images, rather than TextureVK combined samplers:
// bit 15 chooses repeat or clamp without changing the underlying layer image.
layout(set = 1, binding = 4) uniform sampler terrainRepeatSampler;
layout(set = 1, binding = 5) uniform sampler terrainClampSampler;
layout(set = 1, binding = 6) uniform texture2D terrainLayers[];

// Required visual receiver set. No fallback is legal for these resources:
// terrain self-shadow, detail, and sky visibility are distinct from CSM and
// must be supplied together by the terrain pass.
layout(set = 2, binding = 0) uniform sampler2D terrainSelfShadow;
layout(set = 2, binding = 1) uniform sampler2D terrainDetail;
layout(set = 2, binding = 2) uniform sampler2D terrainSkyVisibility;

const uint kTransitionBit = 0x8000u;
const uint kLayerMask = 0x7fffu;

// Fine heightfield sample used by the ceiling comparison.  This is kept in the
// fragment stage (rather than consuming the CDLOD-interpolated y) so distant
// node morphing cannot turn into patch-sized shadow flicker.
float HeightAt(vec2 worldXZ)
{
    vec2 texel = vec2(terrain.dimensions.xy - uvec2(1u));
    vec2 uv = (worldXZ - terrain.worldOrigin) / max(terrain.terrainGrid * texel, vec2(0.0001));
    return texture(heightmap, clamp(uv, vec2(0.0), vec2(1.0))).r;
}

uint LandEntry(ivec2 landCell)
{
    ivec2 extent = ivec2(terrain.dimensions.z);
    return texelFetch(indexMap, clamp(landCell, ivec2(0), extent - ivec2(1)), 0).r;
}

vec4 SampleNativeLayer(uint entry, vec2 tileUv, vec2 tileUvDx, vec2 tileUvDy)
{
    const uint layer = entry & kLayerMask;
    uint safeLayer = min(layer, terrain.dimensions.w - 1u);

    if (layer >= terrain.dimensions.w)
        return vec4(0.0);

    const bool transition = (entry & kTransitionBit) != 0u;
    if (transition)
        return textureGrad(sampler2D(terrainLayers[nonuniformEXT(safeLayer)], terrainClampSampler),
                           tileUv, tileUvDx, tileUvDy);
    return textureGrad(sampler2D(terrainLayers[nonuniformEXT(safeLayer)], terrainRepeatSampler),
                       tileUv, tileUvDx, tileUvDy);
}

// Keep terrain on the same receiver-bias and PCF contract as scene meshes.
// Terrain is the largest shadow receiver, so a constant-bias 2x2 lookup here
// would make tree shadows visibly change character as they cross onto ground.
float CascadeShadow(vec3 worldPosition, vec3 worldNormal, vec3 worldDx, vec3 worldDy)
{
    if (frame.shadowCtl.x <= 0.5 || frame.cascadeCtl.x < 0.5)
        return 1.0;
    float eyeDepth = dot(worldPosition, frame.camFwd.xyz);
    int count = int(frame.cascadeCtl.x);
    int cascade = count;
    for (int i = 0; i < 4 && i < count; ++i)
        if (eyeDepth <= frame.cascadeSplits[i]) { cascade = i; break; }
    if (cascade >= count)
        return 1.0;
    mat4 vp = frame.cascadeVP[cascade];
    vec3 normal = normalize(worldNormal);
    vec3 sunDirection = normalize(-frame.sunDirection.xyz);
    float cosTheta = clamp(dot(normal, sunDirection), -1.0, 1.0);
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float rowMagnitude = max(length(vec3(vp[0][0], vp[1][0], vp[2][0])), 1e-6);
    float texelWorld = 2.0 * frame.shadowCtl.w / rowMagnitude;
    vec4 lightPos = vp * vec4(worldPosition + normal * (2.0 * texelWorld * sinTheta), 1.0);
    vec3 p = lightPos.xyz / lightPos.w;
    vec2 uv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    if (any(lessThanEqual(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0))) || p.z <= 0.0 || p.z >= 1.0)
        return 1.0;
    vec4 lightDx = vp * vec4(worldDx, 0.0);
    vec4 lightDy = vp * vec4(worldDy, 0.0);
    vec2 uvDx = vec2(0.5 * lightDx.x, -0.5 * lightDx.y);
    vec2 uvDy = vec2(0.5 * lightDy.x, -0.5 * lightDy.y);
    float determinant = uvDx.x * uvDy.y - uvDx.y * uvDy.x;
    vec2 depthGradient = vec2(0.0);
    if (abs(determinant) > 1e-12)
        depthGradient = vec2(lightDx.z * uvDy.y - lightDy.z * uvDx.y,
                             lightDy.z * uvDx.x - lightDx.z * uvDy.x) / determinant;
    float gradientLimit = 0.02 / max(frame.shadowCtl.w, 1e-6);
    depthGradient = clamp(depthGradient, vec2(-gradientLimit), vec2(gradientLimit));
    float planeBias = min(2.0 * frame.shadowCtl.w * (abs(depthGradient.x) + abs(depthGradient.y)), 0.01);
    float referenceDepth = p.z - frame.cascadeCtl.z * float((cascade + 1) * (cascade + 1)) - planeBias;
    float filtered = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 offset = vec2(float(x), float(y)) * frame.shadowCtl.w;
            float weight = (2.0 - abs(float(x))) * (2.0 - abs(float(y)));
            float depthOffset = clamp(dot(offset, depthGradient), -0.02, 0.02);
            filtered += weight * texture(shadowMap, vec4(uv + offset, float(cascade), referenceDepth + depthOffset));
        }
    }
    return filtered * (1.0 / 16.0);
}

vec3 TerrainLighting(vec3 normal, float directVisibility, float skyVisibility)
{
    vec3 n = normalize(normal);
    // The sky-view mask attenuates only ambient light.  CSM and the terrain
    // heightfield sweep attenuate only the direct sun; local lights remain
    // independent receivers just as they do in the WGPU reference.
    vec3 light = vec3(0.35 * skyVisibility);
    if (frame.lightingParams.x > 0.5)
        light += vec3(max(dot(n, -normalize(frame.sunDirection.xyz)), 0.0) * 0.65 * directVisibility);
    int count = min(int(frame.lightingParams.y + 0.5), 8);
    for (int i = 0; i < count; ++i)
    {
        vec3 toLight = frame.localLightPosition[i].xyz - vWorldPos;
        float d2 = dot(toLight, toLight);
        float range2 = frame.localLightPosition[i].w * frame.localLightPosition[i].w * 100.0;
        if (d2 <= 0.0001 || d2 >= range2) continue;
        float attenuation = min(1.0, frame.localLightPosition[i].w * frame.localLightPosition[i].w / d2);
        float diffuse = max(dot(n, normalize(toLight)), 0.0);
        light += (frame.localLightAmbient[i].rgb + frame.localLightDiffuse[i].rgb * diffuse) * attenuation * frame.lightingParams.z;
    }
    return light;
}

void main()
{
    // The four neighboring land cells are blended continuously. Keep tileUv
    // local for the clamp-transition layer, but derive gradients from the
    // continuous coordinate so a cell edge neither aliases nor forces mip 0.
    vec2 landUv = vLandUv;
    vec2 tileUv = fract(landUv);
    ivec2 landCell = ivec2(floor(landUv));
    vec2 weights = tileUv;
    vec2 tileUvDx = dFdx(landUv);
    vec2 tileUvDy = dFdy(landUv);

    // The R8G8_SNORM map contains the already captured random offsets. Sample
    // it linearly in continuous land space; applying another 0.1 scale here
    // would attenuate the reference jitter a second time.
    vec2 jitterMapUv = (landUv + vec2(0.5)) / max(vec2(terrain.dimensions.z), vec2(1.0));
    vec2 jitter = texture(jitterMap, clamp(jitterMapUv, vec2(0.0), vec2(1.0))).rg;
    vec2 materialUv = tileUv + jitter;

    vec4 lower = mix(SampleNativeLayer(LandEntry(landCell), materialUv, tileUvDx, tileUvDy),
                     SampleNativeLayer(LandEntry(landCell + ivec2(1, 0)), materialUv, tileUvDx, tileUvDy),
                     weights.x);
    vec4 upper = mix(SampleNativeLayer(LandEntry(landCell + ivec2(0, 1)), materialUv, tileUvDx, tileUvDy),
                     SampleNativeLayer(LandEntry(landCell + ivec2(1, 1)), materialUv, tileUvDx, tileUvDy),
                     weights.x);
    vec4 albedo = mix(lower, upper, weights.y);

    // The detail texture's own alpha is the modulation weight.
    const float kDetailTileScale = 32.0 * 2.0;
    vec4 detail = textureGrad(terrainDetail, tileUv * kDetailTileScale,
                              tileUvDx * kDetailTileScale, tileUvDy * kDetailTileScale);
    albedo.rgb *= mix(vec3(1.0), clamp(detail.rgb * 2.0, vec3(0.0), vec3(2.0)), detail.a);

    // Terrain vertices and the scene pass both carry absolute world positions.
    // Keeping the terrain receiver on that contract lets it share the world
    // depth attachment with structures and other opaque geometry.
    vec2 worldXZ = vWorldPos.xz;
    float worldY = vWorldPos.y;

    // The self-shadow resource is world-aligned and deliberately has an
    // extended (normally 2x) grid.  It stores a ceiling rather than a baked
    // scalar: use the fine heightfield sample to keep the test independent of
    // CDLOD morphing, then compose terrain occlusion and CSM by max().
    vec2 maskDims = vec2(textureSize(terrainSelfShadow, 0));
    vec2 maskScale = maskDims / vec2(terrain.dimensions.xy);
    vec2 maskCoord = (worldXZ - terrain.worldOrigin) / terrain.terrainGrid * maskScale;
    vec4 shadowSample = texture(terrainSelfShadow, (maskCoord + vec2(0.5)) / maskDims);
    float fineHeight = HeightAt(worldXZ);
    float selfLit = smoothstep(shadowSample.r - shadowSample.g,
                               shadowSample.r + shadowSample.g + 0.001, fineHeight);
    float terrainOcclusion = clamp(shadowSample.b * (1.0 - selfLit), 0.0, 1.0);
    float csmOcclusion = 1.0 - CascadeShadow(vWorldPos, vWorldNormal, dFdx(vWorldPos), dFdy(vWorldPos));
    float directVisibility = 1.0 - max(csmOcclusion, terrainOcclusion);
    vec2 skyDims = vec2(textureSize(terrainSkyVisibility, 0));
    vec2 skyCoord = (worldXZ - terrain.worldOrigin) / terrain.terrainGrid;
    float skyVisibility = texture(terrainSkyVisibility, (skyCoord + vec2(0.5)) / skyDims).r;
    vec3 color = albedo.rgb * TerrainLighting(vWorldNormal, directVisibility, skyVisibility);

    // Wet band follows the captured sea-level parameters and is applied before
    // fog/HDR composition, preserving linear lighting for the world target.
    float wet = 1.0 - smoothstep(terrain.water.x, terrain.water.x + terrain.wetness.x, worldY);
    color *= mix(vec3(1.0), vec3(terrain.wetness.y), wet);
    float distanceToCamera = length(vWorldPos);
    float fog = frame.fogParams.w > 0.5 ? 1.0 - pow(clamp(distanceToCamera / max(frame.fogParams.y, 1.0), 0.0, 1.0), 3.0) : 1.0;
    // Terrain is always fully opaque. The texture alpha channel carries no
    // coverage data and must not be forwarded into the HDR world buffer
    // (world_composite passes world.a to the swapchain which causes see-through).
    outColor = vec4(mix(frame.fogColor.rgb, color, fog), 1.0);
}
