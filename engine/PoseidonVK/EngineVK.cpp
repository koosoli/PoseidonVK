#include <PoseidonVK/EngineVK.hpp>
#include <PoseidonVK/MeshBuilderVK.hpp>

#include <PoseidonVK/TextBankVK.hpp>
#include <PoseidonVK/TextureVK.hpp>
#include <PoseidonVK/VertexBufferVK.hpp>
#include <PoseidonVK/BootstrapPushConstantsVK.hpp>
#include <Poseidon/Graphics/Core/MatrixConversion.hpp>
#include <Poseidon/Graphics/Rendering/BuildRenderPassDescriptor.hpp>
#include <Poseidon/Graphics/Rendering/Shape/ClipShape.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Graphics/Rendering/Primitives/Clip2D.hpp>
#include <Poseidon/Graphics/Textures/TexturePreload.hpp>
#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/Core/Global.hpp>
#include <unordered_map>
#include <PoseidonVK/BufferVK.hpp>
#include <PoseidonVK/CloudConstantsVK.hpp>
#include <PoseidonVK/DescriptorBindingsVK.hpp>
#include <PoseidonVK/DrawConstantsVK.hpp>
#include <PoseidonVK/GpuSceneVK.hpp>
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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <tuple>
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

struct WorldCompositePushConstants
{
    float exposure;
    uint32_t hdrEnabled;
    uint32_t exposureHistoryValid;
};

static_assert(sizeof(WorldCompositePushConstants) == 12);

struct EyeAdaptationPushConstants
{
    float baseExposure;
    uint32_t historyValid;
};

static_assert(sizeof(EyeAdaptationPushConstants) == 8);

enum class SceneGroup : std::uint32_t
{
    Terrain,
    Opaque,
    Cutout,
    Other,
    Transparent,
    WorldLate,
    Present,
};

constexpr std::uint32_t kCloudVolumeWidth = 96;
constexpr std::uint32_t kCloudVolumeHeight = 48;
constexpr std::uint32_t kCloudVolumeDepth = 96;
constexpr std::uint32_t kCloudLightVolumeWidth = 48;
constexpr std::uint32_t kCloudLightVolumeHeight = 24;
constexpr std::uint32_t kCloudLightVolumeDepth = 48;

render::RenderPassDescriptor ScreenDescriptorFromLegacySpec(int specFlags)
{
    render::BuildContext context;
    context.isIn3DPass = false;
    return render::BuildRenderPassDescriptor(render::SplitLegacy(specFlags), context);
}

bool SameScreenDescriptor(const render::RenderPassDescriptor& a, const render::RenderPassDescriptor& b)
{
    return a.pass == b.pass && a.depth == b.depth && a.blend == b.blend && a.fog == b.fog &&
           a.cull == b.cull && a.frontFace == b.frontFace && a.alpha == b.alpha &&
           a.alphaRef == b.alphaRef && a.surface == b.surface &&
           a.sampler.filter == b.sampler.filter && a.sampler.clampU == b.sampler.clampU &&
           a.sampler.clampV == b.sampler.clampV;
}

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
constexpr const char kSceneCullComputeShader[] =
#include <PoseidonVK/Shaders/scene_cull.comp.glsl.hpp>
    ;
constexpr const char kProceduralSkyVertexShader[] =
#include <PoseidonVK/Shaders/procedural_sky.vert.glsl.hpp>
    ;
constexpr const char kProceduralSkyFragmentShader[] =
#include <PoseidonVK/Shaders/procedural_sky.frag.glsl.hpp>
    ;
constexpr const char kSkyMapBakeVertexShader[] =
#include <PoseidonVK/Shaders/sky_map_bake.vert.glsl.hpp>
    ;
constexpr const char kSkyMapBakeFragmentShader[] =
#include <PoseidonVK/Shaders/sky_map_bake.frag.glsl.hpp>
    ;
constexpr const char kVolumetricCloudVertexShader[] =
#include <PoseidonVK/Shaders/volumetric_clouds.vert.glsl.hpp>
    ;
constexpr const char kVolumetricCloudFragmentShader[] =
#include <PoseidonVK/Shaders/volumetric_clouds.frag.glsl.hpp>
    ;
constexpr const char kCloudDensityErosionComputeShader[] =
#include <PoseidonVK/Shaders/cloud_density_erosion.comp.glsl.hpp>
    ;
constexpr const char kCloudDistanceFieldComputeShader[] =
#include <PoseidonVK/Shaders/cloud_distance_field.comp.glsl.hpp>
    ;
constexpr const char kCloudLightMapComputeShader[] =
#include <PoseidonVK/Shaders/cloud_light_map.comp.glsl.hpp>
    ;
constexpr const char kCloudTemporalFragmentShader[] =
#include <PoseidonVK/Shaders/cloud_temporal.frag.glsl.hpp>
    ;
constexpr const char kCloudCompositeFragmentShader[] =
#include <PoseidonVK/Shaders/cloud_composite.frag.glsl.hpp>
    ;
constexpr const char kWorldCompositeVertexShader[] =
#include <PoseidonVK/Shaders/world_composite.vert.glsl.hpp>
    ;
constexpr const char kWorldCompositeFragmentShader[] =
#include <PoseidonVK/Shaders/world_composite.frag.glsl.hpp>
    ;
constexpr const char kEyeAdaptationFragmentShader[] =
#include <PoseidonVK/Shaders/eye_adaptation.frag.glsl.hpp>
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

    return std::any_of(extensions.begin(), extensions.end(), [requiredExtension](const VkExtensionProperties& extension)
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

    return std::any_of(layers.begin(), layers.end(), [requiredLayer](const VkLayerProperties& layer)
                       { return std::strcmp(layer.layerName, requiredLayer) == 0; });
}

bool HasDeviceExtension(VkPhysicalDevice device, const char* requiredExtension)
{
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS)
        return false;

    std::vector<VkExtensionProperties> extensions(count);
    if (count > 0 && vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()) != VK_SUCCESS)
        return false;

    return std::any_of(extensions.begin(), extensions.end(), [requiredExtension](const VkExtensionProperties& extension)
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
                                                   const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void*)
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
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
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
    // Use UNORM swapchain to avoid double-gamma (textures are already sRGB).
    // A partial gamma boost is applied in the fragment shaders to compensate.
    for (const VkSurfaceFormatKHR& format : formats)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
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
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);

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

bool EngineVK::ProceduralSkyActive() const
{
    return _proceduralSkyEnabled && _proceduralSkyPipeline != VK_NULL_HANDLE && _skyMapBakePipeline != VK_NULL_HANDLE &&
           _skyMapDescriptorSet != VK_NULL_HANDLE && _skyMap.image != VK_NULL_HANDLE && _hasFrameConstants;
}

bool EngineVK::WorldCompositionActive() const
{
    return _hdrEnabled || _volumetricCloudsEnabled;
}

bool EngineVK::Initialize(int width, int height, bool windowed, int bitsPerPixel, const std::string& displayMode)
{
    _bitsPerPixel = bitsPerPixel > 0 ? bitsPerPixel : 32;
    // The cached sky is the Vulkan sky path; retain the environment switch as
    // an explicit compatibility opt-out rather than making it experimental.
    _proceduralSkyEnabled = true;
    // The depth-aware cloud path is the normal Vulkan sky path. Keep the
    // environment switch for diagnostics and compatibility testing.
    _volumetricCloudsEnabled = true;
    if (const char* value = std::getenv("POSEIDON_VK_PROCEDURAL_SKY"))
        _proceduralSkyEnabled = std::strcmp(value, "0") != 0;
    if (const char* value = std::getenv("POSEIDON_VK_VOLUMETRIC_CLOUDS"))
        _volumetricCloudsEnabled = std::strcmp(value, "0") != 0;
    if (const char* value = std::getenv("POSEIDON_VK_HDR"))
        _hdrEnabled = std::strcmp(value, "0") != 0;
    if (const char* value = std::getenv("POSEIDON_VK_TEMPORAL_EXPOSURE"))
        _temporalExposureEnabled = std::strcmp(value, "0") != 0;
    if (const char* value = std::getenv("POSEIDON_VK_CSM"))
        _shadowTuning.enabled = std::strcmp(value, "0") != 0;
    // This is deliberately an exact opt-in rather than the usual non-zero
    // environment convention. It permits only the detail preview
    // fallback below; all map, layer, CSM, and derived-receiver validation
    // remains mandatory.
    if (const char* value = std::getenv("POSEIDON_VK_TERRAIN_EXPERIMENT"))
        _terrainPreviewExperiment = std::strcmp(value, "1") == 0;

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
         !CreateGpuTimingResources() || !CreateFrameConstantsBuffer() ||
          (_volumetricCloudsEnabled && !CreateCloudConstantsBuffer()) || !CreateBootstrapVertexBuffer() || !CreateBootstrapIndexBuffer() ||
         !CreateSceneVertexBuffer() || !CreateSceneIndexBuffer() || !EnsureDrawConstantsBufferCapacity(1) ||
          !CreateFrameDescriptorLayout() || !CreateTextureDescriptorLayout() || !CreateSkyMapDescriptorLayout() ||
            (_volumetricCloudsEnabled && (!CreateVolumetricCloudDescriptorLayout() || !CreateCloudComputeDescriptorLayouts())) ||
           (WorldCompositionActive() && !CreateWorldCompositeDescriptorLayout()) || !CreatePipelineLayout() ||
           (_temporalExposureEnabled && !CreateEyeAdaptationDescriptorLayout()) ||
            !CreateTextureDescriptorPool() || !CreateScenePipelineLayout() || !CreateFrameDescriptorSet() ||
            !CreateGpuSceneResources() ||
             !CreateSkyMapPipelineLayout() ||
            (_volumetricCloudsEnabled && (!CreateVolumetricCloudPipelineLayout() || !CreateCloudComputePipelineLayouts())) ||
             (WorldCompositionActive() && !CreateWorldCompositePipelineLayout()) ||
              (_temporalExposureEnabled && !CreateEyeAdaptationPipelineLayout()) || !CreateCommandPool())
    {
        Shutdown();
        return false;
    }

    // TerrainVK owns immutable map resources and its one-shot map uploads use
    // this pool. It is deliberately optional: failure leaves the established
    // segment path active instead of making Vulkan startup depend on a raster
    // path that is not installed yet.
    if (_terrainDescriptorIndexingSupported &&
        !_terrainVk.Initialize(_physicalDevice, _device, _commandPool, _graphicsQueue))
    {
        LOG_WARN(Graphics, "Vulkan terrain telemetry: mode=legacy-segments reason=TerrainVK-initialization-failed");
    }

    if (!CreateSkyMapResources() || !CreateSkyMapDescriptorSet() ||
           !CreateSwapchain() ||
             (_volumetricCloudsEnabled && (!CreateVolumetricCloudDescriptorSet() || !CreateCloudComputeDescriptorSets())) ||
           (_temporalExposureEnabled && !CreateEyeAdaptationDescriptorSet()) ||
           (WorldCompositionActive() && !CreateWorldCompositeDescriptorSet()) ||
          !CreateBootstrapPipeline() || !CreateScenePipeline() || !CreateProceduralSkyPipeline() ||
            (_volumetricCloudsEnabled && (!CreateVolumetricCloudPipeline() || !CreateCloudComputePipelines())) ||
           (_temporalExposureEnabled && !CreateEyeAdaptationPipeline()) ||
           (WorldCompositionActive() && !CreateWorldCompositePipeline()) ||
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
    bootstrapMesh.localBoundsCenter[0] = 0.425f;
    bootstrapMesh.localBoundsCenter[1] = 0.0f;
    bootstrapMesh.localBoundsCenter[2] = 0.0f;
    bootstrapMesh.localBoundsRadius = 0.48f;
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
    LOG_WARN(Graphics, "Vulkan: scene raster parity is in progress; water and some legacy clipped geometry may differ "
                        "from the GL33 renderer.");
    if (_terrainPreviewExperiment)
    {
        LOG_WARN(Graphics,
                  "Vulkan terrain telemetry: WARN label=TerrainVK-experimental-preview visual-parity=incomplete "
                  "missing-inputs=authored-detail-descriptor "
                  "fallback=deterministic-neutral-detail-only");
    }
    // Resource-only capture keeps the immutable map and its derived masks warm
    // under the legacy receiver. The dedicated path is installed only after the
    // authored detail source has completed upload and all descriptors
    // can be validated against the current render pass.
    if (WantsDedicatedTerrainOpaque())
        LOG_INFO(Graphics, "Vulkan terrain telemetry: mode=dedicated-cdlod nodes={} batches={} legacyDraws=0 "
                           "descriptor-indexing=true params-buffer={}",
                 _terrainVk.VisibleNodes().size(), _terrainVk.VisibleNodes().empty() ? 0u : 1u,
                 _terrainVk.ParamsBuffer().buffer != VK_NULL_HANDLE);
    else if (WantsTerrainOpaqueCapture())
        LOG_INFO(Graphics, "Vulkan terrain telemetry: mode=legacy-segments nodes={} batches=0 legacyDraws=unknown "
                           "terrain-resources=staged descriptor-indexing=true reason={}",
                 _terrainVk.VisibleNodes().size(), _terrainVisualInputReason);
    else
        LOG_WARN(Graphics, "Vulkan terrain telemetry: mode=legacy-segments reason={}",
                 _terrainDescriptorIndexingSupported ? "TerrainVK-not-ready" : "descriptor-indexing-unsupported");
    if (_proceduralSkyEnabled)
        LOG_INFO(Graphics, "Vulkan: HDR cached sky map is enabled");
    if (_volumetricCloudsEnabled)
        LOG_INFO(Graphics, "Vulkan: temporal fixed-world volumetric clouds are enabled");
    if (_hdrEnabled)
        LOG_INFO(Graphics, "Vulkan: HDR world composition is enabled (R16G16B16A16_SFLOAT, exposure={})", _hdrExposure);
    if (_shadowTuning.enabled)
        LOG_INFO(Graphics, "Vulkan: cascaded shadow maps are enabled ({} cascades, {}x{})", _shadowTuning.cascadeCount,
                 _shadowTuning.resolution, _shadowTuning.resolution);

    _textBank = new TextBankVK(*this);

    // Create and register fallback grey texture (neutral grey for missing textures)
    uint32_t fallbackPixel = 0xFF808080;
    _fallbackWhiteTexture = static_cast<TextureVK*>(_textBank->CreateDynamic(1, 1, &fallbackPixel, 4, false));
    if (_fallbackWhiteTexture)
    {
        UnregisterTexture(_fallbackWhiteTexture);
        _fallbackWhiteTexture->_resourceId = TextureVK::kFallbackResourceId;
        RegisterTexture(_fallbackWhiteTexture);
    }

    // Allocate dummy shadow map resources so that we always have a valid VK_IMAGE_VIEW_TYPE_2D_ARRAY image view bound
    // to the frame descriptor set binding 2, preventing Vulkan validation errors.
    EnsureShadowResources(16, 4);

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
    const auto submitStarted = std::chrono::steady_clock::now();
    _lastFrameConstants = vk::BuildFrameConstants(frame);
    _lastFrameConstants.grassParams[0] = _grassParam[0];
    _lastFrameConstants.grassParams[1] = _grassParam[1];
    _lastFrameConstants.grassParams[2] = _grassParam[2];
    _lastFrameConstants.grassParams[3] = _grassParam[3];
    _lastFrameConstants.time[0] = Glob.time.toFloat();
    // Cloud displacement must advance with simulation weather, not wall-clock
    // presentation time. Pauses and replay therefore preserve the cloud field.
    _lastFrameConstants.time[1] = frame.atmosphere.cloudTime;
    _lastFrameConstants.wind[3] = _volumetricCloudsEnabled ? 1.0f : 0.0f;
    _lastFrameConstants.lightingParams[3] = _nightEye;
    UpdateCloudConstants();
    UpdateSkyMapInvalidation();

    // Keep the resource/upload and CDLOD selection side of TerrainVK exercised
    // from the immutable plan. Raster submission remains gated off until the
    // descriptor-array material set and terrain shadow/sky-visibility passes
    // are attached; this must not silently replace the legacy receiver early.
    _terrainOpaqueInSubmittedFrame = frame.terrainOpaque.has_value();
    const render::frame::TerrainOpaque* terrainInput = frame.terrainOpaque ? &*frame.terrainOpaque
                                                                             : (_capturedTerrainOpaque ? &*_capturedTerrainOpaque
                                                                                                       : nullptr);
    if (terrainInput && _terrainVk.Ready())
    {
        if (_terrainVk.Upload(*terrainInput))
        {
            const auto& terrain = *terrainInput;
            if (UpdateTerrainLayerDescriptors(terrain))
            {
                // Detail is a dedicated terrain input, independent of the
                // native material layer array. A source that is still uploading
                // is not legal for CDLOD yet; legacy segments retain ownership
                // until its authored image is shader-readable.
                UpdateTerrainVisualDescriptors();
                _terrainVk.Select(frame.cameraPosition[0], frame.cameraPosition[1], frame.cameraPosition[2],
                                  terrain.visibleXBegin * terrain.landGrid, terrain.visibleZBegin * terrain.landGrid,
                                  terrain.visibleXEnd * terrain.landGrid, terrain.visibleZEnd * terrain.landGrid);
            }
            else
            {
                LOG_WARN(Graphics, "Vulkan terrain telemetry: descriptor update failed revision={}; raster remains disabled",
                         terrain.revision);
            }
        }
        else
        {
            LOG_WARN(Graphics,
                     "Vulkan terrain telemetry: resource upload failed revision={} invalid-indices={}; legacy renderer remains authoritative",
                      terrainInput->revision, _terrainVk.Telemetry().invalidLayerIndices);
        }
    }

    _lastDrawConstants = vk::BuildDrawConstants(frame);
    _lastSceneDrawCommands = vk::BuildSceneDrawCommands(_lastDrawConstants);
    // Submit CSM depth before the receiver command buffer is recorded.  The
    // same-queue submission in EngineVK_Shadow establishes depth-write to
    // fragment-sample visibility without a CPU queue drain.
    const auto shadowRecordStarted = std::chrono::steady_clock::now();
    RenderShadowDepthFramePlan(frame);
    _cpuShadowRecordMs = std::chrono::duration<float, std::milli>(
                             std::chrono::steady_clock::now() - shadowRecordStarted)
                             .count();
    for (auto& group : _sceneCommandGroups)
    {
        group.clear();
        group.reserve(_lastSceneDrawCommands.size());
    }
    for (std::uint32_t commandIndex = 0; commandIndex < _lastSceneDrawCommands.size(); ++commandIndex)
    {
        const vk::SceneDrawCommandVK& command = _lastSceneDrawCommands[commandIndex];
        const auto pass = static_cast<render::PassKind>(_lastDrawConstants[command.drawIndex].pass);
        // The cache-backed sky owns the background.  Do not draw the legacy
        // sky mesh after it on direct-to-swapchain frames.
        if (_proceduralSkyEnabled && pass == render::PassKind::Sky)
            continue;
        SceneGroup group = SceneGroup::Terrain;
        if (WorldCompositionActive())
        {
            switch (pass)
            {
                case render::PassKind::Sky:
                case render::PassKind::TerrainOpaque:
                    break;
                case render::PassKind::WorldOpaque:
                    group = SceneGroup::Opaque;
                    break;
                case render::PassKind::WorldCutout:
                    group = SceneGroup::Cutout;
                    break;
                case render::PassKind::SurfaceOverlay:
                case render::PassKind::WorldWater:
                case render::PassKind::WorldShadow:
                case render::PassKind::WorldLight:
                    group = SceneGroup::Other;
                    break;
                case render::PassKind::WorldTransparent:
                    // Preserve the scene's back-to-front order. Batching these
                    // with opaque overlays reorders partial-alpha house sections.
                    group = SceneGroup::Transparent;
                    break;
                case render::PassKind::CockpitOpaque:
                case render::PassKind::CockpitCutout:
                case render::PassKind::CockpitTransparent:
                case render::PassKind::ScreenSpace3D:
                    group = SceneGroup::Present;
                    break;
            }
        }
        _sceneCommandGroups[static_cast<std::uint32_t>(group)].push_back(commandIndex);
    }
    const auto gpuSceneInputStarted = std::chrono::steady_clock::now();
    BuildGpuSceneInputs();
    _cpuGpuSceneInputMs = std::chrono::duration<float, std::milli>(
                               std::chrono::steady_clock::now() - gpuSceneInputStarted)
                               .count();
    _hasFrameConstants = true;

    // Log summary once per second (every 60 frames) to avoid console spam
    {
        static int s_logFrame = 0;
        if (++s_logFrame % 60 == 1)
        {
            std::string passSummary;
            for (const auto& p : frame.passes)
            {
                if (!passSummary.empty())
                    passSummary += " ";
                passSummary += render::frame::FramePassKindName(p.kind);
                passSummary += "=" + std::to_string(p.draws.size());
            }
            LOG_INFO(Graphics,
                      "FramePlan: {} | {} draws, {} cmds, {} meshes, {} texs, gpu-scene={} instances={} batches={} count={} shadow-draws={}/{}/{}/{} of {}",
                       passSummary, _lastDrawConstants.size(), _lastSceneDrawCommands.size(), _meshRegistry.Size(),
                       _textureRegistry.size(), _gpuSceneEnabled, _gpuSceneInstances.size(), _gpuSceneBatches.size(),
                       _gpuSceneCapabilities.drawIndirectCount ? "indirect-count" : "fixed-indirect",
                       _shadowCascadeDrawCounts[0], _shadowCascadeDrawCounts[1], _shadowCascadeDrawCounts[2],
                        _shadowCascadeDrawCounts[3], _shadowResolvedDrawCount);
            const vk::TerrainVK::DescriptorTelemetry& terrainTelemetry = _terrainVk.Telemetry();
            const std::size_t legacyTerrainDraws =
                _sceneCommandGroups[static_cast<std::uint32_t>(SceneGroup::Terrain)].size();
            const bool dedicatedTerrain = _terrainOpaqueInSubmittedFrame && WantsDedicatedTerrainOpaque();
            LOG_INFO(Graphics,
                       "Vulkan terrain telemetry: mode={} nodes={} batches={} legacyDraws={} resources={} revision={} static-ready={} visual-ready={} captured={} visual-reason={} layers={}/{} capacity={} fallback={} invalid={} invalid-indices={}",
                        dedicatedTerrain ? "dedicated-cdlod" : "legacy-segments", _terrainVk.VisibleNodes().size(),
                        dedicatedTerrain && !_terrainVk.VisibleNodes().empty() ? 1u : 0u,
                        dedicatedTerrain ? 0u : static_cast<unsigned>(legacyTerrainDraws),
                        WantsTerrainOpaqueCapture() ? "staged" : "off",
                        _terrainVk.Revision(), _terrainVk.Ready(),
                        _terrainVk.VisualInputsReady(), frame.terrainOpaque.has_value() || _capturedTerrainOpaque.has_value(),
                        _terrainVisualInputReason,
                        terrainTelemetry.boundLayers, terrainTelemetry.requestedLayers,
                      terrainTelemetry.capacity, terrainTelemetry.fallbackLayers, terrainTelemetry.invalidLayers,
                      terrainTelemetry.invalidLayerIndices);
            LOG_INFO(Graphics,
                     "Vulkan CPU ms: submit={:.2f} gpu-scene-input={:.2f} shadow-record={:.2f} shadow-prepare={:.2f} shadow-secondary={:.2f} command-record={:.2f} fence-wait={:.2f}",
                     _cpuSubmitFramePlanMs, _cpuGpuSceneInputMs, _cpuShadowRecordMs, _cpuShadowPrepareMs,
                     _cpuShadowSecondaryRecordMs, _cpuCommandRecordMs, _cpuFrameFenceWaitMs);
            // Keep this audit until receiver parity is complete.  In Vulkan a
            // `DepthMode::Disabled` descriptor deliberately becomes
            // compare-ALWAYS (RenderStateVK.hpp): such a draw is allowed to
            // paint over terrain regardless of ordering.  Do not "fix" a
            // structure-over-ground report by changing terrain order or its
            // shader; first identify whether the source material explicitly
            // requested this exceptional depth state.
            constexpr std::array<const char*, 5> kSceneGroupNames = {
                "terrain", "opaque", "cutout", "other", "transparent"};
            for (std::uint32_t group = static_cast<std::uint32_t>(SceneGroup::Terrain);
                 group <= static_cast<std::uint32_t>(SceneGroup::Transparent); ++group)
            {
                std::array<std::uint32_t, 4> depthCounts = {};
                for (const std::uint32_t commandIndex : _sceneCommandGroups[group])
                {
                    const auto depth = static_cast<render::DepthMode>(
                        _lastDrawConstants[_lastSceneDrawCommands[commandIndex].drawIndex].depth);
                    ++depthCounts[static_cast<std::uint32_t>(depth)];
                }
                LOG_INFO(Graphics, "Vulkan receiver depth: group={} normal={} readonly={} disabled={} shadow={}",
                         kSceneGroupNames[group], depthCounts[static_cast<std::uint32_t>(render::DepthMode::Normal)],
                         depthCounts[static_cast<std::uint32_t>(render::DepthMode::ReadOnly)],
                         depthCounts[static_cast<std::uint32_t>(render::DepthMode::Disabled)],
                         depthCounts[static_cast<std::uint32_t>(render::DepthMode::Shadow)]);
            }
        }
    }

    UploadFrameConstants();
    if (_volumetricCloudsEnabled)
        vk::UploadMappedBuffer(_cloudConstantsBuffer, &_cloudConstants, sizeof(_cloudConstants));
    UploadDrawConstants();
    _cpuSubmitFramePlanMs = std::chrono::duration<float, std::milli>(
                                std::chrono::steady_clock::now() - submitStarted)
                                .count();
}

