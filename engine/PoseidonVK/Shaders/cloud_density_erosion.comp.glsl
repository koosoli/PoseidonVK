#version 450

// Canonical weather/atmosphere inputs are the FrameConstants uploaded by the
// frame plan. The field is absolute-world and independently reconstructs the
// snapped volume origin used by CloudConstantsVK for raymarching.
layout(set = 0, binding = 0, std140) uniform FrameConstants
{
    mat4 view; mat4 projection; mat4 sunMatrix;
    vec4 viewport; vec4 clipPlanes; vec4 worldRect; vec4 fogParams; vec4 fogColor;
    vec4 lightingParams; vec4 sunDirection;
    vec4 localLightPosition[8]; vec4 localLightDiffuse[8]; vec4 localLightAmbient[8]; vec4 localLightDirection[8];
    vec4 grassParams; vec4 time; vec4 shadowCtl; mat4 cascadeVP[4]; vec4 cascadeSplits; vec4 cascadeCtl;
    vec4 camFwd; vec4 camPos; vec4 specularColor; vec4 specularCtrl; vec4 cloudOrigin; vec4 wind; vec4 windOffset;
    vec4 cloudWeather; vec4 cloudGeometry; vec4 moonDirection; vec4 moonUpAndPhase; vec4 starsOrientation[3]; vec4 skyVisibility;
} frame;

layout(set = 1, binding = 0, r8) uniform writeonly image3D cloudDensity;
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

uint Hash(ivec3 p, uint seed)
{
    uint h = uint(p.x) * 0x8da6b343u;
    h ^= uint(p.y) * 0xd8163841u;
    h ^= uint(p.z) * 0xcb1ab31fu;
    h ^= seed * 0x9e3779b9u;
    h ^= h >> 16u; h *= 0x7feb352du; h ^= h >> 15u; h *= 0x846ca68bu; h ^= h >> 16u;
    return h;
}

float ValueNoise(vec3 p, uint seed)
{
    ivec3 i = ivec3(floor(p));
    vec3 f = fract(p); f = f * f * (3.0 - 2.0 * f);
    float n000 = float(Hash(i + ivec3(0,0,0), seed) & 0x00ffffffu) / 16777215.0;
    float n100 = float(Hash(i + ivec3(1,0,0), seed) & 0x00ffffffu) / 16777215.0;
    float n010 = float(Hash(i + ivec3(0,1,0), seed) & 0x00ffffffu) / 16777215.0;
    float n110 = float(Hash(i + ivec3(1,1,0), seed) & 0x00ffffffu) / 16777215.0;
    float n001 = float(Hash(i + ivec3(0,0,1), seed) & 0x00ffffffu) / 16777215.0;
    float n101 = float(Hash(i + ivec3(1,0,1), seed) & 0x00ffffffu) / 16777215.0;
    float n011 = float(Hash(i + ivec3(0,1,1), seed) & 0x00ffffffu) / 16777215.0;
    float n111 = float(Hash(i + ivec3(1,1,1), seed) & 0x00ffffffu) / 16777215.0;
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

void main()
{
    const ivec3 dimensions = ivec3(96, 48, 96);
    ivec3 voxel = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(voxel, dimensions))) return;
    // cloudWeather.z is the authoritative density multiplier.  Do not leave a
    // residual wisp field in clear weather: the distance and light passes must
    // see an entirely empty volume as well as the raymarcher.
    float weatherDensity = clamp(frame.cloudWeather.z, 0.0, 1.0);
    if (weatherDensity <= 0.0)
    {
        imageStore(cloudDensity, voxel, vec4(0.0));
        return;
    }
    float halfExtent = clamp(frame.cloudGeometry.z, 16384.0, 131072.0) * 0.5;
    float extent = halfExtent * 2.0;
    vec2 volumeOrigin = floor(frame.cloudOrigin.xz / extent + 0.5) * extent;
    vec3 uv = (vec3(voxel) + 0.5) / vec3(dimensions);
    float worldY = mix(frame.cloudGeometry.x, frame.cloudGeometry.y, uv.y);
    vec3 worldPosition = vec3(volumeOrigin.x + (uv.x * 2.0 - 1.0) * halfExtent, worldY,
                              volumeOrigin.y + (uv.z * 2.0 - 1.0) * halfExtent);
    vec3 p = (worldPosition - frame.windOffset.xyz) * 0.00023;
    uint seed = uint(max(frame.skyVisibility.z, 0.0));
    float macro = ValueNoise(p, seed) * 0.55 + ValueNoise(p * 2.07 + 19.7, seed) * 0.30 +
                  ValueNoise(p * 3.61 - 11.3, seed) * 0.15;
    float erosion = ValueNoise(p * 6.17 - 7.1, seed);
    // Keep the volume's weather threshold in lockstep with the sky-map deck.
    // At the authored overcast of about 0.5 this retains broken body clouds,
    // while the lower shoulder supplies sparse material between them.
    float coverage = mix(0.50, 0.24, clamp(frame.cloudWeather.x, 0.0, 1.0));
    float body = smoothstep(coverage - 0.11, coverage + 0.10, macro);
    // Give each macro cell a substantial base and a noise-driven rounded crown.
    // The former wisp shoulder made the whole layer read as uniform haze; this
    // keeps broken coverage but yields discrete cumulus-like cloud bodies.
    float cloudTop = mix(0.48, 0.86, smoothstep(coverage - 0.08, coverage + 0.20, macro));
    float verticalBase = smoothstep(0.01, 0.20, uv.y);
    float verticalTop = 1.0 - smoothstep(cloudTop - 0.18, cloudTop, uv.y);
    float vertical = verticalBase * verticalTop;
    float edge = max(abs(uv.x * 2.0 - 1.0), abs(uv.z * 2.0 - 1.0));
    float edgeFade = 1.0 - clamp((edge - 0.80) / 0.18, 0.0, 1.0);
    // Carve the high-frequency noise from only the outer body. This retains
    // dense, shadow-casting interiors while breaking up silhouettes and crowns.
    float erosionFade = mix(0.58, 1.0, erosion);
    float materialDensity = body * mix(0.29, 0.46, erosion);
    float density = materialDensity * erosionFade * vertical * edgeFade * weatherDensity;
    imageStore(cloudDensity, voxel, vec4(density, 0.0, 0.0, 1.0));
}
