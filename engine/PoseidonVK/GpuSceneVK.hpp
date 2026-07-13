#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace Poseidon::vk
{

// GPU-owned scene input.  This deliberately contains only POD data: it is
// written to a persistent SSBO and consumed by scene_cull.comp without any
// CPU-side pointer or descriptor-table dependency.
struct alignas(16) GpuSceneInstanceVK
{
    float localBoundsCenter[4] = {}; // xyz, conservative local-space radius in w
    std::uint32_t drawIndex = 0;
    std::uint32_t batchIndex = 0;
    std::uint32_t indirectOffset = 0;
    std::uint32_t batchCapacity = 0;
    std::uint32_t lodFirstIndex[4] = {};
    std::uint32_t lodIndexCount[4] = {};
    float lodDistance[4] = {}; // first distance at which LOD n is selected
};

static_assert(offsetof(GpuSceneInstanceVK, localBoundsCenter) == 0);
static_assert(offsetof(GpuSceneInstanceVK, drawIndex) == 16);
static_assert(offsetof(GpuSceneInstanceVK, indirectOffset) == 24);
static_assert(offsetof(GpuSceneInstanceVK, lodFirstIndex) == 32);
static_assert(sizeof(GpuSceneInstanceVK) == 80);

// A batch owns a contiguous segment of the indirect command buffer.  Batches
// never cross mesh/material/pipeline bindings, so an indirect draw is legal
// without descriptor indexing.  The count buffer has one uint per batch.
struct GpuSceneBatchVK
{
    std::uint32_t firstInstance = 0;
    std::uint32_t instanceCount = 0;
    std::uint32_t indirectOffset = 0;
    std::uint32_t countOffset = 0;
    std::uint32_t sourceCommandIndex = 0;
    std::uint32_t sceneGroup = 0;
};

static_assert(sizeof(GpuSceneBatchVK) == 24);
static_assert(sizeof(VkDrawIndexedIndirectCommand) == 20);

struct GpuSceneCapabilitiesVK
{
    bool compute = false;
    bool multiDrawIndirect = false;
    bool shaderDrawParameters = false;
    bool drawIndirectCount = false;

    bool GpuDrivenAvailable() const noexcept
    {
        return compute && multiDrawIndirect && shaderDrawParameters;
    }
};

inline constexpr std::uint32_t SelectGpuSceneLod(const std::array<float, 4>& distances,
                                                 float distance) noexcept
{
    std::uint32_t lod = 0;
    for (std::uint32_t candidate = 1; candidate < distances.size(); ++candidate)
    {
        if (distances[candidate] > 0.0f && distance >= distances[candidate])
            lod = candidate;
    }
    return lod;
}

} // namespace Poseidon::vk
