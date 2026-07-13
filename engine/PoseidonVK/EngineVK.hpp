#pragma once

#include <PoseidonVK/BufferVK.hpp>
#include <PoseidonVK/CloudConstantsVK.hpp>
#include <PoseidonVK/DrawConstantsVK.hpp>
#include <PoseidonVK/FrameConstantsVK.hpp>
#include <PoseidonVK/GpuSceneVK.hpp>
#include <PoseidonVK/MeshRegistryVK.hpp>
#include <PoseidonVK/PipelineCacheVK.hpp>
#include <PoseidonVK/SceneDrawCommandsVK.hpp>
#include <PoseidonVK/ScenePushConstantsVK.hpp>
#include <PoseidonVK/ScreenDrawRoutingVK.hpp>
#include <PoseidonVK/ScreenPushConstantsVK.hpp>
#include <Poseidon/Graphics/Core/TLVertex.hpp>
#include <Poseidon/Graphics/Dummy/EngineDummy.hpp>
#include <Poseidon/Graphics/Shared/WindowMode.hpp>
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace Poseidon
{
class TextBankVK;
class TextureVK;
} // namespace Poseidon

struct SDL_Window;

namespace Poseidon
{

class EngineVK : public EngineDummy
{
  public:
    EngineVK(int width, int height, bool windowed, int bitsPerPixel, const std::string& displayMode);
    ~EngineVK() override;

    bool IsInitialized() const { return _initialized; }

    RString GetDebugName() const override;
    RString GetRendererName() const override;

    void Clear(bool clearZ, bool clear, PackedColor color) override;
    void NextFrame() override;
    bool ConsumesRenderFramePlan() const override { return true; }
    void SubmitFramePlan(const render::frame::Frame& frame) override;
    void HandleEvents() override;

    void InitDraw(bool clear = false, PackedColor color = PackedColor(0)) override;
    VertexBuffer* CreateVertexBuffer(const Shape& src, VBType type) override;
    void DrawSectionTL(const Shape& sMesh, int beg, int end) override;
    bool GetTL() const override { return true; }
    bool GetTLOnSurface() const override { return true; }
    void BeginMeshTL(const Shape& sMesh, int spec, bool dynamic = false) override;
    void EndMeshTL(const Shape& sMesh) override;
    const std::vector<DrawItem>* GetRecordedDraws() const override { return &_drawItems; }
    void PrepareTriangleTL(const MipInfo& mip, const render::LegacySpec& spec) override;
    void PrepareMeshTL(const LightList& lights, const Matrix4& modelToWorld, const render::LegacySpec& spec) override;
    void SetGrassParams(float a1, float a2, float a3 = 0, float a4 = 0) override;
    void EnableNightEye(float night) override;
    AbstractTextBank* TextBank() override;

    // --- 2D / HUD / text screen-space draw path ---
    // These override the EngineDummy no-ops so the menu, HUD, and text render
    // under Vulkan. Each call buffers a TLVertex quad into CPU-side vectors
    // (cleared per frame in InitDraw); background compatibility batches emit
    // before scene geometry and regular 2D batches emit afterward.
    void Draw2D(const Draw2DPars& pars, const Rect2DAbs& rect, const Rect2DAbs& clip) override;
    void DrawPoly(const MipInfo& mip, const Vertex2DAbs* vertices, int nVertices, const Rect2DAbs& clip,
                  int specFlags) override;
    void DrawPoly(const MipInfo& mip, const Vertex2DPixel* vertices, int nVertices, const Rect2DPixel& clip,
                  int specFlags) override;
    void DrawLine(const Line2DAbs& line, PackedColor c0, PackedColor c1, const Rect2DAbs& clip) override;

    // Legacy software T&L compatibility path. Sky/cloud clipping hints emit
    // behind the scene; other pre-transformed geometry remains an overlay.
    void PrepareTriangle(const MipInfo& mip, int specFlags) override;
    void DrawPolygon(const VertexIndex* indices, int nVertices) override;
    void DrawSection(const FaceArray& faces, Offset begin, Offset end) override;
    void PrepareMesh(const render::LegacySpec& spec, ClipFlags clipFlags) override;
    void BeginMesh(TLVertexTable& mesh, const render::LegacySpec& spec) override;
    void EndMesh(TLVertexTable& mesh) override;

    bool IsOpen() const override { return _open; }
    void SetMouseGrab(bool grab) override;
    bool IsMouseGrabbed() const override { return _mouseGrab; }

