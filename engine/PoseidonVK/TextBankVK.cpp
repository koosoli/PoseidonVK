#include <PoseidonVK/TextBankVK.hpp>
#include <PoseidonVK/EngineVK.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>

namespace Poseidon
{

TextBankVK::TextBankVK(EngineVK& engine)
    : _engine(engine)
{
}

TextBankVK::~TextBankVK()
{
    UnlockAllTextures();
    DeleteAllAnimated();
    _textures.Clear();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
int TextBankVK::Find(RStringB name) const
{
    for (int i = 0; i < _textures.Size(); ++i)
    {
        const TextureVK* t = _textures[i];
        if (t && t->GetName() == name)
            return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------
Ref<Texture> TextBankVK::Load(RStringB name)
{
    int idx = Find(name);
    if (idx >= 0)
        return (Texture*)_textures[idx];

    int slot = _textures.Add();
    TextureVK* tex = new TextureVK(_engine);
    _textures[slot] = tex;
    tex->SetName(name);
    tex->Init(name);
    return tex;
}

Ref<Texture> TextBankVK::LoadInterpolated(RStringB n1, RStringB /*n2*/, float /*factor*/)
{
    // Vulkan doesn't need interpolated mips — just load the primary texture.
    return Load(n1);
}

// ---------------------------------------------------------------------------
// Mip use — always return full-res mip 0 (no eviction budget yet)
// ---------------------------------------------------------------------------
MipInfo TextBankVK::UseMipmap(Texture* tex, int level, int /*top*/)
{
    return MipInfo(tex, level);
}

// ---------------------------------------------------------------------------
// Flush / release
// ---------------------------------------------------------------------------
void TextBankVK::FlushTextures()
{
    // Wait for GPU idle before destroying images so in-flight draws finish
    if (_engine._device)
        vkDeviceWaitIdle(_engine._device);
    _textures.Clear();
}

void TextBankVK::ReleaseAllTextures()
{
    if (_engine._device)
        vkDeviceWaitIdle(_engine._device);
    _textures.Clear();
}

// ---------------------------------------------------------------------------
// Dynamic textures (UI / HUD overlays)
// ---------------------------------------------------------------------------
Texture* TextBankVK::CreateDynamic(int w, int h, const void* rgba, uint32_t size, bool /*mipmap*/)
{
    int slot = _textures.Add();
    TextureVK* tex = new TextureVK(_engine);
    _textures[slot] = tex;

    tex->_resourceId = TextureVK::kFallbackResourceId + static_cast<std::uint32_t>(slot) + 1;
    tex->_w = w;
    tex->_h = h;
    tex->_nMipmaps = 1;
    tex->_mipmaps[0]._w = w;
    tex->_mipmaps[0]._h = h;

    // Create RGBA8888 device-local image
    VkResult r = vk::CreateImage2D(
        _engine._physicalDevice, _engine._device,
        static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        tex->_image);
    if (r != VK_SUCCESS)
    {
        LOG_WARN(Graphics, "TextBankVK: CreateDynamic CreateImage2D failed ({})", (int)r);
        return tex;
    }

    vk::TransitionImageLayout(_engine._device, _engine._commandPool, _engine._graphicsQueue,
                              tex->_image.image, 1,
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    UpdateDynamic(tex, rgba, size);

    // Sampler
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(_engine._device, &si, nullptr, &tex->_sampler);

    // Register texture
    _engine.RegisterTexture(tex);

    // Allocate and update descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = _engine._textureDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &_engine._textureDescriptorSetLayout;

    if (vkAllocateDescriptorSets(_engine._device, &allocInfo, &tex->_descriptorSet) == VK_SUCCESS)
    {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = tex->_image.view;
        imageInfo.sampler = tex->_sampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = tex->_descriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(_engine._device, 1, &write, 0, nullptr);
    }

    return tex;
}

void TextBankVK::UpdateDynamic(Texture* t, const void* rgba, uint32_t size)
{
    TextureVK* tex = static_cast<TextureVK*>(t);
    if (!tex || !tex->_image.image || !rgba || size == 0)
        return;

    vk::BufferVK staging;
    VkResult r = vk::CreateHostVisibleBuffer(
        _engine._physicalDevice, _engine._device,
        static_cast<VkDeviceSize>(size),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        staging);
    if (r != VK_SUCCESS)
        return;

    vk::UploadMappedBuffer(staging, rgba, size);

    // Transition back to TRANSFER_DST, copy, then SHADER_READ_ONLY
    vk::TransitionImageLayout(_engine._device, _engine._commandPool, _engine._graphicsQueue,
                              tex->_image.image, 1,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    vk::CopyBufferToImage(_engine._device, _engine._commandPool, _engine._graphicsQueue,
                          staging.buffer, tex->_image.image,
                          static_cast<uint32_t>(tex->_w), static_cast<uint32_t>(tex->_h), 0);

    vk::TransitionImageLayout(_engine._device, _engine._commandPool, _engine._graphicsQueue,
                              tex->_image.image, 1,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vk::DestroyBuffer(_engine._device, staging);
}

} // namespace Poseidon
