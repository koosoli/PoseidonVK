#include <catch2/catch_test_macros.hpp>

#include <PoseidonVK/DescriptorBindingsVK.hpp>

#include <array>

TEST_CASE("Vulkan frame descriptor binding indices are stable and unique", "[vulkan][descriptor-bindings]")
{
    using Poseidon::vk::kFrameConstantsBinding;
    using Poseidon::vk::kDrawConstantsBinding;
    using Poseidon::vk::kShadowMapBinding;
    using Poseidon::vk::kFrameDescriptorSetBindingCount;

    STATIC_REQUIRE(kFrameConstantsBinding == 0);
    STATIC_REQUIRE(kDrawConstantsBinding == 1);
    STATIC_REQUIRE(kShadowMapBinding == 2);
    STATIC_REQUIRE(kFrameDescriptorSetBindingCount == 3);
    STATIC_REQUIRE(kFrameConstantsBinding != kDrawConstantsBinding);
    STATIC_REQUIRE(kFrameConstantsBinding != kShadowMapBinding);
    STATIC_REQUIRE(kDrawConstantsBinding != kShadowMapBinding);
    STATIC_REQUIRE(kFrameConstantsBinding < kFrameDescriptorSetBindingCount);
    STATIC_REQUIRE(kDrawConstantsBinding < kFrameDescriptorSetBindingCount);
    STATIC_REQUIRE(kShadowMapBinding < kFrameDescriptorSetBindingCount);
}

TEST_CASE("Vulkan frame descriptor layout bindings match the contract", "[vulkan][descriptor-bindings]")
{
    const VkDescriptorSetLayoutBinding frameBinding = Poseidon::vk::MakeFrameConstantsLayoutBinding();
    CHECK(frameBinding.binding == Poseidon::vk::kFrameConstantsBinding);
    CHECK(frameBinding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    CHECK(frameBinding.descriptorCount == 1);
    CHECK(frameBinding.stageFlags ==
          (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT));

    const VkDescriptorSetLayoutBinding drawBinding = Poseidon::vk::MakeDrawConstantsLayoutBinding();
    CHECK(drawBinding.binding == Poseidon::vk::kDrawConstantsBinding);
    CHECK(drawBinding.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    CHECK(drawBinding.descriptorCount == 1);
    CHECK(drawBinding.stageFlags == (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT));

    const VkDescriptorSetLayoutBinding shadowBinding = Poseidon::vk::MakeShadowMapLayoutBinding();
    CHECK(shadowBinding.binding == Poseidon::vk::kShadowMapBinding);
    CHECK(shadowBinding.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    CHECK(shadowBinding.descriptorCount == 1);
    CHECK(shadowBinding.stageFlags == VK_SHADER_STAGE_FRAGMENT_BIT);
}

TEST_CASE("Vulkan frame descriptor layout binding set is complete and ordered", "[vulkan][descriptor-bindings]")
{
    const std::array<VkDescriptorSetLayoutBinding, Poseidon::vk::kFrameDescriptorSetBindingCount>
        bindings = Poseidon::vk::MakeFrameDescriptorSetLayoutBindings();

    REQUIRE(bindings.size() == Poseidon::vk::kFrameDescriptorSetBindingCount);
    CHECK(bindings[0].binding == Poseidon::vk::kFrameConstantsBinding);
    CHECK(bindings[0].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    CHECK(bindings[1].binding == Poseidon::vk::kDrawConstantsBinding);
    CHECK(bindings[1].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    CHECK(bindings[2].binding == Poseidon::vk::kShadowMapBinding);
    CHECK(bindings[2].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
}

TEST_CASE("Vulkan frame descriptor pool sizes cover every binding type", "[vulkan][descriptor-bindings]")
{
    const std::array<VkDescriptorPoolSize, Poseidon::vk::kFrameDescriptorSetBindingCount>
        poolSizes = Poseidon::vk::MakeFrameDescriptorPoolSizes();

    REQUIRE(poolSizes.size() == Poseidon::vk::kFrameDescriptorSetBindingCount);
    CHECK(poolSizes[0].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    CHECK(poolSizes[0].descriptorCount == 1);
    CHECK(poolSizes[1].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    CHECK(poolSizes[1].descriptorCount == 1);
    CHECK(poolSizes[2].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    CHECK(poolSizes[2].descriptorCount == 1);
}

TEST_CASE("Vulkan frame descriptor writes target the correct binding", "[vulkan][descriptor-bindings]")
{
    const VkDescriptorSet fakeSet = reinterpret_cast<VkDescriptorSet>(0xC0FFEEull);
    const VkDescriptorBufferInfo frameInfo{VK_NULL_HANDLE, 0, 288};
    const VkDescriptorBufferInfo drawInfo{VK_NULL_HANDLE, 0, 160};
    const VkDescriptorImageInfo shadowInfo{VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    const VkWriteDescriptorSet frameWrite =
        Poseidon::vk::MakeFrameConstantsDescriptorWrite(fakeSet, &frameInfo);
    CHECK(frameWrite.sType == VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    CHECK(frameWrite.dstSet == fakeSet);
    CHECK(frameWrite.dstBinding == Poseidon::vk::kFrameConstantsBinding);
    CHECK(frameWrite.dstArrayElement == 0);
    CHECK(frameWrite.descriptorCount == 1);
    CHECK(frameWrite.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    CHECK(frameWrite.pBufferInfo == &frameInfo);

    const VkWriteDescriptorSet drawWrite =
        Poseidon::vk::MakeDrawConstantsDescriptorWrite(fakeSet, &drawInfo);
    CHECK(drawWrite.sType == VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    CHECK(drawWrite.dstSet == fakeSet);
    CHECK(drawWrite.dstBinding == Poseidon::vk::kDrawConstantsBinding);
    CHECK(drawWrite.dstArrayElement == 0);
    CHECK(drawWrite.descriptorCount == 1);
    CHECK(drawWrite.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    CHECK(drawWrite.pBufferInfo == &drawInfo);

    const VkWriteDescriptorSet shadowWrite =
        Poseidon::vk::MakeShadowMapDescriptorWrite(fakeSet, &shadowInfo);
    CHECK(shadowWrite.sType == VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    CHECK(shadowWrite.dstSet == fakeSet);
    CHECK(shadowWrite.dstBinding == Poseidon::vk::kShadowMapBinding);
    CHECK(shadowWrite.dstArrayElement == 0);
    CHECK(shadowWrite.descriptorCount == 1);
    CHECK(shadowWrite.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    CHECK(shadowWrite.pImageInfo == &shadowInfo);
}
