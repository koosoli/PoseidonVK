#version 450

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
} frame;

// The image is a persistent HDR equirectangular sky map.  It is regenerated
// only when the celestial/weather cache key changes, never for camera motion.
layout(set = 1, binding = 0) uniform sampler2D skyMap;

layout(location = 0) in vec3 vWorldRay;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform ProceduralSkyParams
{
    float hdrEnabled;
} skyParams;

void main()
{
    vec3 ray = normalize(vWorldRay);
    const float invTwoPi = 0.15915494309189535;
    vec2 uv = vec2(atan(ray.z, ray.x) * invTwoPi + 0.5,
                   acos(clamp(ray.y, -1.0, 1.0)) * (1.0 / 3.141592653589793));
    vec3 color = texture(skyMap, uv).rgb;

    if (skyParams.hdrEnabled > 0.5)
    {
        // Preserve radiance above one for the FP16 scene target. The compositor
        // reverses this 1/1.5 source transfer before ACES and bloom.
        outColor = vec4(pow(max(color, vec3(0.0)), vec3(1.0 / 1.5)), 1.0);
    }
    else
    {
        // The direct UNORM swapchain expects display-referred output.
        outColor = vec4(sqrt(clamp(color, 0.0, 1.0)), 1.0);
    }
}
