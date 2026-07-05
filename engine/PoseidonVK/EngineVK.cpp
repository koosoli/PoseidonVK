#include <PoseidonVK/EngineVK.hpp>

#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Graphics/Shared/WindowPlacement.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <type_traits>
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
constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
constexpr const char kBootstrapTriangleVertexShader[] =
#include <PoseidonVK/Shaders/bootstrap_triangle.vert.glsl.hpp>
    ;
constexpr const char kBootstrapTriangleFragmentShader[] =
#include <PoseidonVK/Shaders/bootstrap_triangle.frag.glsl.hpp>
    ;

bool HasInstanceExtension(const char* requiredExtension)
{
    uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS)
        return false;

    std::vector<VkExtensionProperties> extensions(count);
    if (count > 0 && vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()) != VK_SUCCESS)
        return false;

    return std::any_of(extensions.begin(), extensions.end(),
                       [requiredExtension](const VkExtensionProperties& extension)
                       { return std::strcmp(extension.extensionName, requiredExtension) == 0; });
}

bool HasInstanceLayer(const char* requiredLayer)
{
    uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS)
        return false;

    std::vector<VkLayerProperties> layers(count);
    if (count > 0 && vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS)
        return false;

    return std::any_of(layers.begin(), layers.end(),
                       [requiredLayer](const VkLayerProperties& layer)
                       { return std::strcmp(layer.layerName, requiredLayer) == 0; });
}

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

const char* DebugSeverityName(VkDebugUtilsMessageSeverityFlagBitsEXT severity)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        return "error";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        return "warning";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        return "info";
    return "verbose";
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                   VkDebugUtilsMessageTypeFlagsEXT,
                                                   const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                                                   void*)
{
    const char* message = callbackData && callbackData->pMessage ? callbackData->pMessage : "<no message>";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        LOG_ERROR(Graphics, "Vulkan validation [{}]: {}", DebugSeverityName(severity), message);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        LOG_WARN(Graphics, "Vulkan validation [{}]: {}", DebugSeverityName(severity), message);
    else
        LOG_DEBUG(Graphics, "Vulkan validation [{}]: {}", DebugSeverityName(severity), message);
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT MakeDebugMessengerCreateInfo()
{
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = VulkanDebugCallback;
    return createInfo;
}

VkResult CreateDebugUtilsMessenger(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
                                   VkDebugUtilsMessengerEXT* messenger)
{
    auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (!fn)
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    return fn(instance, createInfo, nullptr, messenger);
}

void DestroyDebugUtilsMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger)
{
    auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (fn)
        fn(instance, messenger, nullptr);
}

