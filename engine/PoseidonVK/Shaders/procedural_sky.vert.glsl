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

layout(location = 0) out vec3 vWorldRay;

void main()
{
    const vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vec2 ndc = positions[gl_VertexIndex];
    gl_Position = vec4(ndc, 0.0, 1.0);

    // Unproject at a finite depth. This remains valid if the engine uses an
    // infinite-far projection, unlike unprojecting the far plane.
    vec4 viewRay = inverse(frame.projection) * vec4(ndc, 0.0, 1.0);
    vWorldRay = transpose(mat3(frame.view)) * normalize(viewRay.xyz / viewRay.w);
}