    bool SetWindowMode(WindowMode mode) override;
    WindowMode GetCurrentWindowMode() const override { return _windowMode; }

    int Width() const override { return _width; }
    int Height() const override { return _height; }
    int PixelSize() const override { return _bitsPerPixel; }
    int RefreshRate() const override { return _refreshRate; }
    bool IsWindowed() const override { return _windowMode == WindowMode::Windowed; }
    bool IsResizable() const override { return _windowMode == WindowMode::Windowed; }

    // --- Shadow map overrides ---
    void SetShadowMapsEnabled(bool enabled) override;
    bool ShadowMapsEnabled() const override;
    ShadowMapTuning GetShadowMapTuning() const override;
    void SetShadowMapTuning(const ShadowMapTuning& tuning) override;
    void SetShadowMapSunFactor(float f) override;
    void RenderShadowDepthScene(const float* lightVPs, const float* splitViewDist, const float* camFwd3,
                                int numCascades, int omniCount, int res, const ShadowCasterSet& casters) override;
    bool DumpShadowMap(const char* path) override;
    bool ShadowMapCacheSelfTest() override;
    bool ProceduralSkyActive() const override;

  private:
    bool Initialize(int width, int height, bool windowed, int bitsPerPixel, const std::string& displayMode);
    bool CreateInstance();
    bool CreateDebugMessenger();
    bool CreateSurface();
    bool PickPhysicalDevice();
    bool CreateDevice();
    bool CreateFrameConstantsBuffer();
    bool CreateCloudConstantsBuffer();
    bool CreateBootstrapVertexBuffer();
    bool CreateBootstrapIndexBuffer();
    bool CreateSceneVertexBuffer();
    bool CreateSceneIndexBuffer();
    bool EnsureDrawConstantsBufferCapacity(std::size_t drawCount);
    bool CreateGpuSceneResources();
    bool EnsureGpuSceneCapacity(std::size_t instanceCount, std::size_t batchCount);
    void DestroyGpuSceneResources();
    void BuildGpuSceneInputs();
    void RecordGpuSceneCull(VkCommandBuffer commandBuffer);
    bool CreateFrameDescriptorLayout();
    bool CreateFrameDescriptorSet();
    void UpdateFrameDescriptorSet();
    bool CreatePipelineLayout();
    bool CreateScenePipelineLayout();
    bool CreateSwapchain();
    bool CreateDepthResources();
    bool CreateWorldTarget();
    bool CreateWorldCompositeDescriptorLayout();
    bool CreateWorldCompositeDescriptorSet();
    void UpdateWorldCompositeDescriptorSet(std::uint32_t exposureHistoryIndex);
    bool CreateWorldCompositePipelineLayout();
    bool CreateWorldCompositePipeline();
    bool CreateEyeAdaptationResources();
    void DestroyEyeAdaptationResources();
    bool CreateEyeAdaptationDescriptorLayout();
    bool CreateEyeAdaptationDescriptorSet();
    bool CreateEyeAdaptationPipelineLayout();
    bool CreateEyeAdaptationPipeline();
    void DestroyEyeAdaptationDescriptorResources();
    void DestroyEyeAdaptationPipelineLayout();
    bool CreateGpuTimingResources();
    void DestroyGpuTimingResources();
    void LogGpuTimings();
    bool WorldCompositionActive() const;
    VkFormat FindDepthFormat() const;
    bool CreateBootstrapPipeline();
    bool CreateScenePipeline();
    bool CreateProceduralSkyPipeline();
    bool CreateSkyMapDescriptorLayout();
    bool CreateSkyMapDescriptorSet();
    bool CreateSkyMapPipelineLayout();
    bool CreateSkyMapResources();
    void DestroySkyMapResources();
    void DestroySkyMapDescriptorResources();
    void DestroySkyMapPipelineLayout();
    void UpdateSkyMapInvalidation();
    bool RecordSkyMapBake(VkCommandBuffer commandBuffer);
    bool CreateVolumetricCloudDescriptorLayout();
    bool CreateVolumetricCloudDescriptorSet();
    bool CreateVolumetricCloudPipelineLayout();
    bool CreateVolumetricCloudPipeline();
    bool CreateCloudComputeDescriptorLayouts();
    bool CreateCloudComputeDescriptorSets();
    bool CreateCloudComputePipelineLayouts();
    bool CreateCloudComputePipelines();
    bool CreateCloudResources();
    void DestroyCloudResources();
    void UpdateCloudDescriptorSets(std::uint32_t historyReadIndex);
    void UpdateCloudConstants();
    void RecordCloudVolumeCompute(VkCommandBuffer commandBuffer);
    void DestroyCloudComputeDescriptorResources();
    void DestroyCloudComputePipelineResources();
    void DestroyVolumetricCloudDescriptorResources();
    void DestroyVolumetricCloudPipelineLayout();
    bool CreateScreenPipeline();
    bool CreateScreenPipelineLayout();
    bool CreateScreenDescriptorLayout();
    bool EnsureScreenVertexBufferCapacity(std::size_t vertexCount, std::size_t indexCount);
    void DestroyScreenPipeline();
    void DestroyScreenPipelineLayout();
    void DestroyScreenDescriptorResources();
    void DestroyScreenVertexBuffer();
    void RecordScreenDraws(VkCommandBuffer commandBuffer, vk::ScreenDrawPhaseVK phase);
    void AppendScreenBatch(std::uint32_t textureId, std::uint32_t indexCount, vk::ScreenDrawPhaseVK phase,
                           const render::RenderPassDescriptor& descriptor);
    void PushScreenQuad(const TLVertex* quad, std::uint32_t textureId,
                        const render::RenderPassDescriptor& descriptor);
    bool CreateCommandPool();
    bool CreateSyncObjects();
    bool RecordBootstrapCommand(uint32_t imageIndex);
    void SetObjectName(VkObjectType objectType, uint64_t objectHandle, const char* name) const;
    void BeginDebugLabel(VkCommandBuffer commandBuffer, const char* name, float r, float g, float b) const;
    void EndDebugLabel(VkCommandBuffer commandBuffer) const;
    void UploadFrameConstants();
    bool UploadDrawConstants();
    // Returns the scene VkPipeline for the given render-pass descriptor, creating
    // it on first use. The cache is keyed by (cull, frontFace, depth, blend).
    VkPipeline GetOrCreateScenePipeline(const render::RenderPassDescriptor& desc);
    void DestroyFrameDescriptorResources();
    void DestroyFrameConstantsBuffer();
    void DestroyCloudConstantsBuffer();
    void DestroyDrawConstantsBuffer();
    void DestroyBootstrapVertexBuffer();
    void DestroyBootstrapIndexBuffer();
    void DestroySceneVertexBuffer();
    void DestroySceneIndexBuffer();
    void DestroyScenePipelineLayout();
    void DestroyDepthResources();
    void DestroyWorldTarget();
    void DestroyWorldCompositeDescriptorResources();
    void DestroyWorldCompositePipelineLayout();
    void DestroySwapchain();
    bool RecreateSwapchain();
    void PresentBootstrapFrame();
    void Shutdown();
    void OnResized();