void EngineVK::EnableNightEye(float night)
{
    _nightEye = _nightVision ? 0.0f : std::clamp(night, 0.0f, 1.0f);
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

    // Debug builds want validation unconditionally. Release builds opt in with
    // POSEIDON_VK_VALIDATION=1: diagnosing device loss on the field requires
    // the KHRONOS layer's per-submission error reports.
#ifdef _DEBUG
    const bool wantValidation = true;
#else
    const bool wantValidation = []
    {
        const char* value = std::getenv("POSEIDON_VK_VALIDATION");
        return value && std::strcmp(value, "0") != 0;
    }();
#endif
    const bool hasValidationLayer = HasInstanceLayer(kValidationLayer);
    const bool hasDebugUtils = HasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    _debugUtilsEnabled = hasDebugUtils;
    _validationEnabled = wantValidation && hasValidationLayer && hasDebugUtils;
    if (_debugUtilsEnabled)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (wantValidation && !_validationEnabled)
        LOG_WARN(Graphics, "Vulkan: validation disabled ({}={}, {}={})", kValidationLayer,
                 hasValidationLayer ? "yes" : "no", VK_EXT_DEBUG_UTILS_EXTENSION_NAME, hasDebugUtils ? "yes" : "no");

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Poseidon";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "PoseidonVK";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    // Scene rasterization uses a negative viewport height to preserve the
    // engine's OpenGL Y convention. That is core functionality in Vulkan 1.1.
    appInfo.apiVersion = VK_API_VERSION_1_3;

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
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);
        if (props.apiVersion < VK_API_VERSION_1_3)
        {
            LOG_DEBUG(Graphics, "Vulkan: skipping device '{}' (requires Vulkan 1.3, found {}.{}.{})", props.deviceName,
                      VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
                      VK_VERSION_PATCH(props.apiVersion));
            continue;
        }

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
            // Cloud generation is recorded in the graphics command buffer so
            // its compute-to-raymarch handoff stays ordered in one submission.
            // Do not select a graphics-only family when the cloud path is on.
            if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                (!_volumetricCloudsEnabled || (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)))
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

        if (_volumetricCloudsEnabled)
        {
            VkPhysicalDeviceFeatures features{};
            vkGetPhysicalDeviceFeatures(device, &features);
            VkFormatProperties cloudFormatProperties{};
            vkGetPhysicalDeviceFormatProperties(device, VK_FORMAT_R8_UNORM, &cloudFormatProperties);
            const VkFormatFeatureFlags requiredCloudFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                                               VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
            if (!features.shaderStorageImageExtendedFormats ||
                (cloudFormatProperties.optimalTilingFeatures & requiredCloudFeatures) != requiredCloudFeatures)
            {
                LOG_DEBUG(Graphics, "Vulkan: skipping device without R8 3D storage-image cloud compute support");
                continue;
            }
        }

        _physicalDevice = device;
        _graphicsQueueFamily = graphicsFamily;
        _presentQueueFamily = presentFamily;

        VkPhysicalDeviceProperties selectedProps{};
        vkGetPhysicalDeviceProperties(device, &selectedProps);
        VkPhysicalDeviceVulkan11Features features11{};
        features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features11.pNext = &features12;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &features11;
        vkGetPhysicalDeviceFeatures2(device, &features2);
        _gpuSceneCapabilities.compute = (queueFamilies[graphicsFamily].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        _gpuSceneCapabilities.multiDrawIndirect = features2.features.multiDrawIndirect == VK_TRUE;
        _gpuSceneCapabilities.shaderDrawParameters = features11.shaderDrawParameters == VK_TRUE;
        _gpuSceneCapabilities.drawIndirectCount = features12.drawIndirectCount == VK_TRUE;
        _terrainDescriptorIndexingSupported = features12.descriptorIndexing == VK_TRUE &&
                                               features12.runtimeDescriptorArray == VK_TRUE &&
                                               features12.descriptorBindingPartiallyBound == VK_TRUE &&
                                               features12.descriptorBindingVariableDescriptorCount == VK_TRUE &&
                                               features12.shaderSampledImageArrayNonUniformIndexing == VK_TRUE;
        _maxSamplerAnisotropy = selectedProps.limits.maxSamplerAnisotropy;
        _timestampPeriodNs = selectedProps.limits.timestampPeriod;

        LOG_INFO(Graphics, "Vulkan: selected device '{}' (max anisotropy {}, gpu-scene={}, indirect-count={}, terrain-descriptor-indexing={})",
                  selectedProps.deviceName, _maxSamplerAnisotropy, _gpuSceneCapabilities.GpuDrivenAvailable(),
                  _gpuSceneCapabilities.drawIndirectCount, _terrainDescriptorIndexingSupported);
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

    VkPhysicalDeviceFeatures enabledFeatures{};
    enabledFeatures.samplerAnisotropy = VK_TRUE;
    enabledFeatures.multiDrawIndirect = _gpuSceneCapabilities.multiDrawIndirect ? VK_TRUE : VK_FALSE;
    if (_volumetricCloudsEnabled)
        enabledFeatures.shaderStorageImageExtendedFormats = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pEnabledFeatures = &enabledFeatures;
    VkPhysicalDeviceVulkan11Features features11{};
    features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    features11.shaderDrawParameters = _gpuSceneCapabilities.shaderDrawParameters ? VK_TRUE : VK_FALSE;
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.drawIndirectCount = _gpuSceneCapabilities.drawIndirectCount ? VK_TRUE : VK_FALSE;
    if (_terrainDescriptorIndexingSupported)
    {
        features12.descriptorIndexing = VK_TRUE;
        features12.runtimeDescriptorArray = VK_TRUE;
        features12.descriptorBindingPartiallyBound = VK_TRUE;
        features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
        features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    }
    features11.pNext = &features12;
    createInfo.pNext = &features11;
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
    VkDescriptorSetLayout setLayouts[] = {_frameDescriptorSetLayout, _textureDescriptorSetLayout,
                                           _textureDescriptorSetLayout, _skyMapDescriptorSetLayout};

    VkPushConstantRange pushConstants{};
    pushConstants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstants.offset = 0;
    pushConstants.size = vk::kScenePushConstantsSize;

    VkPipelineLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    createInfo.setLayoutCount = 4;
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
    const VkResult result = vk::CreateHostVisibleBuffer(_physicalDevice, _device, sizeof(kBootstrapTriangleVertices),
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
    const VkResult result = vk::CreateHostVisibleBuffer(_physicalDevice, _device, sizeof(kBootstrapTriangleIndices),
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
    const VkResult result = vk::CreateHostVisibleBuffer(_physicalDevice, _device, sizeof(kSceneQuadVertices),
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
    const VkResult result = vk::CreateHostVisibleBuffer(_physicalDevice, _device, sizeof(kSceneQuadIndices),
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

    const std::size_t capacity = std::max(drawCount, std::max<std::size_t>(64, _drawConstantsCapacity * 2));
    const VkDeviceSize byteSize = static_cast<VkDeviceSize>(vk::DrawConstantsByteSize(capacity));
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
    _drawConstantsCapacity = capacity;
    std::vector<vk::DrawConstantsVK> cleared(capacity);
    vk::UploadMappedBuffer(_drawConstantsBuffer, cleared.data(), vk::DrawConstantsByteSize(cleared.size()));

    SetObjectName(VK_OBJECT_TYPE_BUFFER, VulkanObjectHandle(_drawConstantsBuffer.buffer),
                  "PoseidonVK Draw Constants Buffer");
    SetObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, VulkanObjectHandle(_drawConstantsBuffer.memory),
                  "PoseidonVK Draw Constants Memory");
    if (_frameDescriptorSet)
        UpdateFrameDescriptorSet();
    if (_gpuSceneDescriptorSet)
        EnsureGpuSceneCapacity(_gpuSceneInstanceCapacity, _gpuSceneBatchCapacity);
    return true;
}

bool EngineVK::CreateGpuSceneResources()
{
    if (!_gpuSceneCapabilities.GpuDrivenAvailable())
    {
        LOG_INFO(Graphics, "Vulkan: GPU scene disabled; using retained CPU draw path (compute={}, mdi={}, draw-parameters={})",
                 _gpuSceneCapabilities.compute, _gpuSceneCapabilities.multiDrawIndirect,
                 _gpuSceneCapabilities.shaderDrawParameters);
        return true;
    }

    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
    for (std::uint32_t i = 0; i < bindings.size(); ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(_device, &layoutInfo, nullptr, &_gpuSceneDescriptorSetLayout) != VK_SUCCESS)
        return false;

    const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
                                           {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4}};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_gpuSceneDescriptorPool) != VK_SUCCESS)
        return false;
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = _gpuSceneDescriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &_gpuSceneDescriptorSetLayout;
    if (vkAllocateDescriptorSets(_device, &allocateInfo, &_gpuSceneDescriptorSet) != VK_SUCCESS)
        return false;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.size = sizeof(std::uint32_t) * 2;
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &_gpuSceneDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_gpuScenePipelineLayout) != VK_SUCCESS)
        return false;

    std::vector<std::uint32_t> spirv;
    std::string error;
    if (!CompileBootstrapShader(kSceneCullComputeShader, EShLangCompute, spirv, error))
    {
        LOG_ERROR(Graphics, "Vulkan: GPU scene cull shader compile failed: {}", error);
        return false;
    }
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = spirv.size() * sizeof(std::uint32_t);
    moduleInfo.pCode = spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(_device, &moduleInfo, nullptr, &module) != VK_SUCCESS)
        return false;
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = module;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = _gpuScenePipelineLayout;
    const VkResult result = vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                                     &_gpuSceneCullPipeline);
    vkDestroyShaderModule(_device, module, nullptr);
    if (result != VK_SUCCESS)
        return false;
    _gpuSceneEnabled = EnsureGpuSceneCapacity(64, 64);
    if (_gpuSceneEnabled)
        SetObjectName(VK_OBJECT_TYPE_PIPELINE, VulkanObjectHandle(_gpuSceneCullPipeline), "PoseidonVK GPU Scene Cull");
    LOG_INFO(Graphics, "Vulkan: GPU scene enabled (cull/LOD/indirect, count mode={})",
             _gpuSceneCapabilities.drawIndirectCount ? "VK_KHR_draw_indirect_count/core" : "fixed indirect fallback");
    return _gpuSceneEnabled;
}

bool EngineVK::EnsureGpuSceneCapacity(std::size_t instanceCount, std::size_t batchCount)
{
    if (!_gpuSceneDescriptorSet || !_device || !_physicalDevice)
        return false;
    const std::size_t requiredInstances = std::max<std::size_t>(instanceCount, 1);
    const std::size_t requiredBatches = std::max<std::size_t>(batchCount, 1);
    if (requiredInstances > _gpuSceneInstanceCapacity || requiredBatches > _gpuSceneBatchCapacity)
    {
        vkDeviceWaitIdle(_device);
        const std::size_t newInstances = std::max(requiredInstances, std::max<std::size_t>(64, _gpuSceneInstanceCapacity * 2));
        const std::size_t newBatches = std::max(requiredBatches, std::max<std::size_t>(64, _gpuSceneBatchCapacity * 2));
        vk::BufferVK instances, indirect, counts;
        if (vk::CreateHostVisibleBuffer(_physicalDevice, _device, newInstances * sizeof(vk::GpuSceneInstanceVK),
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, instances) != VK_SUCCESS ||
            vk::CreateDeviceLocalBuffer(_physicalDevice, _device, newInstances * sizeof(VkDrawIndexedIndirectCommand),
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                        indirect) != VK_SUCCESS ||
            vk::CreateDeviceLocalBuffer(_physicalDevice, _device, newBatches * sizeof(std::uint32_t),
                                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                             VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                                         counts) != VK_SUCCESS)
        {
            vk::DestroyBuffer(_device, instances);
            vk::DestroyBuffer(_device, indirect);
            vk::DestroyBuffer(_device, counts);
            return false;
        }
        vk::DestroyBuffer(_device, _gpuSceneInstancesBuffer);
        vk::DestroyBuffer(_device, _gpuSceneIndirectBuffer);
        vk::DestroyBuffer(_device, _gpuSceneCountBuffer);
        _gpuSceneInstancesBuffer = instances;
        _gpuSceneIndirectBuffer = indirect;
        _gpuSceneCountBuffer = counts;
        _gpuSceneInstanceCapacity = newInstances;
        _gpuSceneBatchCapacity = newBatches;
        SetObjectName(VK_OBJECT_TYPE_BUFFER, VulkanObjectHandle(_gpuSceneInstancesBuffer.buffer),
                      "PoseidonVK GPU Scene Instances");
        SetObjectName(VK_OBJECT_TYPE_BUFFER, VulkanObjectHandle(_gpuSceneIndirectBuffer.buffer),
                      "PoseidonVK GPU Scene Indirect Commands");
        SetObjectName(VK_OBJECT_TYPE_BUFFER, VulkanObjectHandle(_gpuSceneCountBuffer.buffer),
                      "PoseidonVK GPU Scene Draw Counts");
    }
    VkDescriptorBufferInfo infos[5] = {{_frameConstantsBuffer.buffer, 0, sizeof(vk::FrameConstantsVK)},
                                        {_drawConstantsBuffer.buffer, 0, _drawConstantsBuffer.size},
                                        {_gpuSceneInstancesBuffer.buffer, 0, _gpuSceneInstancesBuffer.size},
                                        {_gpuSceneCountBuffer.buffer, 0, _gpuSceneCountBuffer.size},
                                        {_gpuSceneIndirectBuffer.buffer, 0, _gpuSceneIndirectBuffer.size}};
    VkWriteDescriptorSet writes[5]{};
    for (std::uint32_t i = 0; i < 5; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = _gpuSceneDescriptorSet;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(_device, 5, writes, 0, nullptr);
    return true;
}

void EngineVK::BuildGpuSceneInputs()
{
    _gpuSceneInstances.clear();
    _gpuSceneBatches.clear();
    if (!_gpuSceneEnabled)
        return;

    auto sameBatchState = [&](const vk::DrawConstantsVK& a, const vk::DrawConstantsVK& b)
    {
        return std::tie(a.pass, a.meshId, a.textureIds[0], a.textureIds[1], a.depth, a.blend, a.fog, a.cull,
                        a.frontFace, a.alpha, a.lighting, a.texGen, a.surface, a.samplerFilter, a.samplerClamp,
                        a.shader, a.alphaRef, a.stencilExclusion) ==
               std::tie(b.pass, b.meshId, b.textureIds[0], b.textureIds[1], b.depth, b.blend, b.fog, b.cull,
                        b.frontFace, b.alpha, b.lighting, b.texGen, b.surface, b.samplerFilter, b.samplerClamp,
                        b.shader, b.alphaRef, b.stencilExclusion);
    };
    const auto batchStateHash = [](const vk::DrawConstantsVK& draw)
    {
        std::uint64_t hash = 1469598103934665603ull;
        const auto mix = [&](std::uint32_t value) { hash = (hash ^ value) * 1099511628211ull; };
        mix(draw.pass);
        mix(draw.meshId);
        mix(draw.textureIds[0]);
        mix(draw.textureIds[1]);
        mix(draw.depth);
        mix(draw.blend);
        mix(draw.fog);
        mix(draw.cull);
        mix(draw.frontFace);
        mix(draw.alpha);
        mix(draw.lighting);
        mix(draw.texGen);
        mix(draw.surface);
        mix(draw.samplerFilter);
        mix(draw.samplerClamp);
        mix(draw.shader);
        mix(draw.alphaRef);
        mix(draw.stencilExclusion);
        return hash;
    };

    for (std::uint32_t groupIndex = 0; groupIndex < _sceneCommandGroups.size(); ++groupIndex)
    {
        const bool orderSensitive = groupIndex == static_cast<std::uint32_t>(SceneGroup::Transparent) ||
                                     groupIndex == static_cast<std::uint32_t>(SceneGroup::WorldLate) ||
                                     groupIndex == static_cast<std::uint32_t>(SceneGroup::Present);
        const auto& commands = _sceneCommandGroups[groupIndex];
        std::vector<std::uint32_t> stateSortedCommands;
        bool canReorder = groupIndex == static_cast<std::uint32_t>(SceneGroup::Opaque) ||
                          groupIndex == static_cast<std::uint32_t>(SceneGroup::Cutout);
        if (canReorder)
        {
            for (std::uint32_t commandIndex : commands)
            {
                const vk::DrawConstantsVK& draw = _lastDrawConstants[_lastSceneDrawCommands[commandIndex].drawIndex];
                if (draw.blend != vk::EnumToUint(render::BlendMode::Opaque) ||
                    draw.depth != vk::EnumToUint(render::DepthMode::Normal))
                {
                    canReorder = false;
                    break;
                }
            }
        }
        if (canReorder)
        {
            // These groups contain only depth-writing, non-blended draws. Stable
            // state bucketing reduces indirect calls without changing visibility.
            struct StateBucket
            {
                std::uint32_t head = UINT32_MAX;
                std::uint32_t tail = UINT32_MAX;
            };
            std::vector<std::uint32_t> next(commands.size(), UINT32_MAX);
            std::vector<StateBucket> buckets;
            buckets.reserve(commands.size());
            std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> bucketIndices;
            bucketIndices.reserve(commands.size());
            for (std::uint32_t position = 0; position < commands.size(); ++position)
            {
                const vk::DrawConstantsVK& draw = _lastDrawConstants[_lastSceneDrawCommands[commands[position]].drawIndex];
                std::vector<std::uint32_t>& candidates = bucketIndices[batchStateHash(draw)];
                std::uint32_t bucketIndex = UINT32_MAX;
                for (std::uint32_t candidate : candidates)
                {
                    const std::uint32_t representative = commands[buckets[candidate].head];
                    if (sameBatchState(_lastDrawConstants[_lastSceneDrawCommands[representative].drawIndex], draw))
                    {
                        bucketIndex = candidate;
                        break;
                    }
                }
                if (bucketIndex == UINT32_MAX)
                {
                    bucketIndex = static_cast<std::uint32_t>(buckets.size());
                    buckets.push_back({position, position});
                    candidates.push_back(bucketIndex);
                }
                else
                {
                    next[buckets[bucketIndex].tail] = position;
                    buckets[bucketIndex].tail = position;
                }
            }
            stateSortedCommands.reserve(commands.size());
            for (const StateBucket& bucket : buckets)
            {
                for (std::uint32_t position = bucket.head; position != UINT32_MAX; position = next[position])
                    stateSortedCommands.push_back(commands[position]);
            }
        }
        const auto& orderedCommands = canReorder ? stateSortedCommands : commands;
        for (std::uint32_t commandIndex : orderedCommands)
        {
            const vk::SceneDrawCommandVK& command = _lastSceneDrawCommands[commandIndex];
            const vk::DrawConstantsVK& draw = _lastDrawConstants[command.drawIndex];
            const vk::MeshResourcesVK* mesh = _meshRegistry.Resolve(command.meshId);
            if (!mesh || !mesh->IsValid())
                mesh = _meshRegistry.Resolve(_bootstrapMeshId);
            if (!mesh || !mesh->IsValid())
                continue;

            bool append = false;
            const bool commandOrderSensitive = orderSensitive ||
                                               draw.blend != vk::EnumToUint(render::BlendMode::Opaque);
            if (!commandOrderSensitive && !_gpuSceneBatches.empty())
            {
                const vk::GpuSceneBatchVK& previous = _gpuSceneBatches.back();
                if (previous.sceneGroup == groupIndex && previous.sourceCommandIndex < _lastSceneDrawCommands.size())
                {
                    const vk::SceneDrawCommandVK& previousCommand = _lastSceneDrawCommands[previous.sourceCommandIndex];
                    append = sameBatchState(_lastDrawConstants[previousCommand.drawIndex], draw);
                }
            }
            if (!append)
            {
                vk::GpuSceneBatchVK batch;
                batch.firstInstance = static_cast<std::uint32_t>(_gpuSceneInstances.size());
                batch.indirectOffset = batch.firstInstance;
                batch.countOffset = static_cast<std::uint32_t>(_gpuSceneBatches.size());
                batch.sourceCommandIndex = commandIndex;
                batch.sceneGroup = groupIndex;
                _gpuSceneBatches.push_back(batch);
            }
            vk::GpuSceneBatchVK& batch = _gpuSceneBatches.back();
            vk::GpuSceneInstanceVK instance;
            instance.localBoundsCenter[0] = mesh->localBoundsCenter[0];
            instance.localBoundsCenter[1] = mesh->localBoundsCenter[1];
            instance.localBoundsCenter[2] = mesh->localBoundsCenter[2];
            instance.localBoundsCenter[3] = mesh->localBoundsRadius;
            instance.drawIndex = command.drawIndex;
            instance.batchIndex = batch.countOffset;
            instance.indirectOffset = batch.indirectOffset;
            // The current mesh uploader exposes LOD0.  The complete input
            // layout carries four ranges/thresholds so mesh streaming can add
            // real LODs without changing the cull/indirect protocol.
            const std::uint32_t firstIndex = command.firstIndex;
            const std::uint32_t indexCount = firstIndex < mesh->indexCount
                                                 ? std::min(command.indexCount, mesh->indexCount - firstIndex)
                                                 : 0u;
            for (std::uint32_t lod = 0; lod < 4; ++lod)
            {
                const bool hasMeshLod = mesh->lodIndexCount[lod] != 0;
                const std::uint32_t lodFirst = hasMeshLod ? mesh->lodFirstIndex[lod] : firstIndex;
                instance.lodFirstIndex[lod] = lodFirst;
                instance.lodIndexCount[lod] = lodFirst < mesh->indexCount
                                                  ? (hasMeshLod ? std::min(mesh->lodIndexCount[lod], mesh->indexCount - lodFirst)
                                                                : indexCount)
                                                  : 0u;
                instance.lodDistance[lod] = mesh->lodDistance[lod];
            }
            ++batch.instanceCount;
            _gpuSceneInstances.push_back(instance);
        }
    }
    // The compute shader reads the final capacity from every instance. Populate
    // it after all batches close so input construction remains linear.
    for (const vk::GpuSceneBatchVK& batch : _gpuSceneBatches)
    {
        const std::uint32_t end = batch.firstInstance + batch.instanceCount;
        for (std::uint32_t instanceIndex = batch.firstInstance; instanceIndex < end; ++instanceIndex)
            _gpuSceneInstances[instanceIndex].batchCapacity = batch.instanceCount;
    }
    if (!EnsureGpuSceneCapacity(_gpuSceneInstances.size(), _gpuSceneBatches.size()))
    {
        LOG_WARN(Graphics, "Vulkan: GPU scene buffer growth failed; retaining legacy draw loop");
        _gpuSceneEnabled = false;
        _gpuSceneInstances.clear();
        _gpuSceneBatches.clear();
        return;
    }
    if (!_gpuSceneInstances.empty())
        vk::UploadMappedBuffer(_gpuSceneInstancesBuffer, _gpuSceneInstances.data(),
                                _gpuSceneInstances.size() * sizeof(vk::GpuSceneInstanceVK));
}

void EngineVK::RecordGpuSceneCull(VkCommandBuffer commandBuffer)
{
    if (!_gpuSceneEnabled || _gpuSceneInstances.empty() || !_gpuSceneCullPipeline)
        return;
    const VkDeviceSize indirectBytes = _gpuSceneInstances.size() * sizeof(VkDrawIndexedIndirectCommand);
    const VkDeviceSize countBytes = _gpuSceneBatches.size() * sizeof(std::uint32_t);
    vkCmdFillBuffer(commandBuffer, _gpuSceneIndirectBuffer.buffer, 0, indirectBytes, 0);
    vkCmdFillBuffer(commandBuffer, _gpuSceneCountBuffer.buffer, 0, countBytes, 0);
    VkBufferMemoryBarrier beforeCompute[5]{};
    for (VkBufferMemoryBarrier& barrier : beforeCompute)
    {
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }
    beforeCompute[0].srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    beforeCompute[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    beforeCompute[0].buffer = _frameConstantsBuffer.buffer;
    beforeCompute[0].size = _frameConstantsBuffer.size;
    beforeCompute[1].srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    beforeCompute[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    beforeCompute[1].buffer = _drawConstantsBuffer.buffer;
    beforeCompute[1].size = _drawConstantsBuffer.size;
    beforeCompute[2].srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    beforeCompute[2].buffer = _gpuSceneInstancesBuffer.buffer;
    beforeCompute[2].size = _gpuSceneInstancesBuffer.size;
    beforeCompute[3].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    beforeCompute[3].buffer = _gpuSceneIndirectBuffer.buffer;
    beforeCompute[3].size = indirectBytes;
    beforeCompute[4].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    beforeCompute[4].buffer = _gpuSceneCountBuffer.buffer;
    beforeCompute[4].size = countBytes;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 5, beforeCompute, 0, nullptr);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _gpuSceneCullPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _gpuScenePipelineLayout, 0, 1,
                            &_gpuSceneDescriptorSet, 0, nullptr);
    const std::uint32_t dispatchInfo[] = {static_cast<std::uint32_t>(_gpuSceneInstances.size()),
                                          static_cast<std::uint32_t>(_gpuSceneBatches.size())};
    vkCmdPushConstants(commandBuffer, _gpuScenePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(dispatchInfo),
                       dispatchInfo);
    vkCmdDispatch(commandBuffer, (dispatchInfo[0] + 63u) / 64u, 1, 1);
    VkBufferMemoryBarrier afterCompute[2]{};
    for (VkBufferMemoryBarrier& barrier : afterCompute)
    {
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    }
    afterCompute[0].buffer = _gpuSceneIndirectBuffer.buffer;
    afterCompute[0].size = indirectBytes;
    afterCompute[1].buffer = _gpuSceneCountBuffer.buffer;
    afterCompute[1].size = countBytes;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, nullptr, 2,
                         afterCompute, 0, nullptr);
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
    poolSize.descriptorCount = 8192; // scene textures plus per-sampler variants

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 8192;
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
    EnsureShadowResources(16, 4);

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

    SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_POOL, VulkanObjectHandle(_descriptorPool), "PoseidonVK Descriptor Pool");
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

    VkDescriptorImageInfo shadowImageInfo{};
    shadowImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    shadowImageInfo.imageView = _shadowDepthImage.view;
    shadowImageInfo.sampler = _shadowSampler;

    std::array<VkWriteDescriptorSet, vk::kFrameDescriptorSetBindingCount> writes = {
        vk::MakeFrameConstantsDescriptorWrite(_frameDescriptorSet, &frameBufferInfo),
        vk::MakeDrawConstantsDescriptorWrite(_frameDescriptorSet, &drawBufferInfo),
        vk::MakeShadowMapDescriptorWrite(_frameDescriptorSet, &shadowImageInfo),
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

    const VkFormat depthFormat = FindDepthFormat();
    if (depthFormat == VK_FORMAT_UNDEFINED)
    {
        LOG_ERROR(Graphics, "Vulkan: no supported depth format for render pass");
        return false;
    }
    _depthFormat = depthFormat;
    auto makeAttachment = [](VkFormat format, VkImageLayout finalLayout)
    {
        VkAttachmentDescription attachment{};
        attachment.format = format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = finalLayout;
        return attachment;
    };
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthWriteRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    if (WorldCompositionActive())
    {
        const VkFormat worldColorFormat =
            _hdrEnabled ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_B8G8R8A8_UNORM;
        // The isolated world target owns all cloud-visible world depth.
        VkAttachmentDescription worldAttachments[] = {
            makeAttachment(worldColorFormat, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            makeAttachment(depthFormat, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)};
        VkSubpassDescription worldSubpass{};
        worldSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        worldSubpass.colorAttachmentCount = 1;
        worldSubpass.pColorAttachments = &colorRef;
        worldSubpass.pDepthStencilAttachment = &depthWriteRef;

        VkSubpassDependency worldDependencies[2]{};
        worldDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        worldDependencies[0].dstSubpass = 0;
        worldDependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        worldDependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        worldDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        worldDependencies[1].srcSubpass = 0;
        worldDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        worldDependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        worldDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        worldDependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        worldDependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 2;
        info.pAttachments = worldAttachments;
        info.subpassCount = 1;
        info.pSubpasses = &worldSubpass;
        info.dependencyCount = 2;
        info.pDependencies = worldDependencies;
        result = vkCreateRenderPass(_device, &info, nullptr, &_renderPass);
        if (result != VK_SUCCESS)
            return false;
        SetObjectName(VK_OBJECT_TYPE_RENDER_PASS, VulkanObjectHandle(_renderPass), "PoseidonVK World Render Pass");

        VkAttachmentDescription presentAttachments[] = {
            makeAttachment(_swapchainFormat, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR),
            makeAttachment(depthFormat, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)};
        VkSubpassDescription presentSubpass{};
        presentSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        presentSubpass.colorAttachmentCount = 1;
        presentSubpass.pColorAttachments = &colorRef;
        presentSubpass.pDepthStencilAttachment = &depthWriteRef;
        VkSubpassDependency presentDependency{};
        presentDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        presentDependency.dstSubpass = 0;
        presentDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                         VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        presentDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        presentDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        info.attachmentCount = 2;
        info.pAttachments = presentAttachments;
        info.subpassCount = 1;
        info.pSubpasses = &presentSubpass;
        info.dependencyCount = 1;
        info.pDependencies = &presentDependency;
        result = vkCreateRenderPass(_device, &info, nullptr, &_presentRenderPass);
        if (result != VK_SUCCESS)
            return false;
        SetObjectName(VK_OBJECT_TYPE_RENDER_PASS, VulkanObjectHandle(_presentRenderPass), "PoseidonVK Present Render Pass");
    }
    else
    {
        VkAttachmentDescription attachments[] = {
            makeAttachment(_swapchainFormat, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR),
            makeAttachment(depthFormat, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthWriteRef;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = dependency.srcStageMask;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 2;
        info.pAttachments = attachments;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;
        result = vkCreateRenderPass(_device, &info, nullptr, &_renderPass);
        if (result != VK_SUCCESS)
            return false;
        SetObjectName(VK_OBJECT_TYPE_RENDER_PASS, VulkanObjectHandle(_renderPass), "PoseidonVK Direct Render Pass");
    }

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
    if (WorldCompositionActive() && !CreateWorldTarget())
        return false;
    if (_volumetricCloudsEnabled && !CreateCloudResources())
        return false;
    if (_temporalExposureEnabled && !CreateEyeAdaptationResources())
        return false;

    _framebuffers.resize(actualImageCount, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < actualImageCount; ++i)
    {
        VkImageView attachments[] = {_swapchainImageViews[i], _depthImageView};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = WorldCompositionActive() ? _presentRenderPass : _renderPass;
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
    const VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
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

bool EngineVK::CreateSkyMapDescriptorLayout()
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
    const VkResult result = vkCreateDescriptorSetLayout(_device, &createInfo, nullptr, &_skyMapDescriptorSetLayout);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: sky-map descriptor layout creation failed: {}", VkResultName(result));
        return false;
    }
    SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, VulkanObjectHandle(_skyMapDescriptorSetLayout),
                  "PoseidonVK HDR Sky Map Descriptor Layout");
    return true;
}

bool EngineVK::CreateSkyMapPipelineLayout()
{
    VkPushConstantRange displayPush{};
    displayPush.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    displayPush.offset = 0;
    displayPush.size = sizeof(float);
    const VkDescriptorSetLayout displayLayouts[] = {_frameDescriptorSetLayout, _skyMapDescriptorSetLayout};
    VkPipelineLayoutCreateInfo displayInfo{};
    displayInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    displayInfo.setLayoutCount = 2;
    displayInfo.pSetLayouts = displayLayouts;
    displayInfo.pushConstantRangeCount = 1;
    displayInfo.pPushConstantRanges = &displayPush;
    VkResult result = vkCreatePipelineLayout(_device, &displayInfo, nullptr, &_skyMapPipelineLayout);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: sky-map display pipeline layout creation failed: {}", VkResultName(result));
        return false;
    }

    VkPipelineLayoutCreateInfo bakeInfo{};
    bakeInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    bakeInfo.setLayoutCount = 1;
    bakeInfo.pSetLayouts = &_frameDescriptorSetLayout;
    result = vkCreatePipelineLayout(_device, &bakeInfo, nullptr, &_skyMapBakePipelineLayout);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: sky-map bake pipeline layout creation failed: {}", VkResultName(result));
        vkDestroyPipelineLayout(_device, _skyMapPipelineLayout, nullptr);
        _skyMapPipelineLayout = VK_NULL_HANDLE;
        return false;
    }
    SetObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, VulkanObjectHandle(_skyMapPipelineLayout),
                  "PoseidonVK HDR Sky Map Display Pipeline Layout");
    SetObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, VulkanObjectHandle(_skyMapBakePipelineLayout),
                  "PoseidonVK HDR Sky Map Bake Pipeline Layout");
    return true;
}

