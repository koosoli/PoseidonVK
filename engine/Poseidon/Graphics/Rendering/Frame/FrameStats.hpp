#pragma once

#include "Frame.hpp"

#include <unordered_set>

namespace Poseidon
{

namespace render::frame
{

// Per-frame dispatch statistics — a pure fold over the Frame value.
// Feeds the --render-frame-log summary line; nothing here touches GL.
struct FrameStats
{
    unsigned int passCount = 0;
    unsigned int drawCount = 0;
    unsigned int maxDrawsInPass = 0;
    unsigned int uniqueTextureCount = 0;
    unsigned int uniqueVertexBufferCount = 0;
    unsigned int uniqueIndexBufferCount = 0;
};

inline FrameStats CountFrameStats(const Frame& f)
{
    FrameStats s;
    std::unordered_set<std::uint32_t> textures;
    std::unordered_set<std::uint32_t> vertexBuffers;
    std::unordered_set<std::uint32_t> indexBuffers;
    for (const auto& p : f.passes)
    {
        ++s.passCount;
        const auto draws = static_cast<unsigned int>(p.draws.size());
        s.drawCount += draws;
        if (draws > s.maxDrawsInPass)
            s.maxDrawsInPass = draws;

        for (const auto& d : p.draws)
        {
            const std::uint32_t vboId = d.mesh.vbo.id != 0 ? d.mesh.vbo.id : d.mesh.id;
            const std::uint32_t iboId = d.mesh.ibo.id != 0 ? d.mesh.ibo.id : d.mesh.id;
            if (vboId != 0)
                vertexBuffers.insert(vboId);
            if (iboId != 0)
                indexBuffers.insert(iboId);

            for (const auto& tex : d.textures)
            {
                if (tex.id != 0)
                    textures.insert(tex.id);
            }
        }
    }
    s.uniqueTextureCount = static_cast<unsigned int>(textures.size());
    s.uniqueVertexBufferCount = static_cast<unsigned int>(vertexBuffers.size());
    s.uniqueIndexBufferCount = static_cast<unsigned int>(indexBuffers.size());
    return s;
}

} // namespace render::frame

} // namespace Poseidon
