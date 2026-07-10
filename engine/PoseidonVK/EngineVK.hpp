#pragma once

#include <PoseidonVK/BufferVK.hpp>
#include <PoseidonVK/DrawConstantsVK.hpp>
#include <PoseidonVK/FrameConstantsVK.hpp>
#include <PoseidonVK/MeshRegistryVK.hpp>
#include <PoseidonVK/PipelineCacheVK.hpp>
#include <PoseidonVK/SceneDrawCommandsVK.hpp>
#include <PoseidonVK/ScenePushConstantsVK.hpp>
#include <PoseidonVK/ScreenPushConstantsVK.hpp>
#include <Poseidon/Graphics/Core/TLVertex.hpp>
#include <Poseidon/Graphics/Dummy/EngineDummy.hpp>
#include <Poseidon/Graphics/Shared/WindowMode.hpp>
#include <vulkan/vulkan.h>

#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>

namespace Poseidon { class TextBankVK; class TextureVK; }

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
    AbstractTextBank* TextBank() override;

    // --- 2D / HUD / text screen-space draw path ---
    // These override the EngineDummy no-ops so the menu, HUD, and text render
    // under Vulkan. Each call buffers a TLVertex quad into CPU-side vectors
    // (cleared per frame in InitDraw); the recorded batches are emitted inside
    // RecordBootstrapCommand after the 3D scene block.
    void Draw2D(const Draw2DPars& pars, const Rect2DAbs& rect, const Rect2DAbs& clip) override;
    void DrawPoly(const MipInfo& mip, const Vertex2DAbs* vertices, int nVertices,
                  const Rect2DAbs& clip, int specFlags) override;
    void DrawPoly(const MipInfo& mip, const Vertex2DPixel* vertices, int nVertices,
                  const Rect2DPixel& clip, int specFlags) override;
    void DrawLine(const Line2DAbs& line, PackedColor c0, PackedColor c1, const Rect2DAbs& clip) override;

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

  private:
    bool Initialize(int width, int height, bool windowed, int bitsPerPixel, const std::string& displayMode);
    bool CreateInstance();
    bool CreateDebugMessenger();
    bool CreateSurface();
    bool PickPhysicalDevice();
    bool CreateDevice();
    bool CreateFrameConstantsBuffer();
    bool CreateBootstrapVertexBuffer();
    bool CreateBootstrapIndexBuffer();
    bool CreateSceneVertexBuffer();
    bool CreateSceneIndexBuffer();
    bool EnsureDrawConstantsBufferCapacity(std::size_t drawCount);
    bool CreateFrameDescriptorLayout();
    bool CreateFrameDescriptorSet();
    void UpdateFrameDescriptorSet();
    bool CreatePipelineLayout();
    bool CreateScenePipelineLayout();
    bool CreateSwapchain();
    bool CreateDepthResources();
    VkFormat FindDepthFormat() const;
    bool CreateBootstrapPipeline();
    bool CreateScenePipeline();
    bool CreateScreenPipeline();
    bool CreateScreenPipelineLayout();
    bool CreateScreenDescriptorLayout();
    bool EnsureScreenVertexBufferCapacity(std::size_t vertexCount, std::size_t indexCount);
    void DestroyScreenPipeline();
    void DestroyScreenPipelineLayout();
    void DestroyScreenDescriptorResources();
    void DestroyScreenVertexBuffer();
    void RecordScreenDraws(VkCommandBuffer commandBuffer);
    void PushScreenQuad(const TLVertex* quad, std::uint32_t textureId);
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
    void DestroyDrawConstantsBuffer();
    void DestroyBootstrapVertexBuffer();
    void DestroyBootstrapIndexBuffer();
    void DestroySceneVertexBuffer();
    void DestroySceneIndexBuffer();
    void DestroyScenePipelineLayout();
    void DestroyDepthResources();
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
    vk::BufferVK _drawConstantsBuffer;
    vk::BufferVK _bootstrapVertexBuffer;
    vk::BufferVK _bootstrapIndexBuffer;
    vk::BufferVK _sceneVertexBuffer;
    vk::BufferVK _sceneIndexBuffer;
    VkDescriptorSetLayout _frameDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet _frameDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout _scenePipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout _textureDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool _textureDescriptorPool = VK_NULL_HANDLE;
    VkPipeline _bootstrapPipeline = VK_NULL_HANDLE;
    VkPipeline _scenePipeline = VK_NULL_HANDLE;
    // Scene shader modules kept alive for pipeline cache variant creation.
    VkShaderModule _sceneVertexModule = VK_NULL_HANDLE;
    VkShaderModule _sceneFragmentModule = VK_NULL_HANDLE;
    vk::PipelineCacheVK _scenePipelineCache;
    // 2D / screen-space pipeline resources.
    VkPipeline _screenPipeline = VK_NULL_HANDLE;
    VkPipelineLayout _screenPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout _screenDescriptorSetLayout = VK_NULL_HANDLE;
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
    VkCommandPool _commandPool = VK_NULL_HANDLE;
    std::vector<VkImage> _swapchainImages;
    std::vector<VkImageView> _swapchainImageViews;
    std::vector<VkFramebuffer> _framebuffers;
    std::vector<VkCommandBuffer> _commandBuffers;
    std::vector<VkSemaphore> _renderFinished;
    VkSemaphore _imageAvailable = VK_NULL_HANDLE;
    VkFence _inFlight = VK_NULL_HANDLE;
    uint32_t _graphicsQueueFamily = UINT32_MAX;
    uint32_t _presentQueueFamily = UINT32_MAX;

    bool _initialized = false;
    bool _open = false;
    bool _mouseGrab = true;
    bool _swapchainDirty = false;
    bool _loggedFirstPresent = false;
    bool _hasFrameConstants = false;
    bool _validationEnabled = false;
    bool _debugUtilsEnabled = false;
    vk::FrameConstantsVK _lastFrameConstants = {};
    std::vector<vk::DrawConstantsVK> _lastDrawConstants;
    std::vector<vk::SceneDrawCommandVK> _lastSceneDrawCommands;
    vk::MeshRegistryVK _meshRegistry;
    std::uint32_t _bootstrapMeshId = 0;
    std::size_t _drawConstantsCapacity = 0;
    VkClearColorValue _clearColor{{0.04f, 0.09f, 0.16f, 1.0f}};
    int _width = 1;
    int _height = 1;
    int _bitsPerPixel = 32;
    int _refreshRate = 0;
    WindowMode _windowMode = WindowMode::Windowed;

    std::vector<DrawItem> _drawItems;
    DrawItem _currentDrawItem;
    std::uint32_t _lastTexture1ResourceId = 1;

    // Per-frame 2D draw accumulation. Vertices/indices are built during the
    // game's Draw2D/DrawPoly/DrawLine calls and emitted in RecordScreenDraws.
    // _screenBatches records (textureId, indexCount) run-length boundaries so
    // the emit loop can bind a new descriptor set only when the texture changes.
    std::vector<TLVertex> _screenVertices;
    std::vector<std::uint16_t> _screenIndices;
    struct ScreenBatchVK
    {
        std::uint32_t textureId = 0;
        std::uint32_t indexCount = 0;
    };
    std::vector<ScreenBatchVK> _screenBatches;

    TextBankVK* _textBank = nullptr;
    std::unordered_map<std::uint32_t, TextureVK*> _textureRegistry;
    TextureVK* _fallbackWhiteTexture = nullptr;

    friend class VertexBufferVK;
    friend class TextureVK;
    friend class TextBankVK;
};

} // namespace Poseidon
