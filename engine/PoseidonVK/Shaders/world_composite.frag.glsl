#version 450

layout(set = 0, binding = 0) uniform sampler2D worldColor;

layout(push_constant) uniform WorldCompositeParams
{
    float exposure;
    uint hdrEnabled;
} composite;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

void main()
{
    vec4 world = texture(worldColor, vUv);
    // Keep the existing cloud compositor exactly display-referred unless HDR is opted in.
    if (composite.hdrEnabled == 0u)
    {
        outColor = world;
        return;
    }

    // Scene shaders store pow(linear, 1/1.5); procedural sky and clouds use
    // sqrt. Without source provenance, this inverse is the closest common fit.
    vec3 scene = pow(max(world.rgb, vec3(0.0)), vec3(1.5));

    // Fused bright-pass bloom: nine nearby taps avoid an intermediate target.
    vec2 texel = 1.0 / vec2(textureSize(worldColor, 0));
    const vec2 offsets[8] = vec2[](vec2(-2.0, 0.0), vec2(2.0, 0.0), vec2(0.0, -2.0), vec2(0.0, 2.0),
                                   vec2(-1.5, -1.5), vec2(1.5, -1.5), vec2(-1.5, 1.5), vec2(1.5, 1.5));
    vec3 bloom = max(scene - vec3(0.55), vec3(0.0)) * 0.25;
    for (int i = 0; i < 8; ++i)
    {
        vec3 sampleScene = pow(max(texture(worldColor, vUv + offsets[i] * texel).rgb, vec3(0.0)), vec3(1.5));
        bloom += max(sampleScene - vec3(0.55), vec3(0.0)) * 0.09375;
    }

    vec3 exposed = (scene + bloom) * composite.exposure;
    // Narkowicz's fitted ACES filmic curve, followed by explicit sRGB for the
    // unchanged UNORM swapchain.
    vec3 mapped = clamp((exposed * (2.51 * exposed + 0.03)) /
                            (exposed * (2.43 * exposed + 0.59) + 0.14),
                        0.0, 1.0);
    vec3 srgb = mix(mapped * 12.92, 1.055 * pow(mapped, vec3(1.0 / 2.4)) - 0.055,
                    step(vec3(0.0031308), mapped));
    outColor = vec4(srgb, world.a);
}
