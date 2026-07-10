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

// DstFormatVK: maps a texture's source (file) format to the in-memory
// decode target that TextureVK will upload to Vulkan.
// Mirrors TextureGL33_Init::DstFormat() but without GL-specific hardware
// capability checks — Vulkan natively supports all 16-bit, 32-bit, and
// DXT formats, so only the P8 palette-expand requires a conversion.
static PacFormat DstFormatVK(PacFormat srcFormat)
{
    switch (srcFormat)
    {
        case PacP8:
            return PacARGB1555; // palette-expand: 8-bit indexed → 16-bit ARGB1555
        default:
            return srcFormat;   // identity for ARGB1555/4444/8888, RGB565, AI88, DXT*
    }
}

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
        case PacAI88: return {VK_FORMAT_R8G8_UNORM, false};
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
    // Block until the GPU finishes any commands that reference this texture
    // before we free the descriptor set, sampler, or image.  Texture
    // destruction happens at mission load/unload boundaries, so a full idle
    // stall is acceptable here.
    if (dev)
        vkDeviceWaitIdle(dev);
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

    // Compute the decode-target format from the source format, then initialise
    // _dFormat on every mip.  This mirrors TextureGL33_Init.cpp lines 233-246:
    //   dFormat = DstFormat(srcFmt, dxt);
    //   for each mip: mip.SetDestFormat(dFormat, 8);
    // SetDestFormat MUST be called during Init — not only during the upload
    // loop — because DstFormat() returns the raw _dFormat member with NO
    // fallback.  Without this step _dFormat stays 0, SetDestFormat(0) later
    // would hit the default Fail path, and LoadPaaBin16 would assert
    // '_sFormat == _dFormat'.
    const PacFormat uploadFmt = DstFormatVK(_src->GetFormat());
    const FormatInfo fi = PacFormatToVk(uploadFmt);
    const uint32_t levels = static_cast<uint32_t>(_nMipmaps);

    for (int i = 0; i < _nMipmaps; ++i)
        _mipmaps[i].SetDestFormat(uploadFmt, 8);

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

    // _dFormat was populated on every mip during Init via SetDestFormat.
    // DstFormat() is now valid (non-zero) — same pattern as GL33_Loading.cpp.
    const PacFormat sharedFmt = _mipmaps[0].DstFormat();

    for (int i = 0; i < _nMipmaps; ++i)
    {
        const PacLevelMem& srcMip = _mipmaps[i];
        PacLevelMem mip = srcMip;
        // Mirror GL33_Loading.cpp:102 — only override if this mip diverges
        // from the shared format (rare mixed-format PAAs).
        if (mip.DstFormat() != sharedFmt)
            mip.SetDestFormat(sharedFmt, 8);

        const auto layout = render::mipmap::ComputeLayout(sharedFmt, srcMip._w, srcMip._h);
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

        // Decode the mip into the staging buffer; _dFormat is already correctly
        // set on mipCopy so LoadPaaBin16's '_sFormat == _dFormat' assertion passes.
        std::vector<char> pixelData(dataSize);
        int ok = _src->GetMipmapData(pixelData.data(), mip, i);
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

