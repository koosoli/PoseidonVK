#pragma once

#include <glad/gl.h>

// glClear with the depth-mask precondition baked in.

namespace Poseidon
{
namespace render::clear
{

inline void ColorDepthStencil(GLbitfield extraMask = 0)
{
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | extraMask);
}

inline void WithMask(GLbitfield mask)
{
    if (mask & GL_DEPTH_BUFFER_BIT)
        glDepthMask(GL_TRUE);
    if (mask)
        glClear(mask);
}

} // namespace render::clear

} // namespace Poseidon