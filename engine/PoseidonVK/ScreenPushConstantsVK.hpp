#pragma once

#include <cstddef>
#include <cstdint>

namespace Poseidon::vk
{

// Push constants for the Vulkan screen-space (2D) pipeline. Pushed at offset 0
// of the screen pipeline's push-constant range.
//
//   vpScale — {2/width, 2/height}, maps screen pixel coordinates to clip range.
//
// 16-aligned so the C++ layout matches the GLSL push_constant block (Vulkan
// requires push constants to be a multiple of 4 bytes, but the spec recommends
// keeping the struct 16-aligned to match std140/scalar block rules).
struct alignas(16) ScreenPushConstantsVK
{
    float vpScale[2] = {0.0f, 0.0f};
    float _pad[2] = {0.0f, 0.0f};
};

inline constexpr std::uint32_t kScreenPushConstantsSize =
    static_cast<std::uint32_t>(sizeof(ScreenPushConstantsVK));

inline ScreenPushConstantsVK BuildScreenPushConstants(int width, int height) noexcept
{
    ScreenPushConstantsVK constants;
    constants.vpScale[0] = width > 0 ? 2.0f / static_cast<float>(width) : 0.0f;
    constants.vpScale[1] = height > 0 ? 2.0f / static_cast<float>(height) : 0.0f;
    return constants;
}

static_assert(offsetof(ScreenPushConstantsVK, vpScale) == 0);
static_assert(sizeof(ScreenPushConstantsVK) == 16);
static_assert(kScreenPushConstantsSize == 16);
static_assert(sizeof(ScreenPushConstantsVK) <= 128, "Screen push constants must stay under the 128 B minimum");

} // namespace Poseidon::vk
