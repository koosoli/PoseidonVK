#pragma once

#include <PoseidonVK/DrawConstantsVK.hpp>

#include <Poseidon/Graphics/Rendering/RenderPassDescriptor.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Poseidon::vk
{

struct SceneDrawCommandVK
{
    std::uint32_t drawIndex = 0;
    std::uint32_t meshId = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t pass = 0;
};

// Shadow commands are intentionally separate from receiver commands.  A
// WorldShadow descriptor is the old projected-shadow accumulation path, not a
// cascade caster; CSM casters arrive through Frame::shadowInput and retain the
// normal scene mesh/index resources.
struct ShadowDrawCommandVK
{
    std::uint32_t casterIndex = 0;
    std::uint32_t meshId = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    render::frame::ShadowCasterAlphaMode alphaMode = render::frame::ShadowCasterAlphaMode::Opaque;
};

inline bool IsDrawableSceneDraw(const DrawConstantsVK& draw) noexcept
{
    // Projected-shadow and light-volume descriptors are not receiver draws.
    // Cascade casters are scheduled by BuildShadowDrawCommands below.
    const auto pass = static_cast<render::PassKind>(draw.pass);
    if (pass == render::PassKind::WorldShadow || pass == render::PassKind::WorldLight)
        return false;
    return draw.meshId != 0 && draw.indexCount != 0;
}

inline std::vector<SceneDrawCommandVK>
BuildSceneDrawCommands(const std::vector<DrawConstantsVK>& draws,
                       std::size_t commandLimit = std::numeric_limits<std::size_t>::max())
{
    std::vector<SceneDrawCommandVK> commands;
    commands.reserve(draws.size());
    for (std::size_t i = 0; i < draws.size() && commands.size() < commandLimit; ++i)
    {
        const DrawConstantsVK& draw = draws[i];
        if (!IsDrawableSceneDraw(draw))
            continue;

        SceneDrawCommandVK command;
        command.drawIndex = static_cast<std::uint32_t>(i);
        command.meshId = draw.meshId;
        command.firstIndex = draw.indexBegin;
        command.indexCount = draw.indexCount;
        command.pass = draw.pass;
        commands.push_back(command);
    }
    return commands;
}

inline bool IsDrawableShadowCaster(const render::frame::ShadowCaster& caster) noexcept
{
    return caster.mesh.HasBackendMesh() && caster.indexCount > 0;
}

inline std::vector<ShadowDrawCommandVK>
BuildShadowDrawCommands(const render::frame::ShadowInput& input,
                        std::size_t commandLimit = std::numeric_limits<std::size_t>::max())
{
    std::vector<ShadowDrawCommandVK> commands;
    commands.reserve(input.casters.size());
    for (std::size_t i = 0; i < input.casters.size() && commands.size() < commandLimit; ++i)
    {
        const render::frame::ShadowCaster& caster = input.casters[i];
        if (!IsDrawableShadowCaster(caster))
            continue;
        ShadowDrawCommandVK command;
        command.casterIndex = static_cast<std::uint32_t>(i);
        command.meshId = caster.mesh.id;
        command.firstIndex = caster.indexBegin > 0 ? static_cast<std::uint32_t>(caster.indexBegin) : 0u;
        command.indexCount = static_cast<std::uint32_t>(caster.indexCount);
        command.alphaMode = caster.alphaMode;
        commands.push_back(command);
    }
    return commands;
}

} // namespace Poseidon::vk
