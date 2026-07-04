#pragma once

#include <glad/gl.h>

// Texture sampler slot 0 bind.

namespace Poseidon
{
namespace render::sampler
{

inline void BindSlot0(GLuint sampler)
{
    glBindSampler(0, sampler);
}

} // namespace render::sampler

} // namespace Poseidon