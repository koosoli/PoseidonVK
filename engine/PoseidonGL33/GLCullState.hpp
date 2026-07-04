#pragma once

#include <glad/gl.h>

// Atomic GL cull-face state bundles.

namespace Poseidon
{
namespace render::cull
{

inline void Back()
{
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
}

inline void Front()
{
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
}

inline void None()
{
    glDisable(GL_CULL_FACE);
}

inline void FrontFaceCW()
{
    glFrontFace(GL_CW);
}
inline void FrontFaceCCW()
{
    glFrontFace(GL_CCW);
}

} // namespace render::cull

} // namespace Poseidon