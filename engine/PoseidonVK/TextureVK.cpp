// TextureVK.cpp — Vulkan texture implementation.
//
// Format mapping from Poseidon's PacFormat to VkFormat, then a staging-buffer
// path: create host-visible buffer, decode mip via ITextureSource, copy to
// device-local VkImage with layout transitions.

#include <PoseidonVK/TextureVK.hpp>
#include <PoseidonVK/EngineVK.hpp>

#include <Poseidon/Graphics/Core/MipmapLayout.hpp>
#include <Poseidon/Graphics/Rendering/Font/Pactext.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>

#include <algorithm>
#include <cstring>
#include <vector>

namespace Poseidon
{

// ---------------------------------------------------------------------------
// Format mapping
// ---------------------------------------------------------------------------
namespace
{
struct FormatInfo
{
    VkFormat vkFmt;
    bool compressed;
};

FormatInfo PacFormatToVk(PacFormat fmt)
{
    switch (fmt)
    {
        case PacDXT1: return {VK_FORMAT_BC1_RGBA_UNORM_BLOCK, true};
        case PacDXT2: // fallthrough
        case PacDXT3: return {VK_FORMAT_BC2_UNORM_BLOCK, true};
        case PacDXT4: // fallthrough
        case PacDXT5: return {VK_FORMAT_BC3_UNORM_BLOCK, true};
        case PacARGB8888: return {VK_FORMAT_B8G8R8A8_UNORM, false};
        case PacRGB565: return {VK_FORMAT_R5G6B5_UNORM_PACK16, false};
        case PacARGB4444: return {VK_FORMAT_B4G4R4A4_UNORM_PACK16, false};
        case PacARGB1555: // fallthrough
        default: return {VK_FORMAT_B5G5R5A1_UNORM_PACK16, false};
    }
}

static std::uint32_t s_nextTextureId = TextureVK::kFallbackResourceId + 1;
} // namespace

// ---------------------------------------------------------------------------
// Ctor / dtor
// ---------------------------------------------------------------------------
TextureVK::TextureVK(EngineVK& engine)
    : _engine(engine)
{
}

TextureVK::~TextureVK()
{
    _engine.UnregisterTexture(this);

    VkDevice dev = _engine._device;
    if (dev && _descriptorSet && _engine._textureDescriptorPool)
    {
        vkFreeDescriptorSets(dev, _engine._textureDescriptorPool, 1, &_descriptorSet);
        _descriptorSet = VK_NULL_HANDLE;
    }
    if (dev && _sampler)
    {
        vkDestroySampler(dev, _sampler, nullptr);
        _sampler = VK_NULL_HANDLE;
    }
    vk::DestroyImage(dev, _image);
}

// ---------------------------------------------------------------------------
// Init — load source, allocate GPU image, upload every mip
// ---------------------------------------------------------------------------
bool TextureVK::Init(RStringB name)
{
    SetName(name);
    _resourceId = s_nextTextureId++;

    ITextureSourceFactory* factory = SelectTextureSourceFactory(name);
    if (!factory || !factory->Check(name))
    {
        LOG_DEBUG(Graphics, "TextureVK: no source for '{}', using fallback ID", (const char*)name);
        // Leave _image null — sampler will also be null.  The frame extractor
        // still gets a unique resource ID so draws are tracked correctly.
        return true; // not a hard failure: game continues with missing-texture colour
    }

    _src = factory->Create(name, _mipmaps, MAX_MIPMAPS);
    if (!_src)
        return true;

    // Count usable mip levels (stop at 2×2 minimum — mirrors TextureDummy)
    const int totalMips = _src->GetMipmapCount();
    _nMipmaps = 0;
    for (int i = 0; i < totalMips; ++i)
    {
        if (_mipmaps[i]._w < 2 || _mipmaps[i]._h < 2)
            break;
        ++_nMipmaps;
    }
    if (_nMipmaps == 0)
        return true;

    _w = _mipmaps[0]._w;
    _h = _mipmaps[0]._h;

    const PacFormat srcFmt = _src->GetFormat();
    const FormatInfo fi = PacFormatToVk(srcFmt);
    const uint32_t levels = static_cast<uint32_t>(_nMipmaps);

    // Create device-local image + view
    VkResult r = vk::CreateImage2D(
        _engine._physicalDevice, _engine._device,
        static_cast<uint32_t>(_w), static_cast<uint32_t>(_h), levels,
        fi.vkFmt,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        _image);
    if (r != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "TextureVK: CreateImage2D failed ({}) for '{}'", (int)r, (const char*)name);
        return true; // fallback
    }

