#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec2 vTexcoord;
layout(location = 3) in float vFogFactor;

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
} frame;

void main()
{
    // Directional light driven from the uploaded frame constants. sunDirection
    // is the world-space direction the light travels, so the vector toward the
    // light is -sunDirection (matching the GL33 vsTransform dot(N, -sunDir)).
    // lightingParams.x carries the sun-enabled flag; when off we fall back to
    // ambient-only so geometry stays visible but unlit.
    vec3 rawSunDir = frame.sunDirection.xyz;
    vec3 sunDir = length(rawSunDir) > 0.0001f ? normalize(rawSunDir) : vec3(0.0f, -1.0f, 0.0f);
    float sunOn = (frame.lightingParams.x > 0.5) ? 1.0 : 0.0;
    float diffuse = max(dot(normalize(vWorldNormal), -sunDir), 0.0f) * sunOn;
    float ambient = 0.35f;
    float light = ambient + diffuse * 0.65f;

    // UV-driven two-tone color so the smoke test can tell the scene draw apart
    // from the bootstrap triangle and confirm UVs travel through correctly.
    vec3 baseColor = mix(vec3(0.10f, 0.55f, 0.85f), vec3(0.85f, 0.40f, 0.10f), vTexcoord.x);
    vec3 litColor = baseColor * light;

    // Fog driven from the uploaded frame constants: mix toward frame.fogColor
    // as distance grows. vFogFactor=1 near (no fog), 0 far (full fog), matching
    // the GL33 vsTransform/vsFog convention.
    vec3 fogged = mix(frame.fogColor.rgb, litColor, vFogFactor);
    outColor = vec4(fogged, 1.0f);
}