    void RegisterTexture(TextureVK* tex);
    void UnregisterTexture(TextureVK* tex);
    TextureVK* ResolveTexture(std::uint32_t id) const;

    bool CreateTextureDescriptorLayout();
    bool CreateTextureDescriptorPool();
    void DestroyTextureDescriptorResources();

    SDL_Window* _window = nullptr;
    VkInstance _instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT _debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR _surface = VK_NULL_HANDLE;
    VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
    VkDevice _device = VK_NULL_HANDLE;
    VkQueue _graphicsQueue = VK_NULL_HANDLE;
    VkQueue _presentQueue = VK_NULL_HANDLE;
    vk::BufferVK _frameConstantsBuffer;
    vk::BufferVK _cloudConstantsBuffer;
    vk::BufferVK _drawConstantsBuffer;
    vk::BufferVK _gpuSceneInstancesBuffer;
    vk::BufferVK _gpuSceneIndirectBuffer;
    vk::BufferVK _gpuSceneCountBuffer;
    vk::BufferVK _bootstrapVertexBuffer;
    vk::BufferVK _bootstrapIndexBuffer;
    vk::BufferVK _sceneVertexBuffer;
    vk::BufferVK _sceneIndexBuffer;
    VkDescriptorSetLayout _frameDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet _frameDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSetLayout _gpuSceneDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool _gpuSceneDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet _gpuSceneDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout _gpuScenePipelineLayout = VK_NULL_HANDLE;
    VkPipeline _gpuSceneCullPipeline = VK_NULL_HANDLE;
    VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout _scenePipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout _textureDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool _textureDescriptorPool = VK_NULL_HANDLE;
    VkPipeline _bootstrapPipeline = VK_NULL_HANDLE;
    VkPipeline _scenePipeline = VK_NULL_HANDLE;
    VkPipeline _proceduralSkyPipeline = VK_NULL_HANDLE;
    VkPipeline _skyMapBakePipeline = VK_NULL_HANDLE;
    VkPipeline _volumetricCloudPipeline = VK_NULL_HANDLE;
    VkPipeline _cloudTemporalPipeline = VK_NULL_HANDLE;
    VkPipeline _cloudCompositePipeline = VK_NULL_HANDLE;
    VkPipeline _cloudDensityErosionPipeline = VK_NULL_HANDLE;
    VkPipeline _cloudDistanceFieldPipeline = VK_NULL_HANDLE;
    VkPipeline _cloudLightMapPipeline = VK_NULL_HANDLE;
    VkPipeline _worldCompositePipeline = VK_NULL_HANDLE;
    VkPipeline _eyeAdaptationPipeline = VK_NULL_HANDLE;
    // Scene shader modules kept alive for pipeline cache variant creation.
    VkShaderModule _sceneVertexModule = VK_NULL_HANDLE;
    VkShaderModule _sceneFragmentModule = VK_NULL_HANDLE;
    vk::PipelineCacheVK _scenePipelineCache;
    VkPipeline _cockpitScenePipeline = VK_NULL_HANDLE;
    vk::PipelineCacheVK _cockpitScenePipelineCache;
    VkPipeline _worldLateScenePipeline = VK_NULL_HANDLE;
    vk::PipelineCacheVK _worldLateScenePipelineCache;
    // 2D / screen-space pipeline resources.
    VkPipeline _screenPipeline = VK_NULL_HANDLE;
    VkPipeline _screenOverlayPipeline = VK_NULL_HANDLE;
    vk::PipelineCacheVK _screenPipelineCache;
    vk::PipelineCacheVK _screenOverlayPipelineCache;
    VkPipelineLayout _screenPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout _screenDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout _skyMapDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool _skyMapDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet _skyMapDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout _skyMapPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout _skyMapBakePipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout _volumetricCloudDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool _volumetricCloudDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet _volumetricCloudDescriptorSet = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> _volumetricCloudDescriptorSets = {};
    VkPipelineLayout _volumetricCloudPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout _cloudGenerationDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout _cloudLightingDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool _cloudComputeDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet _cloudGenerationDescriptorSet = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> _cloudLightingDescriptorSets = {};
    VkPipelineLayout _cloudGenerationPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout _cloudLightingPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout _worldCompositeDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool _worldCompositeDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet _worldCompositeDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout _worldCompositePipelineLayout = VK_NULL_HANDLE;
    VkSampler _worldCompositeSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout _eyeAdaptationDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool _eyeAdaptationDescriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> _eyeAdaptationDescriptorSets = {};
    VkPipelineLayout _eyeAdaptationPipelineLayout = VK_NULL_HANDLE;
    VkSampler _eyeAdaptationSampler = VK_NULL_HANDLE;
    VkShaderModule _screenVertexModule = VK_NULL_HANDLE;
    VkShaderModule _screenFragmentModule = VK_NULL_HANDLE;
    vk::BufferVK _screenVertexBuffer;
    vk::BufferVK _screenIndexBuffer;
    std::size_t _screenVertexCapacity = 0;
    std::size_t _screenIndexCapacity = 0;
    VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
    VkFormat _swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D _swapchainExtent{};
    VkFormat _depthFormat = VK_FORMAT_UNDEFINED;
    VkImage _depthImage = VK_NULL_HANDLE;
    VkDeviceMemory _depthImageMemory = VK_NULL_HANDLE;
    VkImageView _depthImageView = VK_NULL_HANDLE;
    VkRenderPass _renderPass = VK_NULL_HANDLE;
    VkRenderPass _worldLateRenderPass = VK_NULL_HANDLE;
    VkRenderPass _cloudRaymarchRenderPass = VK_NULL_HANDLE;
    VkRenderPass _cloudTemporalRenderPass = VK_NULL_HANDLE;
    VkRenderPass _cloudCompositeRenderPass = VK_NULL_HANDLE;
    VkRenderPass _presentRenderPass = VK_NULL_HANDLE;
    VkRenderPass _skyMapRenderPass = VK_NULL_HANDLE;
    VkImage _worldColorImage = VK_NULL_HANDLE;
    VkDeviceMemory _worldColorImageMemory = VK_NULL_HANDLE;
    VkImageView _worldColorImageView = VK_NULL_HANDLE;
    VkImage _worldDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory _worldDepthImageMemory = VK_NULL_HANDLE;
    VkImageView _worldDepthImageView = VK_NULL_HANDLE;
    VkFramebuffer _worldFramebuffer = VK_NULL_HANDLE;
    VkFramebuffer _worldLateFramebuffer = VK_NULL_HANDLE;
    VkFramebuffer _cloudRaymarchFramebuffer = VK_NULL_HANDLE;
    std::array<VkFramebuffer, 2> _cloudTemporalFramebuffers = {};
    VkFramebuffer _cloudCompositeFramebuffer = VK_NULL_HANDLE;
    VkFramebuffer _skyMapFramebuffer = VK_NULL_HANDLE;
    vk::ImageVK _skyMap;
    VkSampler _skyMapSampler = VK_NULL_HANDLE;
    VkImageLayout _skyMapLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk::ImageVK _cloudCurrent;
    std::array<vk::ImageVK, 2> _cloudHistory = {};
    // Persistent, absolute-world cloud fields.  Density and distance are
    // rebuilt when the simulation moves their clipped volume; illumination is
    // ping-ponged each submitted simulation frame.
    vk::ImageVK _cloudDensityVolume;
    vk::ImageVK _cloudDistanceVolume;
    std::array<vk::ImageVK, 2> _cloudLightVolumes = {};
    VkSampler _cloudSampler = VK_NULL_HANDLE;
    VkRenderPass _eyeAdaptationRenderPass = VK_NULL_HANDLE;
    std::array<vk::ImageVK, 2> _eyeAdaptationHistory = {};
    std::array<VkFramebuffer, 2> _eyeAdaptationFramebuffers = {};
    VkCommandPool _commandPool = VK_NULL_HANDLE;
    std::vector<VkImage> _swapchainImages;
    std::vector<VkImageView> _swapchainImageViews;
    std::vector<VkFramebuffer> _framebuffers;
    std::vector<VkCommandBuffer> _commandBuffers;
    std::vector<VkSemaphore> _renderFinished;
    VkSemaphore _imageAvailable = VK_NULL_HANDLE;
    VkFence _inFlight = VK_NULL_HANDLE;
    VkQueryPool _gpuTimingQueryPool = VK_NULL_HANDLE;
    uint32_t _graphicsQueueFamily = UINT32_MAX;
    uint32_t _presentQueueFamily = UINT32_MAX;