bool EngineVK::CreateSkyMapResources()
{
    constexpr std::uint32_t kSkyMapWidth = 512;
    constexpr std::uint32_t kSkyMapHeight = 256;
    constexpr VkFormat kSkyMapFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(_physicalDevice, kSkyMapFormat, &formatProperties);
    const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((formatProperties.optimalTilingFeatures & required) != required)
    {
        LOG_ERROR(Graphics, "Vulkan: R16G16B16A16 sky-map attachments are unsupported by this device");
        return false;
    }

    const VkResult imageResult = vk::CreateImage2D(
        _physicalDevice, _device, kSkyMapWidth, kSkyMapHeight, 1, kSkyMapFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _skyMap);
    if (imageResult != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: HDR sky-map image creation failed: {}", VkResultName(imageResult));
        return false;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    if (vkCreateSampler(_device, &samplerInfo, nullptr, &_skyMapSampler) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: HDR sky-map sampler creation failed");
        DestroySkyMapResources();
        return false;
    }

    VkAttachmentDescription attachment{};
    attachment.format = kSkyMapFormat;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // The explicit barriers in RecordSkyMapBake own the image layout changes.
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    VkRenderPassCreateInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    passInfo.attachmentCount = 1;
    passInfo.pAttachments = &attachment;
    passInfo.subpassCount = 1;
    passInfo.pSubpasses = &subpass;
    if (vkCreateRenderPass(_device, &passInfo, nullptr, &_skyMapRenderPass) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: HDR sky-map render pass creation failed");
        DestroySkyMapResources();
        return false;
    }

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = _skyMapRenderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &_skyMap.view;
    framebufferInfo.width = kSkyMapWidth;
    framebufferInfo.height = kSkyMapHeight;
    framebufferInfo.layers = 1;
    if (vkCreateFramebuffer(_device, &framebufferInfo, nullptr, &_skyMapFramebuffer) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: HDR sky-map framebuffer creation failed");
        DestroySkyMapResources();
        return false;
    }

    _skyMapLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    _skyMapDirty = true;
    _skyMapValid = false;
    SetObjectName(VK_OBJECT_TYPE_IMAGE, VulkanObjectHandle(_skyMap.image), "PoseidonVK HDR Cached Sky Map");
    SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, VulkanObjectHandle(_skyMap.view), "PoseidonVK HDR Cached Sky Map View");
    SetObjectName(VK_OBJECT_TYPE_SAMPLER, VulkanObjectHandle(_skyMapSampler), "PoseidonVK HDR Cached Sky Map Sampler");
    SetObjectName(VK_OBJECT_TYPE_RENDER_PASS, VulkanObjectHandle(_skyMapRenderPass), "PoseidonVK HDR Sky Map Bake Render Pass");
    SetObjectName(VK_OBJECT_TYPE_FRAMEBUFFER, VulkanObjectHandle(_skyMapFramebuffer), "PoseidonVK HDR Sky Map Bake Framebuffer");
    return true;
}

bool EngineVK::CreateSkyMapDescriptorSet()
{
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VkResult result = vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_skyMapDescriptorPool);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: HDR sky-map descriptor pool creation failed: {}", VkResultName(result));
        return false;
    }
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = _skyMapDescriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &_skyMapDescriptorSetLayout;
    result = vkAllocateDescriptorSets(_device, &allocateInfo, &_skyMapDescriptorSet);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: HDR sky-map descriptor set allocation failed: {}", VkResultName(result));
        return false;
    }
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = _skyMapSampler;
    imageInfo.imageView = _skyMap.view;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = _skyMapDescriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(_device, 1, &write, 0, nullptr);
    SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_POOL, VulkanObjectHandle(_skyMapDescriptorPool), "PoseidonVK HDR Sky Map Descriptor Pool");
    SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, VulkanObjectHandle(_skyMapDescriptorSet), "PoseidonVK HDR Sky Map Descriptor Set");
    return true;
}

void EngineVK::DestroySkyMapResources()
{
    if (_device && _skyMapFramebuffer)
        vkDestroyFramebuffer(_device, _skyMapFramebuffer, nullptr);
    _skyMapFramebuffer = VK_NULL_HANDLE;
    if (_device && _skyMapRenderPass)
        vkDestroyRenderPass(_device, _skyMapRenderPass, nullptr);
    _skyMapRenderPass = VK_NULL_HANDLE;
    if (_device && _skyMapSampler)
        vkDestroySampler(_device, _skyMapSampler, nullptr);
    _skyMapSampler = VK_NULL_HANDLE;
    vk::DestroyImage(_device, _skyMap);
    _skyMapLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    _skyMapValid = false;
    _skyMapDirty = true;
}

void EngineVK::DestroySkyMapDescriptorResources()
{
    _skyMapDescriptorSet = VK_NULL_HANDLE;
    if (_device && _skyMapDescriptorPool)
        vkDestroyDescriptorPool(_device, _skyMapDescriptorPool, nullptr);
    _skyMapDescriptorPool = VK_NULL_HANDLE;
    if (_device && _skyMapDescriptorSetLayout)
        vkDestroyDescriptorSetLayout(_device, _skyMapDescriptorSetLayout, nullptr);
    _skyMapDescriptorSetLayout = VK_NULL_HANDLE;
}

void EngineVK::DestroySkyMapPipelineLayout()
{
    if (_device && _skyMapPipelineLayout)
        vkDestroyPipelineLayout(_device, _skyMapPipelineLayout, nullptr);
    _skyMapPipelineLayout = VK_NULL_HANDLE;
    if (_device && _skyMapBakePipelineLayout)
        vkDestroyPipelineLayout(_device, _skyMapBakePipelineLayout, nullptr);
    _skyMapBakePipelineLayout = VK_NULL_HANDLE;
}

bool EngineVK::CreateCloudConstantsBuffer()
{
    const VkResult result = vk::CreateHostVisibleBuffer(_physicalDevice, _device, sizeof(vk::CloudConstantsVK),
                                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, _cloudConstantsBuffer);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: cloud constants buffer creation failed: {}", VkResultName(result));
        return false;
    }
    vk::UploadMappedBuffer(_cloudConstantsBuffer, &_cloudConstants, sizeof(_cloudConstants));
    SetObjectName(VK_OBJECT_TYPE_BUFFER, VulkanObjectHandle(_cloudConstantsBuffer.buffer),
                  "PoseidonVK Cloud Constants Buffer");
    return true;
}

void EngineVK::UpdateCloudConstants()
{
    if (!_volumetricCloudsEnabled)
        return;

    const float now = _lastFrameConstants.time[1];
    const float delta = _cloudLastUpdateSeconds < 0.0f ? 0.0f : std::clamp(now - _cloudLastUpdateSeconds, 0.0f, 0.1f);
    _cloudLastUpdateSeconds = now;
    const vk::CloudConstantsVK previous = _cloudConstants;
    // The simulation owns the world extent.  Snap the persistent field to that
    // extent so camera-relative renderer state never changes cloud placement.
    const float volumeExtent = std::clamp(_lastFrameConstants.cloudGeometry[2], 16384.0f, 131072.0f);
    const float volumeHalfExtent = volumeExtent * 0.5f;
    // The volume origin is its centre. Adding half the extent here placed the
    // camera on an edge, so clouds vanished whenever the view faced outward.
    const float originX = std::floor(_lastFrameConstants.cloudOrigin[0] / volumeExtent + 0.5f) * volumeExtent;
    const float originZ = std::floor(_lastFrameConstants.cloudOrigin[2] / volumeExtent + 0.5f) * volumeExtent;
    const bool movedVolume = _cloudHistoryValid &&
                             (originX != previous.volumeOrigin[0] || originZ != previous.volumeOrigin[2]);

    _cloudConstants.previousView = _previousCloudFrameConstants.view;
    _cloudConstants.previousProjection = _previousCloudFrameConstants.projection;
    std::memcpy(_cloudConstants.previousCameraPosition, previous.cameraPosition, sizeof(_cloudConstants.previousCameraPosition));
    std::memcpy(_cloudConstants.previousWindOffset, previous.windOffset, sizeof(_cloudConstants.previousWindOffset));
    _cloudConstants.cameraPosition[0] = _lastFrameConstants.cloudOrigin[0];
    _cloudConstants.cameraPosition[1] = _lastFrameConstants.cloudOrigin[1];
    _cloudConstants.cameraPosition[2] = _lastFrameConstants.cloudOrigin[2];
    _cloudConstants.windOffset[0] = previous.windOffset[0] + _lastFrameConstants.wind[0] * delta;
    _cloudConstants.windOffset[1] = previous.windOffset[1] + _lastFrameConstants.wind[1] * delta;
    _cloudConstants.windOffset[2] = previous.windOffset[2] + _lastFrameConstants.wind[2] * delta;
    _cloudConstants.volumeOrigin[0] = originX;
    _cloudConstants.volumeOrigin[1] = _lastFrameConstants.cloudGeometry[0];
    _cloudConstants.volumeOrigin[2] = originZ;
    _cloudConstants.volumeOrigin[3] = volumeHalfExtent;
    _cloudConstants.renderSizeAndHistory[0] = static_cast<float>(_cloudCurrent.width);
    _cloudConstants.renderSizeAndHistory[1] = static_cast<float>(_cloudCurrent.height);
    _cloudConstants.renderSizeAndHistory[2] = _cloudHistoryValid && !movedVolume ? 1.0f : 0.0f;
    _cloudConstants.renderSizeAndHistory[3] = static_cast<float>(_cloudFrameIndex++);
    _lastFrameConstants.windOffset[0] = _cloudConstants.windOffset[0];
    _lastFrameConstants.windOffset[1] = _cloudConstants.windOffset[1];
    _lastFrameConstants.windOffset[2] = _cloudConstants.windOffset[2];
    _previousCloudFrameConstants = _lastFrameConstants;
    if (movedVolume)
        _cloudHistoryValid = false;
    const float base = _lastFrameConstants.cloudGeometry[0];
    const float top = _lastFrameConstants.cloudGeometry[1];
    const float halfExtent = _cloudConstants.volumeOrigin[3];
    if (top <= base || halfExtent <= 0.0f)
        return;

    const std::array<float, 11> parameters = {
        _cloudConstants.volumeOrigin[0], _cloudConstants.volumeOrigin[2], _cloudConstants.windOffset[0],
        _cloudConstants.windOffset[1], _cloudConstants.windOffset[2], _lastFrameConstants.cloudWeather[0],
        _lastFrameConstants.cloudWeather[2], _lastFrameConstants.skyVisibility[2], base, top, halfExtent};
    const float voxelWorldSize = (halfExtent * 2.0f) / static_cast<float>(kCloudVolumeWidth);
    const bool volumeMoved = !_cloudVolumeBuilt || parameters[0] != _cloudVolumeBuildParameters[0] ||
                             parameters[1] != _cloudVolumeBuildParameters[1];
    const bool advected = !_cloudVolumeBuilt ||
                          std::abs(parameters[2] - _cloudVolumeBuildParameters[2]) >= voxelWorldSize * 0.5f ||
                          std::abs(parameters[3] - _cloudVolumeBuildParameters[3]) >= voxelWorldSize * 0.5f ||
                          std::abs(parameters[4] - _cloudVolumeBuildParameters[4]) >= voxelWorldSize * 0.5f;
    const bool weatherChanged = !_cloudVolumeBuilt || parameters[5] != _cloudVolumeBuildParameters[5] ||
                                parameters[6] != _cloudVolumeBuildParameters[6] ||
                                parameters[7] != _cloudVolumeBuildParameters[7] ||
                                parameters[8] != _cloudVolumeBuildParameters[8] ||
                                parameters[9] != _cloudVolumeBuildParameters[9] ||
                                parameters[10] != _cloudVolumeBuildParameters[10];
    if (volumeMoved || advected || weatherChanged)
    {
        // Rebuilds are quantized to a density voxel.  The command recorder owns
        // the actual GPU work so no CPU voxel payload or staging upload exists.
        _cloudVolumeBuildParameters = parameters;
        _cloudVolumeRebuildPending = true;
        LOG_INFO(Graphics,
                 "Vulkan: cloud-volume rebuild overcast={:.3f} alpha={:.3f} rain={:.3f} height={:.0f}..{:.0f}",
                 _lastFrameConstants.cloudWeather[0], _lastFrameConstants.cloudWeather[2],
                 _lastFrameConstants.cloudWeather[1], base, top);
    }
}

bool EngineVK::CreateWorldTarget()
{
    if (_hdrEnabled)
    {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(_physicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT, &properties);
        constexpr VkFormatFeatureFlags required =
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((properties.optimalTilingFeatures & required) != required)
        {
            LOG_ERROR(Graphics, "Vulkan: HDR world composition requires R16G16B16A16_SFLOAT color attachment and sampling support");
            return false;
        }
    }

    auto createImage = [&](VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, VkImage& image,
                           VkDeviceMemory& memory, VkImageView& view, const char* name)
    {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = {_swapchainExtent.width, _swapchainExtent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = usage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(_device, &imageInfo, nullptr, &image) != VK_SUCCESS)
            return false;
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(_device, image, &requirements);
        const uint32_t memoryType =
            vk::FindMemoryType(_physicalDevice, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memoryType == vk::kInvalidMemoryType)
            return false;
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType;
        if (vkAllocateMemory(_device, &allocation, nullptr, &memory) != VK_SUCCESS ||
            vkBindImageMemory(_device, image, memory, 0) != VK_SUCCESS)
            return false;
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspect;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(_device, &viewInfo, nullptr, &view) != VK_SUCCESS)
            return false;
        SetObjectName(VK_OBJECT_TYPE_IMAGE, VulkanObjectHandle(image), name);
        return true;
    };

    const VkFormat worldColorFormat = _hdrEnabled ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_B8G8R8A8_UNORM;
    if (!createImage(worldColorFormat,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                      VK_IMAGE_ASPECT_COLOR_BIT, _worldColorImage, _worldColorImageMemory, _worldColorImageView,
                      _hdrEnabled ? "PoseidonVK World HDR Color" : "PoseidonVK World UNORM Color") ||
        !createImage(_depthFormat,
                      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
                          VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_IMAGE_ASPECT_DEPTH_BIT, _worldDepthImage, _worldDepthImageMemory, _worldDepthImageView,
                     "PoseidonVK World Depth"))
    {
        DestroyWorldTarget();
        return false;
    }

    VkImageView attachments[] = {_worldColorImageView, _worldDepthImageView};
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = _renderPass;
    framebufferInfo.attachmentCount = 2;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = _swapchainExtent.width;
    framebufferInfo.height = _swapchainExtent.height;
    framebufferInfo.layers = 1;
    if (vkCreateFramebuffer(_device, &framebufferInfo, nullptr, &_worldFramebuffer) != VK_SUCCESS)
    {
        DestroyWorldTarget();
        return false;
    }
    SetObjectName(VK_OBJECT_TYPE_FRAMEBUFFER, VulkanObjectHandle(_worldFramebuffer), "PoseidonVK World Framebuffer");
    return true;
}

bool EngineVK::CreateWorldPrepassTarget()
{
    if (!_worldDepthImageView || _swapchainExtent.width == 0 || _swapchainExtent.height == 0)
        return false;
    constexpr VkFormat kNormalFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(_physicalDevice, kNormalFormat, &properties);
    const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((properties.optimalTilingFeatures & required) != required)
    {
        LOG_ERROR(Graphics, "Vulkan: depth-normal prepass requires a sampled FP16 colour attachment");
        return false;
    }
    DestroyWorldPrepassTarget();
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = kNormalFormat;
    imageInfo.extent = {_swapchainExtent.width, _swapchainExtent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(_device, &imageInfo, nullptr, &_worldNormalImage) != VK_SUCCESS)
        return false;
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(_device, _worldNormalImage, &requirements);
    const uint32_t memoryType =
        vk::FindMemoryType(_physicalDevice, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType == vk::kInvalidMemoryType)
    {
        DestroyWorldPrepassTarget();
        return false;
    }
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(_device, &allocation, nullptr, &_worldNormalImageMemory) != VK_SUCCESS ||
        vkBindImageMemory(_device, _worldNormalImage, _worldNormalImageMemory, 0) != VK_SUCCESS)
    {
        DestroyWorldPrepassTarget();
        return false;
    }
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = _worldNormalImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = kNormalFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(_device, &viewInfo, nullptr, &_worldNormalImageView) != VK_SUCCESS)
    {
        DestroyWorldPrepassTarget();
        return false;
    }
    VkAttachmentDescription attachments[2]{};
    attachments[0].format = kNormalFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[1].format = _depthFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    subpass.pDepthStencilAttachment = &depth;
    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    VkRenderPassCreateInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    passInfo.attachmentCount = 2;
    passInfo.pAttachments = attachments;
    passInfo.subpassCount = 1;
    passInfo.pSubpasses = &subpass;
    passInfo.dependencyCount = 2;
    passInfo.pDependencies = dependencies;
    if (vkCreateRenderPass(_device, &passInfo, nullptr, &_worldPrepassRenderPass) != VK_SUCCESS)
    {
        DestroyWorldPrepassTarget();
        return false;
    }
    VkImageView views[] = {_worldNormalImageView, _worldDepthImageView};
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = _worldPrepassRenderPass;
    framebufferInfo.attachmentCount = 2;
    framebufferInfo.pAttachments = views;
    framebufferInfo.width = _swapchainExtent.width;
    framebufferInfo.height = _swapchainExtent.height;
    framebufferInfo.layers = 1;
    if (vkCreateFramebuffer(_device, &framebufferInfo, nullptr, &_worldPrepassFramebuffer) != VK_SUCCESS)
    {
        DestroyWorldPrepassTarget();
        return false;
    }
    SetObjectName(VK_OBJECT_TYPE_IMAGE, VulkanObjectHandle(_worldNormalImage), "PoseidonVK World Prepass Normal");
    SetObjectName(VK_OBJECT_TYPE_RENDER_PASS, VulkanObjectHandle(_worldPrepassRenderPass), "PoseidonVK World Depth Normal Prepass");
    SetObjectName(VK_OBJECT_TYPE_FRAMEBUFFER, VulkanObjectHandle(_worldPrepassFramebuffer), "PoseidonVK World Prepass Framebuffer");
    return true;
}

void EngineVK::DestroyWorldPrepassTarget()
{
    if (_worldPrepassFramebuffer) vkDestroyFramebuffer(_device, _worldPrepassFramebuffer, nullptr);
    if (_worldPrepassRenderPass) vkDestroyRenderPass(_device, _worldPrepassRenderPass, nullptr);
    if (_worldNormalImageView) vkDestroyImageView(_device, _worldNormalImageView, nullptr);
    if (_worldNormalImage) vkDestroyImage(_device, _worldNormalImage, nullptr);
    if (_worldNormalImageMemory) vkFreeMemory(_device, _worldNormalImageMemory, nullptr);
    _worldPrepassFramebuffer = VK_NULL_HANDLE;
    _worldPrepassRenderPass = VK_NULL_HANDLE;
    _worldNormalImageView = VK_NULL_HANDLE;
    _worldNormalImage = VK_NULL_HANDLE;
    _worldNormalImageMemory = VK_NULL_HANDLE;
}

void EngineVK::DestroyWorldTarget()
{
    DestroyWorldPrepassTarget();
    if (_worldFramebuffer)
    {
        vkDestroyFramebuffer(_device, _worldFramebuffer, nullptr);
        _worldFramebuffer = VK_NULL_HANDLE;
    }
    auto destroyImage = [&](VkImageView& view, VkImage& image, VkDeviceMemory& memory)
    {
        if (view)
            vkDestroyImageView(_device, view, nullptr);
        if (image)
            vkDestroyImage(_device, image, nullptr);
        if (memory)
            vkFreeMemory(_device, memory, nullptr);
        view = VK_NULL_HANDLE;
        image = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
    };
    destroyImage(_worldColorImageView, _worldColorImage, _worldColorImageMemory);
    destroyImage(_worldDepthImageView, _worldDepthImage, _worldDepthImageMemory);
}

