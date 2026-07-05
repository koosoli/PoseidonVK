#include <PoseidonVK/EngineVK.hpp>

#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Graphics/Shared/WindowPlacement.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <vector>

extern void SDLInput_BufferKeyEvent(SDL_Scancode sc, bool down, DWORD timestamp);
extern void SDLInput_BufferMouseButton(int btn, bool down);
extern void SDLInput_BufferMouseMotion(float dx, float dy);
extern void SDLInput_BufferMouseWheel(float dy);
extern void SDLInput_BufferUIKeyEvent(SDL_Keycode key, bool down);
extern void SDLInput_BufferUICharEvent(const char* text);

namespace Poseidon
{

namespace
{
bool HasDeviceExtension(VkPhysicalDevice device, const char* requiredExtension)
{
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS)
        return false;

    std::vector<VkExtensionProperties> extensions(count);
    if (count > 0 &&
        vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()) != VK_SUCCESS)
        return false;

    return std::any_of(extensions.begin(), extensions.end(),
                       [requiredExtension](const VkExtensionProperties& extension)
                       { return std::strcmp(extension.extensionName, requiredExtension) == 0; });
}

const char* VkResultName(VkResult result)
{
    switch (result)
    {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_SUBOPTIMAL_KHR:
            return "VK_SUBOPTIMAL_KHR";
        default:
            return "VkResult";
    }
}

VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const VkSurfaceFormatKHR& format : formats)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return format;
    }
    return formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                           : formats.front();
}

VkExtent2D ClampExtent(int width, int height, const VkSurfaceCapabilitiesKHR& capabilities)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return capabilities.currentExtent;

    VkExtent2D extent{
        static_cast<uint32_t>(std::max(width, 1)),
        static_cast<uint32_t>(std::max(height, 1)),
    };
    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return extent;
}
} // namespace

EngineVK::EngineVK(int width, int height, bool windowed, int bitsPerPixel, const std::string& displayMode)
{
    _initialized = Initialize(width, height, windowed, bitsPerPixel, displayMode);
}

EngineVK::~EngineVK()
{
    Shutdown();
}

RString EngineVK::GetDebugName() const
{
    return "VK";
}

RString EngineVK::GetRendererName() const
{
    return "Vulkan Bootstrap";
}

