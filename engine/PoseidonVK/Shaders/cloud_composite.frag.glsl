#version 450
layout(set = 1, binding = 0, std140) uniform CloudConstants
{
    mat4 previousView; mat4 previousProjection; vec4 cameraPosition; vec4 previousCameraPosition;
    vec4 windOffset; vec4 previousWindOffset; vec4 volumeOrigin; vec4 renderSizeAndHistory;
} cloud;
layout(set = 1, binding = 6) uniform sampler2D cloudHistory;
layout(location = 2) in vec2 vUv;
layout(location = 0) out vec4 outColor;
void main() { outColor = texture(cloudHistory, vUv); }