bool EngineVK::CreateCloudResources()
{
    if (!_worldColorImageView || !_worldDepthImageView)
        return false;
    constexpr VkFormat kCloudFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(_physicalDevice, kCloudFormat, &properties);
    const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((properties.optimalTilingFeatures & required) != required)
    {
        LOG_ERROR(Graphics, "Vulkan: temporal clouds require R16G16B16A16_SFLOAT render-target sampling support");
        return false;
    }
    DestroyCloudResources();
    const std::uint32_t width = std::max(1u, (_swapchainExtent.width + 1u) / 2u);
    const std::uint32_t height = std::max(1u, (_swapchainExtent.height + 1u) / 2u);
    const auto createCloudImage = [&](vk::ImageVK& image, const char* name)
    {
        if (vk::CreateImage2D(_physicalDevice, _device, width, height, 1, kCloudFormat,
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image) != VK_SUCCESS)
            return false;
        SetObjectName(VK_OBJECT_TYPE_IMAGE, VulkanObjectHandle(image.image), name);
        return true;
    };
    if (!createCloudImage(_cloudCurrent, "PoseidonVK Cloud Raymarch") ||
        !createCloudImage(_cloudHistory[0], "PoseidonVK Cloud History 0") ||
        !createCloudImage(_cloudHistory[1], "PoseidonVK Cloud History 1"))
    {
        DestroyCloudResources();
        return false;
    }
    // Every cloud target is sampled by a later pass and is reused next frame.
    // Establish shader-read layout once so the render passes can explicitly
    // transition from that known state instead of relying on undefined layout.
    for (vk::ImageVK* image : std::array<vk::ImageVK*, 3>{&_cloudCurrent, &_cloudHistory[0], &_cloudHistory[1]})
    {
        vk::TransitionImageLayout(_device, _commandPool, _graphicsQueue, image->image, 1,
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    vkGetPhysicalDeviceFormatProperties(_physicalDevice, VK_FORMAT_R8_UNORM, &properties);
    constexpr VkFormatFeatureFlags volumeFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                                     VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    if ((properties.optimalTilingFeatures & volumeFeatures) != volumeFeatures)
    {
        LOG_ERROR(Graphics, "Vulkan: cloud volume fields require sampled and storage R8_UNORM support");
        DestroyCloudResources();
        return false;
    }
    const auto createVolume = [&](vk::ImageVK& image, std::uint32_t volumeWidth, std::uint32_t volumeHeight,
                                  std::uint32_t volumeDepth, const char* name)
    {
        if (vk::CreateImage3D(_physicalDevice, _device, volumeWidth, volumeHeight, volumeDepth, VK_FORMAT_R8_UNORM,
                              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image) != VK_SUCCESS)
            return false;
        SetObjectName(VK_OBJECT_TYPE_IMAGE, VulkanObjectHandle(image.image), name);
        vk::TransitionImageLayout(_device, _commandPool, _graphicsQueue, image.image, 1,
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        return true;
    };
    if (!createVolume(_cloudDensityVolume, kCloudVolumeWidth, kCloudVolumeHeight, kCloudVolumeDepth,
                      "PoseidonVK Cloud Density Volume") ||
        !createVolume(_cloudDistanceVolume, kCloudVolumeWidth, kCloudVolumeHeight, kCloudVolumeDepth,
                      "PoseidonVK Cloud Distance Volume") ||
        !createVolume(_cloudLightVolumes[0], kCloudLightVolumeWidth, kCloudLightVolumeHeight, kCloudLightVolumeDepth,
                      "PoseidonVK Cloud Light Volume 0") ||
        !createVolume(_cloudLightVolumes[1], kCloudLightVolumeWidth, kCloudLightVolumeHeight, kCloudLightVolumeDepth,
                       "PoseidonVK Cloud Light Volume 1"))
    {
        LOG_ERROR(Graphics, "Vulkan: persistent cloud volume resource creation failed");
        DestroyCloudResources();
        return false;
    }
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    if (vkCreateSampler(_device, &samplerInfo, nullptr, &_cloudSampler) != VK_SUCCESS)
    {
        DestroyCloudResources();
        return false;
    }
    const auto createColorPass = [&](VkRenderPass& pass, VkFormat format, VkAttachmentLoadOp loadOp)
    {
        VkAttachmentDescription attachment{};
        attachment.format = format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = loadOp;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        VkSubpassDependency dependencies[2] = {};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &attachment;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 2;
        info.pDependencies = dependencies;
        return vkCreateRenderPass(_device, &info, nullptr, &pass) == VK_SUCCESS;
    };
    if (!createColorPass(_cloudRaymarchRenderPass, kCloudFormat, VK_ATTACHMENT_LOAD_OP_CLEAR) ||
        !createColorPass(_cloudTemporalRenderPass, kCloudFormat, VK_ATTACHMENT_LOAD_OP_DONT_CARE) ||
        !createColorPass(_cloudCompositeRenderPass,
                         _hdrEnabled ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_B8G8R8A8_UNORM,
                         VK_ATTACHMENT_LOAD_OP_LOAD))
    {
        DestroyCloudResources();
        return false;
    }
    VkAttachmentDescription lateAttachments[2] = {};
    lateAttachments[0].format = _hdrEnabled ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_B8G8R8A8_UNORM;
    lateAttachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    lateAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    lateAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    lateAttachments[0].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    lateAttachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    lateAttachments[1].format = _depthFormat;
    lateAttachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    lateAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    lateAttachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    lateAttachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    lateAttachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    lateAttachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    lateAttachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkAttachmentReference lateColor{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference lateDepth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkSubpassDescription lateSubpass{};
    lateSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    lateSubpass.colorAttachmentCount = 1;
    lateSubpass.pColorAttachments = &lateColor;
    lateSubpass.pDepthStencilAttachment = &lateDepth;
    VkSubpassDependency lateDependencies[2] = {};
    lateDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    lateDependencies[0].dstSubpass = 0;
    lateDependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    lateDependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    lateDependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    lateDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    lateDependencies[1].srcSubpass = 0;
    lateDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    lateDependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    lateDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    lateDependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    lateDependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo lateInfo{};
    lateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    lateInfo.attachmentCount = 2;
    lateInfo.pAttachments = lateAttachments;
    lateInfo.subpassCount = 1;
    lateInfo.pSubpasses = &lateSubpass;
    lateInfo.dependencyCount = 2;
    lateInfo.pDependencies = lateDependencies;
    if (vkCreateRenderPass(_device, &lateInfo, nullptr, &_worldLateRenderPass) != VK_SUCCESS)
    {
        DestroyCloudResources();
        return false;
    }
    const auto createFramebuffer = [&](VkRenderPass pass, VkImageView view, VkFramebuffer& framebuffer,
                                       std::uint32_t framebufferWidth, std::uint32_t framebufferHeight)
    {
        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = pass;
        info.attachmentCount = 1;
        info.pAttachments = &view;
        info.width = framebufferWidth;
        info.height = framebufferHeight;
        info.layers = 1;
        return vkCreateFramebuffer(_device, &info, nullptr, &framebuffer) == VK_SUCCESS;
    };
    if (!createFramebuffer(_cloudRaymarchRenderPass, _cloudCurrent.view, _cloudRaymarchFramebuffer, width, height) ||
        !createFramebuffer(_cloudTemporalRenderPass, _cloudHistory[0].view, _cloudTemporalFramebuffers[0], width, height) ||
        !createFramebuffer(_cloudTemporalRenderPass, _cloudHistory[1].view, _cloudTemporalFramebuffers[1], width, height) ||
        !createFramebuffer(_cloudCompositeRenderPass, _worldColorImageView, _cloudCompositeFramebuffer,
                           _swapchainExtent.width, _swapchainExtent.height))
    {
        DestroyCloudResources();
        return false;
    }
    VkImageView lateViews[] = {_worldColorImageView, _worldDepthImageView};
    VkFramebufferCreateInfo lateFramebufferInfo{};
    lateFramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    lateFramebufferInfo.renderPass = _worldLateRenderPass;
    lateFramebufferInfo.attachmentCount = 2;
    lateFramebufferInfo.pAttachments = lateViews;
    lateFramebufferInfo.width = _swapchainExtent.width;
    lateFramebufferInfo.height = _swapchainExtent.height;
    lateFramebufferInfo.layers = 1;
    if (vkCreateFramebuffer(_device, &lateFramebufferInfo, nullptr, &_worldLateFramebuffer) != VK_SUCCESS)
    {
        DestroyCloudResources();
        return false;
    }
    _cloudHistoryValid = false;
    _cloudHistoryCurrentIndex = 0;
    _cloudLightVolumeReadIndex = 0;
    _cloudVolumeBuilt = false;
    _cloudVolumeRebuildPending = true;
    return true;
}

void EngineVK::DestroyCloudResources()
{
    for (VkFramebuffer& framebuffer : _cloudTemporalFramebuffers)
    {
        if (framebuffer) vkDestroyFramebuffer(_device, framebuffer, nullptr);
        framebuffer = VK_NULL_HANDLE;
    }
    const std::array<VkFramebuffer*, 3> framebuffers = {
        &_cloudRaymarchFramebuffer, &_cloudCompositeFramebuffer, &_worldLateFramebuffer};
    for (VkFramebuffer* framebuffer : framebuffers)
    {
        if (*framebuffer) vkDestroyFramebuffer(_device, *framebuffer, nullptr);
        *framebuffer = VK_NULL_HANDLE;
    }
    const std::array<VkRenderPass*, 4> passes = {
        &_cloudRaymarchRenderPass, &_cloudTemporalRenderPass, &_cloudCompositeRenderPass,
        &_worldLateRenderPass};
    for (VkRenderPass* pass : passes)
    {
        if (*pass) vkDestroyRenderPass(_device, *pass, nullptr);
        *pass = VK_NULL_HANDLE;
    }
    if (_cloudSampler) vkDestroySampler(_device, _cloudSampler, nullptr);
    _cloudSampler = VK_NULL_HANDLE;
    vk::DestroyImage(_device, _cloudCurrent);
    for (vk::ImageVK& image : _cloudHistory) vk::DestroyImage(_device, image);
    vk::DestroyImage(_device, _cloudDensityVolume);
    vk::DestroyImage(_device, _cloudDistanceVolume);
    for (vk::ImageVK& image : _cloudLightVolumes) vk::DestroyImage(_device, image);
    _cloudHistoryValid = false;
    _cloudHistoryCurrentIndex = 0;
    _cloudLightVolumeReadIndex = 0;
    _cloudVolumeBuilt = false;
    _cloudVolumeRebuildPending = true;
}

void EngineVK::RecordCloudVolumeCompute(VkCommandBuffer commandBuffer)
{
    if (!_cloudGenerationDescriptorSet || !_cloudLightingDescriptorSets[0] || !_cloudDensityErosionPipeline ||
        !_cloudDistanceFieldPipeline || !_cloudLightMapPipeline)
        return;

    const auto imageBarrier = [&](VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                                  VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                                  VkAccessFlags srcAccess, VkAccessFlags dstAccess)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    };
    const VkPipelineStageFlags shaderReadStages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    if (_cloudVolumeRebuildPending)
    {
        // Density erosion and the distance transform share GENERAL layouts.
        // The explicit compute write/read barrier prevents the distance pass
        // from observing partially written density voxels.
        imageBarrier(_cloudDensityVolume.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                     shaderReadStages, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_ACCESS_SHADER_WRITE_BIT);
        imageBarrier(_cloudDistanceVolume.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                     shaderReadStages, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_ACCESS_SHADER_WRITE_BIT);
        VkDescriptorSet generationSets[] = {_frameDescriptorSet, _cloudGenerationDescriptorSet};
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _cloudDensityErosionPipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _cloudGenerationPipelineLayout, 0, 2,
                                generationSets, 0, nullptr);
        vkCmdDispatch(commandBuffer, (kCloudVolumeWidth + 3u) / 4u, (kCloudVolumeHeight + 3u) / 4u,
                      (kCloudVolumeDepth + 3u) / 4u);
        imageBarrier(_cloudDensityVolume.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _cloudDistanceFieldPipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _cloudGenerationPipelineLayout, 0, 2,
                                generationSets, 0, nullptr);
        vkCmdDispatch(commandBuffer, (kCloudVolumeWidth + 3u) / 4u, (kCloudVolumeHeight + 3u) / 4u,
                      (kCloudVolumeDepth + 3u) / 4u);
        imageBarrier(_cloudDensityVolume.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, shaderReadStages,
                     VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                     VK_ACCESS_SHADER_READ_BIT);
        imageBarrier(_cloudDistanceVolume.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, shaderReadStages, VK_ACCESS_SHADER_WRITE_BIT,
                     VK_ACCESS_SHADER_READ_BIT);
        _cloudVolumeRebuildPending = false;
        _cloudVolumeBuilt = true;
    }

    // Illumination is progressive: every submitted cloud frame writes the
    // non-sampled light volume, publishes it to the fragment stage, then makes
    // that volume the raymarch read side. The previous read volume is never
    // written while the graphics pass can sample it.
    const std::uint32_t lightWriteIndex = 1u - _cloudLightVolumeReadIndex;
    imageBarrier(_cloudLightVolumes[lightWriteIndex].image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_IMAGE_LAYOUT_GENERAL, shaderReadStages, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                 VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    VkDescriptorSet lightingSets[] = {_frameDescriptorSet, _cloudLightingDescriptorSets[lightWriteIndex]};
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _cloudLightMapPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _cloudLightingPipelineLayout, 0, 2,
                            lightingSets, 0, nullptr);
    vkCmdDispatch(commandBuffer, (kCloudLightVolumeWidth + 3u) / 4u, (kCloudLightVolumeHeight + 3u) / 4u,
                  (kCloudLightVolumeDepth + 3u) / 4u);
    imageBarrier(_cloudLightVolumes[lightWriteIndex].image, VK_IMAGE_LAYOUT_GENERAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    _cloudLightVolumeReadIndex = lightWriteIndex;
}

bool EngineVK::CreateEyeAdaptationResources()
{
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(_physicalDevice, VK_FORMAT_R16G16_SFLOAT, &properties);
    constexpr VkFormatFeatureFlags required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((properties.optimalTilingFeatures & required) != required)
    {
        LOG_ERROR(Graphics, "Vulkan: HDR eye adaptation requires R16G16_SFLOAT color attachment and sampling support");
        return false;
    }

    DestroyEyeAdaptationResources();
    for (std::uint32_t i = 0; i < _eyeAdaptationHistory.size(); ++i)
    {
        if (vk::CreateImage2D(_physicalDevice, _device, 1, 1, 1, VK_FORMAT_R16G16_SFLOAT,
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _eyeAdaptationHistory[i]) != VK_SUCCESS)
        {
            DestroyEyeAdaptationResources();
            return false;
        }
        const std::string name = "PoseidonVK Eye Adaptation History " + std::to_string(i);
        SetObjectName(VK_OBJECT_TYPE_IMAGE, VulkanObjectHandle(_eyeAdaptationHistory[i].image), name.c_str());
        vk::TransitionImageLayout(_device, _commandPool, _graphicsQueue, _eyeAdaptationHistory[i].image, 1,
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    VkAttachmentDescription attachment{};
    attachment.format = VK_FORMAT_R16G16_SFLOAT;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 2;
    renderPassInfo.pDependencies = dependencies;
    if (vkCreateRenderPass(_device, &renderPassInfo, nullptr, &_eyeAdaptationRenderPass) != VK_SUCCESS)
    {
        DestroyEyeAdaptationResources();
        return false;
    }
    SetObjectName(VK_OBJECT_TYPE_RENDER_PASS, VulkanObjectHandle(_eyeAdaptationRenderPass),
                  "PoseidonVK Eye Adaptation Render Pass");

    for (std::uint32_t i = 0; i < _eyeAdaptationFramebuffers.size(); ++i)
    {
        VkImageView attachmentView = _eyeAdaptationHistory[i].view;
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = _eyeAdaptationRenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &attachmentView;
        framebufferInfo.width = 1;
        framebufferInfo.height = 1;
        framebufferInfo.layers = 1;
        if (vkCreateFramebuffer(_device, &framebufferInfo, nullptr, &_eyeAdaptationFramebuffers[i]) != VK_SUCCESS)
        {
            DestroyEyeAdaptationResources();
            return false;
        }
    }
    _eyeAdaptationHistoryValid = false;
    _eyeAdaptationPendingWrite = false;
    _eyeAdaptationCurrentIndex = 0;
    return true;
}

void EngineVK::DestroyEyeAdaptationResources()
{
    for (VkFramebuffer& framebuffer : _eyeAdaptationFramebuffers)
    {
        if (framebuffer)
            vkDestroyFramebuffer(_device, framebuffer, nullptr);
        framebuffer = VK_NULL_HANDLE;
    }
    if (_eyeAdaptationRenderPass)
    {
        vkDestroyRenderPass(_device, _eyeAdaptationRenderPass, nullptr);
        _eyeAdaptationRenderPass = VK_NULL_HANDLE;
    }
    for (vk::ImageVK& image : _eyeAdaptationHistory)
        vk::DestroyImage(_device, image);
    _eyeAdaptationHistoryValid = false;
    _eyeAdaptationPendingWrite = false;
    _eyeAdaptationCurrentIndex = 0;
}

bool EngineVK::CreateBootstrapPipeline()
{
    if (_device && _bootstrapPipeline)
    {
        vkDestroyPipeline(_device, _bootstrapPipeline, nullptr);
        _bootstrapPipeline = VK_NULL_HANDLE;
    }
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
    VkPipelineDepthStencilStateCreateInfo depthStencil = vk::BuildDepthStencilState(render::DepthMode::Disabled);

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

    const VkResult result =
        vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_bootstrapPipeline);
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
    if (_device)
    {
        _scenePipelineCache.Destroy(_device);
        _worldPrepassScenePipelineCache.Destroy(_device);
        _cockpitScenePipelineCache.Destroy(_device);
        _worldLateScenePipelineCache.Destroy(_device);
        if (_scenePipeline)
        {
            vkDestroyPipeline(_device, _scenePipeline, nullptr);
            _scenePipeline = VK_NULL_HANDLE;
        }
        if (_worldPrepassScenePipeline)
        {
            vkDestroyPipeline(_device, _worldPrepassScenePipeline, nullptr);
            _worldPrepassScenePipeline = VK_NULL_HANDLE;
        }
        if (_cockpitScenePipeline)
        {
            vkDestroyPipeline(_device, _cockpitScenePipeline, nullptr);
            _cockpitScenePipeline = VK_NULL_HANDLE;
        }
        if (_worldLateScenePipeline)
        {
            vkDestroyPipeline(_device, _worldLateScenePipeline, nullptr);
            _worldLateScenePipeline = VK_NULL_HANDLE;
        }
        if (_sceneVertexModule)
        {
            vkDestroyShaderModule(_device, _sceneVertexModule, nullptr);
            _sceneVertexModule = VK_NULL_HANDLE;
        }
        if (_sceneFragmentModule)
        {
            vkDestroyShaderModule(_device, _sceneFragmentModule, nullptr);
            _sceneFragmentModule = VK_NULL_HANDLE;
        }
        if (_scenePrepassFragmentModule)
        {
            vkDestroyShaderModule(_device, _scenePrepassFragmentModule, nullptr);
            _scenePrepassFragmentModule = VK_NULL_HANDLE;
        }
    }
    std::vector<uint32_t> vertexSpirv;
    std::vector<uint32_t> fragmentSpirv;
    std::vector<uint32_t> prepassFragmentSpirv;
    std::string error;
    std::string sceneVertexSource = kSceneVertexShader;
    if (_gpuSceneEnabled)
    {
        // Embedded GLSL starts with a raw-string newline. Insert after the
        // version directive, never before it: GLSL requires #version first.
        const std::size_t version = sceneVertexSource.find("#version");
        const std::size_t lineEnd = version == std::string::npos ? std::string::npos
                                                                  : sceneVertexSource.find('\n', version);
        if (lineEnd == std::string::npos)
        {
            LOG_ERROR(Graphics, "Vulkan: scene vertex shader has no #version directive");
            return false;
        }
        sceneVertexSource.insert(lineEnd + 1, "#define POSEIDON_GPU_SCENE 1\n");
    }
    if (!CompileBootstrapShader(sceneVertexSource.c_str(), EShLangVertex, vertexSpirv, error))
    {
        LOG_ERROR(Graphics, "Vulkan: scene vertex shader compile failed: {}", error);
        return false;
    }
    if (!CompileBootstrapShader(kSceneFragmentShader, EShLangFragment, fragmentSpirv, error))
    {
        LOG_ERROR(Graphics, "Vulkan: scene fragment shader compile failed: {}", error);
        return false;
    }
    std::string prepassFragmentSource = kSceneFragmentShader;
    const std::size_t prepassVersion = prepassFragmentSource.find("#version");
    const std::size_t prepassLineEnd = prepassVersion == std::string::npos ? std::string::npos
                                                                             : prepassFragmentSource.find('\n', prepassVersion);
    if (prepassLineEnd == std::string::npos)
    {
        LOG_ERROR(Graphics, "Vulkan: scene prepass fragment shader has no #version directive");
        return false;
    }
    prepassFragmentSource.insert(prepassLineEnd + 1, "#define POSEIDON_PREPASS 1\n");
    if (!CompileBootstrapShader(prepassFragmentSource.c_str(), EShLangFragment, prepassFragmentSpirv, error))
    {
        LOG_ERROR(Graphics, "Vulkan: scene prepass fragment shader compile failed: {}", error);
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
    VkShaderModule prepassFragmentModule = VK_NULL_HANDLE;
    if (!createShaderModule(vertexSpirv, "PoseidonVK Scene Vertex Shader", vertexModule) ||
        !createShaderModule(fragmentSpirv, "PoseidonVK Scene Fragment Shader", fragmentModule) ||
        !createShaderModule(prepassFragmentSpirv, "PoseidonVK Scene Prepass Fragment Shader", prepassFragmentModule))
    {
        if (vertexModule)
            vkDestroyShaderModule(_device, vertexModule, nullptr);
        if (fragmentModule)
            vkDestroyShaderModule(_device, fragmentModule, nullptr);
        if (prepassFragmentModule)
            vkDestroyShaderModule(_device, prepassFragmentModule, nullptr);
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
    // Flip Y so OpenGL-convention projection matrices render right-side-up.
    // Vulkan clip-space Y points downward; by using a negative height we make
    // it point upward, matching the engine's expectations.  This requires
    // Vulkan 1.1 / VK_KHR_maintenance1 (already our minimum).
    viewport.y = static_cast<float>(_swapchainExtent.height);
    viewport.width = static_cast<float>(_swapchainExtent.width);
    viewport.height = -static_cast<float>(_swapchainExtent.height);
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

    VkPipelineDepthStencilStateCreateInfo depthStencil = vk::BuildDepthStencilState(render::DepthMode::Normal);

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

    VkResult result =
        vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_scenePipeline);

    if (result == VK_SUCCESS && _worldPrepassRenderPass)
    {
        VkPipelineShaderStageCreateInfo prepassStages[2] = {shaderStages[0], shaderStages[1]};
        prepassStages[1].module = prepassFragmentModule;
        VkGraphicsPipelineCreateInfo prepassInfo = pipelineInfo;
        prepassInfo.pStages = prepassStages;
        prepassInfo.renderPass = _worldPrepassRenderPass;
        result = vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &prepassInfo, nullptr,
                                           &_worldPrepassScenePipeline);
    }

    if (result == VK_SUCCESS && _volumetricCloudsEnabled)
    {
        VkPipelineDepthStencilStateCreateInfo worldLateDepthStencil =
            vk::BuildDepthStencilState(render::DepthMode::ReadOnly);
        pipelineInfo.pDepthStencilState = &worldLateDepthStencil;
        pipelineInfo.renderPass = _worldLateRenderPass;
        pipelineInfo.subpass = 0;
        result = vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_worldLateScenePipeline);
    }
    if (result == VK_SUCCESS && WorldCompositionActive())
    {
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.renderPass = _presentRenderPass;
        pipelineInfo.subpass = 0;
        result = vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                           &_cockpitScenePipeline);
    }

    // Keep the shader modules alive so the pipeline cache can create variants.
    // They will be destroyed in DestroyScenePipelineLayout.
    _sceneVertexModule = vertexModule;
    _sceneFragmentModule = fragmentModule;
    _scenePrepassFragmentModule = prepassFragmentModule;

    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: scene pipeline creation failed: {}", VkResultName(result));
        return false;
    }

    SetObjectName(VK_OBJECT_TYPE_PIPELINE, VulkanObjectHandle(_scenePipeline), "PoseidonVK Scene Quad Pipeline");
    if (_cockpitScenePipeline)
        SetObjectName(VK_OBJECT_TYPE_PIPELINE, VulkanObjectHandle(_cockpitScenePipeline),
                      "PoseidonVK Cockpit Scene Pipeline");
    if (_worldLateScenePipeline)
        SetObjectName(VK_OBJECT_TYPE_PIPELINE, VulkanObjectHandle(_worldLateScenePipeline),
                      "PoseidonVK World Late Scene Pipeline");

    // Initialise the pipeline cache with the fixed state shared across all variants.
    _scenePipelineCache.Init(_device, _renderPass, _scenePipelineLayout, _sceneVertexModule, _sceneFragmentModule,
                              vertexInput, inputAssembly, viewportState, multisampling);
    if (_worldPrepassRenderPass)
        _worldPrepassScenePipelineCache.Init(_device, _worldPrepassRenderPass, _scenePipelineLayout,
                                             _sceneVertexModule, _scenePrepassFragmentModule, vertexInput,
                                             inputAssembly, viewportState, multisampling);
    if (_volumetricCloudsEnabled)
    {
        _worldLateScenePipelineCache.Init(_device, _worldLateRenderPass, _scenePipelineLayout, _sceneVertexModule,
                                           _sceneFragmentModule, vertexInput, inputAssembly, viewportState,
                                           multisampling);
    }
    if (WorldCompositionActive())
    {
        _cockpitScenePipelineCache.Init(_device, _presentRenderPass, _scenePipelineLayout, _sceneVertexModule,
                                        _sceneFragmentModule, vertexInput, inputAssembly, viewportState,
                                       multisampling);
    }

    LOG_INFO(Graphics, "Vulkan: scene pipeline created");
    return true;
}

bool EngineVK::CreateProceduralSkyPipeline()
{
    if (_proceduralSkyPipeline)
    {
        vkDestroyPipeline(_device, _proceduralSkyPipeline, nullptr);
        _proceduralSkyPipeline = VK_NULL_HANDLE;
    }
    if (_skyMapBakePipeline)
    {
        vkDestroyPipeline(_device, _skyMapBakePipeline, nullptr);
        _skyMapBakePipeline = VK_NULL_HANDLE;
    }
    std::vector<uint32_t> vertexSpirv;
    std::vector<uint32_t> fragmentSpirv;
    std::vector<uint32_t> bakeVertexSpirv;
    std::vector<uint32_t> bakeFragmentSpirv;
    std::string error;
    if (!CompileBootstrapShader(kProceduralSkyVertexShader, EShLangVertex, vertexSpirv, error) ||
        !CompileBootstrapShader(kProceduralSkyFragmentShader, EShLangFragment, fragmentSpirv, error) ||
        !CompileBootstrapShader(kSkyMapBakeVertexShader, EShLangVertex, bakeVertexSpirv, error) ||
        !CompileBootstrapShader(kSkyMapBakeFragmentShader, EShLangFragment, bakeFragmentSpirv, error))
    {
        LOG_ERROR(Graphics, "Vulkan: cached sky shader compile failed: {}", error);
        return false;
    }

    auto createShaderModule = [&](const std::vector<uint32_t>& spirv, VkShaderModule& module)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = spirv.size() * sizeof(uint32_t);
        createInfo.pCode = spirv.data();
        return vkCreateShaderModule(_device, &createInfo, nullptr, &module) == VK_SUCCESS;
    };

    VkShaderModule vertexModule = VK_NULL_HANDLE;
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    VkShaderModule bakeVertexModule = VK_NULL_HANDLE;
    VkShaderModule bakeFragmentModule = VK_NULL_HANDLE;
    if (!createShaderModule(vertexSpirv, vertexModule) || !createShaderModule(fragmentSpirv, fragmentModule) ||
        !createShaderModule(bakeVertexSpirv, bakeVertexModule) || !createShaderModule(bakeFragmentSpirv, bakeFragmentModule))
    {
        if (vertexModule) vkDestroyShaderModule(_device, vertexModule, nullptr);
        if (fragmentModule) vkDestroyShaderModule(_device, fragmentModule, nullptr);
        if (bakeVertexModule) vkDestroyShaderModule(_device, bakeVertexModule, nullptr);
        if (bakeFragmentModule) vkDestroyShaderModule(_device, bakeFragmentModule, nullptr);
        LOG_ERROR(Graphics, "Vulkan: cached sky shader module creation failed");
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
    viewport.y = static_cast<float>(_swapchainExtent.height);
    viewport.width = static_cast<float>(_swapchainExtent.width);
    viewport.height = -static_cast<float>(_swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
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
    VkPipelineDepthStencilStateCreateInfo depthStencil = vk::BuildDepthStencilState(render::DepthMode::Disabled);
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
    pipelineInfo.layout = _skyMapPipelineLayout;
    pipelineInfo.renderPass = _renderPass;

    VkResult result = vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_proceduralSkyPipeline);
    if (result == VK_SUCCESS)
    {
        shaderStages[0].module = bakeVertexModule;
        shaderStages[1].module = bakeFragmentModule;
        VkViewport bakeViewport{};
        bakeViewport.width = static_cast<float>(_skyMap.width);
        bakeViewport.height = static_cast<float>(_skyMap.height);
        bakeViewport.minDepth = 0.0f;
        bakeViewport.maxDepth = 1.0f;
        VkRect2D bakeScissor{};
        bakeScissor.extent = {_skyMap.width, _skyMap.height};
        viewportState.pViewports = &bakeViewport;
        viewportState.pScissors = &bakeScissor;
        pipelineInfo.layout = _skyMapBakePipelineLayout;
        pipelineInfo.renderPass = _skyMapRenderPass;
        result = vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_skyMapBakePipeline);
    }
    vkDestroyShaderModule(_device, bakeFragmentModule, nullptr);
    vkDestroyShaderModule(_device, bakeVertexModule, nullptr);
    vkDestroyShaderModule(_device, fragmentModule, nullptr);
    vkDestroyShaderModule(_device, vertexModule, nullptr);
    if (result != VK_SUCCESS)
    {
        if (_proceduralSkyPipeline)
        {
            vkDestroyPipeline(_device, _proceduralSkyPipeline, nullptr);
            _proceduralSkyPipeline = VK_NULL_HANDLE;
        }
        LOG_ERROR(Graphics, "Vulkan: cached sky pipeline creation failed: {}", VkResultName(result));
        return false;
    }

    SetObjectName(VK_OBJECT_TYPE_PIPELINE, VulkanObjectHandle(_proceduralSkyPipeline),
                  "PoseidonVK Cached Sky Display Pipeline");
    SetObjectName(VK_OBJECT_TYPE_PIPELINE, VulkanObjectHandle(_skyMapBakePipeline), "PoseidonVK HDR Sky Map Bake Pipeline");
    LOG_INFO(Graphics, "Vulkan: HDR cached sky-map pipelines created");
    return true;
}

bool EngineVK::CreateCloudComputeDescriptorLayouts()
{
    const std::array<VkDescriptorSetLayoutBinding, 2> generationBindings = {{
        {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    }};
    VkDescriptorSetLayoutCreateInfo generationInfo{};
    generationInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    generationInfo.bindingCount = static_cast<std::uint32_t>(generationBindings.size());
    generationInfo.pBindings = generationBindings.data();
    if (vkCreateDescriptorSetLayout(_device, &generationInfo, nullptr, &_cloudGenerationDescriptorSetLayout) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: cloud generation compute descriptor layout creation failed");
        return false;
    }

    const std::array<VkDescriptorSetLayoutBinding, 2> lightingBindings = {{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    }};
    VkDescriptorSetLayoutCreateInfo lightingInfo{};
    lightingInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lightingInfo.bindingCount = static_cast<std::uint32_t>(lightingBindings.size());
    lightingInfo.pBindings = lightingBindings.data();
    if (vkCreateDescriptorSetLayout(_device, &lightingInfo, nullptr, &_cloudLightingDescriptorSetLayout) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: cloud lighting compute descriptor layout creation failed");
        vkDestroyDescriptorSetLayout(_device, _cloudGenerationDescriptorSetLayout, nullptr);
        _cloudGenerationDescriptorSetLayout = VK_NULL_HANDLE;
        return false;
    }
    SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, VulkanObjectHandle(_cloudGenerationDescriptorSetLayout),
                  "PoseidonVK Cloud Generation Compute Descriptor Layout");
    SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, VulkanObjectHandle(_cloudLightingDescriptorSetLayout),
                  "PoseidonVK Cloud Lighting Compute Descriptor Layout");
    return true;
}

bool EngineVK::CreateCloudComputePipelineLayouts()
{
    const VkDescriptorSetLayout generationLayouts[] = {_frameDescriptorSetLayout, _cloudGenerationDescriptorSetLayout};
    VkPipelineLayoutCreateInfo generationInfo{};
    generationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    generationInfo.setLayoutCount = 2;
    generationInfo.pSetLayouts = generationLayouts;
    if (vkCreatePipelineLayout(_device, &generationInfo, nullptr, &_cloudGenerationPipelineLayout) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: cloud generation compute pipeline layout creation failed");
        return false;
    }
    const VkDescriptorSetLayout lightingLayouts[] = {_frameDescriptorSetLayout, _cloudLightingDescriptorSetLayout};
    VkPipelineLayoutCreateInfo lightingInfo{};
    lightingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lightingInfo.setLayoutCount = 2;
    lightingInfo.pSetLayouts = lightingLayouts;
    if (vkCreatePipelineLayout(_device, &lightingInfo, nullptr, &_cloudLightingPipelineLayout) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: cloud lighting compute pipeline layout creation failed");
        vkDestroyPipelineLayout(_device, _cloudGenerationPipelineLayout, nullptr);
        _cloudGenerationPipelineLayout = VK_NULL_HANDLE;
        return false;
    }
    SetObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, VulkanObjectHandle(_cloudGenerationPipelineLayout),
                  "PoseidonVK Cloud Generation Compute Pipeline Layout");
    SetObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, VulkanObjectHandle(_cloudLightingPipelineLayout),
                  "PoseidonVK Cloud Lighting Compute Pipeline Layout");
    return true;
}

