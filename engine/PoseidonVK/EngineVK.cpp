#include <PoseidonVK/EngineVK.hpp>

#include <PoseidonVK/TextBankVK.hpp>
#include <PoseidonVK/TextureVK.hpp>
#include <PoseidonVK/VertexBufferVK.hpp>
#include <PoseidonVK/BootstrapPushConstantsVK.hpp>
#include <Poseidon/Graphics/Core/MatrixConversion.hpp>
#include <Poseidon/Graphics/Rendering/BuildRenderPassDescriptor.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Graphics/Rendering/Primitives/Clip2D.hpp>
#include <Poseidon/Graphics/Textures/TexturePreload.hpp>
#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <unordered_map>
#include <PoseidonVK/BufferVK.hpp>
#include <PoseidonVK/DescriptorBindingsVK.hpp>
#include <PoseidonVK/DrawConstantsVK.hpp>
#include <PoseidonVK/RenderStateVK.hpp>
#include <PoseidonVK/ScenePushConstantsVK.hpp>
#include <PoseidonVK/VertexLayoutVK.hpp>
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
#include <array>
#include <cstddef>
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

struct BootstrapVertex
{
    float position[2];
    float color[3];
};

static_assert(sizeof(BootstrapVertex) == sizeof(float) * 5);

constexpr BootstrapVertex kBootstrapTriangleVertices[] = {
    {{0.0f, -0.5f}, {0.95f, 0.18f, 0.16f}},
    {{0.5f, 0.5f}, {0.18f, 0.75f, 0.32f}},
    {{-0.5f, 0.5f}, {0.20f, 0.42f, 0.95f}},
};

constexpr uint16_t kBootstrapTriangleIndices[] = {0, 1, 2};
constexpr uint32_t kBootstrapTriangleIndexCount =
    static_cast<uint32_t>(sizeof(kBootstrapTriangleIndices) / sizeof(kBootstrapTriangleIndices[0]));

constexpr const char kSceneVertexShader[] =
#include <PoseidonVK/Shaders/scene.vert.glsl.hpp>
    ;
constexpr const char kSceneFragmentShader[] =
#include <PoseidonVK/Shaders/scene.frag.glsl.hpp>
    ;
constexpr const char kScreenVertexShader[] =
#include <PoseidonVK/Shaders/screen.vert.glsl.hpp>
    ;
constexpr const char kScreenFragmentShader[] =
#include <PoseidonVK/Shaders/screen.frag.glsl.hpp>
    ;

// Scene mesh vertex matching the SVertex contract (pos, norm, uv) consumed by
// vsTransform / scene.vert. Laid out so vk::MakeSceneVertex*Description()
// describes it exactly. A single quad offset to the right of center so the
// lit scene draw is visually distinct from the bootstrap triangle on the left.
struct SceneVertex
{
    float position[3];
    float normal[3];
    float texcoord[2];
};

static_assert(sizeof(SceneVertex) == vk::kSceneVertexStride);

constexpr SceneVertex kSceneQuadVertices[] = {
    {{0.10f, -0.35f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    {{0.75f, -0.35f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
    {{0.75f, 0.35f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
    {{0.10f, 0.35f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
};

constexpr uint16_t kSceneQuadIndices[] = {0, 1, 2, 0, 2, 3};
constexpr uint32_t kSceneQuadIndexCount =
    static_cast<uint32_t>(sizeof(kSceneQuadIndices) / sizeof(kSceneQuadIndices[0]));

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
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_MEMORY_MAP_FAILED:
            return "VK_ERROR_MEMORY_MAP_FAILED";
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
        case VK_ERROR_OUT_OF_POOL_MEMORY:
            return "VK_ERROR_OUT_OF_POOL_MEMORY";
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
        !CreateFrameConstantsBuffer() || !CreateBootstrapVertexBuffer() || !CreateBootstrapIndexBuffer() ||
        !CreateSceneVertexBuffer() || !CreateSceneIndexBuffer() ||
        !EnsureDrawConstantsBufferCapacity(1) || !CreateFrameDescriptorLayout() || !CreateTextureDescriptorLayout() ||
        !CreatePipelineLayout() || !CreateTextureDescriptorPool() ||
        !CreateScenePipelineLayout() || !CreateFrameDescriptorSet() ||
        !CreateCommandPool() || !CreateSwapchain() || !CreateBootstrapPipeline() || !CreateScenePipeline() ||
        !CreateScreenDescriptorLayout() || !CreateScreenPipelineLayout() || !CreateScreenPipeline() ||
        !CreateSyncObjects())
    {
        Shutdown();
        return false;
    }

    // Register the bring-up quad as a real mesh so the scene draw loop resolves
    // it through the mesh registry. Until real per-object uploads land, every
    // draw command shares this mesh; the registry path itself is exercised live.
    constexpr std::uint32_t kBootstrapMeshId = 1;
    vk::MeshResourcesVK bootstrapMesh;
    bootstrapMesh.vertexBuffer = _sceneVertexBuffer.buffer;
    bootstrapMesh.indexBuffer = _sceneIndexBuffer.buffer;
    bootstrapMesh.vertexCount = static_cast<std::uint32_t>(sizeof(kSceneQuadVertices) / sizeof(kSceneQuadVertices[0]));
    bootstrapMesh.indexCount = kSceneQuadIndexCount;
    _meshRegistry.Register(kBootstrapMeshId, bootstrapMesh);
    _bootstrapMeshId = kBootstrapMeshId;

    SDL_GetWindowSizeInPixels(_window, &_width, &_height);
    SDL_StartTextInput(_window);
    SetMouseGrab(true);

    _open = true;
    if (GApp)
        GApp->m_appActive = true;

    LOG_INFO(Graphics, "Vulkan: bootstrap initialized {}x{} mode={} graphics_queue={} present_queue={}", _width,
             _height, static_cast<int>(_windowMode), _graphicsQueueFamily, _presentQueueFamily);
    LOG_WARN(Graphics, "Vulkan: scene raster parity is in progress; 3D scene draws use the real camera transform "
                       "and 2D/HUD/text draws go through the screen pipeline. Sky/water/shadow passes are not yet "
                       "implemented.");

    _textBank = new TextBankVK(*this);

    // Create and register fallback white texture
    uint32_t whitePixel = 0xFFFFFFFF;
    _fallbackWhiteTexture = static_cast<TextureVK*>(_textBank->CreateDynamic(1, 1, &whitePixel, 4, false));
    if (_fallbackWhiteTexture)
    {
        UnregisterTexture(_fallbackWhiteTexture);
        _fallbackWhiteTexture->_resourceId = TextureVK::kFallbackResourceId;
        RegisterTexture(_fallbackWhiteTexture);
    }
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
    PresentBootstrapFrame();
}

void EngineVK::SubmitFramePlan(const render::frame::Frame& frame)
{
    _lastFrameConstants = vk::BuildFrameConstants(frame);
    _lastDrawConstants = vk::BuildDrawConstants(frame);
    _lastSceneDrawCommands = vk::BuildSceneDrawCommands(_lastDrawConstants);
    _hasFrameConstants = true;
    UploadFrameConstants();
    UploadDrawConstants();
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
    VkDescriptorSetLayout setLayouts[] = {_frameDescriptorSetLayout};

    VkPushConstantRange pushConstants{};
    pushConstants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstants.offset = 0;
    pushConstants.size = vk::kBootstrapPushConstantsSize;

    VkPipelineLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    createInfo.setLayoutCount = _frameDescriptorSetLayout ? 1u : 0u;
    createInfo.pSetLayouts = _frameDescriptorSetLayout ? setLayouts : nullptr;
    createInfo.pushConstantRangeCount = 1;
    createInfo.pPushConstantRanges = &pushConstants;

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

bool EngineVK::CreateScenePipelineLayout()
{
    VkDescriptorSetLayout setLayouts[] = {_frameDescriptorSetLayout, _textureDescriptorSetLayout};

    VkPushConstantRange pushConstants{};
    pushConstants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstants.offset = 0;
    pushConstants.size = vk::kScenePushConstantsSize;

    VkPipelineLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    createInfo.setLayoutCount = 2;
    createInfo.pSetLayouts = setLayouts;
    createInfo.pushConstantRangeCount = 1;
    createInfo.pPushConstantRanges = &pushConstants;

    const VkResult result = vkCreatePipelineLayout(_device, &createInfo, nullptr, &_scenePipelineLayout);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: scene pipeline layout creation failed: {}", VkResultName(result));
        return false;
    }
    SetObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, VulkanObjectHandle(_scenePipelineLayout),
                  "PoseidonVK Scene Pipeline Layout");
    return true;
}

bool EngineVK::CreateFrameConstantsBuffer()
{
    const VkResult result = vk::CreateHostVisibleBuffer(_physicalDevice, _device, sizeof(vk::FrameConstantsVK),
                                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, _frameConstantsBuffer);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: frame constants buffer creation failed: {}", VkResultName(result));
        return false;
    }

    UploadFrameConstants();
    SetObjectName(VK_OBJECT_TYPE_BUFFER, VulkanObjectHandle(_frameConstantsBuffer.buffer),
                  "PoseidonVK Frame Constants Buffer");
    SetObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, VulkanObjectHandle(_frameConstantsBuffer.memory),
                  "PoseidonVK Frame Constants Memory");
    return true;
}

bool EngineVK::CreateBootstrapVertexBuffer()
{
    const VkResult result =
        vk::CreateHostVisibleBuffer(_physicalDevice, _device, sizeof(kBootstrapTriangleVertices),
                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, _bootstrapVertexBuffer);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: bootstrap vertex buffer creation failed: {}", VkResultName(result));
        return false;
    }

    vk::UploadMappedBuffer(_bootstrapVertexBuffer, kBootstrapTriangleVertices, sizeof(kBootstrapTriangleVertices));
    SetObjectName(VK_OBJECT_TYPE_BUFFER, VulkanObjectHandle(_bootstrapVertexBuffer.buffer),
                  "PoseidonVK Bootstrap Triangle Vertex Buffer");
    SetObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, VulkanObjectHandle(_bootstrapVertexBuffer.memory),
                  "PoseidonVK Bootstrap Triangle Vertex Memory");
    return true;
}

bool EngineVK::CreateBootstrapIndexBuffer()
{
    const VkResult result =
        vk::CreateHostVisibleBuffer(_physicalDevice, _device, sizeof(kBootstrapTriangleIndices),
                                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT, _bootstrapIndexBuffer);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: bootstrap index buffer creation failed: {}", VkResultName(result));
        return false;
    }

    vk::UploadMappedBuffer(_bootstrapIndexBuffer, kBootstrapTriangleIndices, sizeof(kBootstrapTriangleIndices));
    SetObjectName(VK_OBJECT_TYPE_BUFFER, VulkanObjectHandle(_bootstrapIndexBuffer.buffer),
                  "PoseidonVK Bootstrap Triangle Index Buffer");
    SetObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, VulkanObjectHandle(_bootstrapIndexBuffer.memory),
                  "PoseidonVK Bootstrap Triangle Index Memory");
    return true;
}

