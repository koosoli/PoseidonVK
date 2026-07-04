#version 330 core
layout(location = 0) in vec3 pos;
uniform mat4 uLightVP;
void main() { gl_Position = uLightVP * vec4(pos, 1.0); }