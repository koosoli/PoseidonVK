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
    // This is a policy ceiling, not a shader limit.  Initialize clamps it to
    // the selected device's ordinary sampled-image descriptor limits.
    static constexpr std::uint32_t kRequestedLayerCapacity = 256;
    static constexpr std::uint32_t kShadowMaskScale = 2;
    static constexpr std::uint32_t kShadowMaskDimensionCap = 4096;
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
    static_assert(sizeof(GridVertex) == 12);
    static_assert(sizeof(NodeInstance) == 24);
    // std140 layout consumed by terrain.vert/frag: vec2 + 2 floats, uvec4,
    // vec4 water, vec4 wetness.
    static_assert(sizeof(Params) == 64);
    enum DescriptorBinding : std::uint32_t
    {
        kHeightmapBinding = 0,
        kIndexMapBinding = 1,
        kJitterMapBinding = 2,
        kParamsBinding = 3,
        kRepeatSamplerBinding = 4,
        kClampSamplerBinding = 5,
        kTextureLayersBinding = 6,
    };
    struct LayerBinding
    {
        VkDescriptorImageInfo image = {};
    };
    struct DescriptorTelemetry
    {
        std::uint32_t capacity = 0;
        std::uint32_t requestedLayers = 0;
        std::uint32_t boundLayers = 0;
        std::uint32_t fallbackLayers = 0;
        std::uint32_t invalidLayers = 0;
        std::uint32_t invalidLayerIndices = 0;
    };
    // Matches the WGPU terrain shadow sweep UBO exactly.  The shadow mask stores
    // a world-height ceiling, penumbra half-width and strength in RGBA16F.
    struct ShadowSweep
    {
        float worldOrigin[2] = {};
        float terrainGrid = 1.0f;
        float penumbra = 0.0174532925f;
        float invScale[2] = {1.0f, 1.0f};
        std::uint32_t heightWidth = 1, heightHeight = 1;
        std::uint32_t maskWidth = 1, maskHeight = 1, maxSteps = 512;
        float strength = 1.0f;
        float sunDirection[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    };
    static_assert(sizeof(ShadowSweep) == 64);
    struct SkyVisibilityOptions
    {
        std::uint32_t downsample = 2;
        std::uint32_t azimuths = 12;
        float radiusMeters = 600.0f;
        std::uint32_t blurRadius = 1;
    };
    // Set 2 is terrain-owned. Bindings 0/2 are derived maps; binding 1 has no
    // fallback and must be supplied by the real detail producer.
    struct RasterInputs
    {
        VkDescriptorSetLayout frameDescriptorSetLayout = VK_NULL_HANDLE; // set 0: FrameConstants + CSM
        VkDescriptorSetLayout visualDescriptorSetLayout = VK_NULL_HANDLE; // set 2: terrain visual inputs
        VkDescriptorSet frameDescriptorSet = VK_NULL_HANDLE;
        VkDescriptorSet visualDescriptorSet = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkExtent2D extent = {};
        bool csmBound = false;
        bool selfShadowBound = false;
        bool detailBound = false;
        bool skyVisibilityBound = false;

        bool Complete() const noexcept
        {
            return frameDescriptorSetLayout != VK_NULL_HANDLE && visualDescriptorSetLayout != VK_NULL_HANDLE &&
                   frameDescriptorSet != VK_NULL_HANDLE && visualDescriptorSet != VK_NULL_HANDLE &&
                   renderPass != VK_NULL_HANDLE && extent.width != 0 && extent.height != 0 && csmBound &&
                    selfShadowBound && detailBound && skyVisibilityBound;
        }
    };
    // Required bindings in the terrain-owned set 2. Keep these synchronized
    // with terrain.frag.glsl and its source-resource producers.
    enum VisualInputBinding : std::uint32_t
    {
        kSelfShadowBinding = 0,
        kDetailBinding = 1,
        kSkyVisibilityBinding = 2,
    };

    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device, VkCommandPool commandPool, VkQueue queue);
    void Destroy(VkDevice device);
    bool Upload(const render::frame::TerrainOpaque& terrain);
    // Called by EngineVK after TextureHandle IDs have been resolved to native
    // images. No UPDATE_AFTER_BIND flag is needed: dedicated terrain
    // rasterization updates this set only after its prior use has completed.
    bool UpdateLayerDescriptors(const std::vector<LayerBinding>& layers);
    // Detail is a source asset, not a generated stand-in. Its descriptor must
    // name a completed, shader-readable image. The self-shadow and sky
    // visibility bindings are supplied by this object.
    bool UpdateVisualDescriptors(const VkDescriptorImageInfo& detail);
    // Records the amortized heightfield sweep before a render pass samples the
    // mask.  It dispatches only after a map revision, shadow setting change, or
    // a sun movement greater than ~0.25 degrees.
    bool RecordSelfShadowPass(VkCommandBuffer commandBuffer, float sunToLightX, float sunToLightY,
                              float sunToLightZ);
    bool SetSkyVisibilityOptions(const SkyVisibilityOptions& options);
    // Compiles the CMake-embedded GLSL and creates a pipeline compatible with
    // set 0 (frame/CSM), this object's set 1, and the future visual-input set 2.
    // Returns false until all non-optional visual receiver resources exist.
    // On failure, error receives a diagnostic string suitable for logging.
    bool CreateRasterPipeline(const RasterInputs& inputs, std::string& error);
    void DestroyRasterPipeline(VkDevice device);
    // Records only geometry bindings and one indexed instanced draw.  It does
    // not bind the pipeline or descriptors, so calling it cannot activate the
    // dedicated terrain path by accident.
    bool RecordInstancedGridDraw(VkCommandBuffer commandBuffer) const;
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
    const ImageVK& SelfShadowMask() const noexcept { return _selfShadowMask; }
    const ImageVK& SkyVisibilityMask() const noexcept { return _skyVisibilityMask; }
    // Kept separate from Parameters(): descriptor setup must bind the actual
    // GPU allocation, not a transient CPU Params snapshot.
    const BufferVK& ParamsBuffer() const noexcept { return _paramsBuffer; }
    const Params& Parameters() const noexcept { return _params; }
    VkDescriptorSetLayout DescriptorSetLayout() const noexcept { return _descriptorSetLayout; }
    VkDescriptorSet DescriptorSet() const noexcept { return _descriptorSet; }
    VkDescriptorSetLayout VisualDescriptorSetLayout() const noexcept { return _visualDescriptorSetLayout; }
    VkDescriptorSet VisualDescriptorSet() const noexcept { return _visualDescriptorSet; }
    bool VisualInputsReady() const noexcept { return _visualDescriptorsReady; }
    VkPipelineLayout RasterPipelineLayout() const noexcept { return _rasterPipelineLayout; }
    VkPipeline RasterPipeline() const noexcept { return _rasterPipeline; }
    bool RasterPipelineReady() const noexcept { return _rasterPipeline != VK_NULL_HANDLE; }
    std::uint32_t LayerCapacity() const noexcept { return _layerCapacity; }
    const DescriptorTelemetry& Telemetry() const noexcept { return _telemetry; }

  private:
    bool CreateGrid();
    bool RecreateMapImages(const render::frame::TerrainOpaque& terrain);
    bool CreateDescriptorResources();
    bool CreateVisualDescriptorResources();
    bool CreateShadowComputeResources();
    bool CreateMapSamplers();
    bool UpdateStaticDescriptors();
    bool UpdateVisualDescriptors();
    bool UpdateShadowComputeDescriptors();
    bool RebuildSkyVisibility();
    bool ValidateLayerIndices(const render::frame::TerrainOpaque& terrain);
    void DestroyDescriptorResources(VkDevice device);
    void DestroyVisualDescriptorResources(VkDevice device);
    void DestroyShadowComputeResources(VkDevice device);
    VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
    VkDevice _device = VK_NULL_HANDLE;
    VkCommandPool _commandPool = VK_NULL_HANDLE;
    VkQueue _queue = VK_NULL_HANDLE;
    BufferVK _gridVertices, _gridIndices, _instances, _paramsBuffer;
    ImageVK _heightmap, _indexMap, _jitterMap, _selfShadowMask, _skyVisibilityMask;
    VkDescriptorSetLayout _descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet _descriptorSet = VK_NULL_HANDLE;
    VkSampler _heightSampler = VK_NULL_HANDLE;
    VkSampler _indexSampler = VK_NULL_HANDLE;
    VkSampler _jitterSampler = VK_NULL_HANDLE;
    VkSampler _maskSampler = VK_NULL_HANDLE;
    VkSampler _layerRepeatSampler = VK_NULL_HANDLE;
    VkSampler _layerClampSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout _visualDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool _visualDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet _visualDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorImageInfo _detailDescriptor = {};
    VkDescriptorSetLayout _shadowComputeDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool _shadowComputeDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet _shadowComputeDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout _shadowComputePipelineLayout = VK_NULL_HANDLE;
    VkPipeline _shadowComputePipeline = VK_NULL_HANDLE;
    BufferVK _shadowSweepBuffer;
    VkPipelineLayout _rasterPipelineLayout = VK_NULL_HANDLE;
    VkPipeline _rasterPipeline = VK_NULL_HANDLE;
    std::uint32_t _gridIndexCount = 0;
    std::uint32_t _layerCapacity = 0;
    DescriptorTelemetry _telemetry;
    CdlodRevisionCache _revision;
    std::vector<CdlodNode> _tree;
    std::vector<float> _ranges;
    std::vector<NodeInstance> _visible;
    int _root = -1, _levels = 0;
    float _leafSize = 0.0f;
    Params _params;
    ShadowSweep _shadowSweep;
    SkyVisibilityOptions _skyVisibilityOptions;
    std::vector<float> _skyVisibilitySource;
    std::uint32_t _skyVisibilitySourceWidth = 0;
    std::uint32_t _skyVisibilitySourceHeight = 0;
    float _skyVisibilitySourceGrid = 1.0f;
    std::uint32_t _shadowMaskMaxDimension = 1;
    VkImageLayout _selfShadowLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    float _lastSunToLight[3] = {};
    bool _shadowDirty = true;
    bool _detailDescriptorsReady = false;
    bool _selfShadowPopulated = false;
    bool _visualDescriptorsReady = false;
    bool _ready = false;
};
} // namespace Poseidon::vk