bool EngineVK::CreateSceneVertexBuffer()
{
    const VkResult result =
        vk::CreateHostVisibleBuffer(_physicalDevice, _device, sizeof(kSceneQuadVertices),
                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, _sceneVertexBuffer);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: scene vertex buffer creation failed: {}", VkResultName(result));
        return false;
    }

    vk::UploadMappedBuffer(_sceneVertexBuffer, kSceneQuadVertices, sizeof(kSceneQuadVertices));
    SetObjectName(VK_OBJECT_TYPE_BUFFER, VulkanObjectHandle(_sceneVertexBuffer.buffer),
                  "PoseidonVK Scene Quad Vertex Buffer");
    SetObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, VulkanObjectHandle(_sceneVertexBuffer.memory),
                  "PoseidonVK Scene Quad Vertex Memory");
    return true;
}

bool EngineVK::CreateSceneIndexBuffer()
{
    const VkResult result =
        vk::CreateHostVisibleBuffer(_physicalDevice, _device, sizeof(kSceneQuadIndices),
                                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT, _sceneIndexBuffer);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: scene index buffer creation failed: {}", VkResultName(result));
        return false;
    }

    vk::UploadMappedBuffer(_sceneIndexBuffer, kSceneQuadIndices, sizeof(kSceneQuadIndices));
    SetObjectName(VK_OBJECT_TYPE_BUFFER, VulkanObjectHandle(_sceneIndexBuffer.buffer),
                  "PoseidonVK Scene Quad Index Buffer");
    SetObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, VulkanObjectHandle(_sceneIndexBuffer.memory),
                  "PoseidonVK Scene Quad Index Memory");
    return true;
}

bool EngineVK::EnsureDrawConstantsBufferCapacity(std::size_t drawCount)
{
    if (drawCount == 0 || _drawConstantsCapacity >= drawCount)
        return true;

    if (!_physicalDevice || !_device)
        return false;

    const VkDeviceSize byteSize = static_cast<VkDeviceSize>(vk::DrawConstantsByteSize(drawCount));
    vkDeviceWaitIdle(_device);

    vk::BufferVK replacement;
    const VkResult result = vk::CreateHostVisibleBuffer(_physicalDevice, _device, byteSize,
                                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, replacement);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: draw constants buffer creation failed: {}", VkResultName(result));
        return false;
    }

    vk::DestroyBuffer(_device, _drawConstantsBuffer);
    _drawConstantsBuffer = replacement;
    _drawConstantsCapacity = drawCount;
    std::vector<vk::DrawConstantsVK> cleared(drawCount);
    vk::UploadMappedBuffer(_drawConstantsBuffer, cleared.data(), vk::DrawConstantsByteSize(cleared.size()));

    SetObjectName(VK_OBJECT_TYPE_BUFFER, VulkanObjectHandle(_drawConstantsBuffer.buffer),
                  "PoseidonVK Draw Constants Buffer");
    SetObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, VulkanObjectHandle(_drawConstantsBuffer.memory),
                  "PoseidonVK Draw Constants Memory");
    if (_frameDescriptorSet)
        UpdateFrameDescriptorSet();
    return true;
}

bool EngineVK::EnsureScreenVertexBufferCapacity(std::size_t vertexCount, std::size_t indexCount)
{
    bool needVUpdate = vertexCount > 0 && _screenVertexCapacity < vertexCount;
    bool needIUpdate = indexCount > 0 && _screenIndexCapacity < indexCount;

    if (!needVUpdate && !needIUpdate)
        return true;

    if (!_physicalDevice || !_device)
        return false;

    vkDeviceWaitIdle(_device);

    if (needVUpdate)
    {
        std::size_t newCap = std::max<std::size_t>(_screenVertexCapacity * 2, 1024);
        while (newCap < vertexCount)
            newCap *= 2;

        vk::BufferVK replacement;
        const VkResult result = vk::CreateHostVisibleBuffer(_physicalDevice, _device, newCap * sizeof(TLVertex),
                                                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, replacement);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "Vulkan: screen vertex buffer creation failed: {}", VkResultName(result));
            return false;
        }

        vk::DestroyBuffer(_device, _screenVertexBuffer);
        _screenVertexBuffer = replacement;
        _screenVertexCapacity = newCap;

        SetObjectName(VK_OBJECT_TYPE_BUFFER, VulkanObjectHandle(_screenVertexBuffer.buffer),
                      "PoseidonVK Screen Vertex Buffer");
        SetObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, VulkanObjectHandle(_screenVertexBuffer.memory),
                      "PoseidonVK Screen Vertex Memory");
    }

    if (needIUpdate)
    {
        std::size_t newCap = std::max<std::size_t>(_screenIndexCapacity * 2, 2048);
        while (newCap < indexCount)
            newCap *= 2;

        vk::BufferVK replacement;
        const VkResult result = vk::CreateHostVisibleBuffer(_physicalDevice, _device, newCap * sizeof(std::uint16_t),
                                                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, replacement);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "Vulkan: screen index buffer creation failed: {}", VkResultName(result));
            return false;
        }

        vk::DestroyBuffer(_device, _screenIndexBuffer);
        _screenIndexBuffer = replacement;
        _screenIndexCapacity = newCap;

        SetObjectName(VK_OBJECT_TYPE_BUFFER, VulkanObjectHandle(_screenIndexBuffer.buffer),
                      "PoseidonVK Screen Index Buffer");
        SetObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, VulkanObjectHandle(_screenIndexBuffer.memory),
                      "PoseidonVK Screen Index Memory");
    }

    return true;
}

