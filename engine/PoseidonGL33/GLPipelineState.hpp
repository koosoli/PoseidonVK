#pragma once

#include <glad/gl.h>

// Per-draw pipeline GL state helpers that don't naturally fit into
// a per-mode bundle.

namespace Poseidon
{
namespace render::pipeline
{

inline void SetColorMask(bool write)
{
    const GLboolean v = write ? GL_TRUE : GL_FALSE;
    glColorMask(v, v, v, v);
}

inline void SetPolygonOffsetForDecals(bool enabled)
{
    if (enabled)
    {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -1.0f);
    }
    else
    {
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
}

inline void SetPolygonOffsetForShadows(bool enabled)
{
    if (enabled)
    {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -64.0f);
    }
    else
    {
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
}

inline void EnableDepthTest()
{
    glEnable(GL_DEPTH_TEST);
}
inline void DisableDepthClamp()
{
    glDisable(GL_DEPTH_CLAMP);
}
inline void EnableDepthClamp()
{
    glEnable(GL_DEPTH_CLAMP);
}

inline void SetClearColor(float r, float g, float b, float a)
{
    glClearColor(r, g, b, a);
}

} // namespace render::pipeline

} // namespace Poseidon