template <typename Handle>
uint64_t VulkanObjectHandle(Handle handle)
{
    if constexpr (std::is_pointer_v<Handle>)
        return reinterpret_cast<uint64_t>(handle);
    else
        return static_cast<uint64_t>(handle);
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

bool RecreateSignaledFence(VkDevice device, VkFence& fence)
{
    if (fence)
    {
        vkDestroyFence(device, fence, nullptr);
        fence = VK_NULL_HANDLE;
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    return vkCreateFence(device, &fenceInfo, nullptr, &fence) == VK_SUCCESS;
}

void EnsureGlslangInitialized()
{
    static const bool initialized = []()
    {
        glslang::InitializeProcess();
        return true;
    }();
    (void)initialized;
}

bool CompileBootstrapShader(const char* source, EShLanguage stage, std::vector<uint32_t>& spirv, std::string& error)
{
    EnsureGlslangInitialized();

    glslang::TShader shader(stage);
    const char* strings[] = {source};
    shader.setStrings(strings, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);

    const EShMessages messages = static_cast<EShMessages>(EShMsgVulkanRules | EShMsgSpvRules);
    if (!shader.parse(GetDefaultResources(), 450, false, messages))
    {
        error = shader.getInfoLog();
        return false;
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages))
    {
        error = program.getInfoLog();
        return false;
    }

    glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);
    if (spirv.empty())
    {
        error = "SPIR-V output is empty";
        return false;
    }
    return true;
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

    if (!CreateInstance() || !CreateDebugMessenger() || !CreateSurface() || !PickPhysicalDevice() || !CreateDevice() ||
        !CreatePipelineLayout() || !CreateCommandPool() || !CreateSwapchain() || !CreateBootstrapPipeline() ||
        !CreateSyncObjects())
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
    LOG_WARN(Graphics, "Vulkan: scene rendering is not implemented yet; bootstrap currently presents a clear-color "
                       "frame only");
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
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    if (!sdlExtensions || extensionCount == 0)
    {
        LOG_ERROR(Graphics, "Vulkan: SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError());
        return false;
    }

    std::vector<const char*> extensions(sdlExtensions, sdlExtensions + extensionCount);

#ifdef _DEBUG
    const bool hasValidationLayer = HasInstanceLayer(kValidationLayer);
    const bool hasDebugUtils = HasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    _debugUtilsEnabled = hasDebugUtils;
    _validationEnabled = hasValidationLayer && hasDebugUtils;
    if (_debugUtilsEnabled)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (!_validationEnabled)
        LOG_WARN(Graphics, "Vulkan: validation disabled ({}={}, {}={})", kValidationLayer,
                 hasValidationLayer ? "yes" : "no", VK_EXT_DEBUG_UTILS_EXTENSION_NAME, hasDebugUtils ? "yes" : "no");
#endif

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
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    const char* validationLayers[] = {kValidationLayer};
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (_validationEnabled)
    {
        debugCreateInfo = MakeDebugMessengerCreateInfo();
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = validationLayers;
        createInfo.pNext = &debugCreateInfo;
    }

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &_instance);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkCreateInstance failed: {}", VkResultName(result));
        return false;
    }
    if (_validationEnabled)
        LOG_INFO(Graphics, "Vulkan: validation layer enabled ({})", kValidationLayer);
    else if (_debugUtilsEnabled)
        LOG_INFO(Graphics, "Vulkan: debug utils enabled for object naming");
    return true;
}

bool EngineVK::CreateDebugMessenger()
{
    if (!_validationEnabled)
        return true;

    const VkDebugUtilsMessengerCreateInfoEXT createInfo = MakeDebugMessengerCreateInfo();
    const VkResult result = CreateDebugUtilsMessenger(_instance, &createInfo, &_debugMessenger);
    if (result != VK_SUCCESS)
    {
        LOG_WARN(Graphics, "Vulkan: debug messenger unavailable: {}", VkResultName(result));
        _debugMessenger = VK_NULL_HANDLE;
        return true;
    }
    LOG_INFO(Graphics, "Vulkan: debug messenger installed");
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
    SetObjectName(VK_OBJECT_TYPE_DEVICE, VulkanObjectHandle(_device), "PoseidonVK Device");
    SetObjectName(VK_OBJECT_TYPE_QUEUE, VulkanObjectHandle(_graphicsQueue), "PoseidonVK Graphics Queue");
    if (_presentQueue != _graphicsQueue)
        SetObjectName(VK_OBJECT_TYPE_QUEUE, VulkanObjectHandle(_presentQueue), "PoseidonVK Present Queue");
    return true;
}

