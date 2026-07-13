#version 450

// Keep this declaration in byte-for-byte std140 order with FrameConstantsVK.
// The map bake reads celestial/weather inputs appended after the legacy frame
// data, while existing scene shaders can continue to consume their prefix.
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
    vec4 localLightPosition[8];
    vec4 localLightDiffuse[8];
    vec4 localLightAmbient[8];
    vec4 localLightDirection[8];
    vec4 grassParams;
    vec4 time;
    vec4 shadowCtl;
    mat4 cascadeVP[4];
    vec4 cascadeSplits;
    vec4 cascadeCtl;
    vec4 camFwd;
    vec4 camPos;
    vec4 specularColor;
    vec4 specularCtrl;
    vec4 cloudOrigin;
    vec4 wind;
    vec4 windOffset;
    vec4 cloudWeather;
    vec4 cloudGeometry;
    vec4 moonDirection;
    vec4 moonUpAndPhase;
    vec4 starsOrientation[3];
    vec4 skyVisibility;
} frame;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

const float PI = 3.141592653589793;

float Hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 DirectionFromEquirect(vec2 uv)
{
    float azimuth = (uv.x - 0.5) * (2.0 * PI);
    float elevation = (0.5 - uv.y) * PI;
    return vec3(cos(elevation) * cos(azimuth), sin(elevation), cos(elevation) * sin(azimuth));
}

vec2 EquirectFromDirection(vec3 d)
{
    return vec2(atan(d.z, d.x) * (1.0 / (2.0 * PI)) + 0.5,
                acos(clamp(d.y, -1.0, 1.0)) * (1.0 / PI));
}

float PortableStarField(vec3 ray)
{
    // This is deliberately a deterministic hash field, not an imported star
    // catalogue.  The celestial rotation supplied by LightSun remains the
    // authoritative orientation, while the generated positions are portable.
    vec3 celestial = normalize(vec3(dot(frame.starsOrientation[0].xyz, ray),
                                   dot(frame.starsOrientation[1].xyz, ray),
                                   dot(frame.starsOrientation[2].xyz, ray)));
    vec2 uv = EquirectFromDirection(celestial);
    vec2 grid = uv * vec2(480.0, 240.0);
    vec2 cell = floor(grid);
    vec2 local = fract(grid);
    float seed = floor(frame.skyVisibility.z + 0.5);
    float exists = step(0.994, Hash12(cell + seed * vec2(0.013, 0.071)));
    vec2 position = vec2(Hash12(cell + 11.7 + seed), Hash12(cell + 37.9 + seed));
    float radius = mix(0.10, 0.26, Hash12(cell + 71.3 + seed));
    float disc = 1.0 - smoothstep(radius * 0.45, radius, length(local - position));
    float magnitude = mix(0.35, 2.4, pow(Hash12(cell + 19.1 + seed), 7.0));
    return exists * disc * magnitude;
}

void main()
{
    vec3 ray = DirectionFromEquirect(vUv);
    vec3 sun = -frame.sunDirection.xyz;
    sun /= max(length(sun), 1.0e-4);
    float sunElevation = sun.y;
    float day = smoothstep(-0.10, 0.09, sunElevation) * frame.lightingParams.x;
    float twilight = 1.0 - smoothstep(-0.26, 0.12, sunElevation);
    float horizon = exp(-abs(ray.y) * 13.0);
    float weather = clamp(max(frame.cloudWeather.x, 1.0 - frame.skyVisibility.y), 0.0, 1.0);

    vec3 nightZenith = vec3(0.003, 0.007, 0.022);
    vec3 dayZenith = vec3(0.035, 0.19, 0.62);
    vec3 dayHorizon = mix(frame.fogColor.rgb * 0.8, vec3(0.63, 0.73, 0.91), 0.55);
    vec3 color = mix(nightZenith, dayZenith, day);
    color = mix(color, mix(vec3(0.018, 0.024, 0.055), dayHorizon, day), horizon);
    color += vec3(0.90, 0.20, 0.055) * horizon * twilight * 0.40;
    color = mix(color, frame.fogColor.rgb * mix(0.13, 0.68, day), weather * (0.20 + horizon * 0.50));

    float sunDot = max(dot(ray, sun), 0.0);
    color += vec3(1.0, 0.30, 0.065) * pow(sunDot, 9.0) * (0.35 + day);
    color += vec3(1.0, 0.68, 0.28) * pow(sunDot, 44.0) * (0.45 + day);
    color += vec3(14.0, 10.0, 6.0) * smoothstep(0.99990, 0.999985, sunDot) * day;

    float starsVisible = clamp(frame.skyVisibility.x * frame.skyVisibility.y * (1.0 - weather * 0.80), 0.0, 1.0);
    color += vec3(0.50, 0.66, 1.0) * PortableStarField(ray) * starsVisible;

    vec3 moon = -frame.moonDirection.xyz;
    moon /= max(length(moon), 1.0e-4);
    vec3 moonUp = frame.moonUpAndPhase.xyz - moon * dot(frame.moonUpAndPhase.xyz, moon);
    if (dot(moonUp, moonUp) < 1.0e-6)
        moonUp = abs(moon.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    moonUp = normalize(moonUp);
    vec3 moonRight = normalize(cross(moonUp, moon));
    float moonDot = clamp(dot(ray, moon), -1.0, 1.0);
    float moonAngle = acos(moonDot);
    float phase = clamp(frame.moonUpAndPhase.w, 0.0, 1.0);
    float illumination = sin(phase * PI);
    vec2 moonPlane = vec2(dot(ray, moonRight), dot(ray, moonUp)) / max(sin(max(moonAngle, 0.0001)), 0.0001);
    float moonRadius = 0.010;
    float moonDisc = 1.0 - smoothstep(moonRadius * 0.82, moonRadius, moonAngle);
    // A portable analytic terminator: phase 0.5 is full, phase 0/1 is dark.
    float terminator = sqrt(max(0.0, 1.0 - moonPlane.y * moonPlane.y));
    float lit = smoothstep(-0.05, 0.05, terminator - abs(moonPlane.x - (phase - 0.5) * 2.0));
    float moonVisible = starsVisible * (1.0 - weather * 0.55);
    float halo = exp(-moonAngle * 42.0) * (0.12 + illumination * 0.70) * moonVisible;
    color += vec3(0.22, 0.31, 0.52) * halo;
    color += vec3(1.8, 1.72, 1.45) * moonDisc * lit * moonVisible;

    outColor = vec4(max(color, vec3(0.0)), 1.0);
}