bool EngineVK::Initialize(int width, int height, bool windowed, int bitsPerPixel, const std::string& displayMode)
{
    _bitsPerPixel = bitsPerPixel > 0 ? bitsPerPixel : 32;

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        LOG_ERROR(Graphics, "Vulkan: SDL_Init failed: {}", SDL_GetError());
        return false;
    }

    int desktopW = width > 0 ? width : 800;
    int desktopH = height > 0 ? height : 600;
    int desktopRefresh = 0;
    if (const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay()))
    {
        desktopW = dm->w;
        desktopH = dm->h;
        desktopRefresh = static_cast<int>(dm->refresh_rate + 0.5f);
    }

    DisplayPlacementInput displayCfg;
    displayCfg.width = width;
    displayCfg.height = height;
    displayCfg.displayMode = windowed ? "windowed" : displayMode;

    const WindowPlacement placement = ResolveWindowPlacement(displayCfg, desktopW, desktopH, desktopRefresh);
    _windowMode = placement.mode;
    _width = placement.width;
    _height = placement.height;
    _refreshRate = placement.refreshHz;

    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    switch (_windowMode)
    {
        case WindowMode::Fullscreen:
        case WindowMode::Borderless:
            flags |= SDL_WINDOW_BORDERLESS;
            break;
        case WindowMode::Windowed:
            flags |= SDL_WINDOW_RESIZABLE;
            break;
    }

    _window = SDL_CreateWindow("Poseidon [Vulkan]", _width, _height, flags);
    if (!_window)
    {
        LOG_ERROR(Graphics, "Vulkan: SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }

    if (_windowMode == WindowMode::Borderless)
    {
#ifndef _WIN32
        SDL_SetWindowFullscreenMode(_window, nullptr);
        if (!SDL_SetWindowFullscreen(_window, true))
            LOG_WARN(Graphics, "Vulkan: SDL_SetWindowFullscreen(true) failed for borderless startup: {}",
                     SDL_GetError());
#else
        if (placement.posX != WindowPlacement::kCentered)
            SDL_SetWindowPosition(_window, placement.posX, placement.posY);
#endif
    }
    else if (placement.posX != WindowPlacement::kCentered)
    {
        SDL_SetWindowPosition(_window, placement.posX, placement.posY);
    }

    if (!CreateInstance() || !CreateSurface() || !PickPhysicalDevice() || !CreateDevice() || !CreateCommandPool() ||
        !CreateSwapchain() || !CreateSyncObjects())
    {
        Shutdown();
        return false;
    }

    SDL_GetWindowSizeInPixels(_window, &_width, &_height);
    SDL_StartTextInput(_window);
    SetMouseGrab(true);

    _open = true;
    if (GApp)
        GApp->m_appActive = true;

    LOG_INFO(Graphics, "Vulkan: bootstrap initialized {}x{} mode={} graphics_queue={} present_queue={}", _width,
             _height, static_cast<int>(_windowMode), _graphicsQueueFamily, _presentQueueFamily);
    return true;
}

void EngineVK::Clear(bool, bool clear, PackedColor color)
{
    if (!clear)
        return;

    _clearColor = VkClearColorValue{{
        color.R8() * (1.0f / 255.0f),
        color.G8() * (1.0f / 255.0f),
        color.B8() * (1.0f / 255.0f),
        color.A8() * (1.0f / 255.0f),
    }};
}

void EngineVK::NextFrame()
{
    Engine::NextFrame();
    PresentClearFrame();
}

bool EngineVK::CreateInstance()
{
    Uint32 extensionCount = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    if (!extensions || extensionCount == 0)
    {
        LOG_ERROR(Graphics, "Vulkan: SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError());
        return false;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Poseidon";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "PoseidonVK";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = extensions;

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &_instance);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkCreateInstance failed: {}", VkResultName(result));
        return false;
    }
    return true;
}

bool EngineVK::CreateSurface()
{
    if (!SDL_Vulkan_CreateSurface(_window, _instance, nullptr, &_surface))
    {
        LOG_ERROR(Graphics, "Vulkan: SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
        return false;
    }
    return true;
}

bool EngineVK::PickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    VkResult result = vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr);
    if (result != VK_SUCCESS || deviceCount == 0)
    {
        LOG_ERROR(Graphics, "Vulkan: no physical devices available");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    result = vkEnumeratePhysicalDevices(_instance, &deviceCount, devices.data());
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkEnumeratePhysicalDevices failed: {}", VkResultName(result));
        return false;
    }

    for (VkPhysicalDevice device : devices)
    {
        if (!HasDeviceExtension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            continue;

        uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, queueFamilies.data());

        uint32_t graphicsFamily = UINT32_MAX;
        uint32_t presentFamily = UINT32_MAX;
        for (uint32_t i = 0; i < queueCount; ++i)
        {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                graphicsFamily = i;

            VkBool32 presentSupported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, _surface, &presentSupported);
            if (presentSupported)
                presentFamily = i;

            if (graphicsFamily != UINT32_MAX && presentFamily != UINT32_MAX)
                break;
        }

        if (graphicsFamily == UINT32_MAX || presentFamily == UINT32_MAX)
            continue;

        _physicalDevice = device;
        _graphicsQueueFamily = graphicsFamily;
        _presentQueueFamily = presentFamily;

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);
        LOG_INFO(Graphics, "Vulkan: selected device '{}'", props.deviceName);
        return true;
    }

    LOG_ERROR(Graphics, "Vulkan: no device supports graphics, presentation, and {}", VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    return false;
}

bool EngineVK::CreateDevice()
{
    const float queuePriority = 1.0f;
    std::set<uint32_t> uniqueFamilies = {_graphicsQueueFamily, _presentQueueFamily};
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    queueInfos.reserve(uniqueFamilies.size());
    for (uint32_t family : uniqueFamilies)
    {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;
        queueInfos.push_back(queueInfo);
    }

    const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = deviceExtensions;

    const VkResult result = vkCreateDevice(_physicalDevice, &createInfo, nullptr, &_device);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkCreateDevice failed: {}", VkResultName(result));
        return false;
    }

    vkGetDeviceQueue(_device, _graphicsQueueFamily, 0, &_graphicsQueue);
    vkGetDeviceQueue(_device, _presentQueueFamily, 0, &_presentQueue);
    return true;
}

bool EngineVK::CreateCommandPool()
{
    VkCommandPoolCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    createInfo.queueFamilyIndex = _graphicsQueueFamily;

    const VkResult result = vkCreateCommandPool(_device, &createInfo, nullptr, &_commandPool);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkCreateCommandPool failed: {}", VkResultName(result));
        return false;
    }
    return true;
}

