#pragma once

#include <Poseidon/Core/Types.hpp>
#include <Poseidon/Graphics/Core/MatrixConversion.hpp>
#include <Poseidon/Graphics/Core/RenderState.hpp>
#include <Poseidon/Graphics/Rendering/RenderFlags.hpp>
#include <Poseidon/Graphics/Rendering/RenderPassDescriptor.hpp>
#include <Poseidon/Graphics/Rendering/Frame/Buffers.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Immutable Frame describing a complete frame's render plan.  Built
// by `BuildFrame(SceneInputs)` as a pure function; consumed by the
// pure validators (`ValidateFrame`, `CountFrameStats`).
//
// Design intent:
//   - Distinct struct members for projection / view / world / vpScale —
//     no UBO offset can alias another.
//   - Frame is immutable after Build; no caches, no shared state to
//     leak between draws.
//   - Each Draw carries a *complete* RenderPassDescriptor — partial
//     material state is unrepresentable.
//   - Typed enum classes for PassKind, BufferUsage; one value, one
//     meaning.

namespace Poseidon
{
namespace render::frame
{

// Resource handles.  Opaque handles used by Draws to reference
// vertex/index buffers and textures.  Backend resolves handle ->
// backend-local GPU state at Execute time.  Typed so a TextureHandle cannot be passed
// where a BufferHandle is expected.  (BufferUsage lives in Buffers.hpp.)

struct BufferHandle
{
    std::uint32_t id = 0;
    BufferUsage usage = BufferUsage::Static;
};

struct TextureHandle
{
    std::uint32_t id = 0;
};

// Mesh: opaque backend mesh resource + optional typed buffer ids.  The shared
// frame layer treats `id` as an opaque token; a backend resolves it to whatever
// concrete state it needs before issuing the indexed draw.

struct MeshHandle
{
    std::uint32_t id = 0;
    BufferHandle vbo;
    BufferHandle ibo;