bool EngineVK::CreateCloudComputeDescriptorSets()
{
    DestroyCloudComputeDescriptorResources();
    if (!_cloudDensityVolume.view || !_cloudDistanceVolume.view || !_cloudLightVolumes[0].view || !_cloudSampler ||
        !_cloudGenerationDescriptorSetLayout || !_cloudLightingDescriptorSetLayout)
        return false;
    const std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2},
    }};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 3;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_cloudComputeDescriptorPool) != VK_SUCCESS)
        return false;

    const VkDescriptorSetLayout layouts[] = {_cloudGenerationDescriptorSetLayout, _cloudLightingDescriptorSetLayout,
                                              _cloudLightingDescriptorSetLayout};
    VkDescriptorSet sets[3] = {};
    VkDescriptorSetAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocation.descriptorPool = _cloudComputeDescriptorPool;
    allocation.descriptorSetCount = 3;
    allocation.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(_device, &allocation, sets) != VK_SUCCESS)
    {
        DestroyCloudComputeDescriptorResources();
        return false;
    }
    _cloudGenerationDescriptorSet = sets[0];
    _cloudLightingDescriptorSets = {sets[1], sets[2]};

    VkDescriptorImageInfo generationImages[2] = {
        {VK_NULL_HANDLE, _cloudDensityVolume.view, VK_IMAGE_LAYOUT_GENERAL},
        {VK_NULL_HANDLE, _cloudDistanceVolume.view, VK_IMAGE_LAYOUT_GENERAL},
    };
    VkWriteDescriptorSet generationWrites[2] = {};
    for (std::uint32_t binding = 0; binding < 2; ++binding)
    {
        generationWrites[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        generationWrites[binding].dstSet = _cloudGenerationDescriptorSet;
        generationWrites[binding].dstBinding = binding;
        generationWrites[binding].descriptorCount = 1;
        generationWrites[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        generationWrites[binding].pImageInfo = &generationImages[binding];
    }
    vkUpdateDescriptorSets(_device, 2, generationWrites, 0, nullptr);

    for (std::uint32_t i = 0; i < _cloudLightingDescriptorSets.size(); ++i)
    {
        VkDescriptorImageInfo densityInfo{_cloudSampler, _cloudDensityVolume.view,
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo lightInfo{VK_NULL_HANDLE, _cloudLightVolumes[i].view, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet writes[2] = {};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _cloudLightingDescriptorSets[i], 0, 0, 1,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &densityInfo, nullptr, nullptr};
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _cloudLightingDescriptorSets[i], 1, 0, 1,
                     VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &lightInfo, nullptr, nullptr};
        vkUpdateDescriptorSets(_device, 2, writes, 0, nullptr);
    }
    SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_POOL, VulkanObjectHandle(_cloudComputeDescriptorPool),
                  "PoseidonVK Cloud Compute Descriptor Pool");
    return true;
}

bool EngineVK::CreateCloudComputePipelines()
{
    const auto destroyPipeline = [&](VkPipeline& pipeline)
    {
        if (pipeline)
            vkDestroyPipeline(_device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    };
    destroyPipeline(_cloudDensityErosionPipeline);
    destroyPipeline(_cloudDistanceFieldPipeline);
    destroyPipeline(_cloudLightMapPipeline);
    const auto createPipeline = [&](const char* source, VkPipelineLayout layout, VkPipeline& pipeline, const char* name)
    {
        std::vector<std::uint32_t> spirv;
        std::string error;
        if (!CompileBootstrapShader(source, EShLangCompute, spirv, error))
        {
            LOG_ERROR(Graphics, "Vulkan: {} compile failed: {}", name, error);
            return false;
        }
        VkShaderModuleCreateInfo moduleInfo{};
        moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleInfo.codeSize = spirv.size() * sizeof(std::uint32_t);
        moduleInfo.pCode = spirv.data();
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(_device, &moduleInfo, nullptr, &module) != VK_SUCCESS)
            return false;
        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = module;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = layout;
        const VkResult result = vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
        vkDestroyShaderModule(_device, module, nullptr);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "Vulkan: {} creation failed: {}", name, VkResultName(result));
            return false;
        }
        SetObjectName(VK_OBJECT_TYPE_PIPELINE, VulkanObjectHandle(pipeline), name);
        return true;
    };
    if (!createPipeline(kCloudDensityErosionComputeShader, _cloudGenerationPipelineLayout, _cloudDensityErosionPipeline,
                        "PoseidonVK Cloud Density Erosion Compute Pipeline") ||
        !createPipeline(kCloudDistanceFieldComputeShader, _cloudGenerationPipelineLayout, _cloudDistanceFieldPipeline,
                        "PoseidonVK Cloud Distance Field Compute Pipeline") ||
        !createPipeline(kCloudLightMapComputeShader, _cloudLightingPipelineLayout, _cloudLightMapPipeline,
                        "PoseidonVK Cloud Light Map Compute Pipeline"))
    {
        destroyPipeline(_cloudDensityErosionPipeline);
        destroyPipeline(_cloudDistanceFieldPipeline);
        destroyPipeline(_cloudLightMapPipeline);
        return false;
    }
    return true;
}

bool EngineVK::CreateVolumetricCloudDescriptorLayout()
{
    const std::array<VkDescriptorSetLayoutBinding, 7> bindings = {{
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    }};

    VkDescriptorSetLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    createInfo.pBindings = bindings.data();
    const VkResult result = vkCreateDescriptorSetLayout(_device, &createInfo, nullptr,
                                                        &_volumetricCloudDescriptorSetLayout);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: cloud descriptor layout creation failed: {}", VkResultName(result));
        return false;
    }
    SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, VulkanObjectHandle(_volumetricCloudDescriptorSetLayout),
                  "PoseidonVK Temporal Cloud Descriptor Layout");
    return true;
}

bool EngineVK::CreateVolumetricCloudPipelineLayout()
{
    VkDescriptorSetLayout setLayouts[] = {_frameDescriptorSetLayout, _volumetricCloudDescriptorSetLayout};
    VkPipelineLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    createInfo.setLayoutCount = 2;
    createInfo.pSetLayouts = setLayouts;
    const VkResult result = vkCreatePipelineLayout(_device, &createInfo, nullptr, &_volumetricCloudPipelineLayout);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: volumetric cloud pipeline layout creation failed: {}", VkResultName(result));
        return false;
    }
    SetObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, VulkanObjectHandle(_volumetricCloudPipelineLayout),
                  "PoseidonVK Volumetric Cloud Pipeline Layout");
    return true;
}

bool EngineVK::CreateVolumetricCloudDescriptorSet()
{
    DestroyVolumetricCloudDescriptorResources();
    if (!_worldDepthImageView || !_cloudDensityVolume.view || !_cloudDistanceVolume.view || !_cloudLightVolumes[0].view ||
        !_cloudCurrent.view || !_cloudHistory[0].view ||
        !_cloudConstantsBuffer.buffer || !_volumetricCloudDescriptorSetLayout)
        return false;

    const std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 12},
    }};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 2;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    VkResult result = vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_volumetricCloudDescriptorPool);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: cloud descriptor pool creation failed: {}", VkResultName(result));
        return false;
    }

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = _volumetricCloudDescriptorPool;
    VkDescriptorSetLayout layouts[] = {_volumetricCloudDescriptorSetLayout, _volumetricCloudDescriptorSetLayout};
    allocateInfo.descriptorSetCount = 2;
    allocateInfo.pSetLayouts = layouts;
    result = vkAllocateDescriptorSets(_device, &allocateInfo, _volumetricCloudDescriptorSets.data());
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: cloud descriptor set allocation failed: {}", VkResultName(result));
        DestroyVolumetricCloudDescriptorResources();
        return false;
    }

    UpdateCloudDescriptorSets(0);
    _volumetricCloudDescriptorSet = _volumetricCloudDescriptorSets[0];
    SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_POOL, VulkanObjectHandle(_volumetricCloudDescriptorPool),
                  "PoseidonVK Temporal Cloud Descriptor Pool");
    return true;
}

void EngineVK::UpdateCloudDescriptorSets(std::uint32_t historyReadIndex)
{
    if (!_volumetricCloudDescriptorSets[0] || !_cloudSampler)
        return;
    for (std::uint32_t i = 0; i < _volumetricCloudDescriptorSets.size(); ++i)
    {
        const std::uint32_t historyIndex = i == 0 ? historyReadIndex : 1 - historyReadIndex;
        VkDescriptorBufferInfo constants{_cloudConstantsBuffer.buffer, 0, sizeof(vk::CloudConstantsVK)};
        VkDescriptorImageInfo images[6] = {};
        images[0] = {_cloudSampler, _worldDepthImageView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        images[1] = {_cloudSampler, _cloudDensityVolume.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        images[2] = {_cloudSampler, _cloudDistanceVolume.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        images[3] = {_cloudSampler, _cloudLightVolumes[_cloudLightVolumeReadIndex].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        images[4] = {_cloudSampler, _cloudCurrent.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        images[5] = {_cloudSampler, _cloudHistory[historyIndex].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet writes[7] = {};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _volumetricCloudDescriptorSets[i], 0, 0, 1,
                     VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &constants, nullptr};
        for (std::uint32_t binding = 1; binding < 7; ++binding)
            writes[binding] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _volumetricCloudDescriptorSets[i], binding,
                               0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &images[binding - 1], nullptr, nullptr};
        vkUpdateDescriptorSets(_device, 7, writes, 0, nullptr);
    }
}

bool EngineVK::CreateVolumetricCloudPipeline()
{
    const std::array<VkPipeline*, 3> cloudPipelines = {
        &_volumetricCloudPipeline, &_cloudTemporalPipeline, &_cloudCompositePipeline};
    for (VkPipeline* pipeline : cloudPipelines)
    {
        if (*pipeline)
        {
            vkDestroyPipeline(_device, *pipeline, nullptr);
            *pipeline = VK_NULL_HANDLE;
        }
    }

    std::vector<uint32_t> vertexSpirv;
    std::vector<uint32_t> fragmentSpirv;
    std::string error;
    if (!CompileBootstrapShader(kVolumetricCloudVertexShader, EShLangVertex, vertexSpirv, error) ||
        !CompileBootstrapShader(kVolumetricCloudFragmentShader, EShLangFragment, fragmentSpirv, error))
    {
        LOG_ERROR(Graphics, "Vulkan: volumetric cloud shader compile failed: {}", error);
        return false;
    }

    auto createShaderModule = [&](const std::vector<uint32_t>& spirv, VkShaderModule& module)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = spirv.size() * sizeof(uint32_t);
        createInfo.pCode = spirv.data();
        return vkCreateShaderModule(_device, &createInfo, nullptr, &module) == VK_SUCCESS;
    };
    VkShaderModule vertexModule = VK_NULL_HANDLE;
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    if (!createShaderModule(vertexSpirv, vertexModule) || !createShaderModule(fragmentSpirv, fragmentModule))
    {
        if (vertexModule)
            vkDestroyShaderModule(_device, vertexModule, nullptr);
        if (fragmentModule)
            vkDestroyShaderModule(_device, fragmentModule, nullptr);
        LOG_ERROR(Graphics, "Vulkan: volumetric cloud shader module creation failed");
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
    viewport.y = static_cast<float>(_cloudCurrent.height);
    viewport.width = static_cast<float>(_cloudCurrent.width);
    viewport.height = -static_cast<float>(_cloudCurrent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent = {_cloudCurrent.width, _cloudCurrent.height};
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
    VkPipelineDepthStencilStateCreateInfo depthStencil = vk::BuildDepthStencilState(render::DepthMode::Disabled);
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
    pipelineInfo.layout = _volumetricCloudPipelineLayout;
    pipelineInfo.renderPass = _cloudRaymarchRenderPass;
    const VkResult result =
        vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_volumetricCloudPipeline);
    vkDestroyShaderModule(_device, fragmentModule, nullptr);
    vkDestroyShaderModule(_device, vertexModule, nullptr);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: volumetric cloud pipeline creation failed: {}", VkResultName(result));
        return false;
    }
    SetObjectName(VK_OBJECT_TYPE_PIPELINE, VulkanObjectHandle(_volumetricCloudPipeline),
                  "PoseidonVK Cloud Raymarch Pipeline");

    const auto createAdditionalPipeline = [&](const char* fragmentSource, VkRenderPass renderPass, VkPipeline& output,
                                              bool composite)
    {
        std::vector<uint32_t> vs;
        std::vector<uint32_t> fs;
        std::string compileError;
        if (!CompileBootstrapShader(kVolumetricCloudVertexShader, EShLangVertex, vs, compileError) ||
            !CompileBootstrapShader(fragmentSource, EShLangFragment, fs, compileError))
            return false;
        VkShaderModule vm = VK_NULL_HANDLE;
        VkShaderModule fm = VK_NULL_HANDLE;
        if (!createShaderModule(vs, vm) || !createShaderModule(fs, fm))
        {
            if (vm) vkDestroyShaderModule(_device, vm, nullptr);
            if (fm) vkDestroyShaderModule(_device, fm, nullptr);
            return false;
        }
        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vm;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fm;
        stages[1].pName = "main";
        VkViewport passViewport = viewport;
        VkRect2D passScissor = scissor;
        if (composite)
        {
            passViewport.y = static_cast<float>(_swapchainExtent.height);
            passViewport.width = static_cast<float>(_swapchainExtent.width);
            passViewport.height = -static_cast<float>(_swapchainExtent.height);
            passScissor.extent = _swapchainExtent;
        }
        VkPipelineViewportStateCreateInfo passViewportState = viewportState;
        passViewportState.pViewports = &passViewport;
        passViewportState.pScissors = &passScissor;
        VkPipelineColorBlendAttachmentState passBlend = colorBlendAttachment;
        if (composite)
        {
            passBlend.blendEnable = VK_TRUE;
            passBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            passBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            passBlend.colorBlendOp = VK_BLEND_OP_ADD;
            passBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            passBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            passBlend.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        VkPipelineColorBlendStateCreateInfo passColorBlend = colorBlending;
        passColorBlend.pAttachments = &passBlend;
        VkGraphicsPipelineCreateInfo info = pipelineInfo;
        info.pStages = stages;
        info.pViewportState = &passViewportState;
        info.pColorBlendState = &passColorBlend;
        info.renderPass = renderPass;
        const VkResult pipelineResult = vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &info, nullptr, &output);
        vkDestroyShaderModule(_device, fm, nullptr);
        vkDestroyShaderModule(_device, vm, nullptr);
        return pipelineResult == VK_SUCCESS;
    };
    if (!createAdditionalPipeline(kCloudTemporalFragmentShader, _cloudTemporalRenderPass, _cloudTemporalPipeline, false) ||
        !createAdditionalPipeline(kCloudCompositeFragmentShader, _cloudCompositeRenderPass, _cloudCompositePipeline, true))
    {
        LOG_ERROR(Graphics, "Vulkan: temporal cloud pass pipeline creation failed");
        return false;
    }
    LOG_INFO(Graphics, "Vulkan: persistent 3D cloud field, raymarch, temporal, and composite pipelines created");
    return true;
}

void EngineVK::DestroyVolumetricCloudDescriptorResources()
{
    _volumetricCloudDescriptorSet = VK_NULL_HANDLE;
    _volumetricCloudDescriptorSets = {};
    if (_device && _volumetricCloudDescriptorPool)
    {
        vkDestroyDescriptorPool(_device, _volumetricCloudDescriptorPool, nullptr);
        _volumetricCloudDescriptorPool = VK_NULL_HANDLE;
    }
}

void EngineVK::DestroyCloudComputeDescriptorResources()
{
    _cloudGenerationDescriptorSet = VK_NULL_HANDLE;
    _cloudLightingDescriptorSets = {};
    if (_device && _cloudComputeDescriptorPool)
        vkDestroyDescriptorPool(_device, _cloudComputeDescriptorPool, nullptr);
    _cloudComputeDescriptorPool = VK_NULL_HANDLE;
}

void EngineVK::DestroyCloudComputePipelineResources()
{
    const std::array<VkPipeline*, 3> pipelines = {
        &_cloudDensityErosionPipeline, &_cloudDistanceFieldPipeline, &_cloudLightMapPipeline};
    for (VkPipeline* pipeline : pipelines)
    {
        if (_device && *pipeline)
            vkDestroyPipeline(_device, *pipeline, nullptr);
        *pipeline = VK_NULL_HANDLE;
    }
    if (_device && _cloudGenerationPipelineLayout)
        vkDestroyPipelineLayout(_device, _cloudGenerationPipelineLayout, nullptr);
    _cloudGenerationPipelineLayout = VK_NULL_HANDLE;
    if (_device && _cloudLightingPipelineLayout)
        vkDestroyPipelineLayout(_device, _cloudLightingPipelineLayout, nullptr);
    _cloudLightingPipelineLayout = VK_NULL_HANDLE;
    if (_device && _cloudGenerationDescriptorSetLayout)
        vkDestroyDescriptorSetLayout(_device, _cloudGenerationDescriptorSetLayout, nullptr);
    _cloudGenerationDescriptorSetLayout = VK_NULL_HANDLE;
    if (_device && _cloudLightingDescriptorSetLayout)
        vkDestroyDescriptorSetLayout(_device, _cloudLightingDescriptorSetLayout, nullptr);
    _cloudLightingDescriptorSetLayout = VK_NULL_HANDLE;
}

void EngineVK::DestroyVolumetricCloudPipelineLayout()
{
    if (_device && _volumetricCloudPipelineLayout)
    {
        vkDestroyPipelineLayout(_device, _volumetricCloudPipelineLayout, nullptr);
        _volumetricCloudPipelineLayout = VK_NULL_HANDLE;
    }
    if (_device && _volumetricCloudDescriptorSetLayout)
    {
        vkDestroyDescriptorSetLayout(_device, _volumetricCloudDescriptorSetLayout, nullptr);
        _volumetricCloudDescriptorSetLayout = VK_NULL_HANDLE;
    }
}

bool EngineVK::CreateWorldCompositeDescriptorLayout()
{
    const std::array<VkDescriptorSetLayoutBinding, 3> bindings = {{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    }};
    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.pBindings = bindings.data();
    return vkCreateDescriptorSetLayout(_device, &info, nullptr, &_worldCompositeDescriptorSetLayout) == VK_SUCCESS;
}

bool EngineVK::CreateWorldCompositePipelineLayout()
{
    VkPushConstantRange pushConstants{};
    pushConstants.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstants.size = sizeof(WorldCompositePushConstants);
    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = 1;
    info.pSetLayouts = &_worldCompositeDescriptorSetLayout;
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &pushConstants;
    return vkCreatePipelineLayout(_device, &info, nullptr, &_worldCompositePipelineLayout) == VK_SUCCESS;
}

bool EngineVK::CreateWorldCompositeDescriptorSet()
{
    DestroyWorldCompositeDescriptorResources();
    if (!_worldColorImageView || !_frameConstantsBuffer.buffer || !_worldCompositeDescriptorSetLayout ||
         (_temporalExposureEnabled && !_eyeAdaptationHistory[0].view))
        return false;
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    if (vkCreateSampler(_device, &samplerInfo, nullptr, &_worldCompositeSampler) != VK_SUCCESS)
        return false;
    const std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
    }};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_worldCompositeDescriptorPool) != VK_SUCCESS)
        return false;
    VkDescriptorSetAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocation.descriptorPool = _worldCompositeDescriptorPool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &_worldCompositeDescriptorSetLayout;
    if (vkAllocateDescriptorSets(_device, &allocation, &_worldCompositeDescriptorSet) != VK_SUCCESS)
        return false;
    UpdateWorldCompositeDescriptorSet(0);
    return true;
}

void EngineVK::UpdateWorldCompositeDescriptorSet(std::uint32_t exposureHistoryIndex)
{
    if (!_worldCompositeDescriptorSet || exposureHistoryIndex >= _eyeAdaptationHistory.size())
        return;
    VkDescriptorImageInfo imageInfos[2]{};
    imageInfos[0].sampler = _worldCompositeSampler;
    imageInfos[0].imageView = _worldColorImageView;
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[1].sampler = _worldCompositeSampler;
    imageInfos[1].imageView = _temporalExposureEnabled ? _eyeAdaptationHistory[exposureHistoryIndex].view : _worldColorImageView;
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorBufferInfo frameInfo{};
    frameInfo.buffer = _frameConstantsBuffer.buffer;
    frameInfo.range = sizeof(vk::FrameConstantsVK);
    std::array<VkWriteDescriptorSet, 3> writes{};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _worldCompositeDescriptorSet, 0, 0, 1,
                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfos[0], nullptr, nullptr};
    writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _worldCompositeDescriptorSet, 1, 0, 1,
                 VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &frameInfo, nullptr};
    writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _worldCompositeDescriptorSet, 2, 0, 1,
                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfos[1], nullptr, nullptr};
    vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void EngineVK::DestroyWorldCompositeDescriptorResources()
{
    _worldCompositeDescriptorSet = VK_NULL_HANDLE;
    if (_worldCompositeDescriptorPool)
        vkDestroyDescriptorPool(_device, _worldCompositeDescriptorPool, nullptr);
    if (_worldCompositeSampler)
        vkDestroySampler(_device, _worldCompositeSampler, nullptr);
    _worldCompositeDescriptorPool = VK_NULL_HANDLE;
    _worldCompositeSampler = VK_NULL_HANDLE;
}

void EngineVK::DestroyWorldCompositePipelineLayout()
{
    if (_worldCompositePipelineLayout)
        vkDestroyPipelineLayout(_device, _worldCompositePipelineLayout, nullptr);
    if (_worldCompositeDescriptorSetLayout)
        vkDestroyDescriptorSetLayout(_device, _worldCompositeDescriptorSetLayout, nullptr);
    _worldCompositePipelineLayout = VK_NULL_HANDLE;
    _worldCompositeDescriptorSetLayout = VK_NULL_HANDLE;
}

bool EngineVK::CreateEyeAdaptationDescriptorLayout()
{
    const std::array<VkDescriptorSetLayoutBinding, 2> bindings = {{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    }};
    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.pBindings = bindings.data();
    return vkCreateDescriptorSetLayout(_device, &info, nullptr, &_eyeAdaptationDescriptorSetLayout) == VK_SUCCESS;
}

bool EngineVK::CreateEyeAdaptationDescriptorSet()
{
    DestroyEyeAdaptationDescriptorResources();
    if (!_eyeAdaptationHistory[0].view || !_eyeAdaptationHistory[1].view || !_frameConstantsBuffer.buffer ||
        !_eyeAdaptationDescriptorSetLayout)
        return false;
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    if (vkCreateSampler(_device, &samplerInfo, nullptr, &_eyeAdaptationSampler) != VK_SUCCESS)
        return false;
    const std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
    }};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = static_cast<uint32_t>(_eyeAdaptationDescriptorSets.size());
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_eyeAdaptationDescriptorPool) != VK_SUCCESS)
        return false;
    VkDescriptorSetAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocation.descriptorPool = _eyeAdaptationDescriptorPool;
    allocation.descriptorSetCount = static_cast<uint32_t>(_eyeAdaptationDescriptorSets.size());
    std::array<VkDescriptorSetLayout, 2> layouts = {_eyeAdaptationDescriptorSetLayout, _eyeAdaptationDescriptorSetLayout};
    allocation.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(_device, &allocation, _eyeAdaptationDescriptorSets.data()) != VK_SUCCESS)
        return false;
    for (std::uint32_t writeIndex = 0; writeIndex < _eyeAdaptationDescriptorSets.size(); ++writeIndex)
    {
        VkDescriptorImageInfo historyInfo{};
        historyInfo.sampler = _eyeAdaptationSampler;
        historyInfo.imageView = _eyeAdaptationHistory[1 - writeIndex].view;
        historyInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorBufferInfo frameInfo{};
        frameInfo.buffer = _frameConstantsBuffer.buffer;
        frameInfo.range = sizeof(vk::FrameConstantsVK);
        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = _eyeAdaptationDescriptorSets[writeIndex];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &historyInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = _eyeAdaptationDescriptorSets[writeIndex];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].pBufferInfo = &frameInfo;
        vkUpdateDescriptorSets(_device, 2, writes, 0, nullptr);
    }
    return true;
}

bool EngineVK::CreateEyeAdaptationPipelineLayout()
{
    VkPushConstantRange pushConstants{};
    pushConstants.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstants.size = sizeof(EyeAdaptationPushConstants);
    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = 1;
    info.pSetLayouts = &_eyeAdaptationDescriptorSetLayout;
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &pushConstants;
    return vkCreatePipelineLayout(_device, &info, nullptr, &_eyeAdaptationPipelineLayout) == VK_SUCCESS;
}

void EngineVK::DestroyEyeAdaptationDescriptorResources()
{
    _eyeAdaptationDescriptorSets = {};
    if (_eyeAdaptationDescriptorPool)
        vkDestroyDescriptorPool(_device, _eyeAdaptationDescriptorPool, nullptr);
    if (_eyeAdaptationSampler)
        vkDestroySampler(_device, _eyeAdaptationSampler, nullptr);
    _eyeAdaptationDescriptorPool = VK_NULL_HANDLE;
    _eyeAdaptationSampler = VK_NULL_HANDLE;
}

void EngineVK::DestroyEyeAdaptationPipelineLayout()
{
    if (_eyeAdaptationPipelineLayout)
        vkDestroyPipelineLayout(_device, _eyeAdaptationPipelineLayout, nullptr);
    if (_eyeAdaptationDescriptorSetLayout)
        vkDestroyDescriptorSetLayout(_device, _eyeAdaptationDescriptorSetLayout, nullptr);
    _eyeAdaptationPipelineLayout = VK_NULL_HANDLE;
    _eyeAdaptationDescriptorSetLayout = VK_NULL_HANDLE;
}

bool EngineVK::CreateEyeAdaptationPipeline()
{
    if (_eyeAdaptationPipeline)
        vkDestroyPipeline(_device, _eyeAdaptationPipeline, nullptr);
    _eyeAdaptationPipeline = VK_NULL_HANDLE;
    std::vector<uint32_t> vertexSpirv;
    std::vector<uint32_t> fragmentSpirv;
    std::string error;
    if (!CompileBootstrapShader(kWorldCompositeVertexShader, EShLangVertex, vertexSpirv, error) ||
        !CompileBootstrapShader(kEyeAdaptationFragmentShader, EShLangFragment, fragmentSpirv, error))
        return false;
    auto createModule = [&](const std::vector<uint32_t>& spirv, VkShaderModule& module)
    {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = spirv.size() * sizeof(uint32_t);
        info.pCode = spirv.data();
        return vkCreateShaderModule(_device, &info, nullptr, &module) == VK_SUCCESS;
    };
    VkShaderModule vertexModule = VK_NULL_HANDLE;
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    if (!createModule(vertexSpirv, vertexModule) || !createModule(fragmentSpirv, fragmentModule))
        return false;
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertexModule, "main"};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragmentModule, "main"};
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {1, 1}};
    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo rasterizer =
        vk::BuildRasterizationState(render::CullMode::None, render::FrontFaceMode::CW);
    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth = vk::BuildDepthStencilState(render::DepthMode::Disabled);
    VkPipelineColorBlendAttachmentState blend = vk::BuildColorBlendAttachmentState(render::BlendMode::Opaque);
    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewportState;
    info.pRasterizationState = &rasterizer;
    info.pMultisampleState = &multisampling;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blending;
    info.layout = _eyeAdaptationPipelineLayout;
    info.renderPass = _eyeAdaptationRenderPass;
    const VkResult result = vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &info, nullptr, &_eyeAdaptationPipeline);
    vkDestroyShaderModule(_device, fragmentModule, nullptr);
    vkDestroyShaderModule(_device, vertexModule, nullptr);
    return result == VK_SUCCESS;
}

