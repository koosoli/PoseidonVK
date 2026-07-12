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
#include <Poseidon/Graphics/Textures/LooseTextures.hpp>
#include <Poseidon/Graphics/Textures/PAADecoder.hpp>
#include <Poseidon/IO/FileServer.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/IO/Streams/QBStream.hpp>

#include <algorithm>
#include <bit>
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
// capability checks. P8 palette-expand requires a conversion.
//
// NOTE: PacARGB4444 and PacARGB1555 stay at their native format here so that
// GetMipmapData / LoadPaaBin16's '_sFormat == _dFormat' assertion passes.
// A transcoding step in UploadMips() then converts the raw 16-bit pixels to
// 32-bit RGBA8 (VK_FORMAT_R8G8B8A8_UNORM) before uploading, matching what
// GL33 achieves via GL_BGRA + GL_UNSIGNED_SHORT_*_REV.
static PacFormat DstFormatVK(PacFormat srcFormat)
{
    switch (srcFormat)
    {
        case PacP8:       return PacARGB1555; // palette-expand: 8-bit indexed → 16-bit ARGB1555
        default:          return srcFormat;   // identity: keep native format for GetMipmapData
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
        // PacARGB8888: decoded bytes come out as [R][G][B][A] from the in-memory
        // transcoder (argb4444/1555 → rgba8) and are uploaded as RGBA8_UNORM.
        // PacARGB4444 / PacARGB1555 also map here because UploadMips transcodes
        // those to RGBA8 before upload.
        case PacARGB8888:  return {VK_FORMAT_R8G8B8A8_UNORM, false};
        case PacARGB4444:  return {VK_FORMAT_R8G8B8A8_UNORM, false}; // transcoded in UploadMips
        case PacARGB1555:  return {VK_FORMAT_R8G8B8A8_UNORM, false}; // transcoded in UploadMips
        case PacRGB565:    return {VK_FORMAT_R5G6B5_UNORM_PACK16, false};
        case PacAI88:      return {VK_FORMAT_R8G8B8A8_UNORM, false}; // transcoded in UploadMips: [grey,alpha]->[grey,grey,grey,alpha]
        default:           return {VK_FORMAT_R8G8B8A8_UNORM, false};
    }
}

static std::uint32_t s_nextTextureId = TextureVK::kFallbackResourceId + 1;
} // namespace