    bool _initialized = false;
    bool _open = false;
    bool _mouseGrab = true;
    bool _swapchainDirty = false;
    bool _loggedFirstPresent = false;
    bool _hasFrameConstants = false;
    bool _validationEnabled = false;
    vk::GpuSceneCapabilitiesVK _gpuSceneCapabilities;
    bool _gpuSceneEnabled = false;
    bool _debugUtilsEnabled = false;
    bool _proceduralSkyEnabled = false;
    bool _skyMapDirty = true;
    bool _skyMapValid = false;
    bool _volumetricCloudsEnabled = false;
    bool _cloudHistoryValid = false;
    bool _hdrEnabled = false;
    bool _temporalExposureEnabled = false;
    float _hdrExposure = 1.0f;
    bool _eyeAdaptationHistoryValid = false;
    bool _eyeAdaptationPendingWrite = false;
    std::uint32_t _eyeAdaptationCurrentIndex = 0;
    std::uint32_t _cloudHistoryCurrentIndex = 0;
    std::uint32_t _cloudFrameIndex = 0;
    std::uint32_t _cloudLightVolumeReadIndex = 0;
    float _cloudLastUpdateSeconds = -1.0f;
    bool _cloudVolumeBuilt = false;
    bool _cloudVolumeRebuildPending = true;
    std::array<float, 11> _cloudVolumeBuildParameters = {};
    vk::CloudConstantsVK _cloudConstants = {};
    vk::FrameConstantsVK _previousCloudFrameConstants = {};
    std::uint32_t _eyeAdaptationPendingIndex = 0;
    float _timestampPeriodNs = 0.0f;
    bool _gpuTimingPending = false;
    std::uint32_t _gpuTimingFrameCount = 0;
    vk::FrameConstantsVK _lastFrameConstants = {};
    std::array<float, 40> _skyMapCachedInputs = {};
    std::array<float, 40> _skyMapRequestedInputs = {};
    std::vector<vk::DrawConstantsVK> _lastDrawConstants;
    std::vector<vk::SceneDrawCommandVK> _lastSceneDrawCommands;
    std::array<std::vector<std::uint32_t>, 6> _sceneCommandGroups;
    std::vector<vk::GpuSceneInstanceVK> _gpuSceneInstances;
    std::vector<vk::GpuSceneBatchVK> _gpuSceneBatches;
    vk::MeshRegistryVK _meshRegistry;
    std::uint32_t _bootstrapMeshId = 0;
    std::size_t _drawConstantsCapacity = 0;
    std::size_t _gpuSceneInstanceCapacity = 0;
    std::size_t _gpuSceneBatchCapacity = 0;
    VkClearColorValue _clearColor{{0.0f, 0.0f, 0.0f, 1.0f}};
    int _width = 1;
    int _height = 1;
    int _bitsPerPixel = 32;
    int _refreshRate = 0;
    WindowMode _windowMode = WindowMode::Windowed;