bool EngineVK::CreateSwapchain()
{
    VkSurfaceCapabilitiesKHR capabilities{};
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_physicalDevice, _surface, &capabilities);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed: {}", VkResultName(result));
        return false;
    }

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(_physicalDevice, _surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    if (formatCount > 0)
        vkGetPhysicalDeviceSurfaceFormatsKHR(_physicalDevice, _surface, &formatCount, formats.data());
    if (formats.empty())
    {
        LOG_ERROR(Graphics, "Vulkan: surface reported no formats");
        return false;
    }

    const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(formats);
    const VkExtent2D extent = ClampExtent(_width, _height, capabilities);

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
        imageCount = capabilities.maxImageCount;

    uint32_t queueFamilyIndices[] = {_graphicsQueueFamily, _presentQueueFamily};

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = _surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (_graphicsQueueFamily != _presentQueueFamily)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    createInfo.clipped = VK_TRUE;

    result = vkCreateSwapchainKHR(_device, &createInfo, nullptr, &_swapchain);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkCreateSwapchainKHR failed: {}", VkResultName(result));
        return false;
    }

    _swapchainFormat = surfaceFormat.format;
    _swapchainExtent = extent;

    uint32_t actualImageCount = 0;
    vkGetSwapchainImagesKHR(_device, _swapchain, &actualImageCount, nullptr);
    _swapchainImages.resize(actualImageCount);
    vkGetSwapchainImagesKHR(_device, _swapchain, &actualImageCount, _swapchainImages.data());
    _swapchainImageLayouts.assign(actualImageCount, VK_IMAGE_LAYOUT_UNDEFINED);

    _commandBuffers.resize(actualImageCount);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = _commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = actualImageCount;
    result = vkAllocateCommandBuffers(_device, &allocInfo, _commandBuffers.data());
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkAllocateCommandBuffers failed: {}", VkResultName(result));
        return false;
    }

    LOG_INFO(Graphics, "Vulkan: swapchain created {}x{} images={} format={}", _swapchainExtent.width,
             _swapchainExtent.height, actualImageCount, static_cast<int>(_swapchainFormat));
    return true;
}

bool EngineVK::CreateSyncObjects()
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (vkCreateSemaphore(_device, &semaphoreInfo, nullptr, &_imageAvailable) != VK_SUCCESS ||
        vkCreateSemaphore(_device, &semaphoreInfo, nullptr, &_renderFinished) != VK_SUCCESS ||
        vkCreateFence(_device, &fenceInfo, nullptr, &_inFlight) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: failed to create frame sync objects");
        return false;
    }
    return true;
}

bool EngineVK::RecordClearCommand(uint32_t imageIndex)
{
    if (imageIndex >= _commandBuffers.size())
        return false;

    VkCommandBuffer commandBuffer = _commandBuffers[imageIndex];
    vkResetCommandBuffer(commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkResult result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkBeginCommandBuffer failed: {}", VkResultName(result));
        return false;
    }

    VkImageMemoryBarrier toClear{};
    toClear.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toClear.oldLayout = _swapchainImageLayouts[imageIndex];
    toClear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toClear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toClear.image = _swapchainImages[imageIndex];
    toClear.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toClear.subresourceRange.baseMipLevel = 0;
    toClear.subresourceRange.levelCount = 1;
    toClear.subresourceRange.baseArrayLayer = 0;
    toClear.subresourceRange.layerCount = 1;
    toClear.srcAccessMask = 0;
    toClear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &toClear);

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;
    vkCmdClearColorImage(commandBuffer, _swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &_clearColor, 1, &range);

    VkImageMemoryBarrier toPresent{};
    toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image = _swapchainImages[imageIndex];
    toPresent.subresourceRange = range;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toPresent.dstAccessMask = 0;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &toPresent);

    result = vkEndCommandBuffer(commandBuffer);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkEndCommandBuffer failed: {}", VkResultName(result));
        return false;
    }
    return true;
}

void EngineVK::DestroySwapchain()
{
    if (_device && !_commandBuffers.empty())
    {
        vkFreeCommandBuffers(_device, _commandPool, static_cast<uint32_t>(_commandBuffers.size()),
                             _commandBuffers.data());
        _commandBuffers.clear();
    }
    _swapchainImages.clear();
    _swapchainImageLayouts.clear();
    if (_swapchain)
    {
        vkDestroySwapchainKHR(_device, _swapchain, nullptr);
        _swapchain = VK_NULL_HANDLE;
    }
}

bool EngineVK::RecreateSwapchain()
{
    if (!_device)
        return false;

    vkDeviceWaitIdle(_device);
    DestroySwapchain();
    _swapchainDirty = false;
    return CreateSwapchain();
}

