#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec2 vTexcoord0;
layout(location = 3) in vec2 vTexcoord1;
layout(location = 4) in float vFogFactor;
layout(location = 5) flat in uint vDrawIndex;

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
} frame;

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

layout(set = 1, binding = 0) uniform sampler2D tex0;
layout(set = 2, binding = 0) uniform sampler2D tex1;

// ShaderFamily enum values (mirrors render::ShaderFamily in RenderPassDescriptor.hpp)
// 0 = Normal   1 = Shadow   2 = Water   3 = Detail   4 = Grass   5 = Flat
const uint kFamilyNormal  = 0u;
const uint kFamilyShadow  = 1u;
const uint kFamilyWater   = 2u;
const uint kFamilyDetail  = 3u;
const uint kFamilyGrass   = 4u;
const uint kFamilyFlat    = 5u;

void main()
{
    // -----------------------------------------------------------------------
    // Per-draw constants
    // -----------------------------------------------------------------------
    bool hasDraw   = drawConstants.draws.length() > 0u;
    uint drawIdx   = hasDraw ? min(vDrawIndex, drawConstants.draws.length() - 1u) : 0u;
    uint alphaMode = hasDraw ? drawConstants.draws[drawIdx].alpha    : 0u;
    uint alphaRef  = hasDraw ? drawConstants.draws[drawIdx].alphaRef : 0u;
    uint family    = hasDraw ? drawConstants.draws[drawIdx].shader   : kFamilyNormal;

    // -----------------------------------------------------------------------
    // Directional (sun) + local lighting
    // Only applied when DrawConstants.lighting != 0. Unlit draws (HUD markers,
    // flat-shaded icons, kFamilyFlat) get full-bright light = vec3(1.0).
    // -----------------------------------------------------------------------
    bool litDraw = hasDraw && (drawConstants.draws[drawIdx].lighting != 0u);
    vec3 light   = vec3(1.0f); // default: unlit / full-bright

    if (litDraw)
    {
        vec3 rawSunDir = frame.sunDirection.xyz;
        vec3 sunDir    = length(rawSunDir) > 0.0001f ? normalize(rawSunDir) : vec3(0.0f, -1.0f, 0.0f);
        float sunOn    = (frame.lightingParams.x > 0.5) ? 1.0 : 0.0;
        vec3 normal    = normalize(vWorldNormal);
        float diffuse  = max(dot(normal, -sunDir), 0.0f) * sunOn;
        float ambient  = 0.35f;
        light          = vec3(ambient + diffuse * 0.65f);

        const float minInside2 = 0.95677279f;
        const float maxInside2 = 0.98063081f;
        int   localLightCount  = min(int(frame.lightingParams.y + 0.5f), 8);
        float localLightScale  = max(frame.lightingParams.z, 0.0f);
        for (int i = 0; i < localLightCount; ++i)
        {
            vec3  toLight     = frame.localLightPosition[i].xyz - vWorldPos;
            float size2       = dot(toLight, toLight);
            float startAtten2 = frame.localLightPosition[i].w * frame.localLightPosition[i].w;
            float endAtten2   = startAtten2 * 100.0f;
            if (size2 <= 0.0001f || size2 >= endAtten2)
                continue;

            float cone = 1.0f;
            if (frame.localLightDirection[i].w > 0.5f)
            {
                vec3  beamDir = normalize(frame.localLightDirection[i].xyz);
                float inside  = -dot(toLight, beamDir);
                if (inside <= 0.0f)
                    continue;
                float cos2 = (inside * inside) / size2;
                if (cos2 < minInside2)
                    continue;
                cone = clamp((cos2 - minInside2) / (maxInside2 - minInside2), 0.0f, 1.0f);
            }

            float atten     = (size2 >= startAtten2) ? (startAtten2 / size2) : 1.0f;
            float cosFi     = dot(toLight, normal);
            vec3  localDiff = frame.localLightDiffuse[i].rgb  * localLightScale;
            vec3  localAmb  = frame.localLightAmbient[i].rgb  * localLightScale;
            if (cosFi > 0.0f)
            {
                cosFi *= inversesqrt(size2);
                light += (localDiff * cosFi + localAmb) * (atten * cone);
            }
            else
            {
                light += localAmb * atten;
            }
        }
        light = clamp(light, 0.0f, 1.0f);
    }

    // -----------------------------------------------------------------------
    // Texture samples
    // -----------------------------------------------------------------------
    vec4 c0 = texture(tex0, vTexcoord0);
    vec4 c1 = texture(tex1, vTexcoord1);

    // -----------------------------------------------------------------------
    // Alpha test on c0 (all families)
    // -----------------------------------------------------------------------
    float refValue = float(alphaRef) / 255.0;
    if (alphaMode == 3u) // TestAndBlend — coverage-AA discard
    {
        float cov = clamp((c0.a - refValue) / max(fwidth(c0.a), 1e-4) + 0.5, 0.0, 1.0);
        if (cov <= 0.0) discard;
        c0.a = cov;
    }
    else if (alphaMode == 1u) // Test — hard cutout
    {
        if (c0.a < refValue) discard;
    }

    // -----------------------------------------------------------------------
    // Shader-family blending
    // -----------------------------------------------------------------------
    vec3  baseColor;
    float baseAlpha;

    if (family == kFamilyShadow)
    {
        // Shadow silhouette: black, alpha from tex0.
        // Full material-alpha parity (constColor.a) deferred until per-draw
        // constColor is uploaded; tex0.a is the best current approximation.
        outColor = vec4(0.0, 0.0, 0.0, c0.a);
        return;
    }
    else if (family == kFamilyDetail)
    {
        // Detail blend: lit base color, rgb attenuated by tex1.a * 2 (AT2X).
        baseColor = c0.rgb * light;
        baseColor *= clamp(c1.a * 2.0, 0.0, 1.0);
        baseAlpha = c0.a;
    }
    else if (family == kFamilyGrass)
    {
        // Grass: rgb = lit base modulated by tex1.rgb * 2.
        //        a   = grass coefficient blend with tex1.a.
        baseColor = c0.rgb * light * clamp(c1.rgb * 2.0, vec3(0.0), vec3(1.0));
        float grassCoef = frame.grassParams.x;
        baseAlpha = clamp((grassCoef * 2.0 - 1.0) + c1.a, 0.0, 1.0);
    }
    else if (family == kFamilyWater)
    {
        // Water: bump-normal from tex1 rgb, specular highlight from normal dot.
        vec3  bumpN    = normalize(c1.rgb * 2.0 - 1.0);
        float specular = max(dot(bumpN, vec3(0.0, 1.0, 0.0)), 0.0f);
        specular = pow(specular, 8.0);
        baseColor = c0.rgb * light + vec3(specular * 0.4);
        baseAlpha = c0.a;
    }
    else if (family == kFamilyFlat)
    {
        // Flat: unlit — tex0 color at full brightness regardless of light.
        baseColor = c0.rgb;
        baseAlpha = c0.a;
    }
    else // kFamilyNormal — single-texture directionally lit
    {
        baseColor = c0.rgb * light;
        baseAlpha = c0.a;
    }

    // -----------------------------------------------------------------------
    // Fog: vFogFactor=1 near (no fog), 0 far (full fog).
    // -----------------------------------------------------------------------
    vec3 fogged = mix(frame.fogColor.rgb, baseColor, vFogFactor);
    outColor = vec4(fogged, baseAlpha);
}