bool EngineVK::CreateFrameDescriptorLayout()
{
    const std::array<VkDescriptorSetLayoutBinding, vk::kFrameDescriptorSetBindingCount> bindings =
        vk::MakeFrameDescriptorSetLayoutBindings();

    VkDescriptorSetLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    createInfo.pBindings = bindings.data();

    const VkResult result = vkCreateDescriptorSetLayout(_device, &createInfo, nullptr, &_frameDescriptorSetLayout);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkCreateDescriptorSetLayout(frame constants) failed: {}", VkResultName(result));
        return false;
    }

    SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, VulkanObjectHandle(_frameDescriptorSetLayout),
                  "PoseidonVK Frame Descriptor Set Layout");
    return true;
}

bool EngineVK::CreateTextureDescriptorLayout()
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = 1;
    createInfo.pBindings = &binding;

    const VkResult result = vkCreateDescriptorSetLayout(_device, &createInfo, nullptr, &_textureDescriptorSetLayout);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkCreateDescriptorSetLayout(textures) failed: {}", VkResultName(result));
        return false;
    }
    SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, VulkanObjectHandle(_textureDescriptorSetLayout),
                  "PoseidonVK Texture Descriptor Set Layout");
    return true;
}

bool EngineVK::CreateTextureDescriptorPool()
{
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 2048; // max textures

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 2048;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    const VkResult result = vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_textureDescriptorPool);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkCreateDescriptorPool(textures) failed: {}", VkResultName(result));
        return false;
    }
    SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_POOL, VulkanObjectHandle(_textureDescriptorPool),
                  "PoseidonVK Texture Descriptor Pool");
    return true;
}

void EngineVK::DestroyTextureDescriptorResources()
{
    if (_device)
    {
        if (_textureDescriptorPool)
        {
            vkDestroyDescriptorPool(_device, _textureDescriptorPool, nullptr);
            _textureDescriptorPool = VK_NULL_HANDLE;
        }
        if (_textureDescriptorSetLayout)
        {
            vkDestroyDescriptorSetLayout(_device, _textureDescriptorSetLayout, nullptr);
            _textureDescriptorSetLayout = VK_NULL_HANDLE;
        }
    }
}


bool EngineVK::CreateFrameDescriptorSet()
{
    const std::array<VkDescriptorPoolSize, vk::kFrameDescriptorSetBindingCount> poolSizes =
        vk::MakeFrameDescriptorPoolSizes();

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    VkResult result = vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_descriptorPool);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkCreateDescriptorPool(frame constants) failed: {}", VkResultName(result));
        return false;
    }

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = _descriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &_frameDescriptorSetLayout;

    result = vkAllocateDescriptorSets(_device, &allocateInfo, &_frameDescriptorSet);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkAllocateDescriptorSets(frame constants) failed: {}", VkResultName(result));
        if (_descriptorPool)
        {
            vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
            _descriptorPool = VK_NULL_HANDLE;
        }
        return false;
    }

    UpdateFrameDescriptorSet();

    SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_POOL, VulkanObjectHandle(_descriptorPool),
                  "PoseidonVK Descriptor Pool");
    SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, VulkanObjectHandle(_frameDescriptorSet),
                  "PoseidonVK Frame Descriptor Set");
    return true;
}

void EngineVK::UpdateFrameDescriptorSet()
{
    if (!_device || !_frameDescriptorSet || !_frameConstantsBuffer.buffer || !_drawConstantsBuffer.buffer)
        return;

    VkDescriptorBufferInfo frameBufferInfo{};
    frameBufferInfo.buffer = _frameConstantsBuffer.buffer;
    frameBufferInfo.offset = 0;
    frameBufferInfo.range = sizeof(vk::FrameConstantsVK);

    VkDescriptorBufferInfo drawBufferInfo{};
    drawBufferInfo.buffer = _drawConstantsBuffer.buffer;
    drawBufferInfo.offset = 0;
    drawBufferInfo.range = _drawConstantsBuffer.size;

    std::array<VkWriteDescriptorSet, vk::kFrameDescriptorSetBindingCount> writes = {
        vk::MakeFrameConstantsDescriptorWrite(_frameDescriptorSet, &frameBufferInfo),
        vk::MakeDrawConstantsDescriptorWrite(_frameDescriptorSet, &drawBufferInfo),
    };

    vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
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

    VkAttachmentDescription attachments[2]{};
    attachments[0].format = _swapchainFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    const VkFormat depthFormat = FindDepthFormat();
    if (depthFormat == VK_FORMAT_UNDEFINED)
    {
        LOG_ERROR(Graphics, "Vulkan: no supported depth format for render pass");
        return false;
    }
    _depthFormat = depthFormat;
    attachments[1].format = depthFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = attachments;
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

    if (!CreateDepthResources())
        return false;

    _framebuffers.resize(actualImageCount, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < actualImageCount; ++i)
    {
        VkImageView attachments[] = {_swapchainImageViews[i], _depthImageView};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = _renderPass;
        framebufferInfo.attachmentCount = 2;
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

VkFormat EngineVK::FindDepthFormat() const
{
    const VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
                                   VK_FORMAT_D24_UNORM_S8_UINT};
    for (VkFormat format : candidates)
    {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(_physicalDevice, format, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return format;
    }
    return VK_FORMAT_UNDEFINED;
}

bool EngineVK::CreateDepthResources()
{
    if (_swapchainExtent.width == 0 || _swapchainExtent.height == 0)
        return false;

    // _depthFormat is selected during render-pass creation (CreateSwapchain);
    // re-query only as a fallback if it was not already populated.
    if (_depthFormat == VK_FORMAT_UNDEFINED)
        _depthFormat = FindDepthFormat();
    if (_depthFormat == VK_FORMAT_UNDEFINED)
    {
        LOG_ERROR(Graphics, "Vulkan: no depth format with depth-stencil attachment support found");
        return false;
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = _depthFormat;
    imageInfo.extent = {_swapchainExtent.width, _swapchainExtent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult result = vkCreateImage(_device, &imageInfo, nullptr, &_depthImage);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: depth image creation failed: {}", VkResultName(result));
        return false;
    }

    VkMemoryRequirements memRequirements{};
    vkGetImageMemoryRequirements(_device, _depthImage, &memRequirements);
    const uint32_t memoryType =
        vk::FindMemoryType(_physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType == vk::kInvalidMemoryType)
    {
        LOG_ERROR(Graphics, "Vulkan: no device-local memory type for depth image");
        vkDestroyImage(_device, _depthImage, nullptr);
        _depthImage = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryType;
    result = vkAllocateMemory(_device, &allocInfo, nullptr, &_depthImageMemory);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: depth image memory allocation failed: {}", VkResultName(result));
        vkDestroyImage(_device, _depthImage, nullptr);
        _depthImage = VK_NULL_HANDLE;
        return false;
    }

    result = vkBindImageMemory(_device, _depthImage, _depthImageMemory, 0);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: depth image memory bind failed: {}", VkResultName(result));
        vkFreeMemory(_device, _depthImageMemory, nullptr);
        _depthImageMemory = VK_NULL_HANDLE;
        vkDestroyImage(_device, _depthImage, nullptr);
        _depthImage = VK_NULL_HANDLE;
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = _depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = _depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    result = vkCreateImageView(_device, &viewInfo, nullptr, &_depthImageView);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: depth image view creation failed: {}", VkResultName(result));
        vkFreeMemory(_device, _depthImageMemory, nullptr);
        _depthImageMemory = VK_NULL_HANDLE;
        vkDestroyImage(_device, _depthImage, nullptr);
        _depthImage = VK_NULL_HANDLE;
        return false;
    }

    SetObjectName(VK_OBJECT_TYPE_IMAGE, VulkanObjectHandle(_depthImage), "PoseidonVK Depth Image");
    SetObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, VulkanObjectHandle(_depthImageMemory), "PoseidonVK Depth Image Memory");
    SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, VulkanObjectHandle(_depthImageView), "PoseidonVK Depth Image View");
    return true;
}

