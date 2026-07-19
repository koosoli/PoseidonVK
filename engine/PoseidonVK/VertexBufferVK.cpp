#include <PoseidonVK/VertexBufferVK.hpp>
#include <PoseidonVK/EngineVK.hpp>
#include <PoseidonVK/MeshBuilderVK.hpp>
#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/Graphics/Rendering/Primitives/Poly.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>

#include <algorithm>
#include <cmath>

namespace Poseidon
{

namespace
{
std::uint64_t HashMeshVertices(const std::vector<vk::MeshVertexVK>& vertices)
{
    // FNV-1a is deliberately tiny here: this guards the unusual case where a
    // legitimate CPU rebuild marks the buffer dirty but resolves to the same
    // vertex bytes.  It is never used to infer a mutation; bufferDirty remains
    // the authoritative producer-to-renderer synchronization contract.
    constexpr std::uint64_t kOffset = 1469598103934665603ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;
    std::uint64_t hash = kOffset;
    const auto* bytes = reinterpret_cast<const unsigned char*>(vertices.data());
    const std::size_t byteCount = vertices.size() * sizeof(vk::MeshVertexVK);
    for (std::size_t i = 0; i < byteCount; ++i)
        hash = (hash ^ bytes[i]) * kPrime;
    return hash;
}
} // namespace

VertexBufferVK::~VertexBufferVK()
{
    // The prior frame can still reference this mesh from the main command
    // buffer when a shape is unloaded or its HWTL setting changes.
    if (_engine && _engine->_device && _engine->_inFlight)
        vkWaitForFences(_engine->_device, 1, &_engine->_inFlight, VK_TRUE, UINT64_MAX);
    if (_engine)
    {
        _engine->_meshRegistry.Unregister(_meshResourceId);
    }
    if (_device)
    {
        vk::DestroyBuffer(_device, vertexBuffer);
        vk::DestroyBuffer(_device, indexBuffer);
    }
}

bool VertexBufferVK::Init(EngineVK& engine, const Shape& src, VBType type)
{
    if (src.NVertex() <= 0)
    {
        LOG_DEBUG(Graphics, "Vulkan: Empty vertices during VertexBufferVK init.");
        return false;
    }

    _engine = &engine;
    _device = engine._device;
    _dynamic = (type == VBDynamic || type == VBSmallDiscardable);
    _vertexCount = static_cast<std::uint32_t>(src.NVertex());

    // All Vulkan mesh families share one resource-id namespace.  Shadow-only
    // retained meshes and normal VertexBufferVK meshes must never alias.
    _meshResourceId = engine.AllocateMeshResourceId();

    // Build CPU-side mesh buffers
    const vk::MeshBuffersVK cpuBuffers = vk::BuildMeshBuffersVK(src);
    _indexCount = static_cast<std::uint32_t>(cpuBuffers.indices.size());

    // Create Vulkan vertex buffer
    VkResult result = vk::CreateHostVisibleBuffer(
        engine._physicalDevice, engine._device,
        _vertexCount * sizeof(vk::MeshVertexVK),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexBuffer);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: Failed to create vertex buffer: {}", static_cast<int>(result));
        return false;
    }

    // Upload vertices
    vk::UploadMappedBuffer(vertexBuffer, cpuBuffers.vertices.data(), cpuBuffers.vertices.size() * sizeof(vk::MeshVertexVK));
    _lastUploadHash = HashMeshVertices(cpuBuffers.vertices);
    _haveUploadHash = true;

    // Create and upload Vulkan index buffer if indices exist
    if (_indexCount > 0)
    {
        result = vk::CreateHostVisibleBuffer(
            engine._physicalDevice, engine._device,
            _indexCount * sizeof(std::uint16_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indexBuffer);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "Vulkan: Failed to create index buffer: {}", static_cast<int>(result));
            vk::DestroyBuffer(engine._device, vertexBuffer);
            vertexBuffer = vk::BufferVK{};
            return false;
        }

        // Upload indices
        vk::UploadMappedBuffer(indexBuffer, cpuBuffers.indices.data(), cpuBuffers.indices.size() * sizeof(std::uint16_t));
    }

    // Build per-section index ranges
    _sections.resize(src.NSections());
    std::uint32_t start = 0;
    for (int i = 0; i < src.NSections(); i++)
    {
        const ShapeSection& sec = src.GetSection(i);
        std::uint32_t size = 0;
        for (Offset o = sec.beg; o < sec.end; src.NextFace(o))
        {
            const Poly& face = src.Face(o);
            if (face.N() >= 3)
                size += (face.N() - 2) * 3;
        }
        _sections[i].beg = start;
        _sections[i].end = start + size;
        start += size;
    }

    // Register mesh resources in the registry
    vk::MeshResourcesVK resources;
    resources.vertexBuffer = vertexBuffer.buffer;
    resources.indexBuffer = indexBuffer.buffer;
    resources.vertexCount = _vertexCount;
    resources.indexCount = _indexCount;
    if (!cpuBuffers.vertices.empty())
    {
        float minimum[3] = {cpuBuffers.vertices[0].position[0], cpuBuffers.vertices[0].position[1],
                            cpuBuffers.vertices[0].position[2]};
        float maximum[3] = {minimum[0], minimum[1], minimum[2]};
        for (const vk::MeshVertexVK& vertex : cpuBuffers.vertices)
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                minimum[axis] = std::min(minimum[axis], vertex.position[axis]);
                maximum[axis] = std::max(maximum[axis], vertex.position[axis]);
            }
        }
        for (int axis = 0; axis < 3; ++axis)
            resources.localBoundsCenter[axis] = (minimum[axis] + maximum[axis]) * 0.5f;
        float radiusSquared = 0.0f;
        for (const vk::MeshVertexVK& vertex : cpuBuffers.vertices)
        {
            const float dx = vertex.position[0] - resources.localBoundsCenter[0];
            const float dy = vertex.position[1] - resources.localBoundsCenter[1];
            const float dz = vertex.position[2] - resources.localBoundsCenter[2];
            radiusSquared = std::max(radiusSquared, dx * dx + dy * dy + dz * dz);
        }
        resources.localBoundsRadius = std::sqrt(radiusSquared);
    }

    engine._meshRegistry.Register(_meshResourceId, resources);

    return true;
}

void VertexBufferVK::Update(const Shape& src, bool dynamic)
{
    (void)dynamic;
    // Do NOT upload merely because a buffer is dynamic.  Forest/terrain-
    // conforming meshes are dynamic-capable, but after their producer has
    // published an unchanged frame, re-uploading each instance every frame
    // burns CPU time and host-visible bandwidth.  All vertex-mutating paths
    // must call InvalidateBuffer(), which raises bufferDirty before this point.
    if (!bufferDirty)
        return;

    const vk::MeshBuffersVK cpuBuffers = vk::BuildMeshBuffersVK(src);
    if (cpuBuffers.vertices.size() != _vertexCount)
    {
        LOG_WARN(Graphics, "Vulkan: Vertex count mismatch on update (expected {}, got {})", _vertexCount, cpuBuffers.vertices.size());
        bufferDirty = false;
        return;
    }

    const std::uint64_t uploadHash = HashMeshVertices(cpuBuffers.vertices);
    bufferDirty = false;
    if (_haveUploadHash && uploadHash == _lastUploadHash)
        return;

    vk::UploadMappedBuffer(vertexBuffer, cpuBuffers.vertices.data(), cpuBuffers.vertices.size() * sizeof(vk::MeshVertexVK));
    _lastUploadHash = uploadHash;
    _haveUploadHash = true;
}

} // namespace Poseidon
