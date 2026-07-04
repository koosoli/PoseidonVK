#pragma once

#include <glad/gl.h>

// Atomic GL blend-state bundles.
//
// Each helper here is the unique callsite of `glEnable(GL_BLEND)` /
// `glDisable(GL_BLEND)` / `glBlendFunc` / `glBlendFuncSeparate` for one named
// blend mode.  Callers pick a helper by name; partial state ("blend enabled but
// blend func not set") is unrepresentable because each helper sets both flags
// atomically — there is one entry point per mode and each writes the full
// bundle.

namespace Poseidon
{
namespace render::blend
{

inline void Opaque()
{
    glDisable(GL_BLEND);
}

inline void AlphaBlend()
{
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
}

inline void Additive()
{
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO);
}

// Shadow darken: (ZERO, ONE_MINUS_SRC_ALPHA) — multiplies dst by
// (1 - src.a), used by the stencil-shadow fullscreen darken quad.
inline void Shadow()
{
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
}

} // namespace render::blend

} // namespace Poseidon