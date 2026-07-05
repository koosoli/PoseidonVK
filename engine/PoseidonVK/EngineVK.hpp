#pragma once

#include <Poseidon/Graphics/Dummy/EngineDummy.hpp>
#include <Poseidon/Graphics/Shared/WindowMode.hpp>
#include <vulkan/vulkan.h>

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
    bool CreateSurface();
    bool PickPhysicalDevice();
    bool CreateDevice();
    bool CreateSwapchain();
    bool CreateCommandPool();
    bool CreateSyncObjects();
    bool RecordClearCommand(uint32_t imageIndex);
    void DestroySwapchain();
    bool RecreateSwapchain();
    void PresentClearFrame();
    void Shutdown();
    void OnResized();

    SDL_Window* _window = nullptr;
    VkInstance _instance = VK_NULL_HANDLE;
    VkSurfaceKHR _surface = VK_NULL_HANDLE;
    VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
    VkDevice _device = VK_NULL_HANDLE;
    VkQueue _graphicsQueue = VK_NULL_HANDLE;
    VkQueue _presentQueue = VK_NULL_HANDLE;
    VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
    VkFormat _swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D _swapchainExtent{};
    VkCommandPool _commandPool = VK_NULL_HANDLE;
    std::vector<VkImage> _swapchainImages;
    std::vector<VkImageLayout> _swapchainImageLayouts;
    std::vector<VkCommandBuffer> _commandBuffers;
    VkSemaphore _imageAvailable = VK_NULL_HANDLE;
    VkSemaphore _renderFinished = VK_NULL_HANDLE;
    VkFence _inFlight = VK_NULL_HANDLE;
    uint32_t _graphicsQueueFamily = UINT32_MAX;
    uint32_t _presentQueueFamily = UINT32_MAX;

    bool _initialized = false;
    bool _open = false;
    bool _mouseGrab = true;
    bool _swapchainDirty = false;
    bool _loggedFirstPresent = false;
    VkClearColorValue _clearColor{{0.04f, 0.09f, 0.16f, 1.0f}};
    int _width = 1;
    int _height = 1;
    int _bitsPerPixel = 32;
    int _refreshRate = 0;
    WindowMode _windowMode = WindowMode::Windowed;
};

} // namespace Poseidon
