#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <array>
#include <unordered_map>
#include <vector>

namespace Poseidon::vk
{

// Resolved Vulkan mesh resources for one backend-owned mesh. The shared frame
// layer treats MeshHandle::id as an opaque token; the Vulkan backend resolves
// that token to these concrete buffers before issuing an indexed draw.
//
//   vertexBuffer  — VkBuffer holding SVertex-shaped vertices (pos, norm, uv).
//   indexBuffer   — VkBuffer holding uint16 indices.
//   vertexCount   — number of vertices in vertexBuffer (for bounds checks).
//   indexCount    — number of indices in indexBuffer (for bounds checks).
//
// Both counts are stored so the draw emitter can clamp a draw's firstIndex/
// indexCount against the actual buffer contents (matching the GL33 path's
// DrawSectionTL index-range resolution).
struct MeshResourcesVK
{
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
    // Conservative local-space sphere consumed by the GPU scene culler.  The
    // default deliberately disables rejection for externally registered legacy
    // meshes until their upload path supplies real bounds.
    float localBoundsCenter[3] = {};
    float localBoundsRadius = 1000000.0f;
    // Optional mesh-wide LOD index ranges.  A zero count means "use the draw
    // section range" so existing meshes keep exact legacy behaviour.
    std::array<std::uint32_t, 4> lodFirstIndex = {};
    std::array<std::uint32_t, 4> lodIndexCount = {};
    std::array<float, 4> lodDistance = {}; // LOD0 is always selected at zero.

    bool IsValid() const noexcept { return vertexBuffer != VK_NULL_HANDLE && indexBuffer != VK_NULL_HANDLE; }
};

// Vulkan-side mesh resource registry. Mirrors the GL33 MeshResourceRegistry
// (id -> VAO) but resolves to a (vbo, ibo) pair instead. The registry is the
// single point where the live draw loop looks up a MeshHandle::id to bind the
// correct vertex/index buffers for a draw.
//
// Lifetime: mesh buffers are registered when the Vulkan backend uploads them
// and unregistered when the owning VertexBufferVK is destroyed. The registry
// holds non-owning handles — the caller owns the VkBuffer/VkDeviceMemory
// lifetimes and must keep them valid while registered.
class MeshRegistryVK
{
  public:
    // Registers (or overwrites) the resources for a mesh id. Returns true when
    // a new entry was inserted, false when an existing entry was overwritten.
    bool Register(std::uint32_t id, const MeshResourcesVK& resources) noexcept
    {
        const bool inserted = _entries.find(id) == _entries.end();
        _entries[id] = resources;
        return inserted;
    }

    // Drops the entry for id if present. Returns true when an entry was removed.
    bool Unregister(std::uint32_t id) noexcept { return _entries.erase(id) > 0; }

    // Resolves id to its resources, or nullptr if unknown. The pointer is valid
    // until the next Register/Unregister/Clear call on this registry.
    const MeshResourcesVK* Resolve(std::uint32_t id) const noexcept
    {
        const auto it = _entries.find(id);
        return it != _entries.end() ? &it->second : nullptr;
    }

    bool Contains(std::uint32_t id) const noexcept { return _entries.find(id) != _entries.end(); }

    std::size_t Size() const noexcept { return _entries.size(); }

    void Clear() noexcept { _entries.clear(); }

  private:
    std::unordered_map<std::uint32_t, MeshResourcesVK> _entries;
};

} // namespace Poseidon::vk
