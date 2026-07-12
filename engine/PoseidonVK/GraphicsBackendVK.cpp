#include <PoseidonVK/GraphicsBackendVK.hpp>

#include <PoseidonVK/EngineVK.hpp>
#include <Poseidon/Graphics/GraphicsEngineFactory.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <vulkan/vulkan.h>

#include <string>
#include <vector>

namespace
{

struct VulkanProbeResult
{
    bool loaderReady = false;
    uint32_t apiVersion = VK_API_VERSION_1_0;
    uint32_t instanceExtensionCount = 0;
    uint32_t physicalDeviceCount = 0;
    std::string reason;
};

std::string VkResultName(VkResult result)
{
    switch (result)
    {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_NOT_READY:
            return "VK_NOT_READY";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_EVENT_SET:
            return "VK_EVENT_SET";
        case VK_EVENT_RESET:
            return "VK_EVENT_RESET";
        case VK_INCOMPLETE:
            return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:
            return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS:
            return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
            return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL:
            return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_UNKNOWN:
            return "VK_ERROR_UNKNOWN";
        default:
            return "VkResult(" + std::to_string(static_cast<int>(result)) + ")";
    }
}

std::string VersionString(uint32_t version)
{
    return std::to_string(VK_VERSION_MAJOR(version)) + "." + std::to_string(VK_VERSION_MINOR(version)) + "." +
           std::to_string(VK_VERSION_PATCH(version));
}

VulkanProbeResult ProbeVulkan()
{
    VulkanProbeResult probe;

    VkResult result = vkEnumerateInstanceVersion(&probe.apiVersion);
    if (result != VK_SUCCESS)
    {
        probe.reason = "vkEnumerateInstanceVersion failed: " + VkResultName(result);
        return probe;
    }
    if (probe.apiVersion < VK_API_VERSION_1_3)
    {
        probe.reason = "Vulkan 1.3 is required; loader reports " + VersionString(probe.apiVersion);
        return probe;
    }

    result = vkEnumerateInstanceExtensionProperties(nullptr, &probe.instanceExtensionCount, nullptr);
    if (result != VK_SUCCESS)
    {
        probe.reason = "vkEnumerateInstanceExtensionProperties failed: " + VkResultName(result);
        return probe;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Poseidon";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "PoseidonVK";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;

    VkInstance instance = VK_NULL_HANDLE;
    result = vkCreateInstance(&instanceInfo, nullptr, &instance);
    if (result != VK_SUCCESS)
    {
        probe.reason = "vkCreateInstance failed: " + VkResultName(result);
        return probe;
    }

    result = vkEnumeratePhysicalDevices(instance, &probe.physicalDeviceCount, nullptr);
    if (result != VK_SUCCESS)
    {
        vkDestroyInstance(instance, nullptr);
        probe.reason = "vkEnumeratePhysicalDevices failed: " + VkResultName(result);
        return probe;
    }
    if (probe.physicalDeviceCount == 0)
    {
        vkDestroyInstance(instance, nullptr);
        probe.reason = "no Vulkan physical devices reported by the loader";
        return probe;
    }

    std::vector<VkPhysicalDevice> devices(probe.physicalDeviceCount);
    result = vkEnumeratePhysicalDevices(instance, &probe.physicalDeviceCount, devices.data());
    if (result != VK_SUCCESS)
    {
        vkDestroyInstance(instance, nullptr);
        probe.reason = "vkEnumeratePhysicalDevices failed: " + VkResultName(result);
        return probe;
    }

    bool hasVulkan11Device = false;
    for (VkPhysicalDevice device : devices)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        if (properties.apiVersion >= VK_API_VERSION_1_3)
        {
            hasVulkan11Device = true;
            break;
        }
    }
    vkDestroyInstance(instance, nullptr);
    if (!hasVulkan11Device)
    {
        probe.reason = "no Vulkan 1.3 physical devices reported by the loader";
        return probe;
    }

    probe.loaderReady = true;
    probe.reason = "Vulkan loader ready";
    return probe;
}

const VulkanProbeResult& GetVulkanProbe()
{
    static const VulkanProbeResult probe = ProbeVulkan();
    return probe;
}

void LogVulkanProbeOnce()
{
    static bool logged = false;
    if (logged)
        return;

    logged = true;
    const VulkanProbeResult& probe = GetVulkanProbe();
    if (probe.loaderReady)
    {
        LOG_INFO(Graphics,
                 "Vulkan: loader available (api={}, instance_extensions={}, physical_devices={}); bootstrap renderer "
                 "can initialize when SDL exposes Vulkan window support",
                 VersionString(probe.apiVersion), probe.instanceExtensionCount, probe.physicalDeviceCount);
    }
    else
    {
        LOG_WARN(Graphics, "Vulkan: unavailable ({})", probe.reason);
    }
}

Poseidon::Engine* CreateVulkanBackend(const Poseidon::GraphicsEngineParams& params)
{
    LogVulkanProbeOnce();
    Poseidon::EngineVK* engine =
        new Poseidon::EngineVK(params.width, params.height, params.useWindow, params.bitsPerPixel, params.displayMode);
    if (!engine->IsInitialized())
    {
        delete engine;
        return nullptr;
    }
    return engine;
}

bool IsVulkanAvailable()
{
    LogVulkanProbeOnce();
    return GetVulkanProbe().loaderReady;
}

} // namespace

namespace Poseidon
{

void RegisterVulkanGraphicsBackend()
{
    GraphicsEngineFactory::Register(GraphicsBackendDescriptor{
        "vulkan",
        "Vulkan",
        50,
        &CreateVulkanBackend,
        &IsVulkanAvailable,
    });
}

} // namespace Poseidon
