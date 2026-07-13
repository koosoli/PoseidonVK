#version 450

layout(location = 0) out vec2 vUv;

void main()
{
    const vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vec2 ndc = positions[gl_VertexIndex];
    // Vulkan framebuffer rows run downward; keep v=0 at the equirectangular
    // north pole so sampling the cached image needs no display-time flip.
    vUv = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
