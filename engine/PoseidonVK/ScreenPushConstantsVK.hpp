#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>

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
    std::uint32_t alphaMode = 0;
    float alphaRef = 0.0f;
    float fogColor[4] = {};
    std::uint32_t fogMode = 1;
    float _pad[3] = {};
};

inline constexpr std::uint32_t kScreenPushConstantsSize =
    static_cast<std::uint32_t>(sizeof(ScreenPushConstantsVK));

inline ScreenPushConstantsVK BuildScreenPushConstants(int width, int height, std::uint32_t alphaMode = 0,
                                                       std::uint32_t alphaRef = 0, std::uint32_t fogMode = 1,
                                                       const float* fogColor = nullptr) noexcept
{
    ScreenPushConstantsVK constants;
    constants.vpScale[0] = width > 0 ? 2.0f / static_cast<float>(width) : 0.0f;
    constants.vpScale[1] = height > 0 ? 2.0f / static_cast<float>(height) : 0.0f;
    constants.alphaMode = alphaMode;
    constants.alphaRef = static_cast<float>(alphaRef) * (1.0f / 255.0f);
    constants.fogMode = fogMode;
    if (fogColor)
        std::copy(fogColor, fogColor + 4, constants.fogColor);
    return constants;
}

static_assert(offsetof(ScreenPushConstantsVK, vpScale) == 0);
static_assert(sizeof(ScreenPushConstantsVK) == 48);
static_assert(kScreenPushConstantsSize == 48);
static_assert(sizeof(ScreenPushConstantsVK) <= 128, "Screen push constants must stay under the 128 B minimum");

} // namespace Poseidon::vk
