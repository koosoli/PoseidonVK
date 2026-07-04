#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
void main() { if (texture(uTex, vUV).a < 0.5) discard; }