    bool HasBackendMesh() const noexcept { return id != 0; }
};

// Viewport and camera (frame-global, immutable).

struct Viewport
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct CameraView
{
    GfxMatrix view = {};       // distinct struct member from projection
    GfxMatrix projection = {}; // distinct struct member from view + world
    Viewport viewport = {};
    float nearPlane = 0.07f;
    float farPlane = 1000.0f;
    // World render sub-rect as fractions of the viewport (the aspect
    // pillarbox / manual crop).  Full rect = no crop.  When cropped,
    // the projection's FOV must match the rect's pixel aspect (the
    // no-stretch contract).
    float worldLeft = 0.0f;
    float worldTop = 0.0f;
    float worldRight = 1.0f;
    float worldBottom = 1.0f;
};

inline constexpr std::size_t kMaxFrameLocalLights = 8;

enum class LocalLightKind : std::uint8_t
{
    Point = 0,
    Spot = 1,
};

struct LocalLight
{
    // Camera-relative position matching captured draw world matrices.
    float position[3] = {0.0f, 0.0f, 0.0f};
    // World-space beam direction. Used when kind == Spot.
    float direction[3] = {0.0f, 0.0f, 1.0f};
    float diffuse[3] = {0.0f, 0.0f, 0.0f};
    float ambient[3] = {0.0f, 0.0f, 0.0f};
    float startAtten = 0.0f;
    LocalLightKind kind = LocalLightKind::Point;
};

// Projection and viewportScale live in different struct members at
// different addresses — no byte-offset-table aliasing.  At Execute time
// each is uploaded to its own UBO range or per-shader uniform, with
// non-overlapping layout.

// Per-draw descriptor.  Every Draw carries a complete
// RenderPassDescriptor — "partial material" is unrepresentable.

struct Draw
{
    RenderPassDescriptor descriptor = {}; // complete material state
    GfxMatrix world = {};                 // camera-relative
    MeshHandle mesh = {};
    int indexBegin = 0; // index-buffer offset (raw)
    int indexCount = 0; // primitive count in indices
    std::array<TextureHandle, 4> textures = {};
};

// Byte offset that glDrawElements' `indices` parameter expects (cast
// to void* at the call site).  constexpr so the computation lives in
// one tested place.  Pass indexSize explicitly (the engine's index
// type is `short`, 2 bytes) so a format swap can't silently break it.
inline constexpr std::intptr_t ComputeIndexByteOffset(int firstIndex, std::size_t indexSize) noexcept
{
    return static_cast<std::intptr_t>(firstIndex) * static_cast<std::intptr_t>(indexSize);
}

// Pass: typed kind + draws.  PassKind values are semantically distinct
// — one value, one meaning.  OnSurface decals get their own kind so
// validation can assert the disambiguation strategy.

enum class FramePassKind : std::uint8_t
{
    ShadowAccum,  // stencil-only shadow caster pass
    ShadowDarken, // fullscreen darken quad (consumes shadow stencil)
    Sky,          // sky dome / clouds — drawn first, no z-write
    TerrainOpaque,
    WorldOpaque,
    WorldCutout,
    SurfaceOverlay, // roads / decals (OnSurface) — after terrain
    Water,          // water surface
    WorldTransparent,
    Cockpit,     // first-person / vehicle interior
    ScreenSpace, // HUD, UI, late overlays
};

// Stable label for a pass kind — used by the tri harness shape
// readout and log lines.
inline const char* FramePassKindName(FramePassKind k) noexcept
{
    switch (k)
    {
        case FramePassKind::ShadowAccum:
            return "ShadowAccum";
        case FramePassKind::ShadowDarken:
            return "ShadowDarken";
        case FramePassKind::Sky:
            return "Sky";
        case FramePassKind::TerrainOpaque:
            return "TerrainOpaque";
        case FramePassKind::WorldOpaque:
            return "WorldOpaque";
        case FramePassKind::WorldCutout:
            return "WorldCutout";
        case FramePassKind::SurfaceOverlay:
            return "SurfaceOverlay";
        case FramePassKind::Water:
            return "Water";
        case FramePassKind::WorldTransparent:
            return "WorldTransparent";
        case FramePassKind::Cockpit:
            return "Cockpit";
        case FramePassKind::ScreenSpace:
            return "ScreenSpace";
    }
    return "Unknown";
}

struct Pass
{
    FramePassKind kind = FramePassKind::WorldOpaque;
    bool clearColor = false;
    bool clearDepth = false;
    bool clearStencil = false;
    std::vector<Draw> draws;
};

// Frame: the complete render plan.

struct Frame
{
    CameraView camera = {};
    std::vector<Pass> passes;

    // Frame-global resources (lights, fog, sun) — separate struct
    // members, can't alias each other or the camera matrices.
    GfxMatrix sunMatrix = {};
    float fogStart = 0.0f;
    float fogEnd = 1000.0f;
    std::uint32_t fogColorRGBA = 0; // packed RGBA; type-decoupled from engine PackedColor
    bool sunEnabled = true;
    // World-space direction the sun light travels (normalized). Matches
    // SceneInputs; backends negate it to obtain the vector toward the light.
    float sunDirection[3] = {0.0f, -1.0f, 0.0f};
    float localLightScale = 1.0f;
    std::uint32_t localLightCount = 0;
    std::array<LocalLight, kMaxFrameLocalLights> localLights = {};

    // Per-frame GL error delta carried from SceneInputs.  Non-zero =
    // a new HIGH-severity GL error fired this frame.
    unsigned int newDebugErrors = 0;
    // Most recent HIGH-severity GL_DEBUG message captured by the
    // engine.  ValidateFrame surfaces it so an operator can act on the
    // log line without grep'ing the earlier KHR_debug callback lines.
    std::string lastDebugMessage;
};

} // namespace render::frame

} // namespace Poseidon
