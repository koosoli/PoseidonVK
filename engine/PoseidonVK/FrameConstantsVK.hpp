#pragma once

#include <Poseidon/Graphics/Rendering/Frame/Frame.hpp>

#include <cstddef>
#include <cstdint>

namespace Poseidon::vk
{

struct FrameConstantsVK
{
    GfxMatrix view = {};
    GfxMatrix projection = {};
    GfxMatrix sunMatrix = {};
    float viewport[4] = {};   // x, y, width, height
    float clipPlanes[4] = {}; // near, far, world-left, world-top
    float worldRect[4] = {};  // left, top, right, bottom
    float fogParams[4] = {};  // start, end, inverse range, enabled
    float fogColor[4] = {};   // rgba, normalized
    float lightingParams[4] = {}; // sun enabled, local light count, local light scale, night-eye intensity
    float sunDirection[4] = {};  // xyz world-space travel direction (normalized), w unused
    float localLightPosition[render::frame::kMaxFrameLocalLights][4] = {}; // xyz camera-relative, w startAtten
    float localLightDiffuse[render::frame::kMaxFrameLocalLights][4] = {};
    float localLightAmbient[render::frame::kMaxFrameLocalLights][4] = {};
    float localLightDirection[render::frame::kMaxFrameLocalLights][4] = {}; // xyz beam dir, w spot flag
    float grassParams[4] = {};
    float time[4] = {};  // [0] = game-time seconds (for water UV animation); [1..3] unused
    // Shadow map cascade data — mirrors GL33 PSConstants layout (c2-c26 era).
    // Zeroed by BuildFrameConstants; populated by EngineVK::UpdateShadowFrameConstants.
    float shadowCtl[4] = {};       // {enable, bias, darkness, texelSize}
    float cascadeVP[4][16] = {};   // per-cascade light VP matrices (column-major)
    float cascadeSplits[4] = {};   // per-tier select distance
    float cascadeCtl[4] = {};      // {count, fadeRange, biasBase, omniCount}
    float camFwd[4] = {};
    float camPos[4] = {};  // world-space camera position
    float specularColor[4] = {};  // RGB + power.w
    float specularCtrl[4] = {};   // x = enabled
    float cloudOrigin[4] = {};    // absolute world-space camera position
    float wind[4] = {};           // effective world-space weather velocity
    float windOffset[4] = {};     // accumulated world-space weather displacement
    float cloudWeather[4] = {};   // overcast, rain, density, brightness
    float cloudGeometry[4] = {};  // base, top, extent, simulation time
    float moonDirection[4] = {};
    float moonUpAndPhase[4] = {}; // xyz up, w phase
    float starsOrientation[3][4] = {};
    float skyVisibility[4] = {};  // stars, sky-through, cloud seed, reserved
};

static_assert(sizeof(GfxMatrix) == 64);
static_assert(offsetof(FrameConstantsVK, view) == 0);
static_assert(offsetof(FrameConstantsVK, projection) == 64);
static_assert(offsetof(FrameConstantsVK, sunMatrix) == 128);
static_assert(offsetof(FrameConstantsVK, viewport) == 192);
static_assert(offsetof(FrameConstantsVK, clipPlanes) == 208);
static_assert(offsetof(FrameConstantsVK, worldRect) == 224);
static_assert(offsetof(FrameConstantsVK, fogParams) == 240);
static_assert(offsetof(FrameConstantsVK, fogColor) == 256);
static_assert(offsetof(FrameConstantsVK, lightingParams) == 272);
static_assert(offsetof(FrameConstantsVK, sunDirection) == 288);
static_assert(offsetof(FrameConstantsVK, localLightPosition) == 304);
static_assert(offsetof(FrameConstantsVK, localLightDiffuse) == 432);
static_assert(offsetof(FrameConstantsVK, localLightAmbient) == 560);
static_assert(offsetof(FrameConstantsVK, localLightDirection) == 688);
static_assert(offsetof(FrameConstantsVK, grassParams) == 816);
static_assert(offsetof(FrameConstantsVK, time) == 832);
static_assert(offsetof(FrameConstantsVK, shadowCtl) == 848);
static_assert(offsetof(FrameConstantsVK, cascadeVP) == 864);
static_assert(offsetof(FrameConstantsVK, cascadeSplits) == 1120);
static_assert(offsetof(FrameConstantsVK, cascadeCtl) == 1136);
static_assert(offsetof(FrameConstantsVK, camFwd) == 1152);
static_assert(offsetof(FrameConstantsVK, camPos) == 1168);
    static_assert(offsetof(FrameConstantsVK, specularColor) == 1184);
    static_assert(offsetof(FrameConstantsVK, specularCtrl) == 1200);
    static_assert(offsetof(FrameConstantsVK, cloudOrigin) == 1216);
    static_assert(offsetof(FrameConstantsVK, wind) == 1232);
    static_assert(offsetof(FrameConstantsVK, windOffset) == 1248);
    static_assert(offsetof(FrameConstantsVK, cloudWeather) == 1264);
    static_assert(offsetof(FrameConstantsVK, cloudGeometry) == 1280);
    static_assert(offsetof(FrameConstantsVK, moonDirection) == 1296);
    static_assert(offsetof(FrameConstantsVK, moonUpAndPhase) == 1312);
    static_assert(offsetof(FrameConstantsVK, starsOrientation) == 1328);
    static_assert(offsetof(FrameConstantsVK, skyVisibility) == 1376);
    static_assert(sizeof(FrameConstantsVK) == 1392);

inline float ChannelToFloat(std::uint32_t value) noexcept
{
    return static_cast<float>(value & 0xffu) * (1.0f / 255.0f);
}

inline FrameConstantsVK BuildFrameConstants(const render::frame::Frame& frame) noexcept
{
    FrameConstantsVK constants;
    constants.view = frame.camera.view;
    constants.projection = frame.camera.projection;
    constants.sunMatrix = frame.sunMatrix;

    constants.viewport[0] = static_cast<float>(frame.camera.viewport.x);
    constants.viewport[1] = static_cast<float>(frame.camera.viewport.y);
    constants.viewport[2] = static_cast<float>(frame.camera.viewport.width);
    constants.viewport[3] = static_cast<float>(frame.camera.viewport.height);

    constants.clipPlanes[0] = frame.camera.nearPlane;
    constants.clipPlanes[1] = frame.camera.farPlane;
    constants.clipPlanes[2] = frame.camera.worldLeft;
    constants.clipPlanes[3] = frame.camera.worldTop;

    constants.worldRect[0] = frame.camera.worldLeft;
    constants.worldRect[1] = frame.camera.worldTop;
    constants.worldRect[2] = frame.camera.worldRight;
    constants.worldRect[3] = frame.camera.worldBottom;

    const float fogRange = frame.fogEnd - frame.fogStart;
    constants.fogParams[0] = frame.fogStart;
    constants.fogParams[1] = frame.fogEnd;
    constants.fogParams[2] = fogRange > 0.0f ? 1.0f / fogRange : 0.0f;
    constants.fogParams[3] = fogRange > 0.0f ? 1.0f : 0.0f;

    const std::uint32_t rgba = frame.fogColorRGBA;
    constants.fogColor[0] = ChannelToFloat(rgba >> 24);
    constants.fogColor[1] = ChannelToFloat(rgba >> 16);
    constants.fogColor[2] = ChannelToFloat(rgba >> 8);
    constants.fogColor[3] = ChannelToFloat(rgba);
    constants.lightingParams[0] = frame.sunEnabled ? 1.0f : 0.0f;
    const std::uint32_t localLightCount =
        frame.localLightCount < render::frame::kMaxFrameLocalLights
            ? frame.localLightCount
            : static_cast<std::uint32_t>(render::frame::kMaxFrameLocalLights);
    constants.lightingParams[1] = static_cast<float>(localLightCount);
    constants.lightingParams[2] = frame.localLightScale;
    constants.sunDirection[0] = frame.sunDirection[0];
    constants.sunDirection[1] = frame.sunDirection[1];
    constants.sunDirection[2] = frame.sunDirection[2];
    constants.sunDirection[3] = 0.0f;
    for (std::uint32_t i = 0; i < localLightCount; ++i)
    {
        const render::frame::LocalLight& light = frame.localLights[i];
        constants.localLightPosition[i][0] = light.position[0];
        constants.localLightPosition[i][1] = light.position[1];
        constants.localLightPosition[i][2] = light.position[2];
        constants.localLightPosition[i][3] = light.startAtten;
        constants.localLightDiffuse[i][0] = light.diffuse[0];
        constants.localLightDiffuse[i][1] = light.diffuse[1];
        constants.localLightDiffuse[i][2] = light.diffuse[2];
        constants.localLightDiffuse[i][3] = 0.0f;
        constants.localLightAmbient[i][0] = light.ambient[0];
        constants.localLightAmbient[i][1] = light.ambient[1];
        constants.localLightAmbient[i][2] = light.ambient[2];
        constants.localLightAmbient[i][3] = 0.0f;
        constants.localLightDirection[i][0] = light.direction[0];
        constants.localLightDirection[i][1] = light.direction[1];
        constants.localLightDirection[i][2] = light.direction[2];
        constants.localLightDirection[i][3] =
            light.kind == render::frame::LocalLightKind::Spot ? 1.0f : 0.0f;
    }

    // Extract camera world position from view matrix.
    // view = [R | t] (row-major D3DMATRIX) where t = -R * camPos
    // camPos = -R^T * t
    {
        const auto& v = constants.view;
        constants.camPos[0] = -(v.m[0][0] * v.m[0][3] + v.m[1][0] * v.m[1][3] + v.m[2][0] * v.m[2][3]);
        constants.camPos[1] = -(v.m[0][1] * v.m[0][3] + v.m[1][1] * v.m[1][3] + v.m[2][1] * v.m[2][3]);
        constants.camPos[2] = -(v.m[0][2] * v.m[0][3] + v.m[1][2] * v.m[1][3] + v.m[2][2] * v.m[2][3]);
        constants.camPos[3] = 0.0f;
    }

    // Default specular (white, power 32, always enabled)
    constants.specularColor[0] = 0.8f;
    constants.specularColor[1] = 0.8f;
    constants.specularColor[2] = 0.8f;
    constants.specularColor[3] = 32.0f;
    constants.specularCtrl[0] = 1.0f;
    constants.cloudOrigin[0] = frame.cameraPosition[0];
    constants.cloudOrigin[1] = frame.cameraPosition[1];
    constants.cloudOrigin[2] = frame.cameraPosition[2];
    constants.wind[0] = frame.wind[0];
    constants.wind[1] = frame.wind[1];
    constants.wind[2] = frame.wind[2];
    constants.cloudWeather[0] = frame.atmosphere.overcast;
    constants.cloudWeather[1] = frame.atmosphere.rainDensity;
    constants.cloudWeather[2] = frame.atmosphere.cloudDensity;
    constants.cloudWeather[3] = frame.atmosphere.cloudBrightness;
    constants.cloudGeometry[0] = frame.atmosphere.cloudBase;
    constants.cloudGeometry[1] = frame.atmosphere.cloudTop;
    constants.cloudGeometry[2] = frame.atmosphere.cloudExtent;
    constants.cloudGeometry[3] = frame.atmosphere.cloudTime;
    constants.moonDirection[0] = frame.atmosphere.moonDirection[0];
    constants.moonDirection[1] = frame.atmosphere.moonDirection[1];
    constants.moonDirection[2] = frame.atmosphere.moonDirection[2];
    constants.moonUpAndPhase[0] = frame.atmosphere.moonUp[0];
    constants.moonUpAndPhase[1] = frame.atmosphere.moonUp[1];
    constants.moonUpAndPhase[2] = frame.atmosphere.moonUp[2];
    constants.moonUpAndPhase[3] = frame.atmosphere.moonPhase;
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            constants.starsOrientation[row][column] = frame.atmosphere.starsOrientation[row][column];
    constants.skyVisibility[0] = frame.atmosphere.starsVisibility;
    constants.skyVisibility[1] = frame.atmosphere.skyThrough;
    constants.skyVisibility[2] = static_cast<float>(frame.atmosphere.cloudSeed);

    return constants;
}

} // namespace Poseidon::vk
