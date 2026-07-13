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

layout(set = 1, binding = 0, std140) uniform CloudConstants
{
    mat4 previousView; mat4 previousProjection; vec4 cameraPosition; vec4 previousCameraPosition;
    vec4 windOffset; vec4 previousWindOffset; vec4 volumeOrigin; vec4 renderSizeAndHistory;
} cloud;

layout(set = 1, binding = 1) uniform sampler2D sceneDepth;
layout(set = 1, binding = 2) uniform sampler3D cloudDensity;
layout(set = 1, binding = 3) uniform sampler3D cloudDistance;
layout(set = 1, binding = 4) uniform sampler3D cloudLight;

layout(location = 0) in vec3 vWorldRay;
layout(location = 1) in vec2 vNdc;
layout(location = 2) in vec2 vUv;
layout(location = 0) out vec4 outColor;

vec3 CloudVolumeUv(vec3 worldPos)
{
    vec2 horizontal = (worldPos.xz - cloud.volumeOrigin.xz) / (cloud.volumeOrigin.w * 2.0) + 0.5;
    return vec3(horizontal.x,
                (worldPos.y - frame.cloudGeometry.x) / max(frame.cloudGeometry.y - frame.cloudGeometry.x, 1.0),
                horizontal.y);
}

void main()
{
    vec3 ray = normalize(vWorldRay);
    vec3 camera = cloud.cameraPosition.xyz;
    float depth = texture(sceneDepth, vUv).x;
    float sceneDistance = 60000.0;
    if (depth < 0.999999)
    {
        vec4 scenePosition = inverse(frame.projection) * vec4(vNdc, depth, 1.0);
        sceneDistance = length(scenePosition.xyz / max(scenePosition.w, 1e-5));
    }

    if (abs(ray.y) < 1e-5)
        discard;
    float t0 = (frame.cloudGeometry.x - camera.y) / ray.y;
    float t1 = (frame.cloudGeometry.y - camera.y) / ray.y;
    float entry = max(min(t0, t1), 0.0);
    float exit = min(max(t0, t1), sceneDistance);
    if (exit <= entry)
        discard;

    // All cloud material is now sampled from persistent absolute-world 3D
    // resources.  The distance field advances empty segments conservatively.
    const int steps = 64;
    float stepLength = (exit - entry) / 40.0;
    float jitter = fract(sin(dot(gl_FragCoord.xy + cloud.renderSizeAndHistory.w, vec2(12.9898, 78.233))) * 43758.5453);
    float marchDistance = entry + jitter * stepLength;
    float transmittance = 1.0;
    vec3 scattering = vec3(0.0);
    vec3 sun = normalize(-frame.sunDirection.xyz);
    vec3 sunColor = mix(vec3(0.58, 0.65, 0.75), vec3(1.0, 0.78, 0.52), clamp(sun.y * 2.0 + 0.2, 0.0, 1.0));
    float voxelWorldSize = cloud.volumeOrigin.w * 2.0 / 96.0;
    for (int i = 0; i < steps && marchDistance < exit; ++i)
    {
        vec3 samplePosition = camera + ray * marchDistance;
        vec3 volumeUv = CloudVolumeUv(samplePosition);
        if (any(lessThan(volumeUv, vec3(0.0))) || any(greaterThan(volumeUv, vec3(1.0))))
            break;
        float density = texture(cloudDensity, volumeUv).r;
        if (density <= 0.0)
        {
            float emptyDistance = texture(cloudDistance, volumeUv).r;
            // The current bounded six-axis field may overestimate diagonal
            // distance. Cap its benefit to two regular samples so it can never
            // jump over a valid cloud column.
            float conservativeSkip = max(stepLength, emptyDistance * voxelWorldSize * 0.25);
            marchDistance += min(conservativeSkip, stepLength * 2.0);
            continue;
        }
        float extinction = density * stepLength * 0.00042;
        float lighting = texture(cloudLight, volumeUv).r;
        float powder = 0.55 + 0.45 * pow(max(dot(ray, sun), 0.0), 1.5);
        scattering += transmittance * (1.0 - exp(-extinction)) * sunColor * lighting * powder;
        transmittance *= exp(-extinction);
        marchDistance += stepLength;
        if (transmittance < 0.015)
            break;
    }

    float alpha = 1.0 - transmittance;
    if (alpha < 0.002)
        discard;
    outColor = vec4(scattering, alpha);
}