void EngineVK::PresentClearFrame()
{
    if (!_device || !_swapchain)
        return;

    if (_swapchainDirty && !RecreateSwapchain())
        return;

    vkWaitForFences(_device, 1, &_inFlight, VK_TRUE, UINT64_MAX);
    vkResetFences(_device, 1, &_inFlight);

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(_device, _swapchain, UINT64_MAX, _imageAvailable, VK_NULL_HANDLE,
                                            &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        RecreateSwapchain();
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        LOG_ERROR(Graphics, "Vulkan: vkAcquireNextImageKHR failed: {}", VkResultName(result));
        return;
    }

    if (!RecordClearCommand(imageIndex))
        return;

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &_imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &_commandBuffers[imageIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &_renderFinished;

    result = vkQueueSubmit(_graphicsQueue, 1, &submitInfo, _inFlight);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkQueueSubmit failed: {}", VkResultName(result));
        return;
    }
    _swapchainImageLayouts[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &_renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &_swapchain;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(_presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        _swapchainDirty = true;
    else if (result != VK_SUCCESS)
        LOG_ERROR(Graphics, "Vulkan: vkQueuePresentKHR failed: {}", VkResultName(result));
    else if (!_loggedFirstPresent)
    {
        _loggedFirstPresent = true;
        LOG_INFO(Graphics, "Vulkan: clear-present completed");
    }
}

void EngineVK::Shutdown()
{
    if (_device)
    {
        vkDeviceWaitIdle(_device);
        if (_inFlight)
        {
            vkDestroyFence(_device, _inFlight, nullptr);
            _inFlight = VK_NULL_HANDLE;
        }
        if (_renderFinished)
        {
            vkDestroySemaphore(_device, _renderFinished, nullptr);
            _renderFinished = VK_NULL_HANDLE;
        }
        if (_imageAvailable)
        {
            vkDestroySemaphore(_device, _imageAvailable, nullptr);
            _imageAvailable = VK_NULL_HANDLE;
        }
        DestroySwapchain();
        if (_commandPool)
        {
            vkDestroyCommandPool(_device, _commandPool, nullptr);
            _commandPool = VK_NULL_HANDLE;
        }
        vkDestroyDevice(_device, nullptr);
        _device = VK_NULL_HANDLE;
    }
    if (_surface)
    {
        SDL_Vulkan_DestroySurface(_instance, _surface, nullptr);
        _surface = VK_NULL_HANDLE;
    }
    if (_instance)
    {
        vkDestroyInstance(_instance, nullptr);
        _instance = VK_NULL_HANDLE;
    }
    if (_window)
    {
        SDL_DestroyWindow(_window);
        _window = nullptr;
    }
    _open = false;
}

void EngineVK::HandleEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                _open = false;
                if (GApp)
                    GApp->m_closeRequest = true;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                OnResized();
                break;
            case SDL_EVENT_KEY_DOWN:
                if (!event.key.repeat)
                    SDLInput_BufferKeyEvent(event.key.scancode, true, Foundation::GlobalTickCount());
                SDLInput_BufferUIKeyEvent(event.key.key, true);
                break;
            case SDL_EVENT_KEY_UP:
                SDLInput_BufferKeyEvent(event.key.scancode, false, Foundation::GlobalTickCount());
                SDLInput_BufferUIKeyEvent(event.key.key, false);
                break;
            case SDL_EVENT_TEXT_INPUT:
                SDLInput_BufferUICharEvent(event.text.text);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                int btn = event.button.button - 1;
                if (btn == 1)
                    btn = 2;
                else if (btn == 2)
                    btn = 1;
                SDLInput_BufferMouseButton(btn, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                break;
            }
            case SDL_EVENT_MOUSE_MOTION:
                SDLInput_BufferMouseMotion(event.motion.xrel, event.motion.yrel);
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                SDLInput_BufferMouseWheel(event.wheel.y);
                break;
            default:
                break;
        }
    }
}

void EngineVK::SetMouseGrab(bool grab)
{
    _mouseGrab = grab;
    if (_window)
        SDL_SetWindowRelativeMouseMode(_window, grab);
}

bool EngineVK::SetWindowMode(WindowMode mode)
{
    if (!_window || mode == WindowMode::Fullscreen)
        return false;

    if (mode == WindowMode::Borderless)
    {
        SDL_SetWindowBordered(_window, false);
        _windowMode = WindowMode::Borderless;
        return true;
    }

    SDL_SetWindowBordered(_window, true);
    _windowMode = WindowMode::Windowed;
    return true;
}

void EngineVK::OnResized()
{
    if (_window)
        SDL_GetWindowSizeInPixels(_window, &_width, &_height);
    _swapchainDirty = true;
    FireResizePostHook(_width, _height);
}

} // namespace Poseidon
