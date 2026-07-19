#pragma once

#include <Poseidon/Graphics/Rendering/Primitives/Vertex.hpp>
#include <PoseidonVK/BufferVK.hpp>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace Poseidon
{
class Shape;
class EngineVK;

struct VBSectionInfoVK
{
    std::uint32_t beg = 0;
    std::uint32_t end = 0;
};

class VertexBufferVK : public VertexBuffer
{
public:
    VertexBufferVK() = default;
    ~VertexBufferVK() override;

    bool Init(EngineVK& engine, const Shape& src, VBType type);
    void Update(const Shape& src, bool dynamic) override;

    std::uint32_t GetMeshResourceId() const noexcept { return _meshResourceId; }
    std::uint32_t GetVertexCount() const noexcept { return _vertexCount; }
    std::uint32_t GetIndexCount() const noexcept { return _indexCount; }

    vk::BufferVK vertexBuffer;
    vk::BufferVK indexBuffer;
    std::vector<VBSectionInfoVK> _sections;

private:
    VkDevice _device = VK_NULL_HANDLE;
    EngineVK* _engine = nullptr;
    std::uint32_t _meshResourceId = 0;
    std::uint32_t _vertexCount = 0;
    std::uint32_t _indexCount = 0;
    bool _dynamic = false;
    // Dynamic means the producer may mutate this mesh; it does not mean the
    // bytes changed this frame.  The shape's bufferDirty flag is the mutation
    // contract, and this hash additionally avoids a PCIe upload when a dirty
    // rebuild produces identical vertices.
    bool _haveUploadHash = false;
    std::uint64_t _lastUploadHash = 0;
};

} // namespace Poseidon