bool EngineVK::CreatePipelineLayout()
{
    VkPipelineLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    const VkResult result = vkCreatePipelineLayout(_device, &createInfo, nullptr, &_pipelineLayout);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkCreatePipelineLayout failed: {}", VkResultName(result));
        return false;
    }
    SetObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, VulkanObjectHandle(_pipelineLayout),
                  "PoseidonVK Bootstrap Pipeline Layout");
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
    SetObjectName(VK_OBJECT_TYPE_COMMAND_POOL, VulkanObjectHandle(_commandPool), "PoseidonVK Command Pool");
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
    if (extent.width == 0 || extent.height == 0)
    {
        LOG_WARN(Graphics, "Vulkan: swapchain deferred for zero-sized surface {}x{}", extent.width, extent.height);
        return false;
    }

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
    SetObjectName(VK_OBJECT_TYPE_SWAPCHAIN_KHR, VulkanObjectHandle(_swapchain), "PoseidonVK Swapchain");

    _swapchainFormat = surfaceFormat.format;
    _swapchainExtent = extent;

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = _swapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    result = vkCreateRenderPass(_device, &renderPassInfo, nullptr, &_renderPass);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkCreateRenderPass failed: {}", VkResultName(result));
        return false;
    }
    SetObjectName(VK_OBJECT_TYPE_RENDER_PASS, VulkanObjectHandle(_renderPass), "PoseidonVK Bootstrap Render Pass");

    uint32_t actualImageCount = 0;
    vkGetSwapchainImagesKHR(_device, _swapchain, &actualImageCount, nullptr);
    _swapchainImages.resize(actualImageCount);
    vkGetSwapchainImagesKHR(_device, _swapchain, &actualImageCount, _swapchainImages.data());
    _swapchainImageViews.resize(actualImageCount, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < actualImageCount; ++i)
    {
        const std::string name = "PoseidonVK Swapchain Image " + std::to_string(i);
        SetObjectName(VK_OBJECT_TYPE_IMAGE, VulkanObjectHandle(_swapchainImages[i]), name.c_str());

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = _swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = _swapchainFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        result = vkCreateImageView(_device, &viewInfo, nullptr, &_swapchainImageViews[i]);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "Vulkan: vkCreateImageView failed: {}", VkResultName(result));
            return false;
        }

        const std::string viewName = "PoseidonVK Swapchain Image View " + std::to_string(i);
        SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, VulkanObjectHandle(_swapchainImageViews[i]), viewName.c_str());
    }

    _framebuffers.resize(actualImageCount, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < actualImageCount; ++i)
    {
        VkImageView attachments[] = {_swapchainImageViews[i]};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = _renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = _swapchainExtent.width;
        framebufferInfo.height = _swapchainExtent.height;
        framebufferInfo.layers = 1;

        result = vkCreateFramebuffer(_device, &framebufferInfo, nullptr, &_framebuffers[i]);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "Vulkan: vkCreateFramebuffer failed: {}", VkResultName(result));
            return false;
        }

        const std::string name = "PoseidonVK Framebuffer " + std::to_string(i);
        SetObjectName(VK_OBJECT_TYPE_FRAMEBUFFER, VulkanObjectHandle(_framebuffers[i]), name.c_str());
    }

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
    for (uint32_t i = 0; i < actualImageCount; ++i)
    {
        const std::string name = "PoseidonVK Clear Command Buffer " + std::to_string(i);
        SetObjectName(VK_OBJECT_TYPE_COMMAND_BUFFER, VulkanObjectHandle(_commandBuffers[i]), name.c_str());
    }

    LOG_INFO(Graphics, "Vulkan: swapchain created {}x{} images={} format={}", _swapchainExtent.width,
             _swapchainExtent.height, actualImageCount, static_cast<int>(_swapchainFormat));
    return true;
}

bool EngineVK::CreateBootstrapPipeline()
{
    std::vector<uint32_t> vertexSpirv;
    std::vector<uint32_t> fragmentSpirv;
    std::string error;
    if (!CompileBootstrapShader(kBootstrapTriangleVertexShader, EShLangVertex, vertexSpirv, error))
    {
        LOG_ERROR(Graphics, "Vulkan: bootstrap vertex shader compile failed: {}", error);
        return false;
    }
    if (!CompileBootstrapShader(kBootstrapTriangleFragmentShader, EShLangFragment, fragmentSpirv, error))
    {
        LOG_ERROR(Graphics, "Vulkan: bootstrap fragment shader compile failed: {}", error);
        return false;
    }

    auto createShaderModule = [&](const std::vector<uint32_t>& spirv, const char* name, VkShaderModule& module)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = spirv.size() * sizeof(uint32_t);
        createInfo.pCode = spirv.data();
        const VkResult result = vkCreateShaderModule(_device, &createInfo, nullptr, &module);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "Vulkan: vkCreateShaderModule failed for {}: {}", name, VkResultName(result));
            return false;
        }
        SetObjectName(VK_OBJECT_TYPE_SHADER_MODULE, VulkanObjectHandle(module), name);
        return true;
    };

    VkShaderModule vertexModule = VK_NULL_HANDLE;
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    if (!createShaderModule(vertexSpirv, "PoseidonVK Bootstrap Vertex Shader", vertexModule) ||
        !createShaderModule(fragmentSpirv, "PoseidonVK Bootstrap Fragment Shader", fragmentModule))
    {
        if (vertexModule)
            vkDestroyShaderModule(_device, vertexModule, nullptr);
        if (fragmentModule)
            vkDestroyShaderModule(_device, fragmentModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertexModule;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragmentModule;
    shaderStages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(_swapchainExtent.width);
    viewport.height = static_cast<float>(_swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = _swapchainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = _pipelineLayout;
    pipelineInfo.renderPass = _renderPass;
    pipelineInfo.subpass = 0;

    const VkResult result = vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                                      &_bootstrapPipeline);
    vkDestroyShaderModule(_device, fragmentModule, nullptr);
    vkDestroyShaderModule(_device, vertexModule, nullptr);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkCreateGraphicsPipelines failed: {}", VkResultName(result));
        return false;
    }

    SetObjectName(VK_OBJECT_TYPE_PIPELINE, VulkanObjectHandle(_bootstrapPipeline),
                  "PoseidonVK Bootstrap Triangle Pipeline");
    LOG_INFO(Graphics, "Vulkan: bootstrap triangle pipeline created");
    return true;
}

