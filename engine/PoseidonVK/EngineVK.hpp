#pragma once

#include <PoseidonVK/BufferVK.hpp>
#include <PoseidonVK/DrawConstantsVK.hpp>
#include <PoseidonVK/FrameConstantsVK.hpp>
#include <Poseidon/Graphics/Dummy/EngineDummy.hpp>
#include <Poseidon/Graphics/Shared/WindowMode.hpp>
#include <vulkan/vulkan.h>

#include <cstddef>
#include <string>
#include <vector>

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
    bool EnsureDrawConstantsBufferCapacity(std::size_t drawCount);
    bool CreateFrameDescriptorLayout();
    bool CreateFrameDescriptorSet();
    void UpdateFrameDescriptorSet();
    bool CreatePipelineLayout();
    bool CreateSwapchain();
    bool CreateBootstrapPipeline();
    bool CreateCommandPool();
    bool CreateSyncObjects();
    bool RecordBootstrapCommand(uint32_t imageIndex);
    void SetObjectName(VkObjectType objectType, uint64_t objectHandle, const char* name) const;
    void BeginDebugLabel(VkCommandBuffer commandBuffer, const char* name, float r, float g, float b) const;
    void EndDebugLabel(VkCommandBuffer commandBuffer) const;
    void UploadFrameConstants();
    bool UploadDrawConstants();
    void DestroyFrameDescriptorResources();
    void DestroyFrameConstantsBuffer();
    void DestroyDrawConstantsBuffer();
    void DestroyBootstrapVertexBuffer();
    void DestroyBootstrapIndexBuffer();
    void DestroySwapchain();
    bool RecreateSwapchain();
    void PresentBootstrapFrame();
    void Shutdown();
    void OnResized();

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
    VkDescriptorSetLayout _frameDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet _frameDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
    VkPipeline _bootstrapPipeline = VK_NULL_HANDLE;
    VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
    VkFormat _swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D _swapchainExtent{};
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
    std::size_t _drawConstantsCapacity = 0;
    VkClearColorValue _clearColor{{0.04f, 0.09f, 0.16f, 1.0f}};
    int _width = 1;
    int _height = 1;
    int _bitsPerPixel = 32;
    int _refreshRate = 0;
    WindowMode _windowMode = WindowMode::Windowed;
};

} // namespace Poseidon