bool EngineVK::CreateWorldCompositePipeline()
{
    if (_worldCompositePipeline)
        vkDestroyPipeline(_device, _worldCompositePipeline, nullptr);
    _worldCompositePipeline = VK_NULL_HANDLE;
    std::vector<uint32_t> vertexSpirv;
    std::vector<uint32_t> fragmentSpirv;
    std::string error;
    if (!CompileBootstrapShader(kWorldCompositeVertexShader, EShLangVertex, vertexSpirv, error) ||
        !CompileBootstrapShader(kWorldCompositeFragmentShader, EShLangFragment, fragmentSpirv, error))
        return false;
    auto createModule = [&](const std::vector<uint32_t>& spirv, VkShaderModule& module)
    {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = spirv.size() * sizeof(uint32_t);
        info.pCode = spirv.data();
        return vkCreateShaderModule(_device, &info, nullptr, &module) == VK_SUCCESS;
    };
    VkShaderModule vertexModule = VK_NULL_HANDLE;
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    if (!createModule(vertexSpirv, vertexModule) || !createModule(fragmentSpirv, fragmentModule))
        return false;
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertexModule, "main"};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragmentModule, "main"};
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(_swapchainExtent.width), static_cast<float>(_swapchainExtent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, _swapchainExtent};
    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo rasterizer =
        vk::BuildRasterizationState(render::CullMode::None, render::FrontFaceMode::CW);
    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth = vk::BuildDepthStencilState(render::DepthMode::Disabled);
    VkPipelineColorBlendAttachmentState blend = vk::BuildColorBlendAttachmentState(render::BlendMode::Opaque);
    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewportState;
    info.pRasterizationState = &rasterizer;
    info.pMultisampleState = &multisampling;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blending;
    info.layout = _worldCompositePipelineLayout;
    info.renderPass = _presentRenderPass;
    const VkResult result = vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &info, nullptr, &_worldCompositePipeline);
    vkDestroyShaderModule(_device, fragmentModule, nullptr);
    vkDestroyShaderModule(_device, vertexModule, nullptr);
    return result == VK_SUCCESS;
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
    pushConstants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
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
    if (_device)
    {
        _screenPipelineCache.Destroy(_device);
        _screenOverlayPipelineCache.Destroy(_device);
        if (_screenPipeline)
        {
            vkDestroyPipeline(_device, _screenPipeline, nullptr);
            _screenPipeline = VK_NULL_HANDLE;
        }
        if (_screenOverlayPipeline)
        {
            vkDestroyPipeline(_device, _screenOverlayPipeline, nullptr);
            _screenOverlayPipeline = VK_NULL_HANDLE;
        }
        if (_screenVertexModule)
        {
            vkDestroyShaderModule(_device, _screenVertexModule, nullptr);
            _screenVertexModule = VK_NULL_HANDLE;
        }
        if (_screenFragmentModule)
        {
            vkDestroyShaderModule(_device, _screenFragmentModule, nullptr);
            _screenFragmentModule = VK_NULL_HANDLE;
        }
    }
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

    VkPipelineDepthStencilStateCreateInfo depthStencil = vk::BuildDepthStencilState(render::DepthMode::Disabled);

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

    VkResult result =
        vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_screenPipeline);
    if (result == VK_SUCCESS && WorldCompositionActive())
    {
        pipelineInfo.renderPass = _presentRenderPass;
        pipelineInfo.subpass = 0;
        result = vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_screenOverlayPipeline);
    }

    _screenVertexModule = vertexModule;
    _screenFragmentModule = fragmentModule;

    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: screen pipeline creation failed: {}", VkResultName(result));
        return false;
    }

    SetObjectName(VK_OBJECT_TYPE_PIPELINE, VulkanObjectHandle(_screenPipeline), "PoseidonVK Screen Background Pipeline");
    if (_screenOverlayPipeline)
        SetObjectName(VK_OBJECT_TYPE_PIPELINE, VulkanObjectHandle(_screenOverlayPipeline), "PoseidonVK Screen Overlay Pipeline");
    _screenPipelineCache.Init(_device, _renderPass, _screenPipelineLayout, _screenVertexModule, _screenFragmentModule,
                               vertexInput, inputAssembly, viewportState, multisampling);
    if (WorldCompositionActive())
        _screenOverlayPipelineCache.Init(_device, _presentRenderPass, _screenPipelineLayout, _screenVertexModule,
                                          _screenFragmentModule, vertexInput, inputAssembly, viewportState, multisampling);
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

    if (!_shadowInFlight && vkCreateFence(_device, &fenceInfo, nullptr, &_shadowInFlight) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: failed to create shadow fence");
        return false;
    }
    SetObjectName(VK_OBJECT_TYPE_FENCE, VulkanObjectHandle(_shadowInFlight), "PoseidonVK Shadow Fence");

    if (_renderFinished.size() == _swapchainImages.size() &&
        std::all_of(_renderFinished.begin(), _renderFinished.end(),
                    [](VkSemaphore semaphore) { return semaphore != VK_NULL_HANDLE; }))
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

    auto fn =
        reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetDeviceProcAddr(_device, "vkCmdEndDebugUtilsLabelEXT"));
    if (fn)
        fn(commandBuffer);
}

void EngineVK::UploadFrameConstants()
{
    UpdateShadowFrameConstants();
    vk::UploadMappedBuffer(_frameConstantsBuffer, &_lastFrameConstants, sizeof(_lastFrameConstants));
}

bool EngineVK::CreateGpuTimingResources()
{
    VkQueryPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = 11;
    if (vkCreateQueryPool(_device, &info, nullptr, &_gpuTimingQueryPool) != VK_SUCCESS)
        return false;

    info.queryCount = 2;
    if (vkCreateQueryPool(_device, &info, nullptr, &_shadowTimingQueryPool) != VK_SUCCESS)
    {
        vkDestroyQueryPool(_device, _gpuTimingQueryPool, nullptr);
        _gpuTimingQueryPool = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void EngineVK::DestroyGpuTimingResources()
{
    if (_gpuTimingQueryPool)
        vkDestroyQueryPool(_device, _gpuTimingQueryPool, nullptr);
    if (_shadowTimingQueryPool)
        vkDestroyQueryPool(_device, _shadowTimingQueryPool, nullptr);
    _gpuTimingQueryPool = VK_NULL_HANDLE;
    _shadowTimingQueryPool = VK_NULL_HANDLE;
    _gpuTimingPending = false;
    _shadowTimingPending = false;
}

void EngineVK::LogGpuTimings()
{
    if ((!_gpuTimingPending && !_shadowTimingPending) || _timestampPeriodNs <= 0.0f)
        return;

    double shadowMs = 0.0;
    if (_shadowTimingPending && _shadowTimingQueryPool)
    {
        std::array<std::uint64_t, 2> shadowTimestamps{};
        const VkResult shadowResult = vkGetQueryPoolResults(
            _device, _shadowTimingQueryPool, 0, static_cast<uint32_t>(shadowTimestamps.size()),
            sizeof(shadowTimestamps), shadowTimestamps.data(), sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT);
        if (shadowResult == VK_SUCCESS)
        {
            _shadowTimingPending = false;
            shadowMs = static_cast<double>(shadowTimestamps[1] - shadowTimestamps[0]) * _timestampPeriodNs * 1e-6;
        }
    }

    if (!_gpuTimingPending || !_gpuTimingQueryPool)
        return;

    std::array<std::uint64_t, 11> timestamps{};
    const VkResult result = vkGetQueryPoolResults(_device, _gpuTimingQueryPool, 0,
                                                   static_cast<uint32_t>(timestamps.size()), sizeof(timestamps),
                                                   timestamps.data(), sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT);
    if (result != VK_SUCCESS)
        return;

    _gpuTimingPending = false;
    if (++_gpuTimingFrameCount % 300 != 1)
        return;

    const auto elapsedMs = [&](std::size_t begin, std::size_t end)
    {
        return static_cast<double>(timestamps[end] - timestamps[begin]) * _timestampPeriodNs * 1e-6;
    };
    LOG_INFO(Graphics,
              "Vulkan GPU ms: shadow={:.2f} world={:.2f} terrain={:.2f} opaque={:.2f} cutout={:.2f} other={:.2f} clouds={:.2f} late={:.2f} compositor={:.2f} present={:.2f}",
              shadowMs, elapsedMs(0, 7), elapsedMs(0, 1), elapsedMs(1, 2), elapsedMs(2, 3), elapsedMs(3, 4),
              elapsedMs(5, 6), elapsedMs(6, 7), elapsedMs(8, 9), elapsedMs(8, 10));
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

void EngineVK::UpdateSkyMapInvalidation()
{
    std::size_t index = 0;
    const auto append = [&](float value) { _skyMapRequestedInputs[index++] = value; };
    // Camera matrices deliberately do not participate: this is a world-space
    // environment map and must survive ordinary camera movement.
    append(_lastFrameConstants.lightingParams[0]);
    for (float value : _lastFrameConstants.sunDirection) append(value);
    for (float value : _lastFrameConstants.fogColor) append(value);
    for (float value : _lastFrameConstants.cloudWeather) append(value);
    // Geometry changes alter weather coverage, but cloudTime is animation and
    // belongs to the cloud subsystem rather than forcing a sky-map bake.
    for (std::size_t i = 0; i < 3; ++i) append(_lastFrameConstants.cloudGeometry[i]);
    for (float value : _lastFrameConstants.moonDirection) append(value);
    for (float value : _lastFrameConstants.moonUpAndPhase) append(value);
    for (const auto& row : _lastFrameConstants.starsOrientation)
        for (float value : row) append(value);
    for (float value : _lastFrameConstants.skyVisibility) append(value);
    while (index < _skyMapRequestedInputs.size()) _skyMapRequestedInputs[index++] = 0.0f;

    if (!_skyMapValid)
    {
        _skyMapDirty = true;
        return;
    }
    constexpr float kSkyCacheEpsilon = 1.0e-4f;
    for (std::size_t i = 0; i < _skyMapRequestedInputs.size(); ++i)
    {
        if (std::abs(_skyMapRequestedInputs[i] - _skyMapCachedInputs[i]) > kSkyCacheEpsilon)
        {
            _skyMapDirty = true;
            return;
        }
    }
}

bool EngineVK::RecordSkyMapBake(VkCommandBuffer commandBuffer)
{
    if (!_skyMapDirty)
        return true;
    if (!_skyMap.image || !_skyMapFramebuffer || !_skyMapRenderPass || !_skyMapBakePipeline || !_frameDescriptorSet)
    {
        LOG_ERROR(Graphics, "Vulkan: cannot bake sky map; persistent sky resources are incomplete");
        return false;
    }

    VkImageMemoryBarrier toColor{};
    toColor.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toColor.oldLayout = _skyMapLayout;
    toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColor.image = _skyMap.image;
    toColor.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toColor.subresourceRange.levelCount = 1;
    toColor.subresourceRange.layerCount = 1;
    const VkPipelineStageFlags sourceStage = _skyMapLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                 ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                                 : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    toColor.srcAccessMask = _skyMapLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? VK_ACCESS_SHADER_READ_BIT : 0;
    toColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, sourceStage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toColor);

    BeginDebugLabel(commandBuffer, "PoseidonVK HDR Sky Map Bake", 0.18f, 0.32f, 0.80f);
    VkClearValue clear{};
    VkRenderPassBeginInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passInfo.renderPass = _skyMapRenderPass;
    passInfo.framebuffer = _skyMapFramebuffer;
    passInfo.renderArea.extent = {_skyMap.width, _skyMap.height};
    passInfo.clearValueCount = 1;
    passInfo.pClearValues = &clear;
    vkCmdBeginRenderPass(commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _skyMapBakePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _skyMapBakePipelineLayout, 0, 1,
                            &_frameDescriptorSet, 0, nullptr);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);
    EndDebugLabel(commandBuffer);

    VkImageMemoryBarrier toSample = toColor;
    toSample.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toSample.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toSample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toSample);
    _skyMapLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    _skyMapCachedInputs = _skyMapRequestedInputs;
    _skyMapDirty = false;
    _skyMapValid = true;
    return true;
}

