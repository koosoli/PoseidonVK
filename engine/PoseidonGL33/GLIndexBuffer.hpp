#pragma once

#include <glad/gl.h>

#ifndef NDEBUG
#include <Poseidon/Dev/Debug/DebugTrap.hpp>
#endif

// IBO bind that enforces the active-VAO precondition.

namespace Poseidon
{
namespace render::ibo
{

inline void BindOnActiveVao(GLuint ibo)
{
#ifndef NDEBUG
    GLint currentVao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &currentVao);
    if (currentVao == 0)
    {
        BREAK();
    }
#endif
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
}

} // namespace render::ibo

} // namespace Poseidon