bool EngineVK::CreateSyncObjects()
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (!_imageAvailable && vkCreateSemaphore(_device, &semaphoreInfo, nullptr, &_imageAvailable) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: failed to create image-available semaphore");
        return false;
    }
    SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, VulkanObjectHandle(_imageAvailable),
                  "PoseidonVK Image Available Semaphore");

    if (!_inFlight && vkCreateFence(_device, &fenceInfo, nullptr, &_inFlight) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: failed to create frame fence");
        return false;
    }
    SetObjectName(VK_OBJECT_TYPE_FENCE, VulkanObjectHandle(_inFlight), "PoseidonVK Frame Fence");

    if (_renderFinished.size() == _swapchainImages.size() &&
        std::all_of(_renderFinished.begin(), _renderFinished.end(), [](VkSemaphore semaphore)
                    { return semaphore != VK_NULL_HANDLE; }))
    {
        return true;
    }

    for (VkSemaphore semaphore : _renderFinished)
    {
        if (semaphore)
            vkDestroySemaphore(_device, semaphore, nullptr);
    }
    _renderFinished.clear();

    _renderFinished.resize(_swapchainImages.size(), VK_NULL_HANDLE);
    for (std::size_t i = 0; i < _renderFinished.size(); ++i)
    {
        VkSemaphore& semaphore = _renderFinished[i];
        if (vkCreateSemaphore(_device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "Vulkan: failed to create per-image render semaphore");
            return false;
        }
        const std::string name = "PoseidonVK Render Finished Semaphore " + std::to_string(i);
        SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, VulkanObjectHandle(semaphore), name.c_str());
    }
    return true;
}

void EngineVK::SetObjectName(VkObjectType objectType, uint64_t objectHandle, const char* name) const
{
    if (!_debugUtilsEnabled || !_device || objectHandle == 0 || !name)
        return;

    auto fn = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetDeviceProcAddr(_device, "vkSetDebugUtilsObjectNameEXT"));
    if (!fn)
        return;

    VkDebugUtilsObjectNameInfoEXT nameInfo{};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.objectType = objectType;
    nameInfo.objectHandle = objectHandle;
    nameInfo.pObjectName = name;
    fn(_device, &nameInfo);
}

void EngineVK::BeginDebugLabel(VkCommandBuffer commandBuffer, const char* name, float r, float g, float b) const
{
    if (!_debugUtilsEnabled || !commandBuffer || !name)
        return;

    auto fn = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(_device, "vkCmdBeginDebugUtilsLabelEXT"));
    if (!fn)
        return;

    VkDebugUtilsLabelEXT label{};
    label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    label.pLabelName = name;
    label.color[0] = r;
    label.color[1] = g;
    label.color[2] = b;
    label.color[3] = 1.0f;
    fn(commandBuffer, &label);
}

void EngineVK::EndDebugLabel(VkCommandBuffer commandBuffer) const
{
    if (!_debugUtilsEnabled || !commandBuffer)
        return;

    auto fn = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(_device, "vkCmdEndDebugUtilsLabelEXT"));
    if (fn)
        fn(commandBuffer);
}