bool EngineVK::RecordBootstrapCommand(uint32_t imageIndex)
{
    if (imageIndex >= _commandBuffers.size() || imageIndex >= _framebuffers.size())
        return false;

    VkCommandBuffer commandBuffer = _commandBuffers[imageIndex];
    vkResetCommandBuffer(commandBuffer, 0);
    _eyeAdaptationPendingWrite = false;
    const bool recordEyeAdaptation = _hdrEnabled && _hasFrameConstants && _eyeAdaptationPipeline &&
                                     _eyeAdaptationPipelineLayout && _eyeAdaptationDescriptorSets[0] &&
                                     _eyeAdaptationRenderPass;
    const std::uint32_t exposureHistoryIndex = recordEyeAdaptation
                                                   ? (_eyeAdaptationHistoryValid ? 1 - _eyeAdaptationCurrentIndex : 0)
                                                   : _eyeAdaptationCurrentIndex;
    if (WorldCompositionActive())
        UpdateWorldCompositeDescriptorSet(exposureHistoryIndex);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkResult result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: vkBeginCommandBuffer failed: {}", VkResultName(result));
        return false;
    }
    if (_gpuTimingQueryPool)
    {
        vkCmdResetQueryPool(commandBuffer, _gpuTimingQueryPool, 0, 11);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, _gpuTimingQueryPool, 0);
    }

    // Texture decode, staging, copy recording, and submission all remain on
    // this render thread.  The queued lifecycle below owns staging through the
    // frame fence without a per-texture queue idle.
    RecordPendingTextureUploads(commandBuffer);

    // Bake before opening the world pass so the cached HDR image is available
    // to the sky draw and remains independent from scene depth/world geometry.
    if (ProceduralSkyActive() && !RecordSkyMapBake(commandBuffer))
        return false;

    // Heightfield self-shadow is a separate long-range receiver mask, not a
    // replacement for the CSM depth submission above.  Record it before any
    // render pass can sample set-2 binding 0; TerrainVK performs the explicit
    // storage-write -> fragment-read barrier and skips unchanged sun frames.
    if (_terrainVk.Ready())
    {
        const float* sunTravel = _lastFrameConstants.sunDirection;
        _terrainVk.RecordSelfShadowPass(commandBuffer, -sunTravel[0], -sunTravel[1], -sunTravel[2]);
        // The graphics pipeline is constructed only after binding 0's compute
        // write has been transitioned to shader-read layout.
        if (_terrainVk.VisualInputsReady())
            CreateTerrainRasterPipeline();
    }

    // Compute writes compact VkDrawIndexedIndirectCommand streams before the
    // render pass.  The explicit compute->indirect barrier lives in this call.
    RecordGpuSceneCull(commandBuffer);

    BeginDebugLabel(commandBuffer, WorldCompositionActive() ? "PoseidonVK World Render Pass"
                                                             : "PoseidonVK Direct Render Pass",
                    0.04f, 0.35f, 0.75f);

    VkClearValue clearValues[2]{};
    clearValues[0].color = _clearColor;
    clearValues[1].depthStencil.depth = 1.0f;
    clearValues[1].depthStencil.stencil = 0;

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = _renderPass;
    renderPassInfo.framebuffer = WorldCompositionActive() ? _worldFramebuffer : _framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = _swapchainExtent;
    renderPassInfo.clearValueCount = 2;
    renderPassInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    if (_bootstrapPipeline && !_hasFrameConstants)
    {
        const vk::BootstrapPushConstantsVK constants = vk::BuildBootstrapPushConstants(
            static_cast<int>(_swapchainExtent.width), static_cast<int>(_swapchainExtent.height), _clearColor.float32);

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
        // The bootstrap triangle is a legacy debugging tool; commenting out the draw call
        // prevents it from rendering during startup logos, menus, or gameplay, keeping the background clean.
        /*
        vkCmdPushConstants(commandBuffer, _pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, vk::kBootstrapPushConstantsSize, &constants);
        vkCmdDrawIndexed(commandBuffer, kBootstrapTriangleIndexCount, 1, 0, 0, 0);
        */
    }
    if (_screenPipeline)
    {
        // Software-transformed clouds have no hardware mesh. Draw their NoZBuf
        // batches before the world so foreground geometry can overwrite them.
        RecordScreenDraws(commandBuffer, vk::ScreenDrawPhaseVK::Background);
    }
    if (ProceduralSkyActive())
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _proceduralSkyPipeline);
        VkDescriptorSet skySets[] = {_frameDescriptorSet, _skyMapDescriptorSet};
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _skyMapPipelineLayout, 0, 2,
                                skySets, 0, nullptr);
        const float hdrEnabled = _hdrEnabled ? 1.0f : 0.0f;
        vkCmdPushConstants(commandBuffer, _skyMapPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(hdrEnabled),
                           &hdrEnabled);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }
    auto recordSceneDraws = [&](SceneGroup group)
    {
        const VkPipeline fallbackScenePipeline = group == SceneGroup::WorldLate   ? _worldLateScenePipeline
                                                : group == SceneGroup::Present    ? _cockpitScenePipeline
                                                                                   : _scenePipeline;
        vk::PipelineCacheVK& scenePipelineCache = group == SceneGroup::WorldLate ? _worldLateScenePipelineCache
                                                 : group == SceneGroup::Present ? _cockpitScenePipelineCache
                                                                               : _scenePipelineCache;
        if (!fallbackScenePipeline)
            return;

        VkPipeline lastBoundPipeline = fallbackScenePipeline;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, fallbackScenePipeline);
        if (_frameDescriptorSet)
        {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _scenePipelineLayout, 0, 1,
                                     &_frameDescriptorSet, 0, nullptr);
        }
        // The dedicated water material samples the cached HDR sky map at set
        // 3.  Bind it for this shader family layout up front; non-water draws
        // do not dynamically access it.
        if (_skyMapDescriptorSet)
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _scenePipelineLayout, 3, 1,
                                    &_skyMapDescriptorSet, 0, nullptr);

        // GPU scene batches are constructed only across identical mesh/material
        // state (and never reordered in transparent/cockpit groups).  Compute
        // compacts visible instances into each batch's indirect segment.
        bool hasGpuBatches = false;
        if (_gpuSceneEnabled)
        {
            for (const vk::GpuSceneBatchVK& batch : _gpuSceneBatches)
            {
                if (batch.sceneGroup == static_cast<std::uint32_t>(group))
                {
                    hasGpuBatches = true;
                    break;
                }
            }
        }
        if (hasGpuBatches)
        {
            // All GPU-scene draws select their DrawConstants entry through
            // gl_BaseInstanceARB, so this fallback push is invariant per group.
            const vk::ScenePushConstantsVK constants =
                vk::BuildScenePushConstants(vk::BuildIdentityScenePushConstants().world, true, 0);
            vkCmdPushConstants(commandBuffer, _scenePipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               vk::kScenePushConstantsSize, &constants);

            VkDescriptorSet lastBoundTexDescriptorSet = VK_NULL_HANDLE;
            VkDescriptorSet lastBoundTex1DescriptorSet = VK_NULL_HANDLE;
            VkBuffer lastBoundVertexBuffer = VK_NULL_HANDLE;
            VkBuffer lastBoundIndexBuffer = VK_NULL_HANDLE;
            for (const vk::GpuSceneBatchVK& batch : _gpuSceneBatches)
            {
                if (batch.sceneGroup != static_cast<std::uint32_t>(group) || batch.instanceCount == 0)
                    continue;
                const vk::SceneDrawCommandVK& command = _lastSceneDrawCommands[batch.sourceCommandIndex];
                const vk::DrawConstantsVK& draw = _lastDrawConstants[command.drawIndex];
                vk::PipelineKeyVK key;
                key.cull = static_cast<render::CullMode>(draw.cull);
                key.frontFace = static_cast<render::FrontFaceMode>(draw.frontFace);
                key.depth = group == SceneGroup::WorldLate ? render::DepthMode::ReadOnly
                                                           : static_cast<render::DepthMode>(draw.depth);
                key.blend = static_cast<render::BlendMode>(draw.blend);
                key.surface = static_cast<render::SurfaceMode>(draw.surface);
                VkPipeline pipeline = scenePipelineCache.Get(key);
                if (!pipeline)
                    pipeline = fallbackScenePipeline;
                if (pipeline != lastBoundPipeline)
                {
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    lastBoundPipeline = pipeline;
                }

                TextureVK* texture = draw.textureIds[0] ? ResolveTexture(draw.textureIds[0]) : nullptr;
                VkDescriptorSet textureSet = texture ? texture->GetDescriptorSet(draw.samplerFilter, draw.samplerClamp)
                                                     : VK_NULL_HANDLE;
                if (!textureSet && _fallbackWhiteTexture)
                    textureSet = _fallbackWhiteTexture->GetDescriptorSet(draw.samplerFilter, draw.samplerClamp);
                if (textureSet && textureSet != lastBoundTexDescriptorSet)
                {
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _scenePipelineLayout, 1, 1,
                                            &textureSet, 0, nullptr);
                    lastBoundTexDescriptorSet = textureSet;
                }
                TextureVK* texture1 = draw.textureIds[1] ? ResolveTexture(draw.textureIds[1]) : nullptr;
                const auto shaderFamily = static_cast<render::ShaderFamily>(draw.shader);
                const std::uint32_t clamp1 = (shaderFamily == render::ShaderFamily::Detail ||
                                              shaderFamily == render::ShaderFamily::Grass)
                                                 ? 0u
                                                 : draw.samplerClamp;
                VkDescriptorSet textureSet1 = texture1 ? texture1->GetDescriptorSet(draw.samplerFilter, clamp1)
                                                        : VK_NULL_HANDLE;
                if (!textureSet1 && _fallbackWhiteTexture)
                    textureSet1 = _fallbackWhiteTexture->GetDescriptorSet(draw.samplerFilter, clamp1);
                if (textureSet1 && textureSet1 != lastBoundTex1DescriptorSet)
                {
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _scenePipelineLayout, 2, 1,
                                            &textureSet1, 0, nullptr);
                    lastBoundTex1DescriptorSet = textureSet1;
                }
                const vk::MeshResourcesVK* mesh = _meshRegistry.Resolve(command.meshId);
                if (!mesh || !mesh->IsValid())
                    mesh = _meshRegistry.Resolve(_bootstrapMeshId);
                if (!mesh || !mesh->IsValid())
                    continue;
                const VkDeviceSize vertexOffset = 0;
                if (mesh->vertexBuffer != lastBoundVertexBuffer)
                {
                    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &mesh->vertexBuffer, &vertexOffset);
                    lastBoundVertexBuffer = mesh->vertexBuffer;
                }
                if (mesh->indexBuffer != lastBoundIndexBuffer)
                {
                    vkCmdBindIndexBuffer(commandBuffer, mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT16);
                    lastBoundIndexBuffer = mesh->indexBuffer;
                }
                const VkDeviceSize indirectOffset =
                    static_cast<VkDeviceSize>(batch.indirectOffset) * sizeof(VkDrawIndexedIndirectCommand);
                if (_gpuSceneCapabilities.drawIndirectCount)
                {
                    vkCmdDrawIndexedIndirectCount(commandBuffer, _gpuSceneIndirectBuffer.buffer, indirectOffset,
                                                  _gpuSceneCountBuffer.buffer,
                                                  static_cast<VkDeviceSize>(batch.countOffset) * sizeof(std::uint32_t),
                                                  batch.instanceCount, sizeof(VkDrawIndexedIndirectCommand));
                }
                else
                {
                    // Fixed-count fallback is valid because vkCmdFillBuffer
                    // zeroes every slot before compute; invisible slots carry
                    // indexCount=0 and therefore rasterise nothing.
                    vkCmdDrawIndexedIndirect(commandBuffer, _gpuSceneIndirectBuffer.buffer, indirectOffset,
                                             batch.instanceCount, sizeof(VkDrawIndexedIndirectCommand));
                }
            }
            return;
        }

        const auto& groupCommands = _sceneCommandGroups[static_cast<std::uint32_t>(group)];
        if (!groupCommands.empty())
        {
            VkDescriptorSet lastBoundTexDescriptorSet = VK_NULL_HANDLE;
            VkDescriptorSet lastBoundTex1DescriptorSet = VK_NULL_HANDLE;
            VkBuffer lastBoundVertexBuffer = VK_NULL_HANDLE;
            VkBuffer lastBoundIndexBuffer = VK_NULL_HANDLE;
            int logEveryN = 100;
            for (std::uint32_t commandIndex : groupCommands)
            {
                vk::PipelineKeyVK key;
                const vk::SceneDrawCommandVK& command = _lastSceneDrawCommands[commandIndex];
                const vk::DrawConstantsVK& draw = _lastDrawConstants[command.drawIndex];

                // Log first few draws and any with missing resources
                static std::uint32_t s_loggedDrawIdx = 0;
                bool logThis = (s_loggedDrawIdx < 20);
                ++s_loggedDrawIdx;
                if (logThis)
                {
                    LOG_DEBUG(Graphics,
                              "SceneDraw[{}]: pass={} meshId={} tex0={} idxRange={}+{}"
                              " cull={} frontFace={} depth={} blend={}",
                              s_loggedDrawIdx - 1, draw.pass, draw.meshId, draw.textureIds[0], draw.indexBegin,
                              draw.indexCount, draw.cull, draw.frontFace, draw.depth, draw.blend);
                }

                key.cull = static_cast<render::CullMode>(draw.cull);
                key.frontFace = static_cast<render::FrontFaceMode>(draw.frontFace);
                key.depth = static_cast<render::DepthMode>(draw.depth);
                key.blend = static_cast<render::BlendMode>(draw.blend);
                key.surface = static_cast<render::SurfaceMode>(draw.surface);
                if (group == SceneGroup::WorldLate)
                    key.depth = render::DepthMode::ReadOnly;

                VkPipeline pipeline = scenePipelineCache.Get(key);
                if (!pipeline)
                    pipeline = fallbackScenePipeline;

                if (pipeline != lastBoundPipeline)
                {
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    lastBoundPipeline = pipeline;
                }

                // Bind texture descriptor set (Set 1)
                VkDescriptorSet texDescriptorSet = VK_NULL_HANDLE;
                std::uint32_t texId = draw.textureIds[0];
                bool texFound = false;
                if (texId != 0)
                {
                    TextureVK* tex = ResolveTexture(texId);
                    if (tex)
                    {
                        texDescriptorSet = tex->GetDescriptorSet(draw.samplerFilter, draw.samplerClamp);
                        texFound = true;
                    }
                }

                if (!texFound && logThis)
                {
                    LOG_DEBUG(Graphics, "  >> tex0 id={} NOT FOUND in registry ({} entries), using fallback", texId,
                              _textureRegistry.size());
                }

                if (texDescriptorSet == VK_NULL_HANDLE && _fallbackWhiteTexture)
                {
                    texDescriptorSet = _fallbackWhiteTexture->GetDescriptorSet(draw.samplerFilter, draw.samplerClamp);
                }

                if (texDescriptorSet != VK_NULL_HANDLE && texDescriptorSet != lastBoundTexDescriptorSet)
                {
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _scenePipelineLayout, 1, 1,
                                            &texDescriptorSet, 0, nullptr);
                    lastBoundTexDescriptorSet = texDescriptorSet;
                }

                // Bind texture descriptor set (Set 2)
                VkDescriptorSet tex1DescriptorSet = VK_NULL_HANDLE;
                std::uint32_t texId1 = draw.textureIds[1];
                const auto shaderFamily = static_cast<render::ShaderFamily>(draw.shader);
                const bool isTiledDetail = shaderFamily == render::ShaderFamily::Detail ||
                                           shaderFamily == render::ShaderFamily::Grass;
                // Transition tiles clamp their base layer, but the detail layer
                // must repeat across its 32x UV scale.
                const std::uint32_t tex1SamplerClamp = isTiledDetail ? 0u : draw.samplerClamp;
                if (texId1 != 0)
                {
                    TextureVK* tex = ResolveTexture(texId1);
                    if (tex)
                        tex1DescriptorSet = tex->GetDescriptorSet(draw.samplerFilter, tex1SamplerClamp);
                }

                if (tex1DescriptorSet == VK_NULL_HANDLE && _fallbackWhiteTexture)
                {
                    tex1DescriptorSet = _fallbackWhiteTexture->GetDescriptorSet(draw.samplerFilter, tex1SamplerClamp);
                }

                if (tex1DescriptorSet != VK_NULL_HANDLE && tex1DescriptorSet != lastBoundTex1DescriptorSet)
                {
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _scenePipelineLayout, 2, 1,
                                            &tex1DescriptorSet, 0, nullptr);
                    lastBoundTex1DescriptorSet = tex1DescriptorSet;
                }

                // Resolve the draw's mesh via the registry. Until real per-object
                // uploads land, most commands resolve to the bring-up quad; the
                // resolve-and-bind path itself is exercised live against real ids.
                const vk::MeshResourcesVK* mesh = _meshRegistry.Resolve(command.meshId);
                bool meshFound = (mesh && mesh->IsValid());
                if (!meshFound)
                    mesh = _meshRegistry.Resolve(_bootstrapMeshId);
                if (!mesh || !mesh->IsValid())
                {
                    if (logThis)
                        LOG_DEBUG(Graphics, "  >> meshId={} NOT FOUND (bootstrap={}, valid={}) SKIPPING",
                                  command.meshId, _bootstrapMeshId, mesh ? mesh->IsValid() : false);
                    continue;
                }
                if (!meshFound && logThis)
                {
                    LOG_DEBUG(Graphics, "  >> meshId={} NOT FOUND, using bootstrap mesh", command.meshId);
                }

                if (mesh->vertexBuffer && mesh->vertexBuffer != lastBoundVertexBuffer)
                {
                    VkBuffer vertexBuffers[] = {mesh->vertexBuffer};
                    VkDeviceSize offsets[] = {0};
                    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                    lastBoundVertexBuffer = mesh->vertexBuffer;
                }
                if (mesh->indexBuffer && mesh->indexBuffer != lastBoundIndexBuffer)
                {
                    vkCmdBindIndexBuffer(commandBuffer, mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT16);
                    lastBoundIndexBuffer = mesh->indexBuffer;
                }

                // Push the real per-draw world matrix so the shader's fallback
                // path (used when useDrawConstants is false) also matches. With
                // useDrawConstants=true the shader prefers draws[drawIndex].world
                // from the SSBO; this push constant keeps the two paths in sync.
                const GfxMatrix& drawWorld = (command.drawIndex < _lastDrawConstants.size())
                                                 ? _lastDrawConstants[command.drawIndex].world
                                                 : vk::BuildIdentityScenePushConstants().world;
                const vk::ScenePushConstantsVK constants =
                    vk::BuildScenePushConstants(drawWorld, true, command.drawIndex);
                vkCmdPushConstants(commandBuffer, _scenePipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   vk::kScenePushConstantsSize, &constants);

                // Clamp the draw's index range against the resolved mesh's actual
                // index count so a stale/oversized firstIndex+indexCount cannot
                // read past the buffer (uint16 indices).
                const uint32_t firstIndex = command.firstIndex;
                const uint32_t meshIndexCount = mesh->indexCount;
                const uint32_t indexCount = (firstIndex + command.indexCount <= meshIndexCount)
                                                ? command.indexCount
                                                : (firstIndex < meshIndexCount ? meshIndexCount - firstIndex : 0);
                if (indexCount > 0)
                    vkCmdDrawIndexed(commandBuffer, indexCount, 1, firstIndex, 0, 0);
            }
        }
        else if (group == SceneGroup::Terrain && _hasFrameConstants)
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
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _scenePipelineLayout, 2, 1,
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
            vkCmdPushConstants(commandBuffer, _scenePipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               vk::kScenePushConstantsSize, &sceneConstants);
            vkCmdDrawIndexed(commandBuffer, kSceneQuadIndexCount, 1, 0, 0, 0);
        }
    };
    const bool recordDedicatedTerrain = _terrainOpaqueInSubmittedFrame && WantsDedicatedTerrainOpaque();
    if (recordDedicatedTerrain)
    {
        VkDescriptorSet terrainSets[] = {_frameDescriptorSet, _terrainVk.DescriptorSet(), _terrainVk.VisualDescriptorSet()};
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _terrainVk.RasterPipeline());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _terrainVk.RasterPipelineLayout(), 0, 3,
                                terrainSets, 0, nullptr);
        if (!_terrainVk.RecordInstancedGridDraw(commandBuffer))
        {
            // Do not substitute a placeholder draw. Invalidate the pipeline so
            // the next source collection keeps legacy segments authoritative.
            LOG_WARN(Graphics, "Vulkan terrain telemetry: dedicated CDLOD draw rejected; reverting to legacy segments");
            _terrainVk.DestroyRasterPipeline(_device);
            _terrainVisualInputReason = "dedicated grid draw rejected";
        }
    }
    else
    {
        recordSceneDraws(SceneGroup::Terrain);
    }
    if (WorldCompositionActive())
    {
        if (_gpuTimingQueryPool)
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, _gpuTimingQueryPool, 1);
        recordSceneDraws(SceneGroup::Opaque);
        if (_gpuTimingQueryPool)
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, _gpuTimingQueryPool, 2);
        recordSceneDraws(SceneGroup::Cutout);
        if (_gpuTimingQueryPool)
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, _gpuTimingQueryPool, 3);
        recordSceneDraws(SceneGroup::Other);
        if (_gpuTimingQueryPool)
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, _gpuTimingQueryPool, 4);
        recordSceneDraws(SceneGroup::Transparent);
        vkCmdEndRenderPass(commandBuffer);
        if (_gpuTimingQueryPool)
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, _gpuTimingQueryPool, 5);
        if (_volumetricCloudsEnabled && _volumetricCloudPipeline && _cloudTemporalPipeline &&
            _cloudCompositePipeline && _volumetricCloudDescriptorSets[0] && _cloudGenerationDescriptorSet &&
            _cloudLightingDescriptorSets[0] && _cloudDensityErosionPipeline && _cloudDistanceFieldPipeline &&
            _cloudLightMapPipeline && _hasFrameConstants)
        {
            BeginDebugLabel(commandBuffer, "PoseidonVK Temporal Volumetric Clouds", 0.85f, 0.85f, 0.95f);
            const std::uint32_t historyRead = _cloudHistoryCurrentIndex;
            const std::uint32_t historyWrite = 1 - historyRead;
            RecordCloudVolumeCompute(commandBuffer);
            // Bind the light-volume read side only after the compute write has
            // crossed its compute-to-fragment visibility barrier.
            UpdateCloudDescriptorSets(historyRead);
            const auto recordCloudPass = [&](VkRenderPass pass, VkFramebuffer framebuffer, VkExtent2D extent,
                                             VkPipeline pipeline, VkDescriptorSet cloudSet)
            {
                VkRenderPassBeginInfo passInfo{};
                passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                passInfo.renderPass = pass;
                passInfo.framebuffer = framebuffer;
                passInfo.renderArea.extent = extent;
                VkClearValue clear{};
                if (pass == _cloudRaymarchRenderPass)
                {
                    // Cloud fragments discard where the volume is empty. Clear
                    // those pixels so a previous frame cannot ghost into view.
                    passInfo.clearValueCount = 1;
                    passInfo.pClearValues = &clear;
                }
                vkCmdBeginRenderPass(commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                VkDescriptorSet sets[] = {_frameDescriptorSet, cloudSet};
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _volumetricCloudPipelineLayout,
                                        0, 2, sets, 0, nullptr);
                vkCmdDraw(commandBuffer, 3, 1, 0, 0);
                vkCmdEndRenderPass(commandBuffer);
            };
            const VkExtent2D cloudExtent{_cloudCurrent.width, _cloudCurrent.height};
            recordCloudPass(_cloudRaymarchRenderPass, _cloudRaymarchFramebuffer, cloudExtent, _volumetricCloudPipeline,
                            _volumetricCloudDescriptorSets[0]);
            recordCloudPass(_cloudTemporalRenderPass, _cloudTemporalFramebuffers[historyWrite], cloudExtent,
                            _cloudTemporalPipeline, _volumetricCloudDescriptorSets[0]);
            recordCloudPass(_cloudCompositeRenderPass, _cloudCompositeFramebuffer, _swapchainExtent,
                            _cloudCompositePipeline, _volumetricCloudDescriptorSets[1]);
            _cloudHistoryCurrentIndex = historyWrite;
            _cloudHistoryValid = true;
            EndDebugLabel(commandBuffer);
        }
        if (_gpuTimingQueryPool)
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, _gpuTimingQueryPool, 6);
        if (_worldLateRenderPass && _worldLateFramebuffer)
        {
            renderPassInfo.renderPass = _worldLateRenderPass;
            renderPassInfo.framebuffer = _worldLateFramebuffer;
            renderPassInfo.clearValueCount = 0;
            renderPassInfo.pClearValues = nullptr;
            vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            recordSceneDraws(SceneGroup::WorldLate);
            vkCmdEndRenderPass(commandBuffer);
        }
        if (_gpuTimingQueryPool)
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, _gpuTimingQueryPool, 7);
        EndDebugLabel(commandBuffer);

        if (recordEyeAdaptation)
        {
            // The 1x1 pass runs after world rendering and before legacy UI presentation.
            BeginDebugLabel(commandBuffer, "PoseidonVK Eye Adaptation", 0.85f, 0.55f, 0.15f);
            VkRenderPassBeginInfo adaptationPassInfo{};
            adaptationPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            adaptationPassInfo.renderPass = _eyeAdaptationRenderPass;
            adaptationPassInfo.framebuffer = _eyeAdaptationFramebuffers[exposureHistoryIndex];
            adaptationPassInfo.renderArea.extent = {1, 1};
            vkCmdBeginRenderPass(commandBuffer, &adaptationPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _eyeAdaptationPipeline);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _eyeAdaptationPipelineLayout, 0, 1,
                                    &_eyeAdaptationDescriptorSets[exposureHistoryIndex], 0, nullptr);
            const EyeAdaptationPushConstants constants{_hdrExposure, _eyeAdaptationHistoryValid ? 1u : 0u};
            vkCmdPushConstants(commandBuffer, _eyeAdaptationPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(constants), &constants);
            vkCmdDraw(commandBuffer, 3, 1, 0, 0);
            vkCmdEndRenderPass(commandBuffer);
            EndDebugLabel(commandBuffer);
            _eyeAdaptationPendingWrite = true;
            _eyeAdaptationPendingIndex = exposureHistoryIndex;
        }

        // Keep briefing/control objects and HUD out of the world target. They are
        // display-referred legacy UI, often depth-disabled, and must be drawn only
        // after world/cloud composition or the mission binder is cloud-occluded.
        BeginDebugLabel(commandBuffer, "PoseidonVK Present Render Pass", 0.18f, 0.65f, 0.42f);
        renderPassInfo.renderPass = _presentRenderPass;
        renderPassInfo.framebuffer = _framebuffers[imageIndex];
        renderPassInfo.clearValueCount = 2;
        renderPassInfo.pClearValues = clearValues;
        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        if (_gpuTimingQueryPool)
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, _gpuTimingQueryPool, 8);
        if (_worldCompositePipeline && _worldCompositeDescriptorSet)
        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _worldCompositePipeline);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _worldCompositePipelineLayout, 0,
                                    1, &_worldCompositeDescriptorSet, 0, nullptr);
            const WorldCompositePushConstants constants{_hdrExposure, _hdrEnabled ? 1u : 0u,
                                                         recordEyeAdaptation || _eyeAdaptationHistoryValid ? 1u : 0u};
            vkCmdPushConstants(commandBuffer, _worldCompositePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(constants), &constants);
            vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        }
        if (_gpuTimingQueryPool)
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, _gpuTimingQueryPool, 9);
        recordSceneDraws(SceneGroup::Present);
    }
    RecordScreenDraws(commandBuffer, vk::ScreenDrawPhaseVK::Overlay);
    vkCmdEndRenderPass(commandBuffer);
    if (_gpuTimingQueryPool && WorldCompositionActive())
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, _gpuTimingQueryPool, 10);

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
    // Cached pipelines reference the render pass destroyed below. They must be
    // recreated for the new swapchain's render pass and viewport dimensions.
    _scenePipelineCache.Destroy(_device);
    _worldPrepassScenePipelineCache.Destroy(_device);
    _cockpitScenePipelineCache.Destroy(_device);
    _worldLateScenePipelineCache.Destroy(_device);
    // This pipeline bakes the active render pass and viewport. Maps and their
    // descriptor sets survive resize, but the graphics pipeline must not.
    _terrainVk.DestroyRasterPipeline(_device);

    if (_proceduralSkyPipeline)
    {
        vkDestroyPipeline(_device, _proceduralSkyPipeline, nullptr);
        _proceduralSkyPipeline = VK_NULL_HANDLE;
    }
    if (_skyMapBakePipeline)
    {
        vkDestroyPipeline(_device, _skyMapBakePipeline, nullptr);
        _skyMapBakePipeline = VK_NULL_HANDLE;
    }
    if (_volumetricCloudPipeline)
    {
        vkDestroyPipeline(_device, _volumetricCloudPipeline, nullptr);
        _volumetricCloudPipeline = VK_NULL_HANDLE;
    }
    if (_cloudTemporalPipeline)
    {
        vkDestroyPipeline(_device, _cloudTemporalPipeline, nullptr);
        _cloudTemporalPipeline = VK_NULL_HANDLE;
    }
    if (_cloudCompositePipeline)
    {
        vkDestroyPipeline(_device, _cloudCompositePipeline, nullptr);
        _cloudCompositePipeline = VK_NULL_HANDLE;
    }
    if (_worldCompositePipeline)
    {
        vkDestroyPipeline(_device, _worldCompositePipeline, nullptr);
        _worldCompositePipeline = VK_NULL_HANDLE;
    }
    if (_eyeAdaptationPipeline)
    {
        vkDestroyPipeline(_device, _eyeAdaptationPipeline, nullptr);
        _eyeAdaptationPipeline = VK_NULL_HANDLE;
    }
    DestroyVolumetricCloudDescriptorResources();
    DestroyCloudComputeDescriptorResources();
    DestroyWorldCompositeDescriptorResources();
    DestroyEyeAdaptationDescriptorResources();

    if (_scenePipeline)
    {
        vkDestroyPipeline(_device, _scenePipeline, nullptr);
        _scenePipeline = VK_NULL_HANDLE;
    }
    if (_cockpitScenePipeline)
    {
        vkDestroyPipeline(_device, _cockpitScenePipeline, nullptr);
        _cockpitScenePipeline = VK_NULL_HANDLE;
    }
    if (_worldLateScenePipeline)
    {
        vkDestroyPipeline(_device, _worldLateScenePipeline, nullptr);
        _worldLateScenePipeline = VK_NULL_HANDLE;
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
    if (_screenOverlayPipeline)
    {
        vkDestroyPipeline(_device, _screenOverlayPipeline, nullptr);
        _screenOverlayPipeline = VK_NULL_HANDLE;
    }

    for (VkFramebuffer framebuffer : _framebuffers)
    {
        if (framebuffer)
            vkDestroyFramebuffer(_device, framebuffer, nullptr);
    }
    _framebuffers.clear();

    DestroyCloudResources();
    DestroyWorldTarget();
    DestroyEyeAdaptationResources();

    if (_presentRenderPass)
    {
        vkDestroyRenderPass(_device, _presentRenderPass, nullptr);
        _presentRenderPass = VK_NULL_HANDLE;
    }

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

void EngineVK::DestroyCloudConstantsBuffer()
{
    vk::DestroyBuffer(_device, _cloudConstantsBuffer);
}

void EngineVK::DestroyDrawConstantsBuffer()
{
    vk::DestroyBuffer(_device, _drawConstantsBuffer);
    _drawConstantsCapacity = 0;
}

void EngineVK::DestroyGpuSceneResources()
{
    _gpuSceneEnabled = false;
    _gpuSceneInstances.clear();
    _gpuSceneBatches.clear();
    vk::DestroyBuffer(_device, _gpuSceneInstancesBuffer);
    vk::DestroyBuffer(_device, _gpuSceneIndirectBuffer);
    vk::DestroyBuffer(_device, _gpuSceneCountBuffer);
    _gpuSceneInstanceCapacity = 0;
    _gpuSceneBatchCapacity = 0;
    if (_gpuSceneCullPipeline)
        vkDestroyPipeline(_device, _gpuSceneCullPipeline, nullptr);
    if (_gpuScenePipelineLayout)
        vkDestroyPipelineLayout(_device, _gpuScenePipelineLayout, nullptr);
    if (_gpuSceneDescriptorPool)
        vkDestroyDescriptorPool(_device, _gpuSceneDescriptorPool, nullptr);
    if (_gpuSceneDescriptorSetLayout)
        vkDestroyDescriptorSetLayout(_device, _gpuSceneDescriptorSetLayout, nullptr);
    _gpuSceneCullPipeline = VK_NULL_HANDLE;
    _gpuScenePipelineLayout = VK_NULL_HANDLE;
    _gpuSceneDescriptorPool = VK_NULL_HANDLE;
    _gpuSceneDescriptorSet = VK_NULL_HANDLE;
    _gpuSceneDescriptorSetLayout = VK_NULL_HANDLE;
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
    LOG_INFO(Graphics, "Vulkan: DestroyScenePipelineLayout - fragmentModule={}, vertexModule={}, layout={}",
             (void*)_sceneFragmentModule, (void*)_sceneVertexModule, (void*)_scenePipelineLayout);
    _scenePipelineCache.Destroy(_device);
    _cockpitScenePipelineCache.Destroy(_device);
    _worldLateScenePipelineCache.Destroy(_device);

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
    if (_device)
        _screenPipelineCache.Destroy(_device);
    if (_device)
        _screenOverlayPipelineCache.Destroy(_device);
    if (_device && _screenPipeline)
    {
        vkDestroyPipeline(_device, _screenPipeline, nullptr);
        _screenPipeline = VK_NULL_HANDLE;
    }
    if (_device && _screenOverlayPipeline)
    {
        vkDestroyPipeline(_device, _screenOverlayPipeline, nullptr);
        _screenOverlayPipeline = VK_NULL_HANDLE;
    }
}

void EngineVK::DestroyScreenPipelineLayout()
{
    LOG_INFO(Graphics, "Vulkan: DestroyScreenPipelineLayout - fragmentModule={}, vertexModule={}, layout={}",
             (void*)_screenFragmentModule, (void*)_screenVertexModule, (void*)_screenPipelineLayout);
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
    const bool recreated = CreateSwapchain() &&
                            (!_volumetricCloudsEnabled || CreateVolumetricCloudDescriptorSet()) &&
                            (!_volumetricCloudsEnabled || CreateCloudComputeDescriptorSets()) &&
                            (!_temporalExposureEnabled || CreateEyeAdaptationDescriptorSet()) &&
                            (!WorldCompositionActive() || CreateWorldCompositeDescriptorSet()) &&
                            CreateBootstrapPipeline() && CreateScenePipeline() && CreateProceduralSkyPipeline() &&
                            (!_volumetricCloudsEnabled || CreateVolumetricCloudPipeline()) &&
                            (!_temporalExposureEnabled || CreateEyeAdaptationPipeline()) &&
                            (!WorldCompositionActive() || CreateWorldCompositePipeline()) && CreateScreenPipeline() &&
                           CreateSyncObjects();
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

    const auto fenceWaitStarted = std::chrono::steady_clock::now();
    vkWaitForFences(_device, 1, &_inFlight, VK_TRUE, UINT64_MAX);
    _cpuFrameFenceWaitMs = std::chrono::duration<float, std::milli>(
                               std::chrono::steady_clock::now() - fenceWaitStarted)
                               .count();
    LogGpuTimings();
    ReleaseCompletedTextureResources();

    uint32_t imageIndex = 0;
    VkResult result =
        vkAcquireNextImageKHR(_device, _swapchain, UINT64_MAX, _imageAvailable, VK_NULL_HANDLE, &imageIndex);
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

    const auto commandRecordStarted = std::chrono::steady_clock::now();
    const bool recordedCommand = RecordBootstrapCommand(imageIndex);
    _cpuCommandRecordMs = std::chrono::duration<float, std::milli>(
                              std::chrono::steady_clock::now() - commandRecordStarted)
                              .count();
    if (!recordedCommand)
    {
        DiscardRecordedTextureUploads();
        return;
    }
    if (_scenePrepassFragmentModule)
    {
        vkDestroyShaderModule(_device, _scenePrepassFragmentModule, nullptr);
        _scenePrepassFragmentModule = VK_NULL_HANDLE;
    }

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
        DiscardRecordedTextureUploads();
        RecreateSignaledFence(_device, _inFlight);
        return;
    }
    CommitRecordedTextureUploads();
    if (_eyeAdaptationPendingWrite)
    {
        _eyeAdaptationCurrentIndex = _eyeAdaptationPendingIndex;
        _eyeAdaptationHistoryValid = true;
        _eyeAdaptationPendingWrite = false;
    }
    _gpuTimingPending = _gpuTimingQueryPool != VK_NULL_HANDLE && WorldCompositionActive();
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
    DestroyShadowResources();

    _fonts.Clear();
    // Destroy the texture bank before the device so GPU images are freed first.
    delete _textBank;
    _textBank = nullptr;
    _fallbackWhiteTexture = nullptr;

    if (_device)
    {
        vkDeviceWaitIdle(_device);
        _terrainVk.Destroy(_device);
        ReleaseCompletedTextureResources();
        DestroyShadowCasterMeshes();
        if (_inFlight)
        {
            vkDestroyFence(_device, _inFlight, nullptr);
            _inFlight = VK_NULL_HANDLE;
        }
        if (_shadowInFlight)
        {
            vkDestroyFence(_device, _shadowInFlight, nullptr);
            _shadowInFlight = VK_NULL_HANDLE;
        }
        if (_imageAvailable)
        {
            vkDestroySemaphore(_device, _imageAvailable, nullptr);
            _imageAvailable = VK_NULL_HANDLE;
        }
        DestroySwapchain();
        DestroySkyMapResources();
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
        DestroySkyMapPipelineLayout();
        DestroySkyMapDescriptorResources();
        DestroyCloudComputePipelineResources();
        DestroyVolumetricCloudPipelineLayout();
        DestroyWorldCompositePipelineLayout();
        DestroyEyeAdaptationPipelineLayout();
        DestroyScenePipelineLayout();
        DestroyFrameDescriptorResources();
        DestroyTextureDescriptorResources();
        DestroyGpuSceneResources();
        DestroyFrameConstantsBuffer();
        DestroyCloudConstantsBuffer();
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
        DestroyGpuTimingResources();
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

bool EngineVK::CompileShader(const char* source, int stage, std::vector<uint32_t>& spirv, std::string& error)
{
    return CompileBootstrapShader(source, static_cast<EShLanguage>(stage), spirv, error);
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
    // A captured resource ID must always resolve to a bindable image. Unknown
    // texture implementations have no Vulkan descriptor, so record fallback.
    if (auto* tvk = dynamic_cast<TextureVK*>(tex); tvk && tvk->HasValidGpuImage())
        return tvk->GetResourceId();
    return TextureVK::kFallbackResourceId;
}
} // namespace

AbstractTextBank* EngineVK::TextBank()
{
    return _textBank;
}

std::uint32_t EngineVK::ShadowCasterTextureResourceId(Texture* texture)
{
    // Use the same live TextureVK/fallback resolution as normal scene draws.
    // The source collector only carries this opaque id into Frame::shadowInput.
    return GetOrCreateTextureResourceId(texture);
}

bool EngineVK::WantsDedicatedTerrainOpaque() const
{
    // Capturing TerrainOpaque replaces the legacy receiver in BuildFrame. Do
    // not advertise it while TerrainVK has only map resources: that would make
    // terrain disappear rather than render with incomplete CSM/self-shadow and
    // visual-input bindings. CreateRasterPipeline() itself refuses to create
    // the pipeline until those inputs are declared complete.
    // TEMPORARY SAFETY GATE: TerrainVK's samplerless native layer array is
    // compiled and bound, but its sampled material result still differs from
    // the authoritative legacy receiver.  Do not let the experiment replace
    // textured ground until the reference material blend is proven in-frame.
    // Resource capture stays enabled so the remaining GPU path can be tested
    // without regressing player-visible terrain.
    constexpr bool kDedicatedTerrainVisualParityValidated = false;
    if (!kDedicatedTerrainVisualParityValidated)
        return false;
    const vk::TerrainVK::DescriptorTelemetry& layers = _terrainVk.Telemetry();
    return _terrainPreviewExperiment && _terrainDescriptorIndexingSupported && _terrainVk.Ready() && _terrainVk.VisualInputsReady() &&
           _terrainVk.RasterPipelineReady() && !_terrainVk.VisibleNodes().empty() && layers.requestedLayers != 0 &&
           layers.boundLayers == layers.requestedLayers && layers.fallbackLayers == 0 && layers.invalidLayers == 0 &&
           layers.invalidLayerIndices == 0;
}

bool EngineVK::WantsTerrainOpaqueCapture() const
{
    // This captures immutable map data for TerrainVK's resource producers while
    // preserving legacy segments until every visual descriptor is genuinely
    // ready. It is intentionally broader than WantsDedicatedTerrainOpaque().
    // Dedicated terrain stays opt-in (POSEIDON_VK_TERRAIN_EXPERIMENT=1) while
    // visual parity is incomplete. Without the experiment, no capture, no
    // upload, and no dedicated draw: legacy segments remain the sole receiver.
    return _terrainPreviewExperiment && _terrainDescriptorIndexingSupported && _terrainVk.Ready();
}

bool EngineVK::CaptureDedicatedTerrainOpaque(const render::frame::TerrainOpaque& terrain)
{
    if (!WantsTerrainOpaqueCapture() || !terrain.Valid())
        return false;

    // TerrainOpaque is the frame-owned value contract. Copy it before the
    // landscape's working vectors can be reused by the next draw operation.
    _capturedTerrainOpaque = terrain;
    // Capture occurs after InitDraw has waited the previous submission fence,
    // so descriptor replacement is safe here.  Re-resolve the snapshot's
    // layers before returning the ownership hand-off: no queued upload may
    // turn into a white/neutral terrain layer on a dedicated frame.
    if (_terrainVk.RasterPipelineReady() && !UpdateTerrainLayerDescriptors(terrain))
    {
        _terrainVk.DestroyRasterPipeline(_device);
        _terrainVisualInputReason = "terrain layer descriptor population failed during dedicated capture";
        return false;
    }
    const vk::TerrainVK::DescriptorTelemetry& layers = _terrainVk.Telemetry();
    if (_terrainVk.RasterPipelineReady() &&
        (layers.requestedLayers != terrain.textureLayers.size() || layers.boundLayers != terrain.textureLayers.size() ||
         layers.fallbackLayers != 0 || layers.invalidLayers != 0 ||
         layers.invalidLayerIndices != 0))
    {
        _terrainVk.DestroyRasterPipeline(_device);
        _terrainVisualInputReason = "terrain layer source is pending, missing, or invalid; dedicated CDLOD requires native images";
        return false;
    }
    return true;
}

bool EngineVK::GetDedicatedTerrainOpaque(render::frame::TerrainOpaque& terrain) const
{
    if (!WantsDedicatedTerrainOpaque() || !_capturedTerrainOpaque || !_capturedTerrainOpaque->Valid())
        return false;

    terrain = *_capturedTerrainOpaque;
    return true;
}

std::uint32_t EngineVK::TerrainTextureResourceId(Texture* texture)
{
    // Terrain layers share the normal texture registry and fallback guarantee;
    // descriptor-array setup will consume these same stable resource ids.
    return GetOrCreateTextureResourceId(texture);
}

std::uint32_t EngineVK::RetainShadowCasterMesh(const Shape& shape, bool dynamic)
{
    if (!_device || shape.NVertex() <= 0)
        return 0;

    ShadowCasterMeshVK& mesh = _shadowCasterMeshes[&shape];
    // This is a retained upload/cache, not a TL-vertex conversion. Static
    // casters follow the reference collector's invariant: their source pose
    // does not change, so after the first upload no CPU mesh rebuild occurs.
    // Dynamic casters rebuild bytes before each depth submission.
    const bool needsSourceUpload = mesh.resourceId == 0 || dynamic;
    if (!needsSourceUpload)
        return mesh.resourceId;

    const vk::MeshBuffersVK source = vk::BuildMeshBuffersVK(shape);
    if (source.vertices.empty() || source.indices.empty())
        return 0;
    const std::uint32_t vertexCount = static_cast<std::uint32_t>(source.vertices.size());
    const std::uint32_t indexCount = static_cast<std::uint32_t>(source.indices.size());
    const bool needsAllocation = mesh.resourceId == 0 || mesh.vertexCount != vertexCount || mesh.indexCount != indexCount;
    if (needsAllocation)
    {
        if (mesh.resourceId != 0)
        {
            _meshRegistry.Unregister(mesh.resourceId);
            vk::DestroyBuffer(_device, mesh.vertexBuffer);
            vk::DestroyBuffer(_device, mesh.indexBuffer);
        }
        mesh = ShadowCasterMeshVK{};
        mesh.resourceId = AllocateMeshResourceId();
        if (vk::CreateHostVisibleBuffer(_physicalDevice, _device, source.vertices.size() * sizeof(vk::MeshVertexVK),
                                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertexBuffer) != VK_SUCCESS ||
            vk::CreateHostVisibleBuffer(_physicalDevice, _device, source.indices.size() * sizeof(std::uint16_t),
                                        VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.indexBuffer) != VK_SUCCESS)
        {
            vk::DestroyBuffer(_device, mesh.vertexBuffer);
            vk::DestroyBuffer(_device, mesh.indexBuffer);
            mesh = ShadowCasterMeshVK{};
            return 0;
        }
        mesh.vertexCount = vertexCount;
        mesh.indexCount = indexCount;
    }

    vk::UploadMappedBuffer(mesh.vertexBuffer, source.vertices.data(), source.vertices.size() * sizeof(vk::MeshVertexVK));
    vk::UploadMappedBuffer(mesh.indexBuffer, source.indices.data(), source.indices.size() * sizeof(std::uint16_t));

    vk::MeshResourcesVK resources;
    resources.vertexBuffer = mesh.vertexBuffer.buffer;
    resources.indexBuffer = mesh.indexBuffer.buffer;
    resources.vertexCount = mesh.vertexCount;
    resources.indexCount = mesh.indexCount;
    float minimum[3] = {source.vertices[0].position[0], source.vertices[0].position[1], source.vertices[0].position[2]};
    float maximum[3] = {minimum[0], minimum[1], minimum[2]};
    for (const vk::MeshVertexVK& vertex : source.vertices)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            minimum[axis] = std::min(minimum[axis], vertex.position[axis]);
            maximum[axis] = std::max(maximum[axis], vertex.position[axis]);
        }
    }
    for (int axis = 0; axis < 3; ++axis)
        resources.localBoundsCenter[axis] = (minimum[axis] + maximum[axis]) * 0.5f;
    float radiusSquared = 0.0f;
    for (const vk::MeshVertexVK& vertex : source.vertices)
    {
        const float dx = vertex.position[0] - resources.localBoundsCenter[0];
        const float dy = vertex.position[1] - resources.localBoundsCenter[1];
        const float dz = vertex.position[2] - resources.localBoundsCenter[2];
        radiusSquared = std::max(radiusSquared, dx * dx + dy * dy + dz * dz);
    }
    resources.localBoundsRadius = std::sqrt(radiusSquared);
    _meshRegistry.Register(mesh.resourceId, resources);
    return mesh.resourceId;
}

