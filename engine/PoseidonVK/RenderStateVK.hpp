#pragma once

#include <Poseidon/Graphics/Rendering/RenderPassDescriptor.hpp>
#include <vulkan/vulkan.h>

namespace Poseidon::vk
{

inline VkCullModeFlags ToVkCullMode(render::CullMode mode) noexcept
{
    switch (mode)
    {
        case render::CullMode::Back:
            return VK_CULL_MODE_BACK_BIT;
        case render::CullMode::Front:
            return VK_CULL_MODE_FRONT_BIT;
        case render::CullMode::None:
            return VK_CULL_MODE_NONE;
    }
    return VK_CULL_MODE_BACK_BIT;
}

inline VkFrontFace ToVkFrontFace(render::FrontFaceMode mode) noexcept
{
    // RenderPassDescriptor carries the mesh winding after the engine's camera
    // projection convention has been applied. Preserve that convention here:
    // inverting it culls every ordinary front-facing terrain and model polygon.
    switch (mode)
    {
        case render::FrontFaceMode::CW:
            return VK_FRONT_FACE_CLOCKWISE;
        case render::FrontFaceMode::CCW:
            return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }
    return VK_FRONT_FACE_CLOCKWISE;
}

inline VkPipelineRasterizationStateCreateInfo BuildRasterizationState(
    render::CullMode cull, render::FrontFaceMode frontFace) noexcept
{
    VkPipelineRasterizationStateCreateInfo state{};
    state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    state.polygonMode = VK_POLYGON_MODE_FILL;
    state.cullMode = ToVkCullMode(cull);
    state.frontFace = ToVkFrontFace(frontFace);
    state.lineWidth = 1.0f;
    return state;
}

inline VkPipelineDepthStencilStateCreateInfo BuildDepthStencilState(render::DepthMode mode) noexcept
{
    VkPipelineDepthStencilStateCreateInfo state{};
    state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    state.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    state.minDepthBounds = 0.0f;
    state.maxDepthBounds = 1.0f;

    switch (mode)
    {
        case render::DepthMode::Normal:
            state.depthTestEnable = VK_TRUE;
            state.depthWriteEnable = VK_TRUE;
            break;
        case render::DepthMode::ReadOnly:
            state.depthTestEnable = VK_TRUE;
            state.depthWriteEnable = VK_FALSE;
            break;
        case render::DepthMode::Disabled:
            state.depthTestEnable = VK_TRUE;
            state.depthWriteEnable = VK_FALSE;
            state.depthCompareOp = VK_COMPARE_OP_ALWAYS;
            break;
        case render::DepthMode::Shadow:
            state.depthTestEnable = VK_TRUE;
            state.depthWriteEnable = VK_FALSE;
            state.stencilTestEnable = VK_TRUE;
            state.front.compareOp = VK_COMPARE_OP_EQUAL;
            state.front.failOp = VK_STENCIL_OP_KEEP;
            state.front.passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
            state.front.depthFailOp = VK_STENCIL_OP_KEEP;
            state.front.compareMask = 0xff;
            state.front.writeMask = 0xff;
            state.front.reference = 0;
            state.back = state.front;
            break;
    }

    return state;
}

inline VkPipelineColorBlendAttachmentState BuildColorBlendAttachmentState(render::BlendMode mode) noexcept
{
    VkPipelineColorBlendAttachmentState state{};
    state.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    state.colorBlendOp = VK_BLEND_OP_ADD;
    state.alphaBlendOp = VK_BLEND_OP_ADD;

    switch (mode)
    {
        case render::BlendMode::Opaque:
            state.blendEnable = VK_FALSE;
            state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            state.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            break;
        case render::BlendMode::AlphaBlend:
            state.blendEnable = VK_TRUE;
            state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            state.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            break;
        case render::BlendMode::Additive:
            state.blendEnable = VK_TRUE;
            state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            state.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            break;
        case render::BlendMode::Shadow:
            state.blendEnable = VK_TRUE;
            state.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            break;
    }

    return state;
}

// Sampler state translation. These mappings mirror the GL33 sampler-state
// table in EngineGL33_State.cpp so a Vulkan sampler reads identically to its
// GL33 counterpart for the same `SamplerMode`:
//
//   filter Point  -> mag NEAREST, min NEAREST_MIPMAP_NEAREST, mipmap NEAREST
//   filter Linear -> mag LINEAR,  min LINEAR_MIPMAP_LINEAR,  mipmap LINEAR
//   clampU/V true -> CLAMP_TO_EDGE; false -> REPEAT
inline VkFilter ToVkMagFilter(render::SamplerFilter filter) noexcept
{
    return filter == render::SamplerFilter::Point ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

inline VkFilter ToVkMinFilter(render::SamplerFilter filter) noexcept
{
    return filter == render::SamplerFilter::Point ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

inline VkSamplerMipmapMode ToVkMipmapMode(render::SamplerFilter filter) noexcept
{
    return filter == render::SamplerFilter::Point ? VK_SAMPLER_MIPMAP_MODE_NEAREST
                                                   : VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

inline VkSamplerAddressMode ToVkAddressMode(bool clamp) noexcept
{
    return clamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

inline VkSamplerCreateInfo BuildSamplerState(const render::SamplerMode& sampler) noexcept
{
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = ToVkMagFilter(sampler.filter);
    info.minFilter = ToVkMinFilter(sampler.filter);
    info.mipmapMode = ToVkMipmapMode(sampler.filter);
    info.addressModeU = ToVkAddressMode(sampler.clampU);
    info.addressModeV = ToVkAddressMode(sampler.clampV);
    info.addressModeW = ToVkAddressMode(sampler.clampV);
    info.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
    info.unnormalizedCoordinates = VK_FALSE;
    info.compareEnable = VK_FALSE;
    info.compareOp = VK_COMPARE_OP_NEVER;
    info.mipLodBias = 0.0f;
    info.minLod = 0.0f;
    info.maxLod = VK_LOD_CLAMP_NONE;
    return info;
}

} // namespace Poseidon::vk
