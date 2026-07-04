#pragma once

#include <glad/gl.h>

// Atomic GL depth + stencil state bundles.

namespace Poseidon
{
namespace render::depthstencil
{

namespace detail
{
inline void StencilAlwaysReplaceZero()
{
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
}
inline void StencilEqualZeroIncr()
{
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
}
} // namespace detail

inline void Normal(bool hasStencil)
{
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    if (hasStencil)
        detail::StencilAlwaysReplaceZero();
}

inline void ReadOnly(bool hasStencil)
{
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    if (hasStencil)
        detail::StencilAlwaysReplaceZero();
}

inline void Disabled(bool hasStencil)
{
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_FALSE);
    if (hasStencil)
        detail::StencilAlwaysReplaceZero();
}

inline void Shadow(bool hasStencil)
{
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    if (hasStencil)
    {
        glDepthMask(GL_FALSE);
        detail::StencilEqualZeroIncr();
    }
    else
    {
        glDepthMask(GL_TRUE);
    }
}

} // namespace render::depthstencil

} // namespace Poseidon