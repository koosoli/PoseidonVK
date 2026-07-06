#pragma once

#include <PoseidonVK/TextureVK.hpp>
#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>

namespace Poseidon
{
class EngineVK;

// Vulkan texture bank — manages the lifetime of all TextureVK objects for
// one EngineVK instance. Mirrors TextBankGL33's public contract but without
// the GL-specific MipCache / memory-budget machinery. All textures are
// uploaded to device-local memory on first Load() and kept resident until
// FlushTextures() / ReleaseAllTextures() is called.
class TextBankVK : public AbstractTextBank
{
    friend class EngineVK;

public:
    explicit TextBankVK(EngineVK& engine);
    ~TextBankVK() override;

    // --- AbstractTextBank interface ---
    Ref<Texture> Load(RStringB name) override;
    Ref<Texture> LoadInterpolated(RStringB n1, RStringB n2, float factor) override;

    MipInfo UseMipmap(Texture* tex, int level, int top) override;

    int NTextures() const override { return _textures.Size(); }
    Texture* GetTexture(int i) const override { return _textures[i]; }

    void Compact() override {}
    void Preload() override {}
    void FlushTextures() override;
    void ReleaseAllTextures() override;
    void FlushBank(QFBank* /*bank*/) override {}

    // Dynamic RGBA textures (UI overlays, procedural surfaces)
    Texture* CreateDynamic(int w, int h, const void* rgba, uint32_t size, bool mipmap = false) override;
    void UpdateDynamic(Texture* tex, const void* rgba, uint32_t size) override;

private:
    int Find(RStringB name) const;

    EngineVK& _engine;
    LLinkArray<TextureVK> _textures;
};

} // namespace Poseidon
