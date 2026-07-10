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
    float lightingParams[4] = {}; // sun enabled, local light count, local light scale, reserved
    float sunDirection[4] = {};  // xyz world-space travel direction (normalized), w unused
    float localLightPosition[render::frame::kMaxFrameLocalLights][4] = {}; // xyz camera-relative, w startAtten
    float localLightDiffuse[render::frame::kMaxFrameLocalLights][4] = {};
    float localLightAmbient[render::frame::kMaxFrameLocalLights][4] = {};
    float localLightDirection[render::frame::kMaxFrameLocalLights][4] = {}; // xyz beam dir, w spot flag
    float grassParams[4] = {};
    float time[4] = {};  // [0] = game-time seconds (for water UV animation); [1..3] unused
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
static_assert(sizeof(FrameConstantsVK) == 848);

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
    return constants;
}

} // namespace Poseidon::vk