    std::vector<DrawItem> _drawItems;
    DrawItem _currentDrawItem;
    std::uint32_t _lastTexture1ResourceId = 1;
    float _grassParam[4] = {};
    float _nightEye = 0.0f;

    // Per-frame 2D draw accumulation. Vertices/indices are built during the
    // game's Draw2D/DrawPoly/DrawLine calls and emitted in RecordScreenDraws.
    // _screenBatches records phase-aware index ranges so background software-T&L
    // geometry can render before the world while UI remains an overlay.
    std::vector<TLVertex> _screenVertices;
    std::vector<std::uint16_t> _screenIndices;
    struct ScreenBatchVK
    {
        std::uint32_t textureId = 0;
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
        vk::ScreenDrawPhaseVK phase = vk::ScreenDrawPhaseVK::Overlay;
        render::RenderPassDescriptor descriptor = {};
    };
    std::vector<ScreenBatchVK> _screenBatches;
    const TLVertexTable* _screenMesh = nullptr;
    std::size_t _screenMeshBase = 0;
    std::uint32_t _screenTextureId = 0;
    render::RenderPassDescriptor _screenDescriptor = {};
    vk::ScreenDrawPhaseVK _screenMeshPhase = vk::ScreenDrawPhaseVK::Overlay;
    bool _screenBuffersUploaded = false;

