#include <PoseidonVK/BufferVK.hpp>

#include <cstring>

namespace Poseidon::vk
{

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        const bool typeMatches = (typeFilter & (1u << i)) != 0;
        const bool flagsMatch = (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties;
        if (typeMatches && flagsMatch)
            return i;
    }

    return kInvalidMemoryType;
}

VkResult CreateHostVisibleBuffer(VkPhysicalDevice physicalDevice, VkDevice device, VkDeviceSize size,
                                 VkBufferUsageFlags usage, BufferVK& out)
{
    DestroyBuffer(device, out);
    out.size = size;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(device, &bufferInfo, nullptr, &out.buffer);
    if (result != VK_SUCCESS)
    {
        DestroyBuffer(device, out);
        return result;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, out.buffer, &requirements);

    const uint32_t memoryType =
        FindMemoryType(physicalDevice, requirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memoryType == kInvalidMemoryType)
    {
        DestroyBuffer(device, out);
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = memoryType;

    result = vkAllocateMemory(device, &allocateInfo, nullptr, &out.memory);
    if (result != VK_SUCCESS)
    {
        DestroyBuffer(device, out);
        return result;
    }

    result = vkBindBufferMemory(device, out.buffer, out.memory, 0);
    if (result != VK_SUCCESS)
    {
        DestroyBuffer(device, out);
        return result;
    }

    result = vkMapMemory(device, out.memory, 0, size, 0, &out.mapped);
    if (result != VK_SUCCESS)
    {
        DestroyBuffer(device, out);
        return result;
    }

    return VK_SUCCESS;
}

void UploadMappedBuffer(const BufferVK& buffer, const void* data, std::size_t size)
{
    if (!buffer.mapped || !data || size == 0)
        return;

    std::memcpy(buffer.mapped, data, size);
}

void DestroyBuffer(VkDevice device, BufferVK& buffer)
{
    if (device && buffer.mapped)
    {
        vkUnmapMemory(device, buffer.memory);
        buffer.mapped = nullptr;
    }
    if (device && buffer.buffer)
    {
        vkDestroyBuffer(device, buffer.buffer, nullptr);
        buffer.buffer = VK_NULL_HANDLE;
    }
    if (device && buffer.memory)
    {
        vkFreeMemory(device, buffer.memory, nullptr);
        buffer.memory = VK_NULL_HANDLE;
    }
    buffer.size = 0;
}


// ============================================================
// Image helpers
// ============================================================

namespace
{
VkCommandBuffer BeginOneShot(VkDevice device, VkCommandPool pool)
{
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandPool = pool;
    ai.commandBufferCount = 1;

    VkCommandBuffer cb = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &ai, &cb);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);
    return cb;
}

void EndOneShot(VkDevice device, VkCommandPool pool, VkQueue queue, VkCommandBuffer cb)
{
    vkEndCommandBuffer(cb);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, pool, 1, &cb);
}
} // namespace

VkResult CreateImage2D(VkPhysicalDevice physicalDevice, VkDevice device,
                       uint32_t width, uint32_t height, uint32_t mipLevels,
                       VkFormat format, VkImageUsageFlags usage,
                       VkMemoryPropertyFlags memProps, ImageVK& out)
{
    DestroyImage(device, out);
    out.format = format;
    out.width = width;
    out.height = height;
    out.mipLevels = mipLevels;

    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent = {width, height, 1};
    ii.mipLevels = mipLevels;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult r = vkCreateImage(device, &ii, nullptr, &out.image);
    if (r != VK_SUCCESS) return r;

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device, out.image, &req);

    uint32_t memIdx = FindMemoryType(physicalDevice, req.memoryTypeBits, memProps);
    if (memIdx == kInvalidMemoryType)
    {
        DestroyImage(device, out);
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = memIdx;

    r = vkAllocateMemory(device, &mai, nullptr, &out.memory);
    if (r != VK_SUCCESS) { DestroyImage(device, out); return r; }

    r = vkBindImageMemory(device, out.image, out.memory, 0);
    if (r != VK_SUCCESS) { DestroyImage(device, out); return r; }

    // Build image view
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = out.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                     VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.baseMipLevel = 0;
    vi.subresourceRange.levelCount = mipLevels;
    vi.subresourceRange.baseArrayLayer = 0;
    vi.subresourceRange.layerCount = 1;

    r = vkCreateImageView(device, &vi, nullptr, &out.view);
    if (r != VK_SUCCESS) { DestroyImage(device, out); return r; }

    return VK_SUCCESS;
}

void TransitionImageLayout(VkDevice device, VkCommandPool commandPool, VkQueue queue,
                           VkImage image, uint32_t mipLevels,
                           VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkCommandBuffer cb = BeginOneShot(device, commandPool);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    EndOneShot(device, commandPool, queue, cb);
}

void CopyBufferToImage(VkDevice device, VkCommandPool commandPool, VkQueue queue,
                       VkBuffer src, VkImage dst,
                       uint32_t width, uint32_t height, uint32_t mipLevel)
{
    VkCommandBuffer cb = BeginOneShot(device, commandPool);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(cb, src, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    EndOneShot(device, commandPool, queue, cb);
}

void DestroyImage(VkDevice device, ImageVK& img)
{
    if (device && img.view)
    {
        vkDestroyImageView(device, img.view, nullptr);
        img.view = VK_NULL_HANDLE;
    }
    if (device && img.image)
    {
        vkDestroyImage(device, img.image, nullptr);
        img.image = VK_NULL_HANDLE;
    }
    if (device && img.memory)
    {
        vkFreeMemory(device, img.memory, nullptr);
        img.memory = VK_NULL_HANDLE;
    }
    img.format = VK_FORMAT_UNDEFINED;
    img.width = img.height = img.mipLevels = 0;
}

} // namespace Poseidon::vk
