#pragma once

#include <Poseidon/Graphics/Rendering/Frame/Frame.hpp>
#include <Poseidon/Graphics/Rendering/Frame/SceneInputs.hpp>

// Pure function: produce a Frame from typed scene inputs.
//
// BuildFrame is the central decision point of the frame layer.  It is
// the only place that *invents* Pass ordering, clear-flag policy,
// and per-pass classification — everything else flows from its
// output via `ValidateFrame()` and (eventually) `Execute()`.
//
// BuildFrame takes a value-typed `SceneInputs` rather than reading the
// engine's live Scene singleton.  Tests construct SceneInputs inline
// with synthetic data; the scene extractor sits in a separate impure
// file and is the only seam where live engine state crosses in.
//
// Contract:
//   - Output Frame's pass order matches the canonical sequence
//     ShadowAccum → ShadowDarken → Sky → TerrainOpaque → WorldOpaque → WorldCutout
//     → Water → WorldTransparent → Cockpit → ScreenSpace.
//   - Empty passes are *omitted* (no zero-draw Pass entries),
//     so ValidateFrame's pass-ordering check stays linear.
//   - clearColor / clearDepth flags are placed on the first 3D
//     pass that follows a screen-space pass (or frame start).
//   - The output Frame.camera mirrors SceneInputs.camera one-to-one.


namespace Poseidon
{
namespace render::frame
{

Frame BuildFrame(const SceneInputs& s);

} // namespace render::frame

} // namespace Poseidon
