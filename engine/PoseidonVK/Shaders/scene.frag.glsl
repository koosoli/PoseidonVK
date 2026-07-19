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
    vec4 cloudOrigin;
    vec4 wind;
    vec4 windOffset;
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

layout(set = 0, binding = 2) uniform sampler2DArrayShadow shadowMap;
layout(set = 1, binding = 0) uniform sampler2D tex0;
layout(set = 2, binding = 0) uniform sampler2D tex1;
// Cached equirectangular HDR sky radiance.  This is a real environment
// reflection resource; planar terrain/scene reflection and refraction are not
// part of this milestone.
layout(set = 3, binding = 0) uniform sampler2D skyMap;

// ShaderFamily enum values (mirrors render::ShaderFamily in RenderPassDescriptor.hpp)
// 0 = Normal   1 = Shadow   2 = Water   3 = Detail   4 = Grass   5 = Flat
const uint kFamilyNormal  = 0u;
const uint kFamilyShadow  = 1u;
const uint kFamilyWater   = 2u;
const uint kFamilyDetail  = 3u;
const uint kFamilyGrass   = 4u;
const uint kFamilyFlat    = 5u;
const uint kPassWorldOpaque = 0u;
const uint kPassWorldCutout = 1u;
const uint kPassSurfaceOverlay = 6u;
const uint kPassTerrainOpaque = 12u;
const uint kLightingLit         = 0u;
const uint kLightingSunDisabled = 1u;

// Receiver-side CSM evaluation mirrors the reference renderer's stable path:
// move the receiver along its normal, correct the comparison plane per PCF
// tap, then filter a small tent kernel.  Do not fall back to a constant-bias
// single comparison here: it produces the familiar acne/peter-panning trade
// off on tree cutouts and terrain at low sun angles.
float CascadeShadowVisibility(int cascade, vec3 worldPosition, vec3 worldNormal,
                              vec3 worldDx, vec3 worldDy)
{
    mat4 vp = frame.cascadeVP[cascade];
    vec3 sunDirection = normalize(-frame.sunDirection.xyz);
    float cosTheta = clamp(dot(normalize(worldNormal), sunDirection), -1.0, 1.0);
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

    // The orthographic VP x row has magnitude 2 / worldWidth.  This derives
    // a world-space texel size from the actual cascade rather than guessing
    // a bias from its index.
    float rowMagnitude = max(length(vec3(vp[0][0], vp[1][0], vp[2][0])), 1e-6);
    float texelWorld = 2.0 * frame.shadowCtl.w / rowMagnitude;
    vec3 receiver = worldPosition + normalize(worldNormal) * (2.0 * texelWorld * sinTheta);
    vec4 clip = vp * vec4(receiver, 1.0);
    vec3 projected = clip.xyz / clip.w;
    vec2 uv = vec2(projected.x * 0.5 + 0.5, 0.5 - projected.y * 0.5);
    if (any(lessThanEqual(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0))) ||
        projected.z <= 0.0 || projected.z >= 1.0)
        return 1.0;

    // Receiver-plane depth bias (Isidoro): compensate the exact depth slope
    // at each PCF tap.  The clamp handles a nearly singular screen-to-shadow
    // UV transform at grazing angles.
    vec4 clipDx = vp * vec4(worldDx, 0.0);
    vec4 clipDy = vp * vec4(worldDy, 0.0);
    vec2 uvDx = vec2(0.5 * clipDx.x, -0.5 * clipDx.y);
    vec2 uvDy = vec2(0.5 * clipDy.x, -0.5 * clipDy.y);
    float determinant = uvDx.x * uvDy.y - uvDx.y * uvDy.x;
    vec2 depthGradient = vec2(0.0);
    if (abs(determinant) > 1e-12)
    {
        depthGradient = vec2(clipDx.z * uvDy.y - clipDy.z * uvDx.y,
                             clipDy.z * uvDx.x - clipDx.z * uvDy.x) / determinant;
    }
    float gradientLimit = 0.02 / max(frame.shadowCtl.w, 1e-6);
    depthGradient = clamp(depthGradient, vec2(-gradientLimit), vec2(gradientLimit));
    float planeBias = min(2.0 * frame.shadowCtl.w * (abs(depthGradient.x) + abs(depthGradient.y)), 0.01);
    float referenceDepth = projected.z - frame.cascadeCtl.z * float((cascade + 1) * (cascade + 1)) - planeBias;

    // A 3x3 tent of hardware comparison samples: each lookup remains the
    // implementation's bilinear PCF, so this is stable foliage filtering
    // rather than a noisy stochastic approximation.
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