std::uint32_t TextureVK::AllocateResourceId() noexcept
{
    return s_nextTextureId++;
}

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
    if (dev && _engine._textureDescriptorPool)
    {
        for (VkDescriptorSet& descriptorSet : _descriptorVariants)
        {
            if (descriptorSet)
                vkFreeDescriptorSets(dev, _engine._textureDescriptorPool, 1, &descriptorSet);
        }
        if (_descriptorSet)
            vkFreeDescriptorSets(dev, _engine._textureDescriptorPool, 1, &_descriptorSet);
        _descriptorSet = VK_NULL_HANDLE;
    }
    if (dev)
    {
        for (VkSampler& sampler : _samplerVariants)
        {
            if (sampler)
                vkDestroySampler(dev, sampler, nullptr);
        }
        if (_sampler)
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
    _resourceId = AllocateResourceId();

    // Resolve loose/mod texture overrides exactly as GL33 does (TextureGL33_Init.cpp:171).
    // Without this, modded textures and any path redirects registered with the loose
    // texture system are silently ignored, falling through to the fallback black texture.
    RString resolved = Poseidon::Graphics::ResolveLooseTexturePath(name);

    ITextureSourceFactory* factory = SelectTextureSourceFactory(resolved);
    if (!factory || !factory->Check(resolved))
    {
        LOG_DEBUG(Graphics, "TextureVK: no source for '{}', using fallback ID", (const char*)name);
        // Leave _image null — sampler will also be null.  The frame extractor
        // still gets a unique resource ID so draws are tracked correctly.
        return true; // not a hard failure: game continues with missing-texture colour
    }

    _src = factory->Create(resolved, _mipmaps, MAX_MIPMAPS);
    if (!_src)
        return true;

    const PacFormat sourceFormat = _src->GetFormat();
    if (sourceFormat == PacARGB4444 || sourceFormat == PacAI88 || sourceFormat == PacARGB8888)
        _src->ForceAlpha();

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

    if (!CmpStartStr(Name(), "fonts\\"))
        _maxSize = 1024;
    else if (!CmpStartStr(Name(), "merged\\"))
        _maxSize = 2048;
    else if (AbstractTextBank::AnimatedNumber(Name()) >= 0 && IsAlpha())
        _maxSize = ENGINE_CONFIG.maxAnimText;
    else
        _maxSize = ENGINE_CONFIG.maxObjText;

    // Compute the decode-target format from the source format, then initialise
    // _dFormat on every mip.  This mirrors TextureGL33_Init.cpp lines 233-246:
    //   dFormat = DstFormat(srcFmt, dxt);
    //   for each mip: mip.SetDestFormat(dFormat, 8);
    // SetDestFormat MUST be called during Init — not only during the upload
    // loop — because DstFormat() returns the raw _dFormat member with NO
    // fallback.  Without this step _dFormat stays 0, SetDestFormat(0) later
    // would hit the default Fail path, and LoadPaaBin16 would assert
    // '_sFormat == _dFormat'.
    //
    // CHANNEL-ORDER NOTE: PacARGB4444 and PacARGB1555 keep their native format
    // so that GetMipmapData / LoadPaaBin16's '_sFormat==_dFormat' assertion
    // passes. UploadMips will then transcode the raw 16-bit pixels to RGBA8.
    _nativeSrcFormat = sourceFormat;
    const PacFormat uploadFmt = DstFormatVK(_nativeSrcFormat);
    // Use uploadFmt (not _nativeSrcFormat) for the VkImage format lookup so that
    // palette-expand paths (PacP8 → PacARGB1555) get the right VkFormat and the
    // transcoder in UploadMips fires correctly.
    const FormatInfo fi = PacFormatToVk(uploadFmt);
    // Compute the full mip chain so the VkImage backing covers all levels.
    // Non-DXT textures will have missing levels generated via vkCmdBlitImage;
    // DXT textures are limited to whatever the file provides.
    const bool canGenerateMips = (sourceFormat != PacDXT1 && sourceFormat != PacDXT2 &&
                                  sourceFormat != PacDXT3 && sourceFormat != PacDXT4 &&
                                  sourceFormat != PacDXT5);
    const int fullMipCount = canGenerateMips
        ? static_cast<int>(std::bit_width(
              std::max(static_cast<uint32_t>(_w), static_cast<uint32_t>(_h))))
        : _nMipmaps;
    const uint32_t levels = static_cast<uint32_t>(fullMipCount);

    for (int i = 0; i < _nMipmaps; ++i)
        _mipmaps[i].SetDestFormat(uploadFmt, 8);

    // Create device-local image + view with full mip chain capacity.
    // TRANSFER_SRC_BIT is needed when blit-generating missing mip levels.
    VkResult r = vk::CreateImage2D(
        _engine._physicalDevice, _engine._device,
        static_cast<uint32_t>(_w), static_cast<uint32_t>(_h), levels,
        fi.vkFmt,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        _image);
    if (r != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "TextureVK: CreateImage2D failed ({}) for '{}'", (int)r, (const char*)name);
        return true; // fallback
    }

    // Transition first _nMipmaps (file-provided) to TRANSFER_DST for upload;
    // the remaining levels start UNDEFINED and will be transitioned during
    // the blit generation pass inside UploadMips.
    vk::TransitionImageLayout(_engine._device, _engine._commandPool, _engine._graphicsQueue,
                              _image.image, static_cast<uint32_t>(_nMipmaps),
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Upload file mips and generate any missing levels.  This function now
    // handles ALL layout transitions: file mips arrive in TRANSFER_DST, and
    // on success every mip level ends in SHADER_READ_ONLY_OPTIMAL.
    if (!UploadMips())
    {
        LOG_WARN(Graphics, "TextureVK: mip upload partial for '{}'", (const char*)name);
    }

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
    si.anisotropyEnable = VK_TRUE;
    si.maxAnisotropy = _engine._maxSamplerAnisotropy;
    si.compareEnable = VK_FALSE;
    si.minLod = 0.0f;
    si.maxLod = static_cast<float>(std::max(_nMipmaps - 1, 0));
    si.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    if (vkCreateSampler(_engine._device, &si, nullptr, &_sampler) != VK_SUCCESS)
    {
        LOG_WARN(Graphics, "TextureVK: sampler creation failed for '{}'", (const char*)name);
        return true;
    }

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
        _engine.RegisterTexture(this);
    }
    else
    {
        LOG_WARN(Graphics, "TextureVK: descriptor allocation failed for '{}'", (const char*)name);
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

    // Flag whether UploadMips must transcode pixels before GPU upload.
    // Use sharedFmt (the actual decoded format set on the mips via SetDestFormat)
    // rather than _nativeSrcFormat, so that palette-expand paths
    // (PacP8 → PacARGB1555) and any other aliasing are handled correctly.
    // sharedFmt exactly matches what GetMipmapData produces.
    //
    // PacARGB4444 raw bytes: A4R4G4B4 (bits 15-12=A, 11-8=R, 7-4=G, 3-0=B)
    // PacARGB1555 raw bytes: A1R5G5B5 (bit15=A, 14-10=R, 9-5=G, 4-0=B)
    // PacARGB8888 raw bytes: [B,G,R,A] on the supported little-endian target.
    // None of these map correctly to any Vulkan format without conversion.
    // GL33 uses GL_BGRA+GL_UNSIGNED_SHORT/INT_*_REV for the byte-swap; we
    // perform the equivalent conversion in software during UploadMips.
    const bool transcode4444 = (sharedFmt == PacARGB4444);
    const bool transcode1555 = (sharedFmt == PacARGB1555);
    const bool transcode8888 = (sharedFmt == PacARGB8888);
    // PacAI88: two bytes per pixel [grey, alpha].  VK_FORMAT_R8G8_UNORM would
    // put grey in .r and alpha in .g, leaving .a = 1.0 as Vulkan defines for a
    // 2-channel format — alpha tests never fire.  We upload as RGBA8 instead,
    // replicating grey to RGB and copying alpha to the A channel.
    const bool transcodeAI88 = (sharedFmt == PacAI88);
    const bool needsTranscode = transcode4444 || transcode1555 || transcode8888 || transcodeAI88;

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

        // Allocate staging buffer. Transcoded formats are always 4 bytes/pixel
        // (RGBA8) regardless of source width: 16-bit formats expand, 8888 swaps in-place.
        // AI88 expands from 2 bytes/pixel to 4 bytes/pixel.
        const std::size_t pixelCount = static_cast<std::size_t>(srcMip._w) * static_cast<std::size_t>(srcMip._h);
        const std::size_t uploadSize = needsTranscode ? pixelCount * 4u : dataSize;

        vk::BufferVK staging;
        VkResult r = vk::CreateHostVisibleBuffer(
            _engine._physicalDevice, _engine._device,
            static_cast<VkDeviceSize>(uploadSize),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            staging);
        if (r != VK_SUCCESS)
            continue;

        // Decode the mip into a temporary 16-bit buffer; _dFormat is already
        // correctly set on mipCopy so LoadPaaBin16's assertion passes.
        std::vector<char> pixelData(dataSize);
        int ok = _src->GetMipmapData(pixelData.data(), mip, i);
        if (!ok)
            std::memset(pixelData.data(), 0, dataSize);

        // Transcode pixels to Vulkan-compatible RGBA8 layout when required.
        //   ARGB4444: bits 15-12=A, 11-8=R, 7-4=G, 3-0=B  → [R8,G8,B8,A8]
        //   ARGB1555: bit15=A, 14-10=R, 9-5=G, 4-0=B       → [R8,G8,B8,A8]
        //   ARGB8888: raw bytes [B,G,R,A]                   → [R8,G8,B8,A8]
        // All output as VK_FORMAT_R8G8B8A8_UNORM.
        std::vector<char> transcodedData;
        const void* uploadPtr = pixelData.data();
        if (needsTranscode)
        {
            transcodedData.resize(uploadSize);
            uint8_t* dst8 = reinterpret_cast<uint8_t*>(transcodedData.data());
            if (transcode4444)
            {
                const uint16_t* src16 = reinterpret_cast<const uint16_t*>(pixelData.data());
                for (std::size_t p = 0; p < pixelCount; ++p)
                {
                    uint16_t px = src16[p];
                    uint8_t  a  = (px >> 12) & 0xF;
                    uint8_t  rv = (px >> 8)  & 0xF;
                    uint8_t  g  = (px >> 4)  & 0xF;
                    uint8_t  b  =  px        & 0xF;
                    // Expand 4-bit channels to 8-bit by replicating the nibble.
                    dst8[p * 4 + 0] = (rv << 4) | rv;
                    dst8[p * 4 + 1] = (g  << 4) | g;
                    dst8[p * 4 + 2] = (b  << 4) | b;
                    dst8[p * 4 + 3] = (a  << 4) | a;
                }
            }
            else if (transcode1555)
            {
                const uint16_t* src16 = reinterpret_cast<const uint16_t*>(pixelData.data());
                for (std::size_t p = 0; p < pixelCount; ++p)
                {
                    uint16_t px = src16[p];
                    uint8_t  a  = (px >> 15) & 0x1;
                    uint8_t  rv = (px >> 10) & 0x1F;
                    uint8_t  g  = (px >> 5)  & 0x1F;
                    uint8_t  b  =  px        & 0x1F;
                    // Expand 5-bit channels (and 1-bit alpha) to 8-bit.
                    dst8[p * 4 + 0] = (rv << 3) | (rv >> 2);
                    dst8[p * 4 + 1] = (g  << 3) | (g  >> 2);
                    dst8[p * 4 + 2] = (b  << 3) | (b  >> 2);
                    dst8[p * 4 + 3] = a ? 255u : 0u;
                }
            }
            else if (transcodeAI88)
            {
                // [grey8, alpha8] → [grey8, grey8, grey8, alpha8]
                const uint8_t* src8 = reinterpret_cast<const uint8_t*>(pixelData.data());
                for (std::size_t p = 0; p < pixelCount; ++p)
                {
                    const uint8_t grey  = src8[p * 2 + 0];
                    const uint8_t alpha = src8[p * 2 + 1];
                    dst8[p * 4 + 0] = grey;
                    dst8[p * 4 + 1] = grey;
                    dst8[p * 4 + 2] = grey;
                    dst8[p * 4 + 3] = alpha;
                }
            }
            else // transcode8888: raw bytes [B,G,R,A] → [R,G,B,A]
            {
                const uint8_t* src8 = reinterpret_cast<const uint8_t*>(pixelData.data());
                for (std::size_t p = 0; p < pixelCount; ++p)
                {
                    dst8[p * 4 + 0] = src8[p * 4 + 2]; // R
                    dst8[p * 4 + 1] = src8[p * 4 + 1]; // G
                    dst8[p * 4 + 2] = src8[p * 4 + 0]; // B
                    dst8[p * 4 + 3] = src8[p * 4 + 3]; // A
                }
            }
            uploadPtr = transcodedData.data();
        }

        vk::UploadMappedBuffer(staging, uploadPtr, uploadSize);

        // Copy staging buffer → device image mip i
        vk::CopyBufferToImage(_engine._device, _engine._commandPool, _engine._graphicsQueue,
                              staging.buffer, _image.image,
                              static_cast<uint32_t>(mip._w), static_cast<uint32_t>(mip._h),
                              static_cast<uint32_t>(i));

        vk::DestroyBuffer(_engine._device, staging);
    }

    // -----------------------------------------------------------------------
    // Generate missing mip levels + finalize layout
    // -----------------------------------------------------------------------
    // File mips are now in TRANSFER_DST_OPTIMAL.  Non-DXT textures may need
    // the remaining levels generated via vkCmdBlitImage.  After that (or
    // immediately if no generation is needed) everything transitions to
    // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL.

    const bool canGenerate = (sharedFmt != PacDXT1 && sharedFmt != PacDXT2 &&
                              sharedFmt != PacDXT3 && sharedFmt != PacDXT4 &&
                              sharedFmt != PacDXT5);
    const uint32_t fullMipCount = static_cast<uint32_t>(canGenerate
        ? std::bit_width(std::max(static_cast<uint32_t>(_w), static_cast<uint32_t>(_h)))
        : _nMipmaps);
    const uint32_t fileMipCount = static_cast<uint32_t>(_nMipmaps);

    if (fullMipCount <= fileMipCount)
    {
        // No generation needed — just transition file mips to SHADER_READ_ONLY.
        vk::TransitionImageLayout(_engine._device, _engine._commandPool, _engine._graphicsQueue,
                                  _image.image, fileMipCount,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        return true;
    }

    // Need to generate missing mip levels via blit.
    _nMipmaps = static_cast<int>(fullMipCount);

    VkCommandBuffer cb;
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = _engine._commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(_engine._device, &allocInfo, &cb);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &beginInfo);

    // Helper lambda: pipeline barrier to transition a range of mip levels
    auto imageBarrier = [&](VkPipelineStageFlags srcMask, VkPipelineStageFlags dstMask,
                            uint32_t baseMip, uint32_t mipCount,
                            VkImageLayout oldLayout, VkImageLayout newLayout)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = _image.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = baseMip;
        barrier.subresourceRange.levelCount = mipCount;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;
        vkCmdPipelineBarrier(cb, srcMask, dstMask, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    };

    // Transition file mips from TRANSFER_DST to TRANSFER_SRC for blit source
    imageBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                 0, fileMipCount,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    int srcW = _w, srcH = _h;
    for (uint32_t skip = 1; skip < fileMipCount; ++skip) { srcW = std::max(srcW / 2, 1); srcH = std::max(srcH / 2, 1); }

    for (uint32_t i = fileMipCount; i < fullMipCount; ++i)
    {
        int dstW = std::max(srcW / 2, 1);
        int dstH = std::max(srcH / 2, 1);

        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {srcW, srcH, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {dstW, dstH, 1};

        // Transition destination mip UNDEFINED → TRANSFER_DST
        imageBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                     i, 1,
                     VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        vkCmdBlitImage(cb, _image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       _image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit, VK_FILTER_LINEAR);

        // Transition this mip to TRANSFER_SRC so it can be used as source for the next level
        if (i < fullMipCount - 1)
        {
            imageBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         i, 1,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        }

        srcW = dstW;
        srcH = dstH;
    }

    // Every level except the final generated one was used as a blit source.
    // The final level remains TRANSFER_DST because nothing consumes it as a source.
    imageBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                 0, fullMipCount - 1,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    imageBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                 fullMipCount - 1, 1,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkEndCommandBuffer(cb);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cb;
    vkQueueSubmit(_engine._graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(_engine._graphicsQueue);
    vkFreeCommandBuffers(_engine._device, _engine._commandPool, 1, &cb);
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

VkDescriptorSet TextureVK::GetDescriptorSet(std::uint32_t samplerFilter, std::uint32_t samplerClamp) const
{
    const std::uint32_t key = ((samplerFilter & 1u) << 2) | (samplerClamp & 3u);
    if (!_image.view || !_engine._device || !_engine._textureDescriptorPool)
        return GetDescriptorSet();
    if (key == _baseSamplerKey)
        return GetDescriptorSet();
    if (_descriptorVariants[key])
        return _descriptorVariants[key];

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = samplerFilter == 1u ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    samplerInfo.minFilter = samplerInfo.magFilter;
    samplerInfo.mipmapMode = samplerFilter == 1u ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = (samplerClamp & 1u) ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = (samplerClamp & 2u) ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = samplerInfo.addressModeV;
    samplerInfo.maxLod = static_cast<float>(std::max(_nMipmaps - 1, 0));
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = _engine._maxSamplerAnisotropy;

    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(_engine._device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
        return GetDescriptorSet();

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = _engine._textureDescriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &_engine._textureDescriptorSetLayout;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(_engine._device, &allocateInfo, &descriptorSet) != VK_SUCCESS)
    {
        vkDestroySampler(_engine._device, sampler, nullptr);
        return GetDescriptorSet();
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = _image.view;
    imageInfo.sampler = sampler;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(_engine._device, 1, &write, 0, nullptr);

    _samplerVariants[key] = sampler;
    _descriptorVariants[key] = descriptorSet;
    return descriptorSet;
}

// ---------------------------------------------------------------------------
// Texture virtuals
// ---------------------------------------------------------------------------
bool TextureVK::IsTransparent() const { return _src && _src->IsTransparent(); }
bool TextureVK::IsAlpha() const { return _src && _src->IsAlpha(); }
Color TextureVK::GetColor() { return _src ? _src->GetAverageColor() : HBlack; }

Color TextureVK::GetPixel(int level, float u, float v) const
{
    if (!_src || level < 0 || level >= _nMipmaps)
        return HWhite;

    const PacLevelMem& mip = _mipmaps[level];
    const std::size_t dataSize = static_cast<std::size_t>(mip._pitch) * mip._h;
    if (dataSize == 0)
        return HWhite;

    std::vector<char> pixels(dataSize);
    if (!_src->GetMipmapData(pixels.data(), mip, level))
        return HWhite;
    return mip.GetPixel(pixels.data(), u, v);
}

AlphaStats::Kind TextureVK::GetAlphaClass()
{
    if (_alphaClass >= 0)
        return static_cast<AlphaStats::Kind>(_alphaClass);

    AlphaStats::Kind kind = AlphaStats::Opaque;
    if (_src)
    {
        const bool hasAlpha = _src->IsAlpha();
        const bool chroma = _src->IsTransparent();
        const bool oneBit = _src->GetFormat() == PacDXT1;
        AlphaStats decoded;
        const AlphaStats* decodedPtr = nullptr;

        if (hasAlpha && !oneBit)
        {
            QIFStream in;
            GFileServer->Open(in, Name());
            const int size = in.fail() ? 0 : in.rest();
            if (size > 0)
            {
                std::vector<char> fileData(static_cast<std::size_t>(size));
                in.read(fileData.data(), size);
                const char* name = Name();
                const std::size_t len = name ? std::strlen(name) : 0;
                const bool isPaa = len >= 4 && (name[len - 1] == 'a' || name[len - 1] == 'A');
                const DecodedImage image = DecodePAABuffer(fileData.data(), fileData.size(), isPaa);
                if (image.valid())
                {
                    decoded = ClassifyAlpha(image.rgba.data(), image.rgba.size() / 4);
                    decodedPtr = &decoded;
                }
            }
        }
        kind = ClassifyTextureAlpha(hasAlpha, chroma, oneBit, decodedPtr);
    }

    _alphaClass = static_cast<signed char>(kind);
    return kind;
}

} // namespace Poseidon