void EngineVK::DestroyShadowCasterMeshes()
{
    for (auto& entry : _shadowCasterMeshes)
    {
        _meshRegistry.Unregister(entry.second.resourceId);
        vk::DestroyBuffer(_device, entry.second.vertexBuffer);
        vk::DestroyBuffer(_device, entry.second.indexBuffer);
    }
    _shadowCasterMeshes.clear();
}

VkPipeline EngineVK::GetOrCreateScenePipeline(const render::RenderPassDescriptor& desc)
{
    return _scenePipelineCache.Get(desc);
}

void EngineVK::RegisterTexture(TextureVK* tex)
{
    if (tex && tex->HasValidGpuImage())
        _textureRegistry[tex->GetResourceId()] = tex;
}

void EngineVK::QueueTextureUpload(TextureVK* tex)
{
    if (!tex)
        return;
    if (std::find(_pendingTextureUploads.begin(), _pendingTextureUploads.end(), tex) == _pendingTextureUploads.end())
        _pendingTextureUploads.push_back(tex);
}

void EngineVK::CancelTextureUpload(TextureVK* tex)
{
    _pendingTextureUploads.erase(std::remove(_pendingTextureUploads.begin(), _pendingTextureUploads.end(), tex),
                                 _pendingTextureUploads.end());
    _recordedTextureUploads.erase(std::remove(_recordedTextureUploads.begin(), _recordedTextureUploads.end(), tex),
                                  _recordedTextureUploads.end());
}

void EngineVK::RetireTextureResources(vk::ImageVK image, VkSampler sampler, VkDescriptorSet descriptorSet,
                                      std::array<VkSampler, 8> samplerVariants,
                                      std::array<VkDescriptorSet, 8> descriptorVariants,
                                      std::vector<vk::BufferVK> pendingStaging)
{
    RetiredTextureResourcesVK retired;
    retired.image = image;
    retired.sampler = sampler;
    retired.descriptorSet = descriptorSet;
    retired.samplerVariants = samplerVariants;
    retired.descriptorVariants = descriptorVariants;
    retired.pendingStaging = std::move(pendingStaging);
    _retiredTextureResources.push_back(std::move(retired));
}

void EngineVK::RetireTextureStaging(vk::BufferVK staging)
{
    if (!staging.buffer && !staging.memory)
        return;
    RetiredTextureResourcesVK retired;
    retired.pendingStaging.push_back(staging);
    _retiredTextureResources.push_back(std::move(retired));
}

void EngineVK::RecordPendingTextureUploads(VkCommandBuffer commandBuffer)
{
    for (TextureVK* texture : _pendingTextureUploads)
    {
        if (!texture)
            continue;
        if (texture->RecordPendingUpload(commandBuffer))
        {
            _recordedTextureUploads.push_back(texture);
        }
    }
    _pendingTextureUploads.clear();
}

void EngineVK::CommitRecordedTextureUploads()
{
    for (TextureVK* texture : _recordedTextureUploads)
    {
        if (texture)
            texture->CommitPendingUpload(_inFlightTextureUploadStaging);
    }
    _recordedTextureUploads.clear();
}

void EngineVK::DiscardRecordedTextureUploads()
{
    // No command buffer was submitted, so its barriers did not affect image
    // layout and its staging must remain available for a later recording.
    // Re-queue the texture instead of destroying the source and pretending its
    // image made the transition to SHADER_READ_ONLY_OPTIMAL.
    for (TextureVK* texture : _recordedTextureUploads)
    {
        if (texture)
        {
            texture->DiscardPendingUpload();
            QueueTextureUpload(texture);
        }
    }
    _recordedTextureUploads.clear();
}

void EngineVK::ReleaseCompletedTextureResources()
{
    // Call only after _inFlight has completed (or vkDeviceWaitIdle at
    // shutdown). This is the sole destruction point for queued transfer
    // staging and for images/descriptors retired by TextureVK.
    for (vk::BufferVK& staging : _inFlightTextureUploadStaging)
        vk::DestroyBuffer(_device, staging);
    _inFlightTextureUploadStaging.clear();
    for (RetiredTextureResourcesVK& retired : _retiredTextureResources)
    {
        for (vk::BufferVK& staging : retired.pendingStaging)
            vk::DestroyBuffer(_device, staging);
        if (_device && _textureDescriptorPool)
        {
            for (VkDescriptorSet& descriptor : retired.descriptorVariants)
            {
                if (descriptor)
                    vkFreeDescriptorSets(_device, _textureDescriptorPool, 1, &descriptor);
            }
            if (retired.descriptorSet)
                vkFreeDescriptorSets(_device, _textureDescriptorPool, 1, &retired.descriptorSet);
        }
        if (_device)
        {
            for (VkSampler& sampler : retired.samplerVariants)
            {
                if (sampler)
                    vkDestroySampler(_device, sampler, nullptr);
            }
            if (retired.sampler)
                vkDestroySampler(_device, retired.sampler, nullptr);
        }
        vk::DestroyImage(_device, retired.image);
    }
    _retiredTextureResources.clear();
}

void EngineVK::UnregisterTexture(TextureVK* tex)
{
    if (!tex)
        return;
    const auto it = _textureRegistry.find(tex->GetResourceId());
    if (it != _textureRegistry.end() && it->second == tex)
        _textureRegistry.erase(it);
}

TextureVK* EngineVK::ResolveTexture(std::uint32_t id) const
{
    auto it = _textureRegistry.find(id);
    if (it != _textureRegistry.end())
        return it->second;
    return nullptr;
}

bool EngineVK::UpdateTerrainLayerDescriptors(const render::frame::TerrainOpaque& terrain)
{
    std::vector<vk::TerrainVK::LayerBinding> layers;
    layers.reserve(terrain.textureLayers.size());
    for (const render::frame::TextureHandle handle : terrain.textureLayers)
    {
        vk::TerrainVK::LayerBinding layer;
        TextureVK* source = ResolveTexture(handle.id);
        VkDescriptorImageInfo sourceImage{};
        const bool sourceReady = source && source != _fallbackWhiteTexture.GetRef() &&
                                 source->IsGpuReadyForSampling() && source->GetSampledImageInfo(sourceImage);
        if (sourceReady)
        {
            layer.image = sourceImage;
        }
        else
        {
            // A terrain layer never receives a stand-in descriptor. The
            // renderer waits for its actual TextureVK image, preserving the
            // sampled-image array's material identity and mip chain.
            _terrainVisualInputReason = "terrain layer image is absent or not ready; no terrain fallback is permitted";
            return false;
        }
        layers.push_back(layer);
    }
    return _terrainVk.UpdateLayerDescriptors(layers);
}

bool EngineVK::UpdateTerrainVisualDescriptors()
{
    if (!_terrainVk.Ready())
    {
        _terrainVisualInputReason = "TerrainVK is not initialized";
        return false;
    }

    // This source intentionally bypasses the normal material fallback policy.
    // The experimental gate below is the sole exception: it can supply a
    // deterministic descriptor for absent detail, never for material layers,
    // immutable maps, CSM, self-shadow, or sky visibility.
    const auto resolveAuthored = [&](TextureVK* texture, const char* sourceName, VkDescriptorImageInfo& image,
                                     std::string& reason)
    {
        if (!texture)
        {
            reason = std::string("CfgDetailTextures.") + sourceName + " is not configured or its asset is unavailable";
            return false;
        }
        if (texture == _fallbackWhiteTexture.GetRef())
        {
            reason = std::string("CfgDetailTextures.") + sourceName + " resolved to the forbidden fallback texture";
            return false;
        }
        if (!texture->HasValidGpuImage() || !texture->IsGpuReadyForSampling())
        {
            reason = std::string("CfgDetailTextures.") + sourceName + " upload has not completed";
            return false;
        }
        if (!texture->GetSampledImageInfo(image) || image.imageView == VK_NULL_HANDLE || image.sampler == VK_NULL_HANDLE ||
            image.imageLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            reason = std::string("CfgDetailTextures.") + sourceName + " has no shader-readable Vulkan image";
            return false;
        }
        return true;
    };

    TextureVK* detail = _textBank ? _textBank->GetDetailTexture() : nullptr;
    VkDescriptorImageInfo detailImage{};
    std::string detailReason;
    const bool detailReady = resolveAuthored(detail, "detail", detailImage, detailReason);
    if (!detailReady)
    {
        if (!_terrainPreviewExperiment || !_fallbackWhiteTexture)
        {
            _terrainVisualInputReason = detailReason;
            _terrainVk.DestroyRasterPipeline(_device);
            return false;
        }

        // The 1x1 RGBA(128,128,128,255) fallback is deterministic.  With the
        // terrain shader's doubled-detail modulation it is a stable neutral
        // approximation for an absent detail descriptor. Its queued upload is
        // recorded before the render pass on this same queue.
        VkDescriptorImageInfo previewFallback{};
        if (!_fallbackWhiteTexture->GetSampledImageInfo(previewFallback) ||
            previewFallback.imageView == VK_NULL_HANDLE || previewFallback.sampler == VK_NULL_HANDLE ||
            previewFallback.imageLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            _terrainVisualInputReason = "experimental preview fallback descriptor is unavailable";
            _terrainVk.DestroyRasterPipeline(_device);
            return false;
        }
        detailImage = previewFallback;
        _terrainVisualInputReason = "EXPERIMENTAL visual parity incomplete; missing " + detailReason +
                                    "; deterministic fallback bound only for absent detail descriptor";
    }
    if (!_terrainVk.UpdateVisualDescriptors(detailImage))
    {
        _terrainVisualInputReason = "TerrainVK rejected detail descriptor population";
        _terrainVk.DestroyRasterPipeline(_device);
        return false;
    }
    if (detailReady)
        _terrainVisualInputReason = "authored detail descriptor populated";
    return true;
}

bool EngineVK::CreateTerrainRasterPipeline()
{
    if (_terrainVk.RasterPipelineReady())
        return true;
    if (!_terrainVk.VisualInputsReady())
    {
        _terrainVisualInputReason = "self-shadow/sky-visibility or authored detail descriptor is not populated";
        return false;
    }
    const vk::TerrainVK::DescriptorTelemetry& layers = _terrainVk.Telemetry();
    if (layers.requestedLayers == 0 || layers.boundLayers != layers.requestedLayers || layers.fallbackLayers != 0 ||
        layers.invalidLayers != 0 || layers.invalidLayerIndices != 0)
    {
        _terrainVisualInputReason = "terrain layer array has pending/fallback/invalid entries";
        return false;
    }

    vk::TerrainVK::RasterInputs inputs;
    inputs.frameDescriptorSetLayout = _frameDescriptorSetLayout;
    inputs.visualDescriptorSetLayout = _terrainVk.VisualDescriptorSetLayout();
    inputs.frameDescriptorSet = _frameDescriptorSet;
    inputs.visualDescriptorSet = _terrainVk.VisualDescriptorSet();
    inputs.renderPass = _renderPass;
    inputs.extent = _swapchainExtent;
    inputs.csmBound = _shadowDepthImage.view != VK_NULL_HANDLE && _shadowSampler != VK_NULL_HANDLE;
    inputs.selfShadowBound = true;
    inputs.detailBound = true;
    inputs.skyVisibilityBound = true;
    if (!_terrainVk.CreateRasterPipeline(inputs, _terrainVisualInputReason))
    {
        static std::string s_lastLoggedReason;
        if (s_lastLoggedReason != _terrainVisualInputReason)
        {
            LOG_WARN(Graphics, "Vulkan terrain telemetry: terrain raster pipeline creation failed: {}", _terrainVisualInputReason);
            s_lastLoggedReason = _terrainVisualInputReason;
        }
        _terrainVisualInputReason = "terrain raster pipeline creation failed: " + _terrainVisualInputReason;
        return false;
    }
    const bool experimentalFallback =
        _terrainPreviewExperiment && _terrainVisualInputReason.rfind("EXPERIMENTAL visual parity incomplete", 0) == 0;
    if (experimentalFallback)
    {
        LOG_WARN(Graphics,
                  "Vulkan terrain telemetry: WARN label=TerrainVK-experimental-preview visual-parity=incomplete "
                  "activation=validated-detail-fallback reason={}",
                 _terrainVisualInputReason);
    }
    else
    {
        _terrainVisualInputReason = "dedicated CDLOD pipeline and all visual descriptors validated";
        LOG_INFO(Graphics, "Vulkan terrain telemetry: mode=dedicated-cdlod nodes={} batches={} legacyDraws=0 activation=validated",
                 _terrainVk.VisibleNodes().size(), _terrainVk.VisibleNodes().empty() ? 0u : 1u);
    }
    return true;
}

void EngineVK::InitDraw(bool clear, PackedColor color)
{
    // Dynamic mesh updates and buffer releases happen during draw preparation.
    // Do not write or destroy memory the previous submitted frame still reads.
    if (_device && _inFlight)
        vkWaitForFences(_device, 1, &_inFlight, VK_TRUE, UINT64_MAX);
    Engine::InitDraw(clear, color);
    if (_textBank)
        _textBank->StartFrame();
    // Source validation happens before Landscape asks whether CDLOD may replace
    // legacy terrain. That prevents a texture-bank flush from advertising a
    // stale dedicated descriptor for one frame.
    if (_terrainVk.Ready())
        UpdateTerrainVisualDescriptors();
    _drawItems.clear();
    _capturedTerrainOpaque.reset();
    _currentDrawItem = DrawItem{};
    _screenVertices.clear();
    _screenIndices.clear();
    _screenBatches.clear();
    _screenMesh = nullptr;
    _screenMeshBase = 0;
    _screenTextureId = 0;
    _screenDescriptor = {};
    _screenMeshPhase = vk::ScreenDrawPhaseVK::Overlay;
    _screenBuffersUploaded = false;
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
    auto* buf = dynamic_cast<VertexBufferVK*>(sMesh.GetVertexBuffer());
    if (!buf || buf->_sections.empty())
    {
        if (!buf)
            LOG_DEBUG(Graphics, "Vulkan: DrawSectionTL - vertex buffer is not VertexBufferVK, skipping draw");
        return;
    }

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

void EngineVK::BeginMeshTL(const Shape& sMesh, int /*spec*/, bool dynamic)
{
    // Mirror GL33's BeginMeshTL: if the vertex buffer is marked dirty or the
    // shape is dynamic, re-upload the CPU-side vertex data so the GPU buffer
    // reflects any per-frame animation. For static meshes this is a no-op.
    if (sMesh.GetVertexBuffer())
        sMesh.GetVertexBuffer()->Update(sMesh, dynamic);
}

void EngineVK::EndMeshTL(const Shape& /*sMesh*/)
{
    // No-op for Vulkan. GL33 uses EndMeshTL to ClearLights() because it binds
    // per-draw light state as GL uniforms; Vulkan light state is uploaded once
    // per frame into the frame-constants UBO and requires no per-mesh teardown.
}

void EngineVK::PrepareTriangleTL(const MipInfo& mip, const Poseidon::render::LegacySpec& spec)
{
    _currentDrawItem.backendTextureResourceId = GetOrCreateTextureResourceId(mip._texture);
    _currentDrawItem.texture = mip._texture;
    _currentDrawItem.specFlags = spec;

    using B = render::Backend;
    if (render::Has(spec.backend, B::GrassTexture) && _textBank)
    {
        TextureVK* grass = _textBank->GetGrassTexture();
        _lastTexture1ResourceId = grass ? GetOrCreateTextureResourceId(grass) : 1u;
    }
    else if (render::Has(spec.backend, B::DetailTexture) && _textBank)
    {
        TextureVK* detail = _textBank->GetDetailTexture();
        _lastTexture1ResourceId = detail ? GetOrCreateTextureResourceId(detail) : 1u;
    }
    else if (render::Has(spec.backend, B::SpecularTexture) && _textBank)
    {
        TextureVK* spec2 = _textBank->GetSpecularTexture();
        _lastTexture1ResourceId = spec2 ? GetOrCreateTextureResourceId(spec2) : 1u;
    }
    else
    {
        _lastTexture1ResourceId = 1u; // white fallback
    }
    _currentDrawItem.backendTexture1ResourceId = _lastTexture1ResourceId;
}

void EngineVK::SetGrassParams(float a1, float a2, float a3, float a4)
{
    _grassParam[0] = a1;
    _grassParam[1] = a2;
    _grassParam[2] = a3;
    _grassParam[3] = a4;
}

void EngineVK::PrepareMeshTL(const LightList& lights, const Matrix4& modelToWorld,
                             const Poseidon::render::LegacySpec& spec)
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
    _currentDrawItem.passKindHint = GetPassKindHint();
    if (GScene && render::Has(spec.routing, render::Routing::IsColored))
    {
        ColorVal tint = GScene->GetConstantColor();
        _currentDrawItem.tint[0] = tint.R();
        _currentDrawItem.tint[1] = tint.G();
        _currentDrawItem.tint[2] = tint.B();
        _currentDrawItem.tint[3] = tint.A();
    }
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
    PushScreenQuad(pos, textureId, ScreenDescriptorFromLegacySpec(pars.spec));
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
    if (_screenVertices.size() + static_cast<std::size_t>(n) > std::numeric_limits<std::uint16_t>::max())
    {
        LOG_WARN(Graphics, "Vulkan: skipping 2D polygon with {} vertices; screen index range is exhausted", n);
        return;
    }

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

    AppendScreenBatch(textureId, indexCount, vk::ScreenDrawPhaseVK::Overlay, ScreenDescriptorFromLegacySpec(specFlags));
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
    if (_screenVertices.size() + static_cast<std::size_t>(n) > std::numeric_limits<std::uint16_t>::max())
    {
        LOG_WARN(Graphics, "Vulkan: skipping 2D polygon with {} vertices; screen index range is exhausted", n);
        return;
    }

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

    AppendScreenBatch(textureId, indexCount, vk::ScreenDrawPhaseVK::Overlay, ScreenDescriptorFromLegacySpec(specFlags));
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

void EngineVK::PrepareTriangle(const MipInfo& mip, int specFlags)
{
    _screenTextureId = GetOrCreateTextureResourceId(mip._texture);
    _screenDescriptor = ScreenDescriptorFromLegacySpec(specFlags);
}

void EngineVK::DrawPolygon(const VertexIndex* indices, int nVertices)
{
    if (!_screenMesh || !indices || nVertices < 3)
        return;

    const int meshVertexCount = _screenMesh->NVertex();
    for (int i = 0; i < nVertices; ++i)
    {
        if (indices[i] < 0 || indices[i] >= meshVertexCount)
            return;
    }

    const std::uint32_t indexCount = static_cast<std::uint32_t>((nVertices - 2) * 3);
    for (int i = 1; i < nVertices - 1; ++i)
    {
        _screenIndices.push_back(static_cast<std::uint16_t>(_screenMeshBase + indices[0]));
        _screenIndices.push_back(static_cast<std::uint16_t>(_screenMeshBase + indices[i]));
        _screenIndices.push_back(static_cast<std::uint16_t>(_screenMeshBase + indices[i + 1]));
    }

    AppendScreenBatch(_screenTextureId, indexCount, _screenMeshPhase, _screenDescriptor);
}

void EngineVK::DrawSection(const FaceArray& faces, Offset begin, Offset end)
{
    for (Offset index = begin; index < end; faces.Next(index))
    {
        const Poly& face = faces[index];
        DrawPolygon(face.GetVertexList(), face.N());
    }
}

void EngineVK::PrepareMesh(const render::LegacySpec& spec, ClipFlags clipFlags)
{
    if (WorldCompositionActive())
    {
        // Background software-T&L belongs to the world target; overlay batches
        // use the fresh depth attachment in the present pass.
        _screenMeshPhase = vk::ScreenDrawPhaseFromLegacyContext(spec, clipFlags);
    }
    else
    {
        _screenMeshPhase = ProceduralSkyActive() ? vk::ScreenDrawPhaseVK::Overlay
                                                  : vk::ScreenDrawPhaseFromLegacyContext(spec, clipFlags);
    }
}

void EngineVK::BeginMesh(TLVertexTable& mesh, const render::LegacySpec& /*spec*/)
{
    const std::size_t vertexCount = static_cast<std::size_t>(mesh.NVertex());
    if (_screenVertices.size() + vertexCount > std::numeric_limits<std::uint16_t>::max())
    {
        LOG_WARN(Graphics, "Vulkan: skipping software-T&L mesh with {} vertices; screen index range is exhausted",
                 vertexCount);
        _screenMesh = nullptr;
        return;
    }

    _screenMeshBase = _screenVertices.size();
    _screenVertices.insert(_screenVertices.end(), mesh.VertexData(), mesh.VertexData() + vertexCount);
    _screenMesh = &mesh;
}

void EngineVK::EndMesh(TLVertexTable& /*mesh*/)
{
    _screenMesh = nullptr;
}

void EngineVK::PushScreenQuad(const TLVertex* quad, std::uint32_t textureId,
                               const render::RenderPassDescriptor& descriptor)
{
    if (_screenVertices.size() + 4 > std::numeric_limits<std::uint16_t>::max())
    {
        LOG_WARN(Graphics, "Vulkan: skipping screen quad; screen index range is exhausted");
        return;
    }

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

    AppendScreenBatch(textureId, 6, vk::ScreenDrawPhaseVK::Overlay, descriptor);
}

void EngineVK::AppendScreenBatch(std::uint32_t textureId, std::uint32_t indexCount, vk::ScreenDrawPhaseVK phase,
                                  const render::RenderPassDescriptor& descriptor)
{
    if (indexCount == 0)
        return;

    const std::uint32_t firstIndex = static_cast<std::uint32_t>(_screenIndices.size() - indexCount);
    if (!_screenBatches.empty())
    {
        ScreenBatchVK& previous = _screenBatches.back();
        if (previous.textureId == textureId && SameScreenDescriptor(previous.descriptor, descriptor) && previous.phase == phase &&
            previous.firstIndex + previous.indexCount == firstIndex)
        {
            previous.indexCount += indexCount;
            return;
        }
    }

    _screenBatches.push_back({textureId, firstIndex, indexCount, phase, descriptor});
}

void EngineVK::RecordScreenDraws(VkCommandBuffer commandBuffer, vk::ScreenDrawPhaseVK phase)
{
    if (_screenVertices.empty() || _screenIndices.empty())
        return;

    if (!_screenBuffersUploaded)
    {
        if (!EnsureScreenVertexBufferCapacity(_screenVertices.size(), _screenIndices.size()))
            return;

        vk::UploadMappedBuffer(_screenVertexBuffer, _screenVertices.data(), _screenVertices.size() * sizeof(TLVertex));
        vk::UploadMappedBuffer(_screenIndexBuffer, _screenIndices.data(),
                               _screenIndices.size() * sizeof(std::uint16_t));
        _screenBuffersUploaded = true;
    }

    VkBuffer vertexBuffers[] = {_screenVertexBuffer.buffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, _screenIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);

    VkDescriptorSet lastBoundTexDescriptorSet = VK_NULL_HANDLE;
    VkPipeline lastBoundPipeline = VK_NULL_HANDLE;

    for (const ScreenBatchVK& batch : _screenBatches)
    {
        if (batch.phase != phase || batch.indexCount == 0)
            continue;

        const bool useOverlaySubpass = phase == vk::ScreenDrawPhaseVK::Overlay && WorldCompositionActive();
        vk::PipelineCacheVK& pipelineCache = useOverlaySubpass ? _screenOverlayPipelineCache : _screenPipelineCache;
        const VkPipeline fallbackPipeline = useOverlaySubpass ? _screenOverlayPipeline : _screenPipeline;
        vk::PipelineKeyVK pipelineKey = vk::KeyFromDescriptor(batch.descriptor);
        const VkPipeline pipeline = pipelineCache.Get(pipelineKey);
        if (pipeline != lastBoundPipeline)
        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline ? pipeline : fallbackPipeline);
            lastBoundPipeline = pipeline;
        }

        const std::uint32_t samplerFilter = static_cast<std::uint32_t>(batch.descriptor.sampler.filter);
        const std::uint32_t samplerClamp = vk::BuildSamplerClampMask(batch.descriptor.sampler);
        const vk::ScreenPushConstantsVK constants = vk::BuildScreenPushConstants(
            _width, _height, static_cast<std::uint32_t>(batch.descriptor.alpha), batch.descriptor.alphaRef,
            static_cast<std::uint32_t>(batch.descriptor.fog), _lastFrameConstants.fogColor);
        vkCmdPushConstants(commandBuffer, _screenPipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           vk::kScreenPushConstantsSize, &constants);

        VkDescriptorSet texDescriptorSet = VK_NULL_HANDLE;
        if (batch.textureId != 0)
        {
            TextureVK* tex = ResolveTexture(batch.textureId);
            if (tex)
                texDescriptorSet = tex->GetDescriptorSet(samplerFilter, samplerClamp);
        }
        if (texDescriptorSet == VK_NULL_HANDLE && _fallbackWhiteTexture)
        {
            texDescriptorSet = _fallbackWhiteTexture->GetDescriptorSet(samplerFilter, samplerClamp);
        }

        if (texDescriptorSet != VK_NULL_HANDLE && texDescriptorSet != lastBoundTexDescriptorSet)
        {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _screenPipelineLayout, 0, 1,
                                    &texDescriptorSet, 0, nullptr);
            lastBoundTexDescriptorSet = texDescriptorSet;
        }

        vkCmdDrawIndexed(commandBuffer, batch.indexCount, 1, batch.firstIndex, 0, 0);
    }
}

} // namespace Poseidon
