#version 330 core
layout(location = 0) in vec3 pos;
layout(location = 1) in vec2 uv;
uniform mat4 uLightVP;
out vec2 vUV;
void main() { vUV = uv; gl_Position = uLightVP * vec4(pos, 1.0); }