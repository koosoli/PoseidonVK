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

layout(location = 0) in vec3 vWorldRay;
layout(location = 0) out vec4 outColor;

void main()
{
    vec3 ray = normalize(vWorldRay);
    vec3 sun = normalize(-frame.sunDirection.xyz);
    float elevation = clamp(ray.y * 0.5 + 0.5, 0.0, 1.0);
    float day = smoothstep(-0.12, 0.04, sun.y);

    vec3 fog = frame.fogColor.rgb;
    vec3 horizon = mix(vec3(0.025, 0.045, 0.11), mix(fog, vec3(0.62, 0.72, 0.88), 0.35), day);
    vec3 zenith = mix(vec3(0.008, 0.015, 0.055), vec3(0.08, 0.28, 0.65), day);
    vec3 color = mix(horizon, zenith, pow(elevation, 0.55));

    float horizonHaze = exp(-abs(ray.y) * 16.0);
    color = mix(color, fog, horizonHaze * 0.32 * day);

    float sunDot = max(dot(ray, sun), 0.0);
    color += vec3(1.0, 0.48, 0.14) * pow(sunDot, 48.0) * 0.35;
    color += vec3(1.0, 0.88, 0.62) * smoothstep(0.99993, 0.99998, sunDot) * 2.0;

    // The direct UNORM swapchain expects display-referred output until HDR is
    // reintroduced with a validated scene resolve.
    outColor = vec4(sqrt(clamp(color, 0.0, 1.0)), 1.0);
}
