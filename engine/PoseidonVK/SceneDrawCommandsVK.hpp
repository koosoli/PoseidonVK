#pragma once

#include <PoseidonVK/DrawConstantsVK.hpp>

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
};

inline bool IsDrawableSceneDraw(const DrawConstantsVK& draw) noexcept
{
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
        commands.push_back(command);
    }
    return commands;
}

} // namespace Poseidon::vk
