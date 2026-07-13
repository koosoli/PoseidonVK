#pragma once

#include <Poseidon/Graphics/Rendering/Frame/Frame.hpp>

#include <cstdint>
#include <string>
#include <vector>
#include <Poseidon/World/Scene/Camera/Camera.hpp>

// SceneInputs — the *value-typed* view of everything BuildFrame needs
// from the engine to produce a Frame.  Decouples BuildFrame (pure,
// testable, no engine dependency) from the impure step of extracting
// these values from the live Scene/Camera/Object* graph.  Tests
// construct SceneInputs directly with synthetic data; the live engine
// fills it via ExtractSceneInputs.


namespace Poseidon
{
namespace render::frame
{

// A single draw the engine has decided to issue this frame.  Already
// resolved to descriptor + transform + resource handles; BuildFrame
// just bins it into the right Pass.
struct SceneDraw
{
    RenderPassDescriptor descriptor    = {};
    GfxMatrix            world         = {};
    MeshHandle           mesh          = {};
    int                  indexBegin    = 0;
    int                  indexCount    = 0;
    std::array<TextureHandle, 4> textures = {};
    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

// Visibility status for high-level features.  Used by BuildFrame to
// decide whether to emit certain passes.
struct SceneFlags
{
    bool shadowsEnabled       = false;
    bool inFirstPersonView    = false;
    bool hudEnabled           = true;
};

// All the per-frame inputs BuildFrame consumes.
struct SceneInputs
{
    // Camera + viewport (one of these per frame).
    CameraView camera = {};
    // Absolute world-space camera origin for effects that cannot use the
    // camera-relative draw transform, such as world-anchored cloud density.
    float cameraPosition[3] = {0.0f, 0.0f, 0.0f};

    // Lighting + atmospherics (one of each per frame).
    GfxMatrix sunMatrix         = {};
    bool      sunEnabled        = true;
    // World-space direction the sun light travels (normalized). Backends
    // negate it to get the vector pointing toward the light. Default points
    // down-and-forward so unlit/extractor-absent frames still have a sane
    // direction rather than (0,0,0) which would produce NaN diffuse terms.
    float     sunDirection[3]   = {0.0f, -1.0f, 0.0f};
    // Effective world-space weather velocity, including gusts.
    float     wind[3]           = {0.0f, 0.0f, 0.0f};
    float     localLightScale   = 1.0f;
    std::uint32_t localLightCount = 0;
    std::array<LocalLight, kMaxFrameLocalLights> localLights = {};
    float     fogStart          = 0.0f;
    float     fogEnd            = 1000.0f;
    std::uint32_t fogColorRGBA  = 0;
    AtmosphereState atmosphere = {};

    // Visibility flags.
    SceneFlags flags = {};

    // Lifetime-of-process count of HIGH-severity GL driver errors
    // at the moment of extraction.  ValidateFrame fires when this
    // exceeds `lastObservedDebugErrorCount`, indicating a new
    // GL_INVALID_* fired since the previous frame.  Default 0 for
    // backends without driver-level validation.
    unsigned int currentDebugErrorCount   = 0;
    unsigned int lastObservedDebugErrorCount = 0;
    // Most recent HIGH-severity KHR_debug message captured by the
    // backend.  Surfaced in the violation detail so the log line points
    // at the specific GL misuse, not just "+1 error".
    std::string  lastDebugMessage;

    // Per-pass draws the engine has already classified.  Caller
    // splits draws here so BuildFrame doesn't need to reproduce the
    // classification logic.  Order mirrors the canonical pass
    // sequence emitted by BuildFrame.
    std::vector<SceneDraw> shadowDraws;
    std::vector<SceneDraw> skyDraws;
    std::vector<SceneDraw> terrainOpaqueDraws;
    std::vector<SceneDraw> worldOpaqueDraws;
    std::vector<SceneDraw> worldCutoutDraws;
    std::vector<SceneDraw> surfaceOverlayDraws;
    std::vector<SceneDraw> waterDraws;
    std::vector<SceneDraw> worldTransparentDraws;
    std::vector<SceneDraw> cockpitDraws;
    std::vector<SceneDraw> hudDraws;
};

} // namespace render::frame

} // namespace Poseidon