void EngineVK::DestroyDepthResources()
{
    if (_depthImageView)
    {
        vkDestroyImageView(_device, _depthImageView, nullptr);
        _depthImageView = VK_NULL_HANDLE;
    }
    if (_depthImage)
    {
        vkDestroyImage(_device, _depthImage, nullptr);
        _depthImage = VK_NULL_HANDLE;
    }
    if (_depthImageMemory)
    {
        vkFreeMemory(_device, _depthImageMemory, nullptr);
        _depthImageMemory = VK_NULL_HANDLE;
    }
    _depthFormat = VK_FORMAT_UNDEFINED;
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
    VkVertexInputBindingDescription vertexBinding{};
    vertexBinding.binding = 0;
    vertexBinding.stride = sizeof(BootstrapVertex);
    vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertexAttributes[2]{};
    vertexAttributes[0].location = 0;
    vertexAttributes[0].binding = 0;
    vertexAttributes[0].format = VK_FORMAT_R32G32_SFLOAT;
    vertexAttributes[0].offset = offsetof(BootstrapVertex, position);
    vertexAttributes[1].location = 1;
    vertexAttributes[1].binding = 0;
    vertexAttributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexAttributes[1].offset = offsetof(BootstrapVertex, color);
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &vertexBinding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = vertexAttributes;

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

    VkPipelineRasterizationStateCreateInfo rasterizer =
        vk::BuildRasterizationState(render::CullMode::None, render::FrontFaceMode::CW);

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment =
        vk::BuildColorBlendAttachmentState(render::BlendMode::Opaque);

    // Bootstrap triangle does not need depth testing, but the render pass now
    // carries a depth attachment, so the spec requires an explicit depth-stencil
    // state. Disabled maps to test-enabled/always-pass with no write.
    VkPipelineDepthStencilStateCreateInfo depthStencil =
        vk::BuildDepthStencilState(render::DepthMode::Disabled);

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
    pipelineInfo.pDepthStencilState = &depthStencil;
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

bool EngineVK::CreateScenePipeline()
{
    std::vector<uint32_t> vertexSpirv;
    std::vector<uint32_t> fragmentSpirv;
    std::string error;
    if (!CompileBootstrapShader(kSceneVertexShader, EShLangVertex, vertexSpirv, error))
    {
        LOG_ERROR(Graphics, "Vulkan: scene vertex shader compile failed: {}", error);
        return false;
    }
    if (!CompileBootstrapShader(kSceneFragmentShader, EShLangFragment, fragmentSpirv, error))
    {
        LOG_ERROR(Graphics, "Vulkan: scene fragment shader compile failed: {}", error);
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
    if (!createShaderModule(vertexSpirv, "PoseidonVK Scene Vertex Shader", vertexModule) ||
        !createShaderModule(fragmentSpirv, "PoseidonVK Scene Fragment Shader", fragmentModule))
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

    const VkVertexInputBindingDescription vertexBinding = vk::MakeSceneVertexBindingDescription();
    const std::array<VkVertexInputAttributeDescription, vk::kSceneVertexAttributeCount> vertexAttributes =
        vk::MakeSceneVertexAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &vertexBinding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
    vertexInput.pVertexAttributeDescriptions = vertexAttributes.data();

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

    // Cull disabled for bring-up so winding mistakes never hide the quad; the
    // real scene pipeline will set Back/CW once winding matches the mesh loader.
    VkPipelineRasterizationStateCreateInfo rasterizer =
        vk::BuildRasterizationState(render::CullMode::None, render::FrontFaceMode::CW);

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil =
        vk::BuildDepthStencilState(render::DepthMode::Normal);

    VkPipelineColorBlendAttachmentState colorBlendAttachment =
        vk::BuildColorBlendAttachmentState(render::BlendMode::Opaque);

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
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = _scenePipelineLayout;
    pipelineInfo.renderPass = _renderPass;
    pipelineInfo.subpass = 0;

    const VkResult result = vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                                      &_scenePipeline);

    // Keep the shader modules alive so the pipeline cache can create variants.
    // They will be destroyed in DestroyScenePipelineLayout.
    _sceneVertexModule   = vertexModule;
    _sceneFragmentModule = fragmentModule;

    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: scene pipeline creation failed: {}", VkResultName(result));
        return false;
    }

    SetObjectName(VK_OBJECT_TYPE_PIPELINE, VulkanObjectHandle(_scenePipeline), "PoseidonVK Scene Quad Pipeline");

    // Initialise the pipeline cache with the fixed state shared across all variants.
    _scenePipelineCache.Init(_device, _renderPass, _scenePipelineLayout,
                             _sceneVertexModule, _sceneFragmentModule,
                             vertexInput, inputAssembly, viewportState, multisampling);

    LOG_INFO(Graphics, "Vulkan: scene pipeline created");
    return true;
}

bool EngineVK::CreateScreenDescriptorLayout()
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = 1;
    createInfo.pBindings = &binding;

    const VkResult result = vkCreateDescriptorSetLayout(_device, &createInfo, nullptr, &_screenDescriptorSetLayout);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkCreateDescriptorSetLayout(screen) failed: {}", VkResultName(result));
        return false;
    }
    SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, VulkanObjectHandle(_screenDescriptorSetLayout),
                  "PoseidonVK Screen Descriptor Set Layout");
    return true;
}

bool EngineVK::CreateScreenPipelineLayout()
{
    VkDescriptorSetLayout setLayouts[] = {_screenDescriptorSetLayout};

    VkPushConstantRange pushConstants{};
    pushConstants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstants.offset = 0;
    pushConstants.size = vk::kScreenPushConstantsSize;

    VkPipelineLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    createInfo.setLayoutCount = 1;
    createInfo.pSetLayouts = setLayouts;
    createInfo.pushConstantRangeCount = 1;
    createInfo.pPushConstantRanges = &pushConstants;

    const VkResult result = vkCreatePipelineLayout(_device, &createInfo, nullptr, &_screenPipelineLayout);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: screen pipeline layout creation failed: {}", VkResultName(result));
        return false;
    }
    SetObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, VulkanObjectHandle(_screenPipelineLayout),
                  "PoseidonVK Screen Pipeline Layout");
    return true;
}

bool EngineVK::CreateScreenPipeline()
{
    std::vector<uint32_t> vertexSpirv;
    std::vector<uint32_t> fragmentSpirv;
    std::string error;
    if (!CompileBootstrapShader(kScreenVertexShader, EShLangVertex, vertexSpirv, error))
    {
        LOG_ERROR(Graphics, "Vulkan: screen vertex shader compile failed: {}", error);
        return false;
    }
    if (!CompileBootstrapShader(kScreenFragmentShader, EShLangFragment, fragmentSpirv, error))
    {
        LOG_ERROR(Graphics, "Vulkan: screen fragment shader compile failed: {}", error);
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
    if (!createShaderModule(vertexSpirv, "PoseidonVK Screen Vertex Shader", vertexModule) ||
        !createShaderModule(fragmentSpirv, "PoseidonVK Screen Fragment Shader", fragmentModule))
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

    const VkVertexInputBindingDescription vertexBinding = vk::MakeScreenVertexBindingDescription();
    const std::array<VkVertexInputAttributeDescription, vk::kScreenVertexAttributeCount> vertexAttributes =
        vk::MakeScreenVertexAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &vertexBinding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
    vertexInput.pVertexAttributeDescriptions = vertexAttributes.data();

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

    VkPipelineRasterizationStateCreateInfo rasterizer =
        vk::BuildRasterizationState(render::CullMode::None, render::FrontFaceMode::CW);

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil =
        vk::BuildDepthStencilState(render::DepthMode::Disabled);

    VkPipelineColorBlendAttachmentState colorBlendAttachment =
        vk::BuildColorBlendAttachmentState(render::BlendMode::AlphaBlend);

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
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = _screenPipelineLayout;
    pipelineInfo.renderPass = _renderPass;
    pipelineInfo.subpass = 0;

    const VkResult result = vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                                      &_screenPipeline);

    _screenVertexModule = vertexModule;
    _screenFragmentModule = fragmentModule;

    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: screen pipeline creation failed: {}", VkResultName(result));
        return false;
    }

    SetObjectName(VK_OBJECT_TYPE_PIPELINE, VulkanObjectHandle(_screenPipeline), "PoseidonVK Screen Pipeline");
    LOG_INFO(Graphics, "Vulkan: screen pipeline created");
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

void EngineVK::UploadFrameConstants()
{
    vk::UploadMappedBuffer(_frameConstantsBuffer, &_lastFrameConstants, sizeof(_lastFrameConstants));
}

bool EngineVK::UploadDrawConstants()
{
    if (_lastDrawConstants.empty())
        return true;

    if (!EnsureDrawConstantsBufferCapacity(_lastDrawConstants.size()))
        return false;

    vk::UploadMappedBuffer(_drawConstantsBuffer, _lastDrawConstants.data(),
                           vk::DrawConstantsByteSize(_lastDrawConstants.size()));
    return true;
}

