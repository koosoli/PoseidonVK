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
    float halfExtent = clamp(frame.cloudGeometry.z, 16384.0, 131072.0) * 0.5;
    float extent = halfExtent * 2.0;
    vec2 volumeOrigin = floor(frame.cloudOrigin.xz / extent + 0.5) * extent;
    vec3 uv = (vec3(voxel) + 0.5) / vec3(dimensions);
    float worldY = mix(frame.cloudGeometry.x, frame.cloudGeometry.y, uv.y);
    vec3 worldPosition = vec3(volumeOrigin.x + (uv.x * 2.0 - 1.0) * halfExtent, worldY,
                              volumeOrigin.y + (uv.z * 2.0 - 1.0) * halfExtent);
    vec3 p = (worldPosition - frame.windOffset.xyz) * 0.00023;
    uint seed = uint(max(frame.skyVisibility.z, 0.0));
    float macro = ValueNoise(p, seed) * 0.55 + ValueNoise(p * 2.07 + 19.7, seed) * 0.30;
    float erosion = ValueNoise(p * 4.13 - 7.1, seed);
    float coverage = mix(0.72, 0.38, clamp(frame.cloudWeather.x, 0.0, 1.0));
    float vertical = clamp(uv.y / 0.30, 0.0, 1.0) * (1.0 - clamp((uv.y - 0.62) / 0.38, 0.0, 1.0));
    float edge = max(abs(uv.x * 2.0 - 1.0), abs(uv.z * 2.0 - 1.0));
    float edgeFade = 1.0 - clamp((edge - 0.80) / 0.18, 0.0, 1.0);
    float density = max(macro - coverage - (1.0 - erosion) * 0.075, 0.0) * vertical * edgeFade *
                    clamp(frame.cloudWeather.z, 0.0, 1.0);
    imageStore(cloudDensity, voxel, vec4(density, 0.0, 0.0, 1.0));
}
