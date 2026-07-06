#include <Poseidon/Graphics/Rendering/Frame/BuildFrame.hpp>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace Poseidon
{

namespace render::frame
{

namespace
{

// Helper — wraps a SceneDraw into a Frame Draw.  One-to-one today;
// the indirection exists so future per-draw transforms (e.g. world
// matrix camera-relativisation) live in one place.
Draw makeDraw(const SceneDraw& d)
{
    Draw out;
    out.descriptor = d.descriptor;
    out.world = d.world;
    out.mesh = d.mesh;
    out.indexBegin = d.indexBegin;
    out.indexCount = d.indexCount;
    out.textures = d.textures;
    return out;
}

// Emit a Pass with the given kind and the supplied draws.  Empty
// passes are skipped (see contract in BuildFrame.hpp).  Returns
// true iff a pass was emitted (so caller can place a clear flag
// on the first emitted 3D pass).
bool emitPass(Frame& f, FramePassKind kind, const std::vector<SceneDraw>& draws)
{
    if (draws.empty())
        return false;
    Pass p;
    p.kind = kind;
    p.draws.reserve(draws.size());
    for (const auto& d : draws)
        p.draws.push_back(makeDraw(d));
    f.passes.push_back(std::move(p));
    return true;
}

} // namespace

Frame BuildFrame(const SceneInputs& s)
{
    Frame f;
    f.camera = s.camera;
    f.sunMatrix = s.sunMatrix;
    f.sunEnabled = s.sunEnabled;
    f.sunDirection[0] = s.sunDirection[0];
    f.sunDirection[1] = s.sunDirection[1];
    f.sunDirection[2] = s.sunDirection[2];
    f.fogStart = s.fogStart;
    f.fogEnd = s.fogEnd;
    f.fogColorRGBA = s.fogColorRGBA;

    // Carry forward the per-frame GL error delta.  ValidateFrame
    // turns non-zero into a violation.
    f.newDebugErrors = (s.currentDebugErrorCount > s.lastObservedDebugErrorCount)
                           ? (s.currentDebugErrorCount - s.lastObservedDebugErrorCount)
                           : 0;
    f.lastDebugMessage = s.lastDebugMessage;

    // Emit passes in canonical order.  First pass emitted (of any
    // kind) gets the start-of-frame clear flags so the backbuffer
    // doesn't carry whatever was there last frame.
    bool firstEmitted = false;
    auto tryEmit = [&](FramePassKind kind, const std::vector<SceneDraw>& d)
    {
        if (!emitPass(f, kind, d))
            return;
        if (!firstEmitted)
        {
            f.passes.back().clearColor = true;
            f.passes.back().clearDepth = true;
            f.passes.back().clearStencil = true;
            firstEmitted = true;
        }
    };

    // ShadowAccum — stencil-only caster pass.  Gated on
    // shadowsEnabled AND non-empty bucket so a forced-off frame
    // doesn't emit an empty pass.
    if (s.flags.shadowsEnabled)
        tryEmit(FramePassKind::ShadowAccum, s.shadowDraws);

    tryEmit(FramePassKind::Sky, s.skyDraws);
    tryEmit(FramePassKind::WorldOpaque, s.worldOpaqueDraws);
    tryEmit(FramePassKind::WorldCutout, s.worldCutoutDraws);
    tryEmit(FramePassKind::SurfaceOverlay, s.surfaceOverlayDraws);
    tryEmit(FramePassKind::Water, s.waterDraws);
    tryEmit(FramePassKind::WorldTransparent, s.worldTransparentDraws);

    // Cockpit pass — first-person view only.  Execute must re-upload
    // the projection here: the transition out of a screen-space pass
    // back into a 3D one needs the full camera state restored.
    if (s.flags.inFirstPersonView)
        tryEmit(FramePassKind::Cockpit, s.cockpitDraws);

    // HUD / 2D overlays — always last.
    if (s.flags.hudEnabled)
        tryEmit(FramePassKind::ScreenSpace, s.hudDraws);

    // If nothing emitted at all, we still need a clear pass so the
    // backbuffer doesn't carry whatever was there last frame.
    if (!firstEmitted)
    {
        Pass clearPass;
        clearPass.kind = FramePassKind::ScreenSpace;
        clearPass.clearColor = true;
        clearPass.clearDepth = true;
        clearPass.clearStencil = true;
        f.passes.push_back(std::move(clearPass));
    }

    return f;
}

} // namespace render::frame

} // namespace Poseidon
