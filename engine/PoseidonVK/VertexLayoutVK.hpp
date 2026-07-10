#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

namespace Poseidon::vk
{

// Scene-mesh vertex input contract. This mirrors the GL33 `SVertex` layout
// (pos, norm, uv) consumed by `vsTransform`, so a future Vulkan scene pipeline
// reads the SAME byte buffer the GL33 backend uploads and the shader `in`
// declarations stay in lock-step with the live vertex input state.
//
//   location 0: vec3 pos    (R32G32B32_SFLOAT, offset 0)
//   location 1: vec3 normal (R32G32B32_SFLOAT, offset 12)
//   location 2: vec2 uv     (R32G32_SFLOAT,    offset 24)
//
// Total stride: 32 bytes.
inline constexpr uint32_t kSceneVertexLocationPosition = 0;
inline constexpr uint32_t kSceneVertexLocationNormal = 1;
inline constexpr uint32_t kSceneVertexLocationTexcoord = 2;
inline constexpr uint32_t kSceneVertexAttributeCount = 3;

inline constexpr uint32_t kSceneVertexBinding = 0;
inline constexpr VkDeviceSize kSceneVertexStride = 32;

inline constexpr VkDeviceSize kSceneVertexPositionOffset = 0;
inline constexpr VkDeviceSize kSceneVertexNormalOffset = 12;
inline constexpr VkDeviceSize kSceneVertexTexcoordOffset = 24;

inline constexpr VkFormat kSceneVertexPositionFormat = VK_FORMAT_R32G32B32_SFLOAT;
inline constexpr VkFormat kSceneVertexNormalFormat = VK_FORMAT_R32G32B32_SFLOAT;
inline constexpr VkFormat kSceneVertexTexcoordFormat = VK_FORMAT_R32G32_SFLOAT;

inline VkVertexInputBindingDescription MakeSceneVertexBindingDescription() noexcept
{
    VkVertexInputBindingDescription description{};
    description.binding = kSceneVertexBinding;
    description.stride = kSceneVertexStride;
    description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return description;
}

inline VkVertexInputAttributeDescription MakeSceneVertexPositionAttribute() noexcept
{
    VkVertexInputAttributeDescription attribute{};
    attribute.location = kSceneVertexLocationPosition;
    attribute.binding = kSceneVertexBinding;
    attribute.format = kSceneVertexPositionFormat;
    attribute.offset = kSceneVertexPositionOffset;
    return attribute;
}

inline VkVertexInputAttributeDescription MakeSceneVertexNormalAttribute() noexcept
{
    VkVertexInputAttributeDescription attribute{};
    attribute.location = kSceneVertexLocationNormal;
    attribute.binding = kSceneVertexBinding;
    attribute.format = kSceneVertexNormalFormat;
    attribute.offset = kSceneVertexNormalOffset;
    return attribute;
}

inline VkVertexInputAttributeDescription MakeSceneVertexTexcoordAttribute() noexcept
{
    VkVertexInputAttributeDescription attribute{};
    attribute.location = kSceneVertexLocationTexcoord;
    attribute.binding = kSceneVertexBinding;
    attribute.format = kSceneVertexTexcoordFormat;
    attribute.offset = kSceneVertexTexcoordOffset;
    return attribute;
}

inline std::array<VkVertexInputAttributeDescription, kSceneVertexAttributeCount>
MakeSceneVertexAttributeDescriptions() noexcept
{
    return {MakeSceneVertexPositionAttribute(),
            MakeSceneVertexNormalAttribute(),
            MakeSceneVertexTexcoordAttribute()};
}

static_assert(kSceneVertexPositionOffset + 12 == kSceneVertexNormalOffset);
static_assert(kSceneVertexNormalOffset + 12 == kSceneVertexTexcoordOffset);
static_assert(kSceneVertexTexcoordOffset + 8 == kSceneVertexStride);
static_assert(kSceneVertexAttributeCount == 3);

// ---------------------------------------------------------------------------
// Screen-space (2D) vertex input contract.
//
// Mirrors the shared TLVertex struct used by the GL33 vsScreen path so the
// Vulkan screen pipeline reads the same bytes the 2D queue accumulates.
// Only the fields the minimal 2D shader consumes are described:
//
//   location 0: vec3 pos   (R32G32B32_SFLOAT, offset 0)  — screen pixels
//   location 1: float rhw  (R32_SFLOAT,       offset 12) — reciprocal w
//   location 2: vec4 color (B8G8R8A8_UNORM,   offset 16) — packed BGRA
//   location 3: vec2 uv    (R32G32_SFLOAT,    offset 24) — texcoord 0
//
// The TLVertex struct also carries specular (offset 20) and a second texcoord
// (offset 32); those are skipped here because the minimal 2D fragment shader
// does not read them. The stride covers only the 32 bytes up to uv end so
// adjacent vertices pack tightly even though TLVertex is 40 bytes.
inline constexpr uint32_t kScreenVertexLocationPosition = 0;
inline constexpr uint32_t kScreenVertexLocationRhw = 1;
inline constexpr uint32_t kScreenVertexLocationColor = 2;
inline constexpr uint32_t kScreenVertexLocationTexcoord = 3;
inline constexpr uint32_t kScreenVertexAttributeCount = 4;

inline constexpr uint32_t kScreenVertexBinding = 0;
inline constexpr VkDeviceSize kScreenVertexStride = 40; // full TLVertex

inline constexpr VkDeviceSize kScreenVertexPositionOffset = 0;
inline constexpr VkDeviceSize kScreenVertexRhwOffset = 12;
inline constexpr VkDeviceSize kScreenVertexColorOffset = 16;
inline constexpr VkDeviceSize kScreenVertexTexcoordOffset = 24;

inline constexpr VkFormat kScreenVertexPositionFormat = VK_FORMAT_R32G32B32_SFLOAT;
inline constexpr VkFormat kScreenVertexRhwFormat = VK_FORMAT_R32_SFLOAT;
inline constexpr VkFormat kScreenVertexColorFormat = VK_FORMAT_B8G8R8A8_UNORM;
inline constexpr VkFormat kScreenVertexTexcoordFormat = VK_FORMAT_R32G32_SFLOAT;

inline VkVertexInputBindingDescription MakeScreenVertexBindingDescription() noexcept
{
    VkVertexInputBindingDescription description{};
    description.binding = kScreenVertexBinding;
    description.stride = kScreenVertexStride;
    description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return description;
}

inline VkVertexInputAttributeDescription MakeScreenVertexPositionAttribute() noexcept
{
    VkVertexInputAttributeDescription attribute{};
    attribute.location = kScreenVertexLocationPosition;
    attribute.binding = kScreenVertexBinding;
    attribute.format = kScreenVertexPositionFormat;
    attribute.offset = kScreenVertexPositionOffset;
    return attribute;
}

inline VkVertexInputAttributeDescription MakeScreenVertexRhwAttribute() noexcept
{
    VkVertexInputAttributeDescription attribute{};
    attribute.location = kScreenVertexLocationRhw;
    attribute.binding = kScreenVertexBinding;
    attribute.format = kScreenVertexRhwFormat;
    attribute.offset = kScreenVertexRhwOffset;
    return attribute;
}

inline VkVertexInputAttributeDescription MakeScreenVertexColorAttribute() noexcept
{
    VkVertexInputAttributeDescription attribute{};
    attribute.location = kScreenVertexLocationColor;
    attribute.binding = kScreenVertexBinding;
    attribute.format = kScreenVertexColorFormat;
    attribute.offset = kScreenVertexColorOffset;
    return attribute;
}

inline VkVertexInputAttributeDescription MakeScreenVertexTexcoordAttribute() noexcept
{
    VkVertexInputAttributeDescription attribute{};
    attribute.location = kScreenVertexLocationTexcoord;
    attribute.binding = kScreenVertexBinding;
    attribute.format = kScreenVertexTexcoordFormat;
    attribute.offset = kScreenVertexTexcoordOffset;
    return attribute;
}

inline std::array<VkVertexInputAttributeDescription, kScreenVertexAttributeCount>
MakeScreenVertexAttributeDescriptions() noexcept
{
    return {MakeScreenVertexPositionAttribute(),
            MakeScreenVertexRhwAttribute(),
            MakeScreenVertexColorAttribute(),
            MakeScreenVertexTexcoordAttribute()};
}

} // namespace Poseidon::vk