void EngineVK::DestroyFrameDescriptorResources()
{
    _frameDescriptorSet = VK_NULL_HANDLE;
    if (_device && _descriptorPool)
    {
        vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
        _descriptorPool = VK_NULL_HANDLE;
    }
    if (_device && _frameDescriptorSetLayout)
    {
        vkDestroyDescriptorSetLayout(_device, _frameDescriptorSetLayout, nullptr);
        _frameDescriptorSetLayout = VK_NULL_HANDLE;
    }
}

bool EngineVK::RecordBootstrapCommand(uint32_t imageIndex)
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

    BeginDebugLabel(commandBuffer, "PoseidonVK Bootstrap Render Pass", 0.04f, 0.35f, 0.75f);

    VkClearValue clearValues[2]{};
    clearValues[0].color = _clearColor;
    clearValues[1].depthStencil.depth = 1.0f;
    clearValues[1].depthStencil.stencil = 0;

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = _renderPass;
    renderPassInfo.framebuffer = _framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = _swapchainExtent;
    renderPassInfo.clearValueCount = 2;
    renderPassInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    if (_bootstrapPipeline)
    {
        const vk::BootstrapPushConstantsVK constants =
            _hasFrameConstants
                ? vk::BuildBootstrapPushConstants(_lastFrameConstants, _clearColor.float32)
                : vk::BuildBootstrapPushConstants(static_cast<int>(_swapchainExtent.width),
                                                  static_cast<int>(_swapchainExtent.height), _clearColor.float32);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _bootstrapPipeline);
        if (_bootstrapVertexBuffer.buffer)
        {
            VkBuffer vertexBuffers[] = {_bootstrapVertexBuffer.buffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        }
        if (_bootstrapIndexBuffer.buffer)
            vkCmdBindIndexBuffer(commandBuffer, _bootstrapIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);
        if (_frameDescriptorSet)
        {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 0, 1,
                                    &_frameDescriptorSet, 0, nullptr);
        }
        vkCmdPushConstants(commandBuffer, _pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, vk::kBootstrapPushConstantsSize, &constants);
        vkCmdDrawIndexed(commandBuffer, kBootstrapTriangleIndexCount, 1, 0, 0, 0);
    }
    if (_scenePipeline)
    {
        VkPipeline lastBoundPipeline = _scenePipeline;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _scenePipeline);
        if (_frameDescriptorSet)
        {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _scenePipelineLayout, 0, 1,
                                    &_frameDescriptorSet, 0, nullptr);
        }

        if (!_lastSceneDrawCommands.empty())
        {
            VkDescriptorSet lastBoundTexDescriptorSet = VK_NULL_HANDLE;
            for (const vk::SceneDrawCommandVK& command : _lastSceneDrawCommands)
            {
                vk::PipelineKeyVK key;
                const vk::DrawConstantsVK& draw = _lastDrawConstants[command.drawIndex];
                key.cull = static_cast<render::CullMode>(draw.cull);
                key.frontFace = static_cast<render::FrontFaceMode>(draw.frontFace);
                key.depth = static_cast<render::DepthMode>(draw.depth);
                key.blend = static_cast<render::BlendMode>(draw.blend);

                VkPipeline pipeline = _scenePipelineCache.Get(key);
                if (!pipeline)
                    pipeline = _scenePipeline;

                if (pipeline != lastBoundPipeline)
                {
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    lastBoundPipeline = pipeline;
                }

                // Bind texture descriptor set (Set 1)
                VkDescriptorSet texDescriptorSet = VK_NULL_HANDLE;
                std::uint32_t texId = draw.textureIds[0];
                if (texId != 0)
                {
                    TextureVK* tex = ResolveTexture(texId);
                    if (tex)
                        texDescriptorSet = tex->GetDescriptorSet();
                }

                if (texDescriptorSet == VK_NULL_HANDLE && _fallbackWhiteTexture)
                {
                    texDescriptorSet = _fallbackWhiteTexture->GetDescriptorSet();
                }

                if (texDescriptorSet != VK_NULL_HANDLE && texDescriptorSet != lastBoundTexDescriptorSet)
                {
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _scenePipelineLayout, 1, 1,
                                            &texDescriptorSet, 0, nullptr);
                    lastBoundTexDescriptorSet = texDescriptorSet;
                }

                // Resolve the draw's mesh via the registry. Until real per-object
                // uploads land, most commands resolve to the bring-up quad; the
                // resolve-and-bind path itself is exercised live against real ids.
                const vk::MeshResourcesVK* mesh = _meshRegistry.Resolve(command.meshId);
                if (!mesh || !mesh->IsValid())
                    mesh = _meshRegistry.Resolve(_bootstrapMeshId);
                if (!mesh || !mesh->IsValid())
                    continue;

                if (mesh->vertexBuffer)
                {
                    VkBuffer vertexBuffers[] = {mesh->vertexBuffer};
                    VkDeviceSize offsets[] = {0};
                    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                }
                if (mesh->indexBuffer)
                    vkCmdBindIndexBuffer(commandBuffer, mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT16);

                // Push the real per-draw world matrix so the shader's fallback
                // path (used when useDrawConstants is false) also matches. With
                // useDrawConstants=true the shader prefers draws[drawIndex].world
                // from the SSBO; this push constant keeps the two paths in sync.
                const GfxMatrix& drawWorld =
                    (command.drawIndex < _lastDrawConstants.size())
                        ? _lastDrawConstants[command.drawIndex].world
                        : vk::BuildIdentityScenePushConstants().world;
                const vk::ScenePushConstantsVK constants =
                    vk::BuildScenePushConstants(drawWorld, true, command.drawIndex);
                vkCmdPushConstants(commandBuffer, _scenePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                   vk::kScenePushConstantsSize, &constants);

                // Clamp the draw's index range against the resolved mesh's actual
                // index count so a stale/oversized firstIndex+indexCount cannot
                // read past the buffer (uint16 indices).
                const uint32_t firstIndex = command.firstIndex;
                const uint32_t meshIndexCount = mesh->indexCount;
                const uint32_t indexCount =
                    (firstIndex + command.indexCount <= meshIndexCount)
                        ? command.indexCount
                        : (firstIndex < meshIndexCount ? meshIndexCount - firstIndex : 0);
                if (indexCount > 0)
                    vkCmdDrawIndexed(commandBuffer, indexCount, 1, firstIndex, 0, 0);
            }
        }
        else if (_hasFrameConstants)
        {
            // No drawable scene commands yet, but frame constants are available:
            // draw the bring-up quad under the real camera transform so the scene
            // pipeline stays exercised until real meshes arrive. Before the first
            // frame plan (no constants yet) only the bootstrap triangle shows.
            if (_fallbackWhiteTexture)
            {
                VkDescriptorSet fallbackSet = _fallbackWhiteTexture->GetDescriptorSet();
                if (fallbackSet != VK_NULL_HANDLE)
                {
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _scenePipelineLayout, 1, 1,
                                            &fallbackSet, 0, nullptr);
                }
            }

            const vk::MeshResourcesVK* mesh = _meshRegistry.Resolve(_bootstrapMeshId);
            if (mesh && mesh->vertexBuffer)
            {
                VkBuffer vertexBuffers[] = {mesh->vertexBuffer};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
            }
            if (mesh && mesh->indexBuffer)
                vkCmdBindIndexBuffer(commandBuffer, mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT16);
            const vk::ScenePushConstantsVK sceneConstants = vk::BuildIdentityScenePushConstants();
            vkCmdPushConstants(commandBuffer, _scenePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                               vk::kScenePushConstantsSize, &sceneConstants);
            vkCmdDrawIndexed(commandBuffer, kSceneQuadIndexCount, 1, 0, 0, 0);
        }
        RecordScreenDraws(commandBuffer);
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
    if (_scenePipeline)
    {
        vkDestroyPipeline(_device, _scenePipeline, nullptr);
        _scenePipeline = VK_NULL_HANDLE;
    }
    if (_bootstrapPipeline)
    {
        vkDestroyPipeline(_device, _bootstrapPipeline, nullptr);
        _bootstrapPipeline = VK_NULL_HANDLE;
    }
    if (_screenPipeline)
    {
        vkDestroyPipeline(_device, _screenPipeline, nullptr);
        _screenPipeline = VK_NULL_HANDLE;
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

    DestroyDepthResources();

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

void EngineVK::DestroyFrameConstantsBuffer()
{
    vk::DestroyBuffer(_device, _frameConstantsBuffer);
}

void EngineVK::DestroyDrawConstantsBuffer()
{
    vk::DestroyBuffer(_device, _drawConstantsBuffer);
    _drawConstantsCapacity = 0;
}

void EngineVK::DestroyBootstrapVertexBuffer()
{
    vk::DestroyBuffer(_device, _bootstrapVertexBuffer);
}

void EngineVK::DestroyBootstrapIndexBuffer()
{
    vk::DestroyBuffer(_device, _bootstrapIndexBuffer);
}

void EngineVK::DestroySceneVertexBuffer()
{
    vk::DestroyBuffer(_device, _sceneVertexBuffer);
}

void EngineVK::DestroySceneIndexBuffer()
{
    vk::DestroyBuffer(_device, _sceneIndexBuffer);
}

void EngineVK::DestroyScenePipelineLayout()
{
    _scenePipelineCache.Destroy(_device);

    if (_sceneFragmentModule)
    {
        vkDestroyShaderModule(_device, _sceneFragmentModule, nullptr);
        _sceneFragmentModule = VK_NULL_HANDLE;
    }
    if (_sceneVertexModule)
    {
        vkDestroyShaderModule(_device, _sceneVertexModule, nullptr);
        _sceneVertexModule = VK_NULL_HANDLE;
    }
    if (_scenePipelineLayout)
    {
        vkDestroyPipelineLayout(_device, _scenePipelineLayout, nullptr);
        _scenePipelineLayout = VK_NULL_HANDLE;
    }
}

void EngineVK::DestroyScreenPipeline()
{
    if (_device && _screenPipeline)
    {
        vkDestroyPipeline(_device, _screenPipeline, nullptr);
        _screenPipeline = VK_NULL_HANDLE;
    }
}

void EngineVK::DestroyScreenPipelineLayout()
{
    if (_device)
    {
        if (_screenFragmentModule)
        {
            vkDestroyShaderModule(_device, _screenFragmentModule, nullptr);
            _screenFragmentModule = VK_NULL_HANDLE;
        }
        if (_screenVertexModule)
        {
            vkDestroyShaderModule(_device, _screenVertexModule, nullptr);
            _screenVertexModule = VK_NULL_HANDLE;
        }
        if (_screenPipelineLayout)
        {
            vkDestroyPipelineLayout(_device, _screenPipelineLayout, nullptr);
            _screenPipelineLayout = VK_NULL_HANDLE;
        }
    }
}

void EngineVK::DestroyScreenDescriptorResources()
{
    if (_device)
    {
        if (_screenDescriptorSetLayout)
        {
            vkDestroyDescriptorSetLayout(_device, _screenDescriptorSetLayout, nullptr);
            _screenDescriptorSetLayout = VK_NULL_HANDLE;
        }
    }
}

void EngineVK::DestroyScreenVertexBuffer()
{
    vk::DestroyBuffer(_device, _screenVertexBuffer);
    vk::DestroyBuffer(_device, _screenIndexBuffer);
    _screenVertexCapacity = 0;
    _screenIndexCapacity = 0;
}

bool EngineVK::RecreateSwapchain()
{
    if (!_device)
        return false;

    vkDeviceWaitIdle(_device);
    DestroySwapchain();
    const bool recreated =
        CreateSwapchain() && CreateBootstrapPipeline() && CreateScenePipeline() && CreateScreenPipeline() && CreateSyncObjects();
    _swapchainDirty = !recreated;
    return recreated;
}

void EngineVK::PresentBootstrapFrame()
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

    if (!RecordBootstrapCommand(imageIndex))
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
        LOG_INFO(Graphics, "Vulkan: bootstrap-present completed");
    }
}

