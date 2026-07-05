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
    switch (mode)
    {
        case render::FrontFaceMode::CW:
            return VK_FRONT_FACE_CLOCKWISE;
        case render::FrontFaceMode::CCW:
            return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }
    return VK_FRONT_FACE_CLOCKWISE;
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

} // namespace Poseidon::vk
