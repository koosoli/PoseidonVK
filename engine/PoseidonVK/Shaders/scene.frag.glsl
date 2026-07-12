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
    vec4 shadowCtl;
    mat4 cascadeVP[4];
    vec4 cascadeSplits;
    vec4 cascadeCtl;
    vec4 camFwd;
    vec4 camPos;
    vec4 specularColor;
    vec4 specularCtrl;
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
    vec4 tint;
};

layout(set = 0, binding = 1, std430) readonly buffer DrawConstantsBuffer
{
    DrawConstants draws[];
} drawConstants;

layout(set = 0, binding = 2) uniform sampler2DArray shadowMap;
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
const uint kPassTerrainOpaque = 12u;
const uint kLightingLit         = 0u;
const uint kLightingSunDisabled = 1u;
const uint kFogEnabled          = 0u;

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
    uint pass      = hasDraw ? drawConstants.draws[drawIdx].pass     : 0u;
    vec4 tint      = hasDraw ? drawConstants.draws[drawIdx].tint     : vec4(1.0);

    // -----------------------------------------------------------------------
    // Directional (sun) + local lighting
    // Lit and sun-disabled draws receive positioned lights. Only fully lit
    // draws receive directional sun lighting; unlit draws stay full-bright.
    // -----------------------------------------------------------------------
    uint lighting = hasDraw ? drawConstants.draws[drawIdx].lighting : kLightingLit;
    bool litDraw = hasDraw && (lighting == kLightingLit || lighting == kLightingSunDisabled);
    vec3 light = vec3(1.0f); // default: unlit / full-bright

    if (litDraw)
    {
        vec3 rawSunDir = frame.sunDirection.xyz;
        vec3 sunDir    = length(rawSunDir) > 0.0001f ? normalize(rawSunDir) : vec3(0.0f, -1.0f, 0.0f);
        float sunOn = (lighting == kLightingLit && frame.lightingParams.x > 0.5) ? 1.0 : 0.0;
        vec3 normal = normalize(vWorldNormal);
        if (pass == kPassTerrainOpaque)
        {
            // Preserve the legacy segment mesh while restoring some local
            // terrain relief from the actual rasterized heightfield surface.
            vec3 geometricNormal = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
            if (dot(geometricNormal, normal) < 0.0)
                geometricNormal = -geometricNormal;
            normal = normalize(mix(normal, geometricNormal, 0.35));
        }
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
    // Shader-family blending
    // -----------------------------------------------------------------------
    vec3  baseColor;
    float baseAlpha;

    if (family == kFamilyShadow)
    {
        baseColor = vec3(0.0);
        baseAlpha = c0.a;
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
        if (frame.specularCtrl.x > 0.5 && frame.lightingParams.x > 0.5)
        {
            vec3 n = normalize(vWorldNormal);
            vec3 v = normalize(frame.camPos.xyz - vWorldPos);
            vec3 l = normalize(-frame.sunDirection.xyz);
            vec3 h = normalize(l + v);
            float NdotH = max(dot(n, h), 0.0);
            float specPow = max(frame.specularColor.w, 1.0);
            baseColor += frame.specularColor.rgb * pow(NdotH, specPow);
        }
        baseAlpha = c0.a;
    }

    baseColor *= tint.rgb;
    baseAlpha *= tint.a;

    float refValue = float(alphaRef) / 255.0;
    if (alphaMode == 3u) // TestAndBlend — coverage-AA discard
    {
        float cov = clamp((baseAlpha - refValue) / max(fwidth(baseAlpha), 1e-4) + 0.5, 0.0, 1.0);
        if (cov <= 0.0) discard;
        baseAlpha = cov;
    }
    else if (alphaMode == 1u && baseAlpha < refValue)
    {
        discard;
    }

    if (family == kFamilyShadow)
    {
        outColor = vec4(0.0, 0.0, 0.0, baseAlpha);
        return;
    }

    // -----------------------------------------------------------------------
    // Cascade shadow map lookup
    // -----------------------------------------------------------------------
    uint fogMode = hasDraw ? drawConstants.draws[drawIdx].fog : 0u;
    if (family != kFamilyFlat && family != kFamilyWater && fogMode == kFogEnabled && frame.shadowCtl.x > 0.5)
    {
        int nC = int(frame.cascadeCtl.x);
        int omniN = int(frame.cascadeCtl.w);
        float eyeDepth = dot(vWorldPos, frame.camFwd.xyz);
        float dist3D = length(vWorldPos);
        int ci = nC;
        for (int i = 0; i < 4; ++i)
        {
            if (i >= nC) break;
            float metric = (i < omniN) ? dist3D : eyeDepth;
            if (metric <= frame.cascadeSplits[i]) { ci = i; break; }
        }
        if (ci < nC)
        {
            float ts = frame.shadowCtl.w;
            float prevEdge = (ci > 0) ? frame.cascadeSplits[ci - 1] : 0.0;
            float ciMetric = (ci < omniN) ? dist3D : eyeDepth;
            float band = (frame.cascadeSplits[ci] - prevEdge) * 0.15;
            float bw = (ci + 1 < nC) ? clamp((ciMetric - (frame.cascadeSplits[ci] - band)) / max(band, 0.001), 0.0, 1.0) : 0.0;
            float litSum = 0.0;
            float wSum = 0.0;
            for (int p = 0; p < 4; ++p)
            {
                int c = ci + p;
                if (c >= nC) break;
                float w = (p == 0) ? (1.0 - bw) : ((wSum <= 0.0) ? 1.0 : ((p == 1) ? bw : 0.0));
                if (w <= 0.0) continue;
                vec4 cp = frame.cascadeVP[c] * vec4(vWorldPos, 1.0);
                vec3 sc = cp.xyz / cp.w;
                vec2 suv = vec2(sc.x * 0.5 + 0.5, 0.5 - sc.y * 0.5);
                if (suv.x > 0.0 && suv.x < 1.0 && suv.y > 0.0 && suv.y < 1.0 && sc.z > 0.0 && sc.z < 1.0)
                {
                    float bias = frame.cascadeCtl.z * float(c + 1) * float(c + 1);
                    float lit = 0.0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            lit += (sc.z - bias > texture(shadowMap, vec3(suv + vec2(float(dx), float(dy)) * ts, float(c))).r) ? 0.0 : 1.0;
                    litSum += w * (lit / 9.0);
                    wSum += w;
                }
            }
            if (wSum > 0.0)
            {
                float lit = litSum / wSum;
                float lastSplit = frame.cascadeSplits[nC - 1];
                float fade = clamp((lastSplit - eyeDepth) / max(frame.cascadeCtl.y, 0.001), 0.0, 1.0);
                float strength = (1.0 - lit) * fade * clamp(vFogFactor, 0.0, 1.0);
                baseColor.rgb *= mix(1.0, frame.shadowCtl.z, strength);
            }
        }
    }

    // Match GL33's dusk scotopic adjustment. It applies to normal/detail
    // materials only; active NVG suppresses it on the CPU.
    if (family == kFamilyNormal || family == kFamilyDetail)
    {
        float luminance = clamp(dot(baseColor, vec3(0.2, 0.9, 0.4)), 0.0, 1.0);
        float nightBlend = clamp(luminance + (1.0 - frame.lightingParams.w), 0.0, 1.0);
        baseColor = mix(vec3(luminance), baseColor, nightBlend);
    }

    // -----------------------------------------------------------------------
    // Fog: vFogFactor=1 near (no fog), 0 far (full fog).
    // -----------------------------------------------------------------------
    vec3 fogged = mix(frame.fogColor.rgb, baseColor, vFogFactor);
    float luma = dot(fogged, vec3(0.2126, 0.7152, 0.0722));
    fogged = mix(vec3(luma), fogged, 1.08);
    // Partial gamma boost to compensate for UNORM swapchain (no hardware sRGB encode).
    // pow(x, 1/1.5) is between no boost (too dark) and full sRGB (washed out).
    fogged = pow(fogged, vec3(1.0 / 1.5));
    outColor = vec4(fogged, baseAlpha);
}
