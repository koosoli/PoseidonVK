#pragma once

#include <Poseidon/Graphics/Core/MatrixConversion.hpp>

#include <cstddef>
#include <cstdint>

namespace Poseidon::vk
{

// Per-draw push constants for the Vulkan scene pipeline. Pushed at offset 0 of
// the scene pipeline's push-constant range.
//
//   world             — fallback world matrix used when no DrawConstants SSBO
//                       entry is available (bring-up / no frame plan yet).
//   useDrawConstants  — non-zero when the host uploaded at least one entry to
//                       the DrawConstants SSBO at binding 1; the vertex shader
//                       then prefers draws[0].world over the fallback.
//
// 16-aligned so the C++ layout matches the GLSL std430 push_constant block.
struct alignas(16) ScenePushConstantsVK
{
    GfxMatrix world = {};
    std::uint32_t useDrawConstants = 0;
    std::uint32_t drawIndex = 0;
};

inline constexpr std::uint32_t kScenePushConstantsSize =
    static_cast<std::uint32_t>(sizeof(ScenePushConstantsVK));

inline ScenePushConstantsVK BuildScenePushConstants(const GfxMatrix& world, bool useDrawConstants,
                                                   std::uint32_t drawIndex = 0) noexcept
{
    ScenePushConstantsVK constants;
    constants.world = world;
    constants.useDrawConstants = useDrawConstants ? 1u : 0u;
    constants.drawIndex = drawIndex;
    return constants;
}

inline ScenePushConstantsVK BuildIdentityScenePushConstants() noexcept
{
    ScenePushConstantsVK constants;
    constants.world._11 = 1.0f;
    constants.world._22 = 1.0f;
    constants.world._33 = 1.0f;
    constants.world._44 = 1.0f;
    return constants;
}

static_assert(offsetof(ScenePushConstantsVK, world) == 0);
static_assert(offsetof(ScenePushConstantsVK, useDrawConstants) == 64);
static_assert(offsetof(ScenePushConstantsVK, drawIndex) == 68);
static_assert(sizeof(ScenePushConstantsVK) == 80);
static_assert(kScenePushConstantsSize == 80);
static_assert(sizeof(ScenePushConstantsVK) <= 128, "Scene push constants must stay under the 128 B minimum");

} // namespace Poseidon::vk
