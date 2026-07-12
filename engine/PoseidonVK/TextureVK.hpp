#pragma once

#include <PoseidonVK/BufferVK.hpp>
#include <Poseidon/Graphics/Textures/TextureBank.hpp>

#include <array>

namespace Poseidon
{
class EngineVK;

// Vulkan-owned texture. Mirrors the TextureDummy/TextureGL33 pattern:
// Init() decodes the source via the engine-neutral ITextureSource factory,
// creates a device-local VkImage via a host-visible staging buffer, and
// registers a process-local resource ID so the frame extractor can track it.
class TextureVK : public Texture
{
    friend class TextBankVK;
    friend class EngineVK;

public:
    explicit TextureVK(EngineVK& engine);
    ~TextureVK() override;

    bool Init(RStringB name);

    // Opaque ID used by the frame extractor (parallel to TextureGL33::GetResourceId).
    std::uint32_t GetResourceId() const noexcept { return _resourceId; }
    static std::uint32_t AllocateResourceId() noexcept;

    // --- Texture virtuals ---
    int AWidth(int /*level*/ = 0) const override { return _w; }
    int AHeight(int /*level*/ = 0) const override { return _h; }
    int ANMipmaps() const override { return _nMipmaps; }
    void ASetNMipmaps(int n) override
    {
        if (n > _nMipmaps)
            n = _nMipmaps;
        if (n < 1)
            n = 1;
        _nMipmaps = n;
    }

    Color GetPixel(int level, float u, float v) const override;
    Color GetColor() override;
    bool IsTransparent() const override;
    bool IsAlpha() const override;
    AlphaStats::Kind GetAlphaClass() override;

    void SetMaxSize(int sz) override { _maxSize = sz; }
    int AMaxSize() const override { return _maxSize; }

    const PacLevelMem& AMipmap(int level) const override { return _mipmaps[level]; }
    PacLevelMem& AMipmap(int level) override { return _mipmaps[level]; }
    bool VerifyChecksum(const MipInfo&) const override { return true; }

    static constexpr std::uint32_t kFallbackResourceId = 1;

    VkDescriptorSet GetDescriptorSet() const;
    VkDescriptorSet GetDescriptorSet(std::uint32_t samplerFilter, std::uint32_t samplerClamp) const;

private:
    bool UploadMips();

    EngineVK& _engine;
    std::uint32_t _resourceId = 0;
    SRef<ITextureSource> _src;

    int _w = 0;
    int _h = 0;
    int _nMipmaps = 0;
    int _maxSize = 256;
    PacLevelMem _mipmaps[MAX_MIPMAPS];
    // Stores the raw source format before DstFormatVK mapping.
    // Used by UploadMips to select the 16\u219232 transcoder for formats that
    // cannot be natively uploaded to Vulkan with correct channel order.
    PacFormat _nativeSrcFormat = PacARGB1555;

    vk::ImageVK _image;
    VkSampler _sampler = VK_NULL_HANDLE;
    VkDescriptorSet _descriptorSet = VK_NULL_HANDLE;
    mutable std::array<VkSampler, 8> _samplerVariants = {};
    mutable std::array<VkDescriptorSet, 8> _descriptorVariants = {};
    std::uint32_t _baseSamplerKey = 0;
    bool _dynamicImageUploaded = false;
    signed char _alphaClass = -1;
};

} // namespace Poseidon