void EngineVK::Shutdown()
{
    // Destroy the texture bank before the device so GPU images are freed first.
    delete _textBank;
    _textBank = nullptr;
    _fallbackWhiteTexture = nullptr;

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
        DestroyScenePipelineLayout();
        DestroyFrameDescriptorResources();
        DestroyTextureDescriptorResources();
        DestroyFrameConstantsBuffer();
        DestroyDrawConstantsBuffer();
        DestroyBootstrapVertexBuffer();
        DestroyBootstrapIndexBuffer();
        // Clear the mesh registry before destroying the scene buffers it
        // references (the registry holds non-owning VkBuffer handles).
        _meshRegistry.Clear();
        _bootstrapMeshId = 0;
        DestroySceneVertexBuffer();
        DestroySceneIndexBuffer();
        DestroyScreenPipeline();
        DestroyScreenPipelineLayout();
        DestroyScreenDescriptorResources();
        DestroyScreenVertexBuffer();
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

namespace
{
std::uint32_t GetOrCreateTextureResourceId(Texture* tex)
{
    if (!tex)
        return TextureVK::kFallbackResourceId;
    // If this is a real TextureVK, return its registered resource ID.
    if (auto* tvk = dynamic_cast<TextureVK*>(tex))
        return tvk->GetResourceId();
    // Fallback for dummy/unknown textures — assign a stable ID per pointer.
    static std::unordered_map<Texture*, std::uint32_t> s_texToId;
    static std::uint32_t s_nextId = TextureVK::kFallbackResourceId + 1;
    auto it = s_texToId.find(tex);
    if (it != s_texToId.end())
        return it->second;
    std::uint32_t id = s_nextId++;
    s_texToId[tex] = id;
    return id;
}
} // namespace

AbstractTextBank* EngineVK::TextBank()
{
    return _textBank;
}

VkPipeline EngineVK::GetOrCreateScenePipeline(const render::RenderPassDescriptor& desc)
{
    return _scenePipelineCache.Get(desc);
}

void EngineVK::RegisterTexture(TextureVK* tex)
{
    if (tex)
        _textureRegistry[tex->GetResourceId()] = tex;
}

void EngineVK::UnregisterTexture(TextureVK* tex)
{
    if (tex)
        _textureRegistry.erase(tex->GetResourceId());
}

TextureVK* EngineVK::ResolveTexture(std::uint32_t id) const
{
    auto it = _textureRegistry.find(id);
    if (it != _textureRegistry.end())
        return it->second;
    return nullptr;
}



void EngineVK::InitDraw(bool clear, PackedColor color)
{
    Engine::InitDraw(clear, color);
    _drawItems.clear();
    _currentDrawItem = DrawItem{};
    _screenVertices.clear();
    _screenIndices.clear();
    _screenBatches.clear();
}

VertexBuffer* EngineVK::CreateVertexBuffer(const Shape& src, VBType type)
{
    auto* buf = new VertexBufferVK;
    if (buf->Init(*this, src, type))
        return buf;
    delete buf;
    return nullptr;
}

void EngineVK::DrawSectionTL(const Shape& sMesh, int beg, int end)
{
    auto* buf = static_cast<VertexBufferVK*>(sMesh.GetVertexBuffer());
    if (!buf || buf->_sections.empty())
        return;

    PoseidonAssert(end > beg);
    PoseidonAssert(end <= static_cast<int>(buf->_sections.size()));

    const VBSectionInfoVK& siBeg = buf->_sections[beg];
    const VBSectionInfoVK& siEnd = buf->_sections[end - 1];

    std::uint32_t indexCount = siEnd.end - siBeg.beg;
    if (indexCount <= 0)
        return;

    DrawItem item = _currentDrawItem;
    item.isTLDraw = true;
    item.sectionBegin = beg;
    item.sectionEnd = end;
    item.firstIndex = static_cast<int>(siBeg.beg);
    item.indexCount = static_cast<int>(indexCount);
    item.vertexBuffer = buf;
    item.backendMeshResourceId = buf->GetMeshResourceId();
    item.backendTexture1ResourceId = _lastTexture1ResourceId;
    item.passId = SpecToPassId(item.specFlags);
    _drawItems.push_back(item);
}

void EngineVK::PrepareTriangleTL(const MipInfo& mip, const Poseidon::render::LegacySpec& spec)
{
    _currentDrawItem.backendTextureResourceId = GetOrCreateTextureResourceId(mip._texture);
    _currentDrawItem.specFlags = spec;
}

void EngineVK::PrepareMeshTL(const LightList& lights, const Matrix4& modelToWorld, const Poseidon::render::LegacySpec& spec)
{
    GfxMatrix worldMatrix;
    ConvertMatrix(worldMatrix, modelToWorld);
    
    // Camera-relative rendering
    Vector3 camPos = VZero;
    if (GScene && GScene->GetCamera())
        camPos = GScene->GetCamera()->Position();
        
    worldMatrix._41 -= static_cast<float>(camPos.X());
    worldMatrix._42 -= static_cast<float>(camPos.Y());
    worldMatrix._43 -= static_cast<float>(camPos.Z());

    _currentDrawItem = DrawItem{};
    _currentDrawItem.worldMatrix = worldMatrix;
    _currentDrawItem.specFlags = spec;
    _currentDrawItem.bias = GetBias();
}

void EngineVK::Draw2D(const Draw2DPars& pars, const Rect2DAbs& rect, const Rect2DAbs& clip)
{
    PoseidonAssert(pars.mip.IsOK());
    if (!pars.mip.IsOK())
    {
        return;
    }

    if (!_device)
    {
        return;
    }

    float xBeg = rect.x, xEnd = xBeg + rect.w;
    float yBeg = rect.y, yEnd = yBeg + rect.h;

    float uBeg = 0;
    float vBeg = 0;
    float uEnd = 1;
    float vEnd = 1;

    float xc = floatMax(clip.x, 0);
    float yc = floatMax(clip.y, 0);
    float xec = floatMin(clip.x + clip.w, _width);
    float yec = floatMin(clip.y + clip.h, _height);

    if (xBeg < xc)
    {
        uBeg = (xc - xBeg) / rect.w;
        xBeg = xc;
    }
    if (xEnd > xec)
    {
        uEnd = 1 - (xEnd - xec) / rect.w;
        xEnd = xec;
    }
    if (yBeg < yc)
    {
        vBeg = (yc - yBeg) / rect.h;
        yBeg = yc;
    }
    if (yEnd > yec)
    {
        vEnd = 1 - (yEnd - yec) / rect.h;
        yEnd = yec;
    }

    if (xBeg >= xEnd || yBeg >= yEnd)
    {
        return;
    }

    TLVertex pos[4];
    pos[0].rhw = 1;
    pos[0].color = pars.colorTL;
    pos[0].specular = PackedColor(0xff000000);
    pos[0].pos[2] = 0.5;

    pos[1].rhw = 1;
    pos[1].color = pars.colorTR;
    pos[1].specular = PackedColor(0xff000000);
    pos[1].pos[2] = 0.5;

    pos[2].rhw = 1;
    pos[2].color = pars.colorBR;
    pos[2].specular = PackedColor(0xff000000);
    pos[2].pos[2] = 0.5;

    pos[3].rhw = 1;
    pos[3].color = pars.colorBL;
    pos[3].specular = PackedColor(0xff000000);
    pos[3].pos[2] = 0.5;

    float uTL = pars.uTL + uBeg * (pars.uTR - pars.uTL) + vBeg * (pars.uBL - pars.uTL);
    float uTR = pars.uTL + uEnd * (pars.uTR - pars.uTL) + vBeg * (pars.uBL - pars.uTL);
    float uBL = pars.uTL + uBeg * (pars.uTR - pars.uTL) + vEnd * (pars.uBL - pars.uTL);
    float uBR = pars.uTL + uEnd * (pars.uTR - pars.uTL) + vEnd * (pars.uBL - pars.uTL);

    float vTL = pars.vTL + uBeg * (pars.vTR - pars.vTL) + vBeg * (pars.vBL - pars.vTL);
    float vTR = pars.vTL + uEnd * (pars.vTR - pars.vTL) + vBeg * (pars.vBL - pars.vTL);
    float vBL = pars.vTL + uBeg * (pars.vTR - pars.vTL) + vEnd * (pars.vBL - pars.vTL);
    float vBR = pars.vTL + uEnd * (pars.vTR - pars.vTL) + vEnd * (pars.vBL - pars.vTL);

    pos[0].pos[0] = xBeg, pos[0].pos[1] = yBeg;
    pos[1].pos[0] = xEnd, pos[1].pos[1] = yBeg;
    pos[2].pos[0] = xEnd, pos[2].pos[1] = yEnd;
    pos[3].pos[0] = xBeg, pos[3].pos[1] = yEnd;
    pos[0].t0.u = uTL, pos[0].t0.v = vTL;
    pos[1].t0.u = uTR, pos[1].t0.v = vTR;
    pos[2].t0.u = uBR, pos[2].t0.v = vBR;
    pos[3].t0.u = uBL, pos[3].t0.v = vBL;

    std::uint32_t textureId = GetOrCreateTextureResourceId(pars.mip._texture);
    PushScreenQuad(pos, textureId);
}

void EngineVK::DrawPoly(const MipInfo& mip, const Vertex2DAbs* vertices, int n, const Rect2DAbs& clipRect,
                        int specFlags)
{
    if (!_device)
    {
        return;
    }

    const int maxN = 32;

    ClipFlags orClip = 0;
    ClipFlags andClip = ClipAll;
    ClipFlags clipV[maxN];
    for (int i = 0; i < n; i++)
    {
        const Vertex2DAbs& vs = vertices[i];
        float x = vs.x;
        float y = vs.y;
        ClipFlags clip = 0;
        if (x < clipRect.x)
        {
            clip |= ClipLeft;
        }
        else if (x > clipRect.x + clipRect.w)
        {
            clip |= ClipRight;
        }
        if (y < clipRect.y)
        {
            clip |= ClipTop;
        }
        else if (y > clipRect.y + clipRect.h)
        {
            clip |= ClipBottom;
        }
        clipV[i] = clip;
        orClip |= clip;
        andClip &= clip;
    }
    if (andClip)
    {
        return;
    }
    Vertex2DAbs clippedVertices1[maxN];
    Vertex2DAbs clippedVertices2[maxN];
    if (orClip)
    {
        Vertex2DAbs* free = clippedVertices1;
        Vertex2DAbs* used = clippedVertices2;
        for (int i = 0; i < n; i++)
        {
            used[i] = vertices[i];
        }
        if (orClip & ClipTop)
        {
            n = Clip2D(clipRect, free, used, n, InsideTopAbs);
            std::swap(free, used);
        }
        if (orClip & ClipBottom)
        {
            n = Clip2D(clipRect, free, used, n, InsideBottomAbs);
            std::swap(free, used);
        }
        if (orClip & ClipLeft)
        {
            n = Clip2D(clipRect, free, used, n, InsideLeftAbs);
            std::swap(free, used);
        }
        if (orClip & ClipRight)
        {
            n = Clip2D(clipRect, free, used, n, InsideRightAbs);
            std::swap(free, used);
        }
        if (n < 3)
        {
            return;
        }
        vertices = used;
    }

    if (n > maxN)
    {
        n = maxN;
        Fail("Poly: Too much vertices");
    }

    TLVertex gv[maxN];
    for (int i = 0; i < n; i++)
    {
        TLVertex* v = &gv[i];
        const Vertex2DAbs& vs = vertices[i];
        v->pos[0] = vs.x;
        v->pos[1] = vs.y;
        v->pos[2] = vs.z;
        v->rhw = vs.w;
        v->color = vs.color;
        v->specular = PackedColor(0xff000000);
        v->t0.u = vs.u;
        v->t0.v = vs.v;
    }

    std::uint32_t textureId = GetOrCreateTextureResourceId(mip._texture);
    
    std::size_t baseIndex = _screenVertices.size();
    for (int i = 0; i < n; i++)
    {
        _screenVertices.push_back(gv[i]);
    }

    std::uint32_t indexCount = 0;
    for (int i = 1; i < n - 1; ++i)
    {
        _screenIndices.push_back(static_cast<std::uint16_t>(baseIndex + 0));
        _screenIndices.push_back(static_cast<std::uint16_t>(baseIndex + i));
        _screenIndices.push_back(static_cast<std::uint16_t>(baseIndex + i + 1));
        indexCount += 3;
    }

    if (!_screenBatches.empty() && _screenBatches.back().textureId == textureId)
    {
        _screenBatches.back().indexCount += indexCount;
    }
    else
    {
        ScreenBatchVK batch;
        batch.textureId = textureId;
        batch.indexCount = indexCount;
        _screenBatches.push_back(batch);
    }
}

void EngineVK::DrawPoly(const MipInfo& mip, const Vertex2DPixel* vertices, int n, const Rect2DPixel& clipRect,
                        int specFlags)
{
    if (!_device)
    {
        return;
    }

    const int maxN = 32;

    ClipFlags orClip = 0;
    ClipFlags andClip = ClipAll;
    ClipFlags clipV[maxN];
    for (int i = 0; i < n; i++)
    {
        const Vertex2DPixel& vs = vertices[i];
        float x = vs.x;
        float y = vs.y;
        ClipFlags clip = 0;
        if (x < clipRect.x)
        {
            clip |= ClipLeft;
        }
        else if (x > clipRect.x + clipRect.w)
        {
            clip |= ClipRight;
        }
        if (y < clipRect.y)
        {
            clip |= ClipTop;
        }
        else if (y > clipRect.y + clipRect.h)
        {
            clip |= ClipBottom;
        }
        clipV[i] = clip;
        orClip |= clip;
        andClip &= clip;
    }
    if (andClip)
    {
        return;
    }
    Vertex2DPixel clippedVertices1[maxN];
    Vertex2DPixel clippedVertices2[maxN];
    if (orClip)
    {
        Vertex2DPixel* free = clippedVertices1;
        Vertex2DPixel* used = clippedVertices2;
        for (int i = 0; i < n; i++)
        {
            used[i] = vertices[i];
        }
        if (orClip & ClipTop)
        {
            n = Clip2D(clipRect, free, used, n, InsideTopPixel);
            std::swap(free, used);
        }
        if (orClip & ClipBottom)
        {
            n = Clip2D(clipRect, free, used, n, InsideBottomPixel);
            std::swap(free, used);
        }
        if (orClip & ClipLeft)
        {
            n = Clip2D(clipRect, free, used, n, InsideLeftPixel);
            std::swap(free, used);
        }
        if (orClip & ClipRight)
        {
            n = Clip2D(clipRect, free, used, n, InsideRightPixel);
            std::swap(free, used);
        }
        if (n < 3)
        {
            return;
        }
        vertices = used;
    }

    if (n > maxN)
    {
        n = maxN;
        Fail("Poly: Too much vertices");
    }

    TLVertex gv[maxN];
    float x2d = Left2D();
    float y2d = Top2D();

    for (int i = 0; i < n; i++)
    {
        TLVertex* v = &gv[i];
        const Vertex2DPixel& vs = vertices[i];
        v->pos[0] = vs.x + x2d;
        v->pos[1] = vs.y + y2d;
        v->pos[2] = vs.z;
        v->rhw = vs.w;
        v->color = vs.color;
        v->specular = PackedColor(0xff000000);
        v->t0.u = vs.u;
        v->t0.v = vs.v;
    }

    std::uint32_t textureId = GetOrCreateTextureResourceId(mip._texture);
    
    std::size_t baseIndex = _screenVertices.size();
    for (int i = 0; i < n; i++)
    {
        _screenVertices.push_back(gv[i]);
    }

    std::uint32_t indexCount = 0;
    for (int i = 1; i < n - 1; ++i)
    {
        _screenIndices.push_back(static_cast<std::uint16_t>(baseIndex + 0));
        _screenIndices.push_back(static_cast<std::uint16_t>(baseIndex + i));
        _screenIndices.push_back(static_cast<std::uint16_t>(baseIndex + i + 1));
        indexCount += 3;
    }

    if (!_screenBatches.empty() && _screenBatches.back().textureId == textureId)
    {
        _screenBatches.back().indexCount += indexCount;
    }
    else
    {
        ScreenBatchVK batch;
        batch.textureId = textureId;
        batch.indexCount = indexCount;
        _screenBatches.push_back(batch);
    }
}

void EngineVK::DrawLine(const Line2DAbs& line, PackedColor c0, PackedColor c1, const Rect2DAbs& clip)
{
    float x0 = line.beg.x;
    float y0 = line.beg.y;
    float x1 = line.end.x;
    float y1 = line.end.y;
    
    Texture* tex = GPreloadedTextures.New(TextureLine);
    const MipInfo& mip = TextBank()->UseMipmap(tex, 1, 1);

    int specFlags = NoZBuf | IsAlpha | ClampU | ClampV | IsAlphaFog;
    float dx = x1 - x0;
    float dy = y1 - y0;
    float dSize2 = dx * dx + dy * dy;
    float invDSize = dSize2 > 0 ? InvSqrt(dSize2) : 1;

    float pdx = +dy * invDSize, pdy = -dx * invDSize;
    float w = 3.0f;
    x0 -= pdx * (w * 0.5f);
    x1 -= pdx * (w * 0.5f);
    y0 -= pdy * (w * 0.5f);
    y1 -= pdy * (w * 0.5f);
    float x0Side = x0 + pdx * w, y0Side = y0 + pdy * w;
    float x1Side = x1 + pdx * w, y1Side = y1 + pdy * w;

    Vertex2DAbs vertices[4];
    float off = 0.0f;
    vertices[0].x = x0 - off;
    vertices[0].y = y0 - off;
    vertices[0].u = 0.0f;
    vertices[0].v = 0.25f;
    vertices[0].color = c0;

    vertices[1].x = x0Side - off;
    vertices[1].y = y0Side - off;
    vertices[1].u = 0.0f;
    vertices[1].v = 1.0f;
    vertices[1].color = c0;

    vertices[3].x = x1 - off;
    vertices[3].y = y1 - off;
    vertices[3].u = 0.1f;
    vertices[3].v = 0.25f;
    vertices[3].color = c1;

    vertices[2].x = x1Side - off;
    vertices[2].y = y1Side - off;
    vertices[2].u = 0.1f;
    vertices[2].v = 1.0f;
    vertices[2].color = c1;

    DrawPoly(mip, vertices, 4, clip, specFlags);
}

void EngineVK::PushScreenQuad(const TLVertex* quad, std::uint32_t textureId)
{
    std::size_t baseIndex = _screenVertices.size();

    for (int i = 0; i < 4; ++i)
    {
        _screenVertices.push_back(quad[i]);
    }

    _screenIndices.push_back(static_cast<std::uint16_t>(baseIndex + 0));
    _screenIndices.push_back(static_cast<std::uint16_t>(baseIndex + 1));
    _screenIndices.push_back(static_cast<std::uint16_t>(baseIndex + 2));
    _screenIndices.push_back(static_cast<std::uint16_t>(baseIndex + 0));
    _screenIndices.push_back(static_cast<std::uint16_t>(baseIndex + 2));
    _screenIndices.push_back(static_cast<std::uint16_t>(baseIndex + 3));

    const std::uint32_t indexCount = 6;
    if (!_screenBatches.empty() && _screenBatches.back().textureId == textureId)
    {
        _screenBatches.back().indexCount += indexCount;
    }
    else
    {
        ScreenBatchVK batch;
        batch.textureId = textureId;
        batch.indexCount = indexCount;
        _screenBatches.push_back(batch);
    }
}

void EngineVK::RecordScreenDraws(VkCommandBuffer commandBuffer)
{
    if (_screenVertices.empty() || _screenIndices.empty())
        return;

    if (!EnsureScreenVertexBufferCapacity(_screenVertices.size(), _screenIndices.size()))
        return;

    vk::UploadMappedBuffer(_screenVertexBuffer, _screenVertices.data(), _screenVertices.size() * sizeof(TLVertex));
    vk::UploadMappedBuffer(_screenIndexBuffer, _screenIndices.data(), _screenIndices.size() * sizeof(std::uint16_t));

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _screenPipeline);

    VkBuffer vertexBuffers[] = {_screenVertexBuffer.buffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, _screenIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);

    vk::ScreenPushConstantsVK constants = vk::BuildScreenPushConstants(_width, _height);
    vkCmdPushConstants(commandBuffer, _screenPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       vk::kScreenPushConstantsSize, &constants);

    std::uint32_t indexOffset = 0;
    VkDescriptorSet lastBoundTexDescriptorSet = VK_NULL_HANDLE;

    for (const ScreenBatchVK& batch : _screenBatches)
    {
        if (batch.indexCount == 0)
            continue;

        VkDescriptorSet texDescriptorSet = VK_NULL_HANDLE;
        if (batch.textureId != 0)
        {
            TextureVK* tex = ResolveTexture(batch.textureId);
            if (tex)
                texDescriptorSet = tex->GetDescriptorSet();
        }
        if (texDescriptorSet == VK_NULL_HANDLE && _fallbackWhiteTexture)
        {
            texDescriptorSet = _fallbackWhiteTexture->GetDescriptorSet();
        }

        if (texDescriptorSet != VK_NULL_HANDLE && texDescriptorSet != lastBoundTexDescriptorSet)
        {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _screenPipelineLayout, 0, 1,
                                    &texDescriptorSet, 0, nullptr);
            lastBoundTexDescriptorSet = texDescriptorSet;
        }

        vkCmdDrawIndexed(commandBuffer, batch.indexCount, 1, indexOffset, 0, 0);
        indexOffset += batch.indexCount;
    }
}

} // namespace Poseidon
