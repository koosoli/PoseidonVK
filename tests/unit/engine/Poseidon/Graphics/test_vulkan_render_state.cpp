#include <catch2/catch_test_macros.hpp>

#include <PoseidonVK/RenderStateVK.hpp>

TEST_CASE("Vulkan render state maps cull and winding descriptors", "[vulkan][render-state]")
{
    CHECK(Poseidon::vk::ToVkCullMode(Poseidon::render::CullMode::Back) == VK_CULL_MODE_BACK_BIT);
    CHECK(Poseidon::vk::ToVkCullMode(Poseidon::render::CullMode::Front) == VK_CULL_MODE_FRONT_BIT);
    CHECK(Poseidon::vk::ToVkCullMode(Poseidon::render::CullMode::None) == VK_CULL_MODE_NONE);

    CHECK(Poseidon::vk::ToVkFrontFace(Poseidon::render::FrontFaceMode::CW) == VK_FRONT_FACE_CLOCKWISE);
    CHECK(Poseidon::vk::ToVkFrontFace(Poseidon::render::FrontFaceMode::CCW) ==
          VK_FRONT_FACE_COUNTER_CLOCKWISE);
}

TEST_CASE("Vulkan render state maps depth descriptors", "[vulkan][render-state]")
{
    {
        const VkPipelineDepthStencilStateCreateInfo state =
            Poseidon::vk::BuildDepthStencilState(Poseidon::render::DepthMode::Normal);
        CHECK(state.depthTestEnable == VK_TRUE);
        CHECK(state.depthWriteEnable == VK_TRUE);
        CHECK(state.depthCompareOp == VK_COMPARE_OP_LESS_OR_EQUAL);
        CHECK(state.stencilTestEnable == VK_FALSE);
    }
    {
        const VkPipelineDepthStencilStateCreateInfo state =
            Poseidon::vk::BuildDepthStencilState(Poseidon::render::DepthMode::ReadOnly);
        CHECK(state.depthTestEnable == VK_TRUE);
        CHECK(state.depthWriteEnable == VK_FALSE);
        CHECK(state.depthCompareOp == VK_COMPARE_OP_LESS_OR_EQUAL);
    }
    {
        const VkPipelineDepthStencilStateCreateInfo state =
            Poseidon::vk::BuildDepthStencilState(Poseidon::render::DepthMode::Disabled);
        CHECK(state.depthTestEnable == VK_TRUE);
        CHECK(state.depthWriteEnable == VK_FALSE);
        CHECK(state.depthCompareOp == VK_COMPARE_OP_ALWAYS);
    }
    {
        const VkPipelineDepthStencilStateCreateInfo state =
            Poseidon::vk::BuildDepthStencilState(Poseidon::render::DepthMode::Shadow);
        CHECK(state.depthTestEnable == VK_TRUE);
        CHECK(state.depthWriteEnable == VK_FALSE);
        CHECK(state.stencilTestEnable == VK_TRUE);
        CHECK(state.front.compareOp == VK_COMPARE_OP_EQUAL);
        CHECK(state.front.passOp == VK_STENCIL_OP_INCREMENT_AND_CLAMP);
        CHECK(state.front.reference == 0);
        CHECK(state.back.passOp == VK_STENCIL_OP_INCREMENT_AND_CLAMP);
    }
}

TEST_CASE("Vulkan render state maps blend descriptors", "[vulkan][render-state]")
{
    {
        const VkPipelineColorBlendAttachmentState state =
            Poseidon::vk::BuildColorBlendAttachmentState(Poseidon::render::BlendMode::Opaque);
        CHECK(state.blendEnable == VK_FALSE);
        CHECK(state.srcColorBlendFactor == VK_BLEND_FACTOR_ONE);
        CHECK(state.dstColorBlendFactor == VK_BLEND_FACTOR_ZERO);
    }
    {
        const VkPipelineColorBlendAttachmentState state =
            Poseidon::vk::BuildColorBlendAttachmentState(Poseidon::render::BlendMode::AlphaBlend);
        CHECK(state.blendEnable == VK_TRUE);
        CHECK(state.srcColorBlendFactor == VK_BLEND_FACTOR_SRC_ALPHA);
        CHECK(state.dstColorBlendFactor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    }
    {
        const VkPipelineColorBlendAttachmentState state =
            Poseidon::vk::BuildColorBlendAttachmentState(Poseidon::render::BlendMode::Additive);
        CHECK(state.blendEnable == VK_TRUE);
        CHECK(state.srcColorBlendFactor == VK_BLEND_FACTOR_SRC_ALPHA);
        CHECK(state.dstColorBlendFactor == VK_BLEND_FACTOR_ONE);
    }
    {
        const VkPipelineColorBlendAttachmentState state =
            Poseidon::vk::BuildColorBlendAttachmentState(Poseidon::render::BlendMode::Shadow);
        CHECK(state.blendEnable == VK_TRUE);
        CHECK(state.srcColorBlendFactor == VK_BLEND_FACTOR_ZERO);
        CHECK(state.dstColorBlendFactor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    }
}
