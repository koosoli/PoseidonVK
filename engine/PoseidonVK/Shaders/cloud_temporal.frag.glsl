#version 450

layout(set = 0, binding = 0, std140) uniform FrameConstants
{
    mat4 view; mat4 projection; mat4 sunMatrix;
    vec4 viewport; vec4 clipPlanes; vec4 worldRect; vec4 fogParams; vec4 fogColor;
    vec4 lightingParams; vec4 sunDirection;
    vec4 localLightPosition[8]; vec4 localLightDiffuse[8]; vec4 localLightAmbient[8]; vec4 localLightDirection[8];
    vec4 grassParams; vec4 time; vec4 shadowCtl; mat4 cascadeVP[4]; vec4 cascadeSplits; vec4 cascadeCtl;
    vec4 camFwd; vec4 camPos; vec4 specularColor; vec4 specularCtrl; vec4 cloudOrigin; vec4 wind; vec4 windOffset;
} frame;
layout(set = 1, binding = 0, std140) uniform CloudConstants
{
    mat4 previousView; mat4 previousProjection; vec4 cameraPosition; vec4 previousCameraPosition;
    vec4 windOffset; vec4 previousWindOffset; vec4 volumeOrigin; vec4 renderSizeAndHistory;
} cloud;
layout(set = 1, binding = 5) uniform sampler2D cloudCurrent;
layout(set = 1, binding = 6) uniform sampler2D cloudHistory;
layout(location = 0) in vec3 vWorldRay;
layout(location = 1) in vec2 vNdc;
layout(location = 2) in vec2 vUv;
layout(location = 0) out vec4 outColor;
void main()
{
    vec4 current = texture(cloudCurrent, vUv);
    // The first port used an approximate reprojection distance and produced
    // persistent sky ghosts. Keep the fixed-volume path stable until clouds
    // carry the per-pixel march depth needed for exact history reprojection.
    outColor = current;
}