float CloudHash(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float CloudNoise(vec3 p)
{
    vec3 cell = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = CloudHash(cell);
    float n100 = CloudHash(cell + vec3(1.0, 0.0, 0.0));
    float n010 = CloudHash(cell + vec3(0.0, 1.0, 0.0));
    float n110 = CloudHash(cell + vec3(1.0, 1.0, 0.0));
    float n001 = CloudHash(cell + vec3(0.0, 0.0, 1.0));
    float n101 = CloudHash(cell + vec3(1.0, 0.0, 1.0));
    float n011 = CloudHash(cell + vec3(0.0, 1.0, 1.0));
    float n111 = CloudHash(cell + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

float CloudShadow(vec3 cameraRelativePosition)
{
    vec3 toSun = normalize(-frame.sunDirection.xyz);
    if (toSun.y <= 0.02)
        return 1.0;
    vec3 worldPosition = cameraRelativePosition;
    vec2 volumeCentre = (floor(frame.cloudOrigin.xz / 16384.0) + 0.5) * 16384.0;
    if (max(abs(worldPosition.x - volumeCentre.x), abs(worldPosition.z - volumeCentre.y)) > 8192.0)
        return 1.0;
    float travel = max(0.0, (4000.0 - worldPosition.y) / toSun.y);
    // The cloud volume is evaluated in absolute world space. windOffset is
    // accumulated on the CPU, so a weather-direction change does not reset or
    // phase-jump terrain shadows as it would with velocity * absolute time.
    vec3 p = (worldPosition + toSun * travel - frame.windOffset.xyz) * 0.00023;
    float shape = CloudNoise(p) * 0.55 + CloudNoise(p * 2.07 + 19.7) * 0.30 + CloudNoise(p * 4.13 - 7.1) * 0.15;
    float cover = smoothstep(0.52, 0.72, shape);
    return mix(1.0, 0.72, cover);
}

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
    vec3 receiverNormal = normalize(vWorldNormal);

    // Ordinary cutout materials derive alpha from tex0. Reject invisible
    // fragments before lighting and shadow work; Grass is excluded because its
    // alpha also depends on tex1 and the grass coefficient.
    vec4 c0 = texture(tex0, vTexcoord0);
    if (alphaMode == 1u && family != kFamilyGrass && c0.a * tint.a < float(alphaRef) / 255.0)
        discard;

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
        vec3 normal = receiverNormal;
        if (pass == kPassTerrainOpaque)
        {
            // Preserve the legacy segment mesh while restoring some local
            // terrain relief from the actual rasterized heightfield surface.
            vec3 geometricNormal = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
            if (dot(geometricNormal, normal) < 0.0)
                geometricNormal = -geometricNormal;
            normal = normalize(mix(normal, geometricNormal, 0.35));
            receiverNormal = normal;
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
        if (pass == kPassTerrainOpaque && sunOn > 0.0)
        {
            // Apply the same fixed-world field used by the cloud passes after
            // local-light accumulation, preserving readable terrain ambient.
            light = max(light * CloudShadow(vWorldPos), vec3(0.16));
        }
        light = clamp(light, 0.0f, 1.0f);
    }

    // -----------------------------------------------------------------------
    // Texture samples
    // -----------------------------------------------------------------------
    // Most terrain/model/cutout materials use only tex0. Sampling tex1 only
    // for the three shader families that consume it avoids a full-screen
    // second texture read for the common path.
    vec4 c1 = vec4(1.0);
    if (family == kFamilyDetail || family == kFamilyGrass || family == kFamilyWater)
        c1 = texture(tex1, vTexcoord1);

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
        // Animated Gerstner geometry arrives from scene.vert.  The authored
        // bump adds only micro-normal breakup; it is not a texture-only water
        // approximation.
        vec3 bumpN = normalize(c1.rgb * 2.0 - 1.0);
        vec3 n = normalize(mix(normalize(vWorldNormal), bumpN, 0.22));
        vec3 viewDir = normalize(-vWorldPos);
        float ndv = max(dot(n, viewDir), 0.0);
        float fresnel = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);
        vec3 sunDir = normalize(-frame.sunDirection.xyz);
        vec3 reflectionDir = reflect(-viewDir, n);
        float skyU = 0.5 + atan(reflectionDir.z, reflectionDir.x) / 6.2831853;
        float skyV = acos(clamp(reflectionDir.y, -1.0, 1.0)) / 3.14159265;
        vec3 reflectedSky = textureLod(skyMap, vec2(skyU, skyV), 0.0).rgb;
        // Energy-normalised HDR Blinn-Phong sun disc.  It remains unclamped
        // until the common fog/output path so HDR targets retain bloom energy.
        vec3 halfVector = normalize(sunDir + viewDir);
        float sunUp = smoothstep(0.0, 0.06, sunDir.y);
        float sunGlint = pow(max(dot(n, halfVector), 0.0), 220.0) * (222.0 / 25.1327412) * sunUp;
        vec3 body = mix(vec3(0.025, 0.19, 0.23), vec3(0.004, 0.035, 0.075), clamp(1.0 - ndv, 0.0, 1.0));
        baseColor = mix(body * (0.35 + 0.25 * max(dot(n, sunDir), 0.0)), reflectedSky, fresnel);
        baseColor += vec3(1.0, 0.86, 0.62) * sunGlint * 5.0;
        baseAlpha = clamp(0.42 + 0.54 * fresnel, 0.0, 0.96);
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

#ifdef POSEIDON_PREPASS
    // The prepass must exercise exactly the same alpha/discard decisions as
    // the colour replay.  Store a world-space normal in an attachment that is
    // later available to screen-space consumers (Hi-Z/SSR/AO); depth itself is
    // the shared attachment authority for both passes.
    outColor = vec4(normalize(receiverNormal) * 0.5 + 0.5, 1.0);
    return;
#endif

    if (family == kFamilyShadow)
    {
        outColor = vec4(0.0, 0.0, 0.0, baseAlpha);
        return;
    }

    // -----------------------------------------------------------------------
    // Cascade shadow map lookup
    // -----------------------------------------------------------------------
    uint drawPass = hasDraw ? drawConstants.draws[drawIdx].pass : kPassTerrainOpaque;
    bool shadowReceiver = drawPass == kPassTerrainOpaque || drawPass == kPassWorldOpaque ||
                          drawPass == kPassWorldCutout || drawPass == kPassSurfaceOverlay;
    // Fog is an atmospheric effect, not a shadow-receiver classification. Roads
    // intentionally disable fog yet still need the CSM darkening that terrain gets.
    // Water is a read-only depth receiver, never a caster.  Its Gerstner
    // normal feeds the same receiver-plane CSM evaluation as opaque geometry;
    // this removes direct sheen/glint under headland and object shadows.
    bool shadowFamily = family != kFamilyFlat || drawPass == kPassSurfaceOverlay;
    if (shadowReceiver && shadowFamily && frame.shadowCtl.x > 0.5)
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
                float lit = CascadeShadowVisibility(c, vWorldPos, receiverNormal,
                                                     dFdx(vWorldPos), dFdy(vWorldPos));
                litSum += w * lit;
                wSum += w;
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
