#version 450

layout(location = 0) out vec3 vColor;

vec2 kPositions[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);

vec3 kColors[3] = vec3[](
    vec3(0.95, 0.18, 0.16),
    vec3(0.18, 0.75, 0.32),
    vec3(0.20, 0.42, 0.95)
);

void main()
{
    gl_Position = vec4(kPositions[gl_VertexIndex], 0.0, 1.0);
    vColor = kColors[gl_VertexIndex];
}
