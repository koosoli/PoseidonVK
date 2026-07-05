#pragma once

#include <Poseidon/Graphics/Rendering/Frame/Frame.hpp>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace Poseidon::vk
{

struct alignas(16) DrawConstantsVK
{
    GfxMatrix world = {};
    std::uint32_t textureIds[4] = {};
    std::uint32_t meshId = 0;
    std::uint32_t indexBegin = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t pass = 0;
    std::uint32_t depth = 0;
    std::uint32_t blend = 0;
    std::uint32_t fog = 0;
    std::uint32_t cull = 0;
    std::uint32_t frontFace = 0;
    std::uint32_t alpha = 0;
    std::uint32_t lighting = 0;
    std::uint32_t texGen = 0;
    std::uint32_t surface = 0;
    std::uint32_t samplerFilter = 0;
    std::uint32_t samplerClamp = 0;
    std::uint32_t shader = 0;
    std::uint32_t alphaRef = 0;
    std::uint32_t stencilExclusion = 0;
    std::uint32_t reserved[2] = {};
};

static_assert(sizeof(GfxMatrix) == 64);
static_assert(offsetof(DrawConstantsVK, world) == 0);
static_assert(offsetof(DrawConstantsVK, textureIds) == 64);
static_assert(offsetof(DrawConstantsVK, meshId) == 80);
static_assert(offsetof(DrawConstantsVK, depth) == 96);
static_assert(offsetof(DrawConstantsVK, frontFace) == 112);
static_assert(offsetof(DrawConstantsVK, surface) == 128);
static_assert(offsetof(DrawConstantsVK, alphaRef) == 144);
static_assert(sizeof(DrawConstantsVK) == 160);

template <typename Enum>
inline constexpr std::uint32_t EnumToUint(Enum value) noexcept
{
    static_assert(std::is_enum_v<Enum>);
    return static_cast<std::uint32_t>(value);
}

inline constexpr std::uint32_t NonNegativeToUint(int value) noexcept
{
    return value > 0 ? static_cast<std::uint32_t>(value) : 0u;
}

inline constexpr std::uint32_t BuildSamplerClampMask(const render::SamplerMode& sampler) noexcept
{
    return (sampler.clampU ? 1u : 0u) | (sampler.clampV ? 2u : 0u);
}

inline DrawConstantsVK BuildDrawConstants(const render::frame::Draw& draw) noexcept
{
    DrawConstantsVK constants;
    constants.world = draw.world;
    for (std::size_t i = 0; i < 4; ++i)
        constants.textureIds[i] = draw.textures[i].id;

    constants.meshId = draw.mesh.id;
    constants.indexBegin = NonNegativeToUint(draw.indexBegin);
    constants.indexCount = NonNegativeToUint(draw.indexCount);

    const render::RenderPassDescriptor& descriptor = draw.descriptor;
    constants.pass = EnumToUint(descriptor.pass);
    constants.depth = EnumToUint(descriptor.depth);
    constants.blend = EnumToUint(descriptor.blend);
    constants.fog = EnumToUint(descriptor.fog);
    constants.cull = EnumToUint(descriptor.cull);
    constants.frontFace = EnumToUint(descriptor.frontFace);
    constants.alpha = EnumToUint(descriptor.alpha);
    constants.lighting = EnumToUint(descriptor.lighting);
    constants.texGen = EnumToUint(descriptor.texGen);
    constants.surface = EnumToUint(descriptor.surface);
    constants.samplerFilter = EnumToUint(descriptor.sampler.filter);
    constants.samplerClamp = BuildSamplerClampMask(descriptor.sampler);
    constants.shader = EnumToUint(descriptor.shader);
    constants.alphaRef = descriptor.alphaRef;
    constants.stencilExclusion = descriptor.stencilExclusion ? 1u : 0u;
    return constants;
}

inline std::vector<DrawConstantsVK> BuildDrawConstants(const render::frame::Frame& frame)
{
    std::vector<DrawConstantsVK> constants;
    std::size_t drawCount = 0;
    for (const render::frame::Pass& pass : frame.passes)
        drawCount += pass.draws.size();
    constants.reserve(drawCount);

    for (const render::frame::Pass& pass : frame.passes)
    {
        for (const render::frame::Draw& draw : pass.draws)
            constants.push_back(BuildDrawConstants(draw));
    }
    return constants;
}

inline constexpr std::size_t DrawConstantsByteSize(std::size_t drawCount) noexcept
{
    return sizeof(DrawConstantsVK) * drawCount;
}

} // namespace Poseidon::vk
