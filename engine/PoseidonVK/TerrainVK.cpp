#include <PoseidonVK/TerrainVK.hpp>

#include <algorithm>
#include <cmath>

namespace Poseidon::vk
{
bool TerrainVK::Initialize(VkPhysicalDevice physicalDevice, VkDevice device, VkCommandPool commandPool, VkQueue queue)
{
    _physicalDevice = physicalDevice;
    _device = device;
    _commandPool = commandPool;
    _queue = queue;
    if (!device || !physicalDevice || !commandPool || !queue || !CreateGrid())
        return false;
    if (CreateHostVisibleBuffer(physicalDevice, device, 8192 * sizeof(NodeInstance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                _instances) != VK_SUCCESS ||
        CreateHostVisibleBuffer(physicalDevice, device, sizeof(Params), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, _paramsBuffer) !=
            VK_SUCCESS)
    {
        Destroy(device);
        return false;
    }
    UploadMappedBuffer(_paramsBuffer, &_params, sizeof(_params));
    _ready = true;
    return true;
}

bool TerrainVK::CreateGrid()
{
    std::vector<GridVertex> vertices;
    std::vector<std::uint16_t> indices;
    const auto addVertex = [&](float x, float z, float skirt) { vertices.push_back({x, z, skirt}); };
    for (std::uint32_t z = 0; z <= kGridN; ++z)
        for (std::uint32_t x = 0; x <= kGridN; ++x)
            addVertex(float(x) / kGridN, float(z) / kGridN, 0.0f);
    const auto base = [](std::uint32_t x, std::uint32_t z) { return z * (kGridN + 1) + x; };
    const auto quad = [&](std::uint16_t a, std::uint16_t b, std::uint16_t c, std::uint16_t d)
    {
        indices.insert(indices.end(), {a, b, c, a, c, d});
    };
    for (std::uint32_t z = 0; z < kGridN; ++z)
        for (std::uint32_t x = 0; x < kGridN; ++x)
            quad(base(x, z), base(x + 1, z), base(x + 1, z + 1), base(x, z + 1));
    // Duplicate each boundary vertex at skirt=1 and stitch it to the surface.
    std::vector<std::uint16_t> edge;
    for (std::uint32_t x = 0; x <= kGridN; ++x) edge.push_back(base(x, 0));
    for (std::uint32_t z = 1; z <= kGridN; ++z) edge.push_back(base(kGridN, z));
    for (std::uint32_t x = kGridN; x-- > 0;) edge.push_back(base(x, kGridN));
    for (std::uint32_t z = kGridN; z-- > 1;) edge.push_back(base(0, z));
    const std::uint16_t skirtBase = static_cast<std::uint16_t>(vertices.size());
    for (std::uint16_t i : edge) addVertex(vertices[i].x, vertices[i].z, 1.0f);
    for (std::uint16_t i = 0; i < edge.size(); ++i)
    {
        const std::uint16_t n = static_cast<std::uint16_t>((i + 1) % edge.size());
        quad(edge[i], edge[n], skirtBase + n, skirtBase + i);
    }
    if (CreateHostVisibleBuffer(_physicalDevice, _device, vertices.size() * sizeof(GridVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                _gridVertices) != VK_SUCCESS ||
        CreateHostVisibleBuffer(_physicalDevice, _device, indices.size() * sizeof(std::uint16_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                _gridIndices) != VK_SUCCESS)
        return false;
    UploadMappedBuffer(_gridVertices, vertices.data(), vertices.size() * sizeof(GridVertex));
    UploadMappedBuffer(_gridIndices, indices.data(), indices.size() * sizeof(std::uint16_t));
    return true;
}

bool TerrainVK::RecreateMapImages(const render::frame::TerrainOpaque& terrain)
{
    DestroyImage(_device, _heightmap); DestroyImage(_device, _indexMap); DestroyImage(_device, _jitterMap);
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (CreateImage2D(_physicalDevice, _device, terrain.heightWidth, terrain.heightHeight, 1, VK_FORMAT_R32_SFLOAT, usage,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _heightmap) != VK_SUCCESS ||
        CreateImage2D(_physicalDevice, _device, terrain.indexWidth, terrain.indexHeight, 1, VK_FORMAT_R16_UINT, usage,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _indexMap) != VK_SUCCESS ||
        CreateImage2D(_physicalDevice, _device, terrain.indexWidth, terrain.indexHeight, 1, VK_FORMAT_R8G8_SNORM, usage,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _jitterMap) != VK_SUCCESS)
        return false;
    return UploadImage2D(_physicalDevice, _device, _commandPool, _queue, _heightmap, terrain.heights.data(),
                         terrain.heights.size() * sizeof(float)) == VK_SUCCESS &&
           UploadImage2D(_physicalDevice, _device, _commandPool, _queue, _indexMap, terrain.textureIndices.data(),
                         terrain.textureIndices.size() * sizeof(std::uint16_t)) == VK_SUCCESS &&
           UploadImage2D(_physicalDevice, _device, _commandPool, _queue, _jitterMap, terrain.jitterOffsets.data(),
                         terrain.jitterOffsets.size() * sizeof(std::int8_t)) == VK_SUCCESS;
}

bool TerrainVK::Upload(const render::frame::TerrainOpaque& terrain)
{
    if (!_ready || !terrain.Valid()) return false;
    if (_revision.NeedsRebuild(terrain.revision) && !RecreateMapImages(terrain)) return false;
    _params.landGrid = terrain.landGrid; _params.terrainGrid = terrain.terrainGrid;
    _params.heightWidth = terrain.heightWidth; _params.heightHeight = terrain.heightHeight;
    _params.landRange = terrain.indexWidth; _params.layerCount = static_cast<std::uint32_t>(terrain.textureLayers.size());
    _params.seaLevel = terrain.seaLevel;
    UploadMappedBuffer(_paramsBuffer, &_params, sizeof(_params));
    if (_revision.NeedsRebuild(terrain.revision))
    {
        auto bounds = [&](int ox, int oz, int span, float& mn, float& mx)
        {
            for (int z = oz; z <= oz + span; ++z) for (int x = ox; x <= ox + span; ++x)
            {
                const int cx = std::clamp(x, 0, int(terrain.heightWidth) - 1);
                const int cz = std::clamp(z, 0, int(terrain.heightHeight) - 1);
                const float h = terrain.heights[std::size_t(cz) * terrain.heightWidth + cx]; mn = std::min(mn, h); mx = std::max(mx, h);
            }
        };
        const int rootTexels = CdlodRootTexels(int(terrain.heightWidth), int(kGridN));
        BuildCdlodTree(rootTexels, 0, 0, terrain.terrainGrid, kGridN, bounds, _tree, _root, _levels, _leafSize);
        ComputeCdlodRanges(_leafSize * 4.0f, 2.0f, _levels, _ranges);
        _revision.Commit(terrain.revision);
    }
    return true;
}

void TerrainVK::Select(float cameraX, float cameraY, float cameraZ, float x0, float z0, float x1, float z1)
{
    _visible.clear();
    if (_root < 0) return;
    SelectCdlod(_tree, _root, _levels - 1, cameraX, cameraY, cameraZ, _ranges, 0.5f,
                [&](const CdlodNode& n) { return n.originX + n.size > x0 && n.originX < x1 && n.originZ + n.size > z0 && n.originZ < z1; },
                [&](const CdlodSelection& n) { _visible.push_back({n.originX, n.originZ, n.size, std::uint32_t(n.level), n.morphStart, n.morphEnd}); });
    if (_visible.size() <= 8192) UploadMappedBuffer(_instances, _visible.data(), _visible.size() * sizeof(NodeInstance));
}

void TerrainVK::Destroy(VkDevice device)
{
    _ready = false; _visible.clear(); _tree.clear(); _ranges.clear(); _revision.Invalidate();
    DestroyBuffer(device, _gridVertices); DestroyBuffer(device, _gridIndices); DestroyBuffer(device, _instances); DestroyBuffer(device, _paramsBuffer);
    DestroyImage(device, _heightmap); DestroyImage(device, _indexMap); DestroyImage(device, _jitterMap);
}
} // namespace Poseidon::vk