    // Transition whole image to TRANSFER_DST
    vk::TransitionImageLayout(_engine._device, _engine._commandPool, _engine._graphicsQueue,
                              _image.image, levels,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Upload each mip via a host-visible staging buffer
    if (!UploadMips())
    {
        LOG_WARN(Graphics, "TextureVK: mip upload partial for '{}'", (const char*)name);
    }

    // Transition to SHADER_READ_ONLY
    vk::TransitionImageLayout(_engine._device, _engine._commandPool, _engine._graphicsQueue,
                              _image.image, levels,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Create sampler
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.mipLodBias = 0.0f;
    si.anisotropyEnable = VK_FALSE;
    si.compareEnable = VK_FALSE;
    si.minLod = 0.0f;
    si.maxLod = static_cast<float>(_nMipmaps);
    si.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    vkCreateSampler(_engine._device, &si, nullptr, &_sampler);

    // Register texture
    _engine.RegisterTexture(this);

    // Allocate and update descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = _engine._textureDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &_engine._textureDescriptorSetLayout;

    if (vkAllocateDescriptorSets(_engine._device, &allocInfo, &_descriptorSet) == VK_SUCCESS)
    {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = _image.view;
        imageInfo.sampler = _sampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = _descriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(_engine._device, 1, &write, 0, nullptr);
    }

    return true;
}

bool TextureVK::UploadMips()
{
    if (!_src || !_image.image)
        return false;

    const PacFormat srcFmt = _src->GetFormat();

    for (int i = 0; i < _nMipmaps; ++i)
    {
        const PacLevelMem& mip = _mipmaps[i];
        const auto layout = render::mipmap::ComputeLayout(srcFmt, mip._w, mip._h);
        const std::size_t dataSize = static_cast<std::size_t>(layout.dataSize);
        if (dataSize == 0)
            continue;

        // Allocate staging buffer
        vk::BufferVK staging;
        VkResult r = vk::CreateHostVisibleBuffer(
            _engine._physicalDevice, _engine._device,
            static_cast<VkDeviceSize>(dataSize),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            staging);
        if (r != VK_SUCCESS)
            continue;

        // Decode mip into staging buffer
        PacLevelMem mipCopy = mip;
        mipCopy.SetDestFormat(srcFmt, 8);
        std::vector<char> pixelData(dataSize);
        int ok = _src->GetMipmapData(pixelData.data(), mipCopy, i);
        if (!ok)
            std::memset(pixelData.data(), 0, dataSize);

        vk::UploadMappedBuffer(staging, pixelData.data(), dataSize);

        // Copy staging buffer → device image mip i
        vk::CopyBufferToImage(_engine._device, _engine._commandPool, _engine._graphicsQueue,
                              staging.buffer, _image.image,
                              static_cast<uint32_t>(mip._w), static_cast<uint32_t>(mip._h),
                              static_cast<uint32_t>(i));

        vk::DestroyBuffer(_engine._device, staging);
    }
    return true;
}

VkDescriptorSet TextureVK::GetDescriptorSet() const
{
    if (_descriptorSet)
        return _descriptorSet;
    if (_engine._fallbackWhiteTexture)
        return _engine._fallbackWhiteTexture->_descriptorSet;
    return VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// Texture virtuals
// ---------------------------------------------------------------------------
bool TextureVK::IsTransparent() const { return _src && _src->IsTransparent(); }
bool TextureVK::IsAlpha() const { return _src && _src->IsAlpha(); }
Color TextureVK::GetColor() { return _src ? _src->GetAverageColor() : HBlack; }

} // namespace Poseidon

