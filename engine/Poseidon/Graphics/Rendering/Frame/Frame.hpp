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
#include <optional>
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

// Simulation-owned weather and celestial data consumed by renderers. GPU
// volume caches and temporal reconstruction remain backend implementation
// details, but all backends observe the same atmosphere state.
struct AtmosphereState
{
    float overcast = 0.0f;
    float rainDensity = 0.0f;
    float skyThrough = 1.0f;
    float cloudDensity = 0.0f;
    float cloudBrightness = 1.0f;
    float cloudTime = 0.0f;
    float cloudBase = 600.0f;
    float cloudTop = 5000.0f;
    float cloudExtent = 32768.0f;
    std::uint32_t cloudSeed = 0x43574c44u;
    float moonDirection[3] = {0.0f, -1.0f, 0.0f};
    float moonUp[3] = {0.0f, 1.0f, 0.0f};
    float moonPhase = 0.5f;
    float starsVisibility = 0.0f;
    float starsOrientation[3][3] = {{1.0f, 0.0f, 0.0f},
                                    {0.0f, 1.0f, 0.0f},
                                    {0.0f, 0.0f, 1.0f}};
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
    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

// A shadow caster deliberately references the same mesh allocation as its
// receiver draw.  Shadow passes must not manufacture a second, CPU-flattened
// copy of scene geometry: each backend resolves mesh.id to its existing GPU
// vertex/index resources and applies this camera-relative transform.
enum class ShadowCasterAlphaMode : std::uint8_t
{
    Opaque,
    Cutout,
};

struct ShadowCaster
{
    MeshHandle mesh = {};
    GfxMatrix world = {};
    int indexBegin = 0;
    int indexCount = 0;
    ShadowCasterAlphaMode alphaMode = ShadowCasterAlphaMode::Opaque;
    // Meaningful only for Cutout.  This is the material's primary alpha
    // texture, not a backend texture pointer.
    TextureHandle alphaTexture = {};
    float alphaCutoff = 0.5f;
};

// Values required to fit the shared ShadowMath cascades.  The extractor
// snapshots these from the live camera once; a backend never has to walk the
// Scene graph while consuming an immutable Frame.
struct ShadowCamera
{
    float forward[3] = {0.0f, 0.0f, 1.0f};
    float right[3] = {1.0f, 0.0f, 0.0f};
    float up[3] = {0.0f, 1.0f, 0.0f};
    float tanHalfX = 1.0f;
    float tanHalfY = 1.0f;
    float nearDistance = 0.07f;
    float farDistance = 1000.0f;
};

struct ShadowInput
{
    bool enabled = false;
    // Daylight contribution.  Zero disables both the depth submission and
    // receiver sampling, preventing stale cascades at night.
    float sunFactor = 1.0f;
    ShadowCamera camera = {};
    std::vector<ShadowCaster> casters;
};

// Immutable map snapshot for a dedicated terrain backend. The texture-index
// high bit is the legacy transition (ClampU|ClampV) flag; layers remain native
// texture resources and are never packed into an atlas.
struct TerrainOpaque
{
    std::uint64_t revision = 0;
    std::uint32_t heightWidth = 0;
    std::uint32_t heightHeight = 0;
    std::uint32_t indexWidth = 0;
    std::uint32_t indexHeight = 0;
    float terrainGrid = 0.0f;
    float landGrid = 0.0f;
    float seaLevel = 0.0f;
    int visibleXBegin = 0, visibleZBegin = 0, visibleXEnd = 0, visibleZEnd = 0;
    std::vector<float> heights;
    std::vector<std::uint16_t> textureIndices;
    std::vector<std::int8_t> jitterOffsets;
    std::vector<TextureHandle> textureLayers;

    bool Valid() const noexcept
    {
        return heightWidth > 1 && heightHeight > 1 && indexWidth > 0 && indexHeight > 0 && terrainGrid > 0.0f &&
               landGrid > 0.0f && heights.size() == static_cast<std::size_t>(heightWidth) * heightHeight &&
               textureIndices.size() == static_cast<std::size_t>(indexWidth) * indexHeight &&
               jitterOffsets.size() == static_cast<std::size_t>(indexWidth) * indexHeight * 2;
    }
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
    ShadowDepth,  // depth-array cascades; resources in Frame::shadowInput
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
        case FramePassKind::ShadowDepth:
            return "ShadowDepth";
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
    float cameraPosition[3] = {0.0f, 0.0f, 0.0f};
    std::vector<Pass> passes;

    // The backend-neutral CSM contract.  It owns the only Frame-level caster
    // list and points at normal scene mesh resources; ShadowDepth is the
    // scheduling marker in passes.
    ShadowInput shadowInput = {};
    // Mutually exclusive with Draws in the TerrainOpaque pass.
    std::optional<TerrainOpaque> terrainOpaque;

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
    float wind[3] = {0.0f, 0.0f, 0.0f};
    float localLightScale = 1.0f;
    std::uint32_t localLightCount = 0;
    std::array<LocalLight, kMaxFrameLocalLights> localLights = {};
    AtmosphereState atmosphere = {};

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