    TextBankVK* _textBank = nullptr;
    std::unordered_map<std::uint32_t, TextureVK*> _textureRegistry;
    Ref<TextureVK> _fallbackWhiteTexture; // 1x1 neutral-grey fallback for missing textures

    // Shadow resources
    static constexpr int kShadowCascades = 4;
    vk::ImageVK _shadowDepthImage;
    VkSampler _shadowSampler = VK_NULL_HANDLE;
    VkImageView _shadowCascadeViews[kShadowCascades] = {};
    VkRenderPass _shadowRenderPass = VK_NULL_HANDLE;
    VkFramebuffer _shadowFramebuffers[kShadowCascades] = {};
    VkPipeline _shadowDepthPipeline = VK_NULL_HANDLE;
    VkPipelineLayout _shadowDepthPipelineLayout = VK_NULL_HANDLE;
    VkShaderModule _shadowDepthVertexModule = VK_NULL_HANDLE;
    VkPipeline _shadowAlphaPipeline = VK_NULL_HANDLE;
    VkPipelineLayout _shadowAlphaPipelineLayout = VK_NULL_HANDLE;
    VkShaderModule _shadowAlphaVertexModule = VK_NULL_HANDLE;
    VkShaderModule _shadowAlphaFragmentModule = VK_NULL_HANDLE;
    VkCommandBuffer _shadowCommandBuffer = VK_NULL_HANDLE;
    VkFence _shadowInFlight = VK_NULL_HANDLE;
    int _shadowMapRes = 0;
    int _shadowCascades = 0;
    bool _shadowMapActive = false;
    float _shadowMapVP[kShadowCascades * 16] = {};
    float _shadowSplits[kShadowCascades] = {};
    float _shadowCamFwd[3] = {};
    int _shadowOmniCount = 0;
    float _shadowSunFactor = 1.0f;
    ShadowMapTuning _shadowTuning;
    float _maxSamplerAnisotropy = 1.0f;

    bool EnsureShadowResources(int res, int layers);
    void DestroyShadowResources();
    void UpdateShadowFrameConstants();
    bool CreateShadowDepthPipeline();
    void RenderShadowDepthFramePlan(const render::frame::Frame& frame);
    bool CompileShader(const char* source, int stage, std::vector<uint32_t>& spirv, std::string& error);

    friend class VertexBufferVK;
    friend class TextureVK;
    friend class TextBankVK;
};

} // namespace Poseidon
