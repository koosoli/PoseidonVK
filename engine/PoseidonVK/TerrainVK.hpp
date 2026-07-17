#pragma once

#include <PoseidonVK/BufferVK.hpp>
#include <Poseidon/Graphics/Rendering/Frame/Frame.hpp>
#include <Poseidon/World/Terrain/TerrainCdlod.hpp>

#include <cstdint>
#include <vector>

namespace Poseidon::vk
{
// Vulkan ownership for the immutable terrain map contract.  Raster activation
// is intentionally separate: no caller may select this path until its terrain
// self-shadow and sky-visibility resources are populated too.
class TerrainVK
{
  public:
    static constexpr std::uint32_t kGridN = 32;
    struct GridVertex { float x, z, skirt; };
    struct NodeInstance { float originX, originZ, size; std::uint32_t lod; float morphStart, morphEnd; };
    struct Params
    {
        float worldOrigin[2] = {};
        float landGrid = 1.0f, terrainGrid = 1.0f;
        std::uint32_t heightWidth = 1, heightHeight = 1, landRange = 1, layerCount = 0;
        float seaLevel = 0.0f, time = 0.0f, swashSpeed = 0.15f, swashAmplitude = 0.15f;
        float wetHeight = 1.2f, wetDarken = 0.55f, _pad[2] = {};
    };

    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device, VkCommandPool commandPool, VkQueue queue);
    void Destroy(VkDevice device);
    bool Upload(const render::frame::TerrainOpaque& terrain);
    void Select(float cameraX, float cameraY, float cameraZ, float visibleX0, float visibleZ0, float visibleX1,
                float visibleZ1);
    bool Ready() const noexcept { return _ready; }
    std::uint64_t Revision() const noexcept { return _revision.revision; }
    const std::vector<NodeInstance>& VisibleNodes() const noexcept { return _visible; }
    const BufferVK& GridVertices() const noexcept { return _gridVertices; }
    const BufferVK& GridIndices() const noexcept { return _gridIndices; }
    const BufferVK& Instances() const noexcept { return _instances; }
    const ImageVK& Heightmap() const noexcept { return _heightmap; }
    const ImageVK& IndexMap() const noexcept { return _indexMap; }
    const ImageVK& JitterMap() const noexcept { return _jitterMap; }
    const Params& Parameters() const noexcept { return _params; }

  private:
    bool CreateGrid();
    bool RecreateMapImages(const render::frame::TerrainOpaque& terrain);
    VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
    VkDevice _device = VK_NULL_HANDLE;
    VkCommandPool _commandPool = VK_NULL_HANDLE;
    VkQueue _queue = VK_NULL_HANDLE;
    BufferVK _gridVertices, _gridIndices, _instances, _paramsBuffer;
    ImageVK _heightmap, _indexMap, _jitterMap;
    CdlodRevisionCache _revision;
    std::vector<CdlodNode> _tree;
    std::vector<float> _ranges;
    std::vector<NodeInstance> _visible;
    int _root = -1, _levels = 0;
    float _leafSize = 0.0f;
    Params _params;
    bool _ready = false;
};
} // namespace Poseidon::vk