bool EngineVK::RecordClearCommand(uint32_t imageIndex)
{
    if (imageIndex >= _commandBuffers.size() || imageIndex >= _framebuffers.size())
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

    BeginDebugLabel(commandBuffer, "PoseidonVK Render Pass Clear", 0.04f, 0.35f, 0.75f);

    VkClearValue clearValue{};
    clearValue.color = _clearColor;

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = _renderPass;
    renderPassInfo.framebuffer = _framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = _swapchainExtent;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    if (_bootstrapPipeline)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _bootstrapPipeline);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }
    vkCmdEndRenderPass(commandBuffer);

    EndDebugLabel(commandBuffer);

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
    if (_bootstrapPipeline)
    {
        vkDestroyPipeline(_device, _bootstrapPipeline, nullptr);
        _bootstrapPipeline = VK_NULL_HANDLE;
    }

    for (VkFramebuffer framebuffer : _framebuffers)
    {
        if (framebuffer)
            vkDestroyFramebuffer(_device, framebuffer, nullptr);
    }
    _framebuffers.clear();

    if (_renderPass)
    {
        vkDestroyRenderPass(_device, _renderPass, nullptr);
        _renderPass = VK_NULL_HANDLE;
    }

    for (VkImageView imageView : _swapchainImageViews)
    {
        if (imageView)
            vkDestroyImageView(_device, imageView, nullptr);
    }
    _swapchainImageViews.clear();

    for (VkSemaphore semaphore : _renderFinished)
    {
        if (semaphore)
            vkDestroySemaphore(_device, semaphore, nullptr);
    }
    _renderFinished.clear();

    if (_device && !_commandBuffers.empty())
    {
        vkFreeCommandBuffers(_device, _commandPool, static_cast<uint32_t>(_commandBuffers.size()),
                             _commandBuffers.data());
        _commandBuffers.clear();
    }
    _swapchainImages.clear();
    _swapchainFormat = VK_FORMAT_UNDEFINED;
    _swapchainExtent = {};
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
    const bool recreated = CreateSwapchain() && CreateBootstrapPipeline() && CreateSyncObjects();
    _swapchainDirty = !recreated;
    return recreated;
}

void EngineVK::PresentClearFrame()
{
    if (!_device)
        return;

    if (!_swapchain)
    {
        if (_swapchainDirty)
            RecreateSwapchain();
        return;
    }

    if (_swapchainDirty && !RecreateSwapchain())
        return;

    vkWaitForFences(_device, 1, &_inFlight, VK_TRUE, UINT64_MAX);

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

    vkResetFences(_device, 1, &_inFlight);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &_imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &_commandBuffers[imageIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &_renderFinished[imageIndex];

    result = vkQueueSubmit(_graphicsQueue, 1, &submitInfo, _inFlight);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkQueueSubmit failed: {}", VkResultName(result));
        RecreateSignaledFence(_device, _inFlight);
        return;
    }
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &_renderFinished[imageIndex];
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
        if (_pipelineLayout)
        {
            vkDestroyPipelineLayout(_device, _pipelineLayout, nullptr);
            _pipelineLayout = VK_NULL_HANDLE;
        }
        vkDestroyDevice(_device, nullptr);
        _device = VK_NULL_HANDLE;
    }
    if (_surface)
    {
        SDL_Vulkan_DestroySurface(_instance, _surface, nullptr);
        _surface = VK_NULL_HANDLE;
    }
    if (_debugMessenger)
    {
        DestroyDebugUtilsMessenger(_instance, _debugMessenger);
        _debugMessenger = VK_NULL_HANDLE;
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
        OnResized();
        return true;
    }

    SDL_SetWindowBordered(_window, true);
    _windowMode = WindowMode::Windowed;
    OnResized();
    return true;
}

void EngineVK::OnResized()
{
    if (_window)
        SDL_GetWindowSizeInPixels(_window, &_width, &_height);
    _swapchainDirty = true;
    if (_width <= 0 || _height <= 0)
        return;
    FireResizePostHook(_width, _height);
}

} // namespace Poseidon
