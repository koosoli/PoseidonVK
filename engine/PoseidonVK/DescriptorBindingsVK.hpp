#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

namespace Poseidon::vk
{

// Binding indices for the per-frame descriptor set (set 0). These are the
// single source of truth shared by the descriptor set layout, the descriptor
// pool, the descriptor writes, and the shader declarations. Future mesh work
// must keep the live path and the shaders in lock-step with these constants.
inline constexpr uint32_t kFrameConstantsBinding = 0;
inline constexpr uint32_t kDrawConstantsBinding = 1;
inline constexpr uint32_t kShadowMapBinding = 2;
inline constexpr uint32_t kFrameDescriptorSetBindingCount = 3;

// Shader stages that may read each binding. Both constants feed the vertex
// and fragment stages today; lighting and fog are computed fragment-side while
// camera/projection and world transforms are consumed vertex-side.
inline constexpr VkShaderStageFlags kFrameConstantsStages =
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
inline constexpr VkShaderStageFlags kDrawConstantsStages =
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
inline constexpr VkShaderStageFlags kShadowMapStages =
    VK_SHADER_STAGE_FRAGMENT_BIT;

inline VkDescriptorSetLayoutBinding MakeFrameConstantsLayoutBinding() noexcept
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = kFrameConstantsBinding;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = kFrameConstantsStages;
    return binding;
}

inline VkDescriptorSetLayoutBinding MakeDrawConstantsLayoutBinding() noexcept
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = kDrawConstantsBinding;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = kDrawConstantsStages;
    return binding;
}

inline VkDescriptorSetLayoutBinding MakeShadowMapLayoutBinding() noexcept
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = kShadowMapBinding;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = kShadowMapStages;
    return binding;
}

inline std::array<VkDescriptorSetLayoutBinding, kFrameDescriptorSetBindingCount>
MakeFrameDescriptorSetLayoutBindings() noexcept
{
    return {MakeFrameConstantsLayoutBinding(), MakeDrawConstantsLayoutBinding(), MakeShadowMapLayoutBinding()};
}

inline std::array<VkDescriptorPoolSize, kFrameDescriptorSetBindingCount>
MakeFrameDescriptorPoolSizes() noexcept
{
    return {VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}};
}

inline VkWriteDescriptorSet MakeFrameConstantsDescriptorWrite(
    VkDescriptorSet dstSet, const VkDescriptorBufferInfo* bufferInfo) noexcept
{
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = dstSet;
    write.dstBinding = kFrameConstantsBinding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = bufferInfo;
    return write;
}

inline VkWriteDescriptorSet MakeDrawConstantsDescriptorWrite(
    VkDescriptorSet dstSet, const VkDescriptorBufferInfo* bufferInfo) noexcept
{
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = dstSet;
    write.dstBinding = kDrawConstantsBinding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = bufferInfo;
    return write;
}

inline VkWriteDescriptorSet MakeShadowMapDescriptorWrite(
    VkDescriptorSet dstSet, const VkDescriptorImageInfo* imageInfo) noexcept
{
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = dstSet;
    write.dstBinding = kShadowMapBinding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = imageInfo;
    return write;
}

static_assert(kFrameConstantsBinding < kFrameDescriptorSetBindingCount);
static_assert(kDrawConstantsBinding < kFrameDescriptorSetBindingCount);
static_assert(kShadowMapBinding < kFrameDescriptorSetBindingCount);
static_assert(kFrameConstantsBinding != kDrawConstantsBinding);
static_assert(kFrameConstantsBinding != kShadowMapBinding);
static_assert(kDrawConstantsBinding != kShadowMapBinding);

} // namespace Poseidon::vk
