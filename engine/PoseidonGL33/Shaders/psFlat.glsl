#version 330 core
in vec4 vColor;
uniform sampler2DArray shadowMap; // unit 2 — cascade depth-map array (unused unless shadowCtl.x>0.5)
in vec3 vWorldRel;

out vec4 fragColor;

void main() {
    fragColor = vColor;
}