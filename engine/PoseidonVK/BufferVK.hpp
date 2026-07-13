#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>

namespace Poseidon::vk
{

inline constexpr uint32_t kInvalidMemoryType = UINT32_MAX;

struct BufferVK
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
};

struct ImageVK
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    uint32_t mipLevels = 1;
};

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);

VkResult CreateHostVisibleBuffer(VkPhysicalDevice physicalDevice, VkDevice device, VkDeviceSize size,
                                  VkBufferUsageFlags usage, BufferVK& out);

// Persistent GPU work buffers.  Unlike frame uploads these remain device local;
// callers initialise/clear them with transfer commands before compute writes.
VkResult CreateDeviceLocalBuffer(VkPhysicalDevice physicalDevice, VkDevice device, VkDeviceSize size,
                                 VkBufferUsageFlags usage, BufferVK& out);

void UploadMappedBuffer(const BufferVK& buffer, const void* data, std::size_t size);

void DestroyBuffer(VkDevice device, BufferVK& buffer);

// --- Image helpers ---

VkResult CreateImage2D(VkPhysicalDevice physicalDevice, VkDevice device,
                       uint32_t width, uint32_t height, uint32_t mipLevels,
                       VkFormat format, VkImageUsageFlags usage,
                        VkMemoryPropertyFlags memProps, ImageVK& out);

// A sampled 3D field.  Cloud volumes use this instead of pretending a
// screen-space cache is a persistent world resource.
VkResult CreateImage3D(VkPhysicalDevice physicalDevice, VkDevice device,
                       uint32_t width, uint32_t height, uint32_t depth,
                       VkFormat format, VkImageUsageFlags usage,
                       VkMemoryPropertyFlags memProps, ImageVK& out);

void TransitionImageLayout(VkDevice device, VkCommandPool commandPool, VkQueue queue,
                           VkImage image, uint32_t mipLevels,
                           VkImageLayout oldLayout, VkImageLayout newLayout);

void CopyBufferToImage(VkDevice device, VkCommandPool commandPool, VkQueue queue,
                       VkBuffer src, VkImage dst,
                       uint32_t width, uint32_t height, uint32_t mipLevel = 0);

void DestroyImage(VkDevice device, ImageVK& image);

} // namespace Poseidon::vk
