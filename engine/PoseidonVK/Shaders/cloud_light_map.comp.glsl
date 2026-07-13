#version 450

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

layout(set = 1, binding = 0) uniform sampler3D cloudDensity;
layout(set = 1, binding = 1, r8) uniform writeonly image3D cloudLight;
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

void main()
{
    const ivec3 lightDimensions = ivec3(48, 24, 48);
    const ivec3 densityDimensions = ivec3(96, 48, 96);
    ivec3 voxel = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(voxel, lightDimensions))) return;
    vec3 sunVector = -frame.sunDirection.xyz;
    vec3 sun = sunVector / max(length(sunVector), 1e-4);
    vec3 stepDirection = sun / max(max(abs(sun.x), abs(sun.y)), abs(sun.z)) * 2.0;
    vec3 densityPosition = (vec3(voxel) + 0.5) * vec3(densityDimensions) / vec3(lightDimensions);
    float opticalDepth = 0.0;
    for (int sampleIndex = 1; sampleIndex <= 8; ++sampleIndex)
    {
        ivec3 sampleVoxel = ivec3(densityPosition + stepDirection * float(sampleIndex));
        if (all(greaterThanEqual(sampleVoxel, ivec3(0))) && all(lessThan(sampleVoxel, densityDimensions)))
            opticalDepth += texelFetch(cloudDensity, sampleVoxel, 0).r * 0.35;
    }
    float ambient = 0.14 + 0.20 * clamp(frame.cloudWeather.w, 0.0, 1.0);
    float illumination = ambient + (1.0 - ambient) * exp(-opticalDepth);
    imageStore(cloudLight, voxel, vec4(illumination, 0.0, 0.0, 1.0));
}
