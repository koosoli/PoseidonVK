#include <PoseidonVK/TerrainVK.hpp>
#include <PoseidonVK/RenderStateVK.hpp>
#include <PoseidonVK/ShaderCompilerVK.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

namespace
{
constexpr const char kTerrainVertexShader[] =
#include <PoseidonVK/Shaders/terrain.vert.glsl.hpp>
;
constexpr const char kTerrainFragmentShader[] =
#include <PoseidonVK/Shaders/terrain.frag.glsl.hpp>
;
constexpr const char kTerrainShadowComputeShader[] =
#include <PoseidonVK/Shaders/terrain_shadow.comp.glsl.hpp>
;
constexpr float kSunMoveCos = 0.99999f;
constexpr float kShadowPenumbraRadians = 0.0174532925f;

float Hash2(std::uint32_t x, std::uint32_t y)
{
    std::uint32_t n = x * 1597334677u + y * 3812015801u;
    n = (n ^ (n >> 15u)) * 2246822519u;
    n ^= n >> 13u;
    return float(n & 0x00ffffffu) * (1.0f / 16777216.0f);
}

float SampleHeightBilinear(const std::vector<float>& heights, std::uint32_t width, std::uint32_t height, float x, float z)
{
    const float fx = std::floor(x);
    const float fz = std::floor(z);
    const float tx = x - fx;
    const float tz = z - fz;
    const auto at = [&](int sx, int sz)
    {
        const int cx = std::clamp(sx, 0, static_cast<int>(width) - 1);
        const int cz = std::clamp(sz, 0, static_cast<int>(height) - 1);
        return heights[static_cast<std::size_t>(cz) * width + cx];
    };
    const int ix = static_cast<int>(fx);
    const int iz = static_cast<int>(fz);
    const float a = at(ix, iz) + (at(ix + 1, iz) - at(ix, iz)) * tx;
    const float b = at(ix, iz + 1) + (at(ix + 1, iz + 1) - at(ix, iz + 1)) * tx;
    return a + (b - a) * tz;
}

std::vector<float> BlurSkyVisibility(const std::vector<float>& source, std::uint32_t width, std::uint32_t height,
                                      std::uint32_t radius)
{
    if (radius == 0 || width == 0 || height == 0)
        return source;
    const int r = static_cast<int>(radius);
    const float divisor = static_cast<float>(2 * r + 1);
    std::vector<float> temporary(source.size());
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x)
        {
            float sum = 0.0f;
            for (int dx = -r; dx <= r; ++dx)
                sum += source[static_cast<std::size_t>(y) * width +
                              std::clamp(static_cast<int>(x) + dx, 0, static_cast<int>(width) - 1)];
            temporary[static_cast<std::size_t>(y) * width + x] = sum / divisor;
        }
    std::vector<float> result(source.size());
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x)
        {
            float sum = 0.0f;
            for (int dy = -r; dy <= r; ++dy)
                sum += temporary[static_cast<std::size_t>(std::clamp(static_cast<int>(y) + dy, 0,
                                                                       static_cast<int>(height) - 1)) *
                                     width + x];
            result[static_cast<std::size_t>(y) * width + x] = sum / divisor;
        }
    return result;
}
} // namespace

namespace Poseidon::vk
{
bool TerrainVK::Initialize(VkPhysicalDevice physicalDevice, VkDevice device, VkCommandPool commandPool, VkQueue queue)
{
    _physicalDevice = physicalDevice;
    _device = device;
    _commandPool = commandPool;
    _queue = queue;
    if (!device || !physicalDevice || !commandPool || !queue)
        return false;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    _shadowMaskMaxDimension = std::min(kShadowMaskDimensionCap, properties.limits.maxImageDimension2D);
    VkFormatProperties shadowFormatProperties{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT, &shadowFormatProperties);
    const VkFormatFeatureFlags requiredShadowFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                                        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                                        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    if (_shadowMaskMaxDimension == 0 ||
        (shadowFormatProperties.optimalTilingFeatures & requiredShadowFeatures) != requiredShadowFeatures ||
        !CreateDescriptorResources() || !CreateVisualDescriptorResources() || !CreateShadowComputeResources() ||
        !CreateMapSamplers() || !CreateGrid())
    {
        Destroy(device);
        return false;
    }
    if (CreateHostVisibleBuffer(physicalDevice, device, 8192 * sizeof(NodeInstance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                _instances) != VK_SUCCESS ||
        CreateHostVisibleBuffer(physicalDevice, device, sizeof(Params), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, _paramsBuffer) !=
            VK_SUCCESS)
    {
        Destroy(device);
        return false;
    }
    UploadMappedBuffer(_paramsBuffer, &_params, sizeof(_params));
    _shadowSweep.penumbra = kShadowPenumbraRadians;
    _ready = true;
    return true;
}

bool TerrainVK::CreateDescriptorResources()
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(_physicalDevice, &properties);
    // Height/index/jitter are fixed combined samplers. The material layers are
    // a variable sampled-image array paired with TerrainVK's two samplers, so
    // layer capacity is constrained by sampled-image limits only.
    const std::uint32_t maxSampledImages = std::min(properties.limits.maxDescriptorSetSampledImages,
                                                    properties.limits.maxPerStageDescriptorSampledImages);
    const std::uint32_t maxSamplers = std::min(properties.limits.maxDescriptorSetSamplers,
                                                properties.limits.maxPerStageDescriptorSamplers);
    if (maxSampledImages <= 3 || maxSamplers < 5)
        return false;
    _layerCapacity = std::min(kRequestedLayerCapacity, maxSampledImages - 3);

    std::array<VkDescriptorSetLayoutBinding, 7> bindings{};
    for (std::uint32_t binding = kHeightmapBinding; binding <= kJitterMapBinding; ++binding)
    {
        bindings[binding].binding = binding;
        bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[binding].descriptorCount = 1;
        bindings[binding].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    bindings[kParamsBinding].binding = kParamsBinding;
    bindings[kParamsBinding].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[kParamsBinding].descriptorCount = 1;
    bindings[kParamsBinding].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    for (const std::uint32_t binding : {kRepeatSamplerBinding, kClampSamplerBinding})
    {
        bindings[binding].binding = binding;
        bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        bindings[binding].descriptorCount = 1;
        bindings[binding].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    // Vulkan requires a variable-count binding to be the highest-numbered
    // binding in its layout. Binding 6 is the WGPU-style native layer image
    // array; sampler selection is performed in terrain.frag.glsl.
    bindings[kTextureLayersBinding].binding = kTextureLayersBinding;
    bindings[kTextureLayersBinding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[kTextureLayersBinding].descriptorCount = _layerCapacity;
    bindings[kTextureLayersBinding].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorBindingFlags, 7> bindingFlags{};
    bindingFlags[kTextureLayersBinding] = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
                                          VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flagsInfo.bindingCount = static_cast<std::uint32_t>(bindingFlags.size());
    flagsInfo.pBindingFlags = bindingFlags.data();

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = &flagsInfo;
    layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(_device, &layoutInfo, nullptr, &_descriptorSetLayout) != VK_SUCCESS)
        return false;

    const std::array<VkDescriptorPoolSize, 4> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 2},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, _layerCapacity},
    }};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_descriptorPool) != VK_SUCCESS)
        return false;

    VkDescriptorSetVariableDescriptorCountAllocateInfo variableCountInfo{};
    variableCountInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    variableCountInfo.descriptorSetCount = 1;
    variableCountInfo.pDescriptorCounts = &_layerCapacity;
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.pNext = &variableCountInfo;
    allocateInfo.descriptorPool = _descriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &_descriptorSetLayout;
    return vkAllocateDescriptorSets(_device, &allocateInfo, &_descriptorSet) == VK_SUCCESS;
}

bool TerrainVK::CreateVisualDescriptorResources()
{
    // Set 2 follows terrain.frag.glsl exactly. Bindings 0/2 are terrain-owned
    // derived maps; binding 1 arrives from the real detail producer.
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    for (std::uint32_t binding = 0; binding < bindings.size(); ++binding)
    {
        bindings[binding].binding = binding;
        bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[binding].descriptorCount = 1;
        bindings[binding].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(_device, &layoutInfo, nullptr, &_visualDescriptorSetLayout) != VK_SUCCESS)
        return false;
    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<std::uint32_t>(bindings.size())};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &size;
    if (vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_visualDescriptorPool) != VK_SUCCESS)
        return false;
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = _visualDescriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &_visualDescriptorSetLayout;
    return vkAllocateDescriptorSets(_device, &allocateInfo, &_visualDescriptorSet) == VK_SUCCESS;
}

bool TerrainVK::CreateShadowComputeResources()
{
    const std::array<VkDescriptorSetLayoutBinding, 3> bindings = {{
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    }};
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(_device, &layoutInfo, nullptr, &_shadowComputeDescriptorSetLayout) != VK_SUCCESS)
        return false;
    const std::array<VkDescriptorPoolSize, 3> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
    }};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_shadowComputeDescriptorPool) != VK_SUCCESS)
        return false;
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = _shadowComputeDescriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &_shadowComputeDescriptorSetLayout;
    if (vkAllocateDescriptorSets(_device, &allocateInfo, &_shadowComputeDescriptorSet) != VK_SUCCESS ||
        CreateHostVisibleBuffer(_physicalDevice, _device, sizeof(ShadowSweep), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                _shadowSweepBuffer) != VK_SUCCESS)
        return false;
    std::vector<std::uint32_t> spirv;
    std::string error;
    if (!CompileEmbeddedGlsl(kTerrainShadowComputeShader, VK_SHADER_STAGE_COMPUTE_BIT, spirv, error))
        return false;
    VkShaderModule module = VK_NULL_HANDLE;
    if (!CreateEmbeddedShaderModule(_device, spirv, module))
        return false;
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &_shadowComputeDescriptorSetLayout;
    const VkResult layoutResult = vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_shadowComputePipelineLayout);
    if (layoutResult != VK_SUCCESS)
    {
        vkDestroyShaderModule(_device, module, nullptr);
        return false;
    }
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = _shadowComputePipelineLayout;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = module;
    pipelineInfo.stage.pName = "main";
    const VkResult pipelineResult = vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                                              &_shadowComputePipeline);
    vkDestroyShaderModule(_device, module, nullptr);
    return pipelineResult == VK_SUCCESS;
}

bool TerrainVK::CreateMapSamplers()
{
    const auto createSampler = [&](VkFilter filter, VkSampler& sampler)
    {
        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = filter;
        info.minFilter = filter;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.maxLod = 0.0f;
        return vkCreateSampler(_device, &info, nullptr, &sampler) == VK_SUCCESS;
    };
    const auto createLayerSampler = [&](VkSamplerAddressMode addressMode, VkSampler& sampler)
    {
        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = VK_FILTER_LINEAR;
        info.minFilter = VK_FILTER_LINEAR;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.addressModeU = addressMode;
        info.addressModeV = addressMode;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.maxLod = VK_LOD_CLAMP_NONE;
        return vkCreateSampler(_device, &info, nullptr, &sampler) == VK_SUCCESS;
    };
    return createSampler(VK_FILTER_LINEAR, _heightSampler) && createSampler(VK_FILTER_NEAREST, _indexSampler) &&
           createSampler(VK_FILTER_LINEAR, _jitterSampler) && createSampler(VK_FILTER_LINEAR, _maskSampler) &&
           createLayerSampler(VK_SAMPLER_ADDRESS_MODE_REPEAT, _layerRepeatSampler) &&
           createLayerSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, _layerClampSampler);
}

bool TerrainVK::UpdateStaticDescriptors()
{
    if (!_descriptorSet || !_heightmap.view || !_indexMap.view || !_jitterMap.view || !_paramsBuffer.buffer ||
         !_heightSampler || !_indexSampler || !_jitterSampler || !_layerRepeatSampler || !_layerClampSampler)
        return false;

    const std::array<VkDescriptorImageInfo, 3> images = {{
        {_heightSampler, _heightmap.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {_indexSampler, _indexMap.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {_jitterSampler, _jitterMap.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    }};
    VkDescriptorBufferInfo params{};
    params.buffer = _paramsBuffer.buffer;
    params.offset = 0;
    params.range = sizeof(Params);
    std::array<VkWriteDescriptorSet, 6> writes{};
    for (std::uint32_t binding = 0; binding < images.size(); ++binding)
    {
        writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[binding].dstSet = _descriptorSet;
        writes[binding].dstBinding = binding;
        writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[binding].descriptorCount = 1;
        writes[binding].pImageInfo = &images[binding];
    }
    writes[kParamsBinding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[kParamsBinding].dstSet = _descriptorSet;
    writes[kParamsBinding].dstBinding = kParamsBinding;
    writes[kParamsBinding].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[kParamsBinding].descriptorCount = 1;
    writes[kParamsBinding].pBufferInfo = &params;
    const std::array<VkDescriptorImageInfo, 2> layerSamplers = {{{_layerRepeatSampler, VK_NULL_HANDLE,
                                                                    VK_IMAGE_LAYOUT_UNDEFINED},
                                                                   {_layerClampSampler, VK_NULL_HANDLE,
                                                                    VK_IMAGE_LAYOUT_UNDEFINED}}};
    for (std::uint32_t binding = kRepeatSamplerBinding; binding <= kClampSamplerBinding; ++binding)
    {
        writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[binding].dstSet = _descriptorSet;
        writes[binding].dstBinding = binding;
        writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[binding].descriptorCount = 1;
        writes[binding].pImageInfo = &layerSamplers[binding - kRepeatSamplerBinding];
    }
    vkUpdateDescriptorSets(_device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return true;
}

bool TerrainVK::UpdateShadowComputeDescriptors()
{
    if (!_shadowComputeDescriptorSet || !_shadowSweepBuffer.buffer || !_heightmap.view || !_selfShadowMask.view ||
        !_heightSampler)
        return false;
    const VkDescriptorBufferInfo sweepBuffer{_shadowSweepBuffer.buffer, 0, sizeof(ShadowSweep)};
    const VkDescriptorImageInfo heightImage{_heightSampler, _heightmap.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo maskImage{VK_NULL_HANDLE, _selfShadowMask.view, VK_IMAGE_LAYOUT_GENERAL};
    std::array<VkWriteDescriptorSet, 3> writes{};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[0].dstSet = _shadowComputeDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &sweepBuffer;
    for (std::uint32_t binding = 1; binding <= 2; ++binding)
    {
        writes[binding] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[binding].dstSet = _shadowComputeDescriptorSet;
        writes[binding].dstBinding = binding;
        writes[binding].descriptorCount = 1;
    }
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &heightImage;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].pImageInfo = &maskImage;
    vkUpdateDescriptorSets(_device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return true;
}

bool TerrainVK::UpdateVisualDescriptors()
{
    if (!_visualDescriptorSet || !_selfShadowMask.view || !_skyVisibilityMask.view || !_maskSampler ||
        _detailDescriptor.imageView == VK_NULL_HANDLE || _detailDescriptor.sampler == VK_NULL_HANDLE)
        return false;
    const std::array<VkDescriptorImageInfo, 3> images = {{
        {_maskSampler, _selfShadowMask.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        _detailDescriptor,
        {_maskSampler, _skyVisibilityMask.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    }};
    std::array<VkWriteDescriptorSet, 3> writes{};
    for (std::uint32_t binding = 0; binding < writes.size(); ++binding)
    {
        writes[binding] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[binding].dstSet = _visualDescriptorSet;
        writes[binding].dstBinding = binding;
        writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[binding].descriptorCount = 1;
        writes[binding].pImageInfo = &images[binding];
    }
    vkUpdateDescriptorSets(_device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    _detailDescriptorsReady = true;
    _visualDescriptorsReady = _selfShadowPopulated;
    return true;
}

bool TerrainVK::UpdateVisualDescriptors(const VkDescriptorImageInfo& detail)
{
    if (!_ready || detail.imageView == VK_NULL_HANDLE || detail.sampler == VK_NULL_HANDLE ||
        detail.imageLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        return false;
    _detailDescriptor = detail;
    _detailDescriptorsReady = false;
    _visualDescriptorsReady = false;
    return UpdateVisualDescriptors();
}

bool TerrainVK::ValidateLayerIndices(const render::frame::TerrainOpaque& terrain)
{
    _telemetry.capacity = _layerCapacity;
    _telemetry.requestedLayers = static_cast<std::uint32_t>(terrain.textureLayers.size());
    _telemetry.invalidLayerIndices = 0;
    const std::size_t layerCount = terrain.textureLayers.size();
    // A variable descriptor count is fixed at set allocation. Even an unused
    // excess layer would make the captured layer array unbindable, so reject
    // it before uploading any map that could later index the wrong set.
    if (layerCount == 0 || layerCount > _layerCapacity)
    {
        _telemetry.invalidLayerIndices = static_cast<std::uint32_t>(terrain.textureIndices.size());
        return false;
    }
    for (std::uint16_t entry : terrain.textureIndices)
    {
        const std::uint32_t layer = entry & 0x7fffu; // high bit is ClampU|ClampV, never a layer bit.
        if (layer >= layerCount || layer >= _layerCapacity)
            ++_telemetry.invalidLayerIndices;
    }
    return _telemetry.invalidLayerIndices == 0;
}

bool TerrainVK::CreateGrid()
{
    std::vector<GridVertex> vertices;
    std::vector<std::uint16_t> indices;
    const auto addVertex = [&](float x, float z, float skirt) { vertices.push_back({x, z, skirt}); };
    for (std::uint32_t z = 0; z <= kGridN; ++z)
        for (std::uint32_t x = 0; x <= kGridN; ++x)
            addVertex(float(x) / kGridN, float(z) / kGridN, 0.0f);
    const auto base = [](std::uint32_t x, std::uint32_t z) { return z * (kGridN + 1) + x; };
    const auto quad = [&](std::uint16_t a, std::uint16_t b, std::uint16_t c, std::uint16_t d)
    {
        indices.insert(indices.end(), {a, b, c, a, c, d});
    };
    for (std::uint32_t z = 0; z < kGridN; ++z)
        for (std::uint32_t x = 0; x < kGridN; ++x)
            quad(base(x, z), base(x + 1, z), base(x + 1, z + 1), base(x, z + 1));
    // Duplicate each boundary vertex at skirt=1 and stitch it to the surface.
    std::vector<std::uint16_t> edge;
    for (std::uint32_t x = 0; x <= kGridN; ++x) edge.push_back(base(x, 0));
    for (std::uint32_t z = 1; z <= kGridN; ++z) edge.push_back(base(kGridN, z));
    for (std::uint32_t x = kGridN; x-- > 0;) edge.push_back(base(x, kGridN));
    for (std::uint32_t z = kGridN; z-- > 1;) edge.push_back(base(0, z));
    const std::uint16_t skirtBase = static_cast<std::uint16_t>(vertices.size());
    for (std::uint16_t i : edge) addVertex(vertices[i].x, vertices[i].z, 1.0f);
    for (std::uint16_t i = 0; i < edge.size(); ++i)
    {
        const std::uint16_t n = static_cast<std::uint16_t>((i + 1) % edge.size());
        quad(edge[i], edge[n], skirtBase + n, skirtBase + i);
    }
    if (CreateHostVisibleBuffer(_physicalDevice, _device, vertices.size() * sizeof(GridVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                _gridVertices) != VK_SUCCESS ||
        CreateHostVisibleBuffer(_physicalDevice, _device, indices.size() * sizeof(std::uint16_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                _gridIndices) != VK_SUCCESS)
        return false;
    UploadMappedBuffer(_gridVertices, vertices.data(), vertices.size() * sizeof(GridVertex));
    UploadMappedBuffer(_gridIndices, indices.data(), indices.size() * sizeof(std::uint16_t));
    _gridIndexCount = static_cast<std::uint32_t>(indices.size());
    return true;
}

bool TerrainVK::CreateRasterPipeline(const RasterInputs& inputs, std::string& error)
{
    error.clear();
    // Do not manufacture visual data just to make a pipeline green. CSM and
    // self-shadow are receiver inputs, not optional visual polish.
    if (!_ready || !inputs.Complete() || !_visualDescriptorsReady || _descriptorSetLayout == VK_NULL_HANDLE ||
        inputs.visualDescriptorSetLayout != _visualDescriptorSetLayout || inputs.visualDescriptorSet != _visualDescriptorSet)
    {
        if (!_ready)                                                       error = "TerrainVK not ready";
        else if (!inputs.Complete())                                       error = "RasterInputs incomplete (CSM/visual bindings missing)";
        else if (!_visualDescriptorsReady)                                 error = "visual descriptors not ready";
        else if (_descriptorSetLayout == VK_NULL_HANDLE)                   error = "terrain descriptor set layout null";
        else                                                               error = "visual descriptor set/layout mismatch";
        return false;
    }
    DestroyRasterPipeline(_device);

    const std::array<VkDescriptorSetLayout, 3> layouts = {
        inputs.frameDescriptorSetLayout, _descriptorSetLayout, inputs.visualDescriptorSetLayout};
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
    layoutInfo.pSetLayouts = layouts.data();
    if (vkCreatePipelineLayout(_device, &layoutInfo, nullptr, &_rasterPipelineLayout) != VK_SUCCESS)
    {
        error = "vkCreatePipelineLayout failed";
        return false;
    }

    std::vector<std::uint32_t> vertexSpirv, fragmentSpirv;
    if (!CompileEmbeddedGlsl(kTerrainVertexShader, VK_SHADER_STAGE_VERTEX_BIT, vertexSpirv, error))
    {
        error = "terrain.vert.glsl compile: " + error;
        DestroyRasterPipeline(_device);
        return false;
    }
    if (!CompileEmbeddedGlsl(kTerrainFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT, fragmentSpirv, error))
    {
        error = "terrain.frag.glsl compile: " + error;
        DestroyRasterPipeline(_device);
        return false;
    }
    VkShaderModule vertexModule = VK_NULL_HANDLE;
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    if (!CreateEmbeddedShaderModule(_device, vertexSpirv, vertexModule) ||
        !CreateEmbeddedShaderModule(_device, fragmentSpirv, fragmentModule))
    {
        if (vertexModule) vkDestroyShaderModule(_device, vertexModule, nullptr);
        if (fragmentModule) vkDestroyShaderModule(_device, fragmentModule, nullptr);
        DestroyRasterPipeline(_device);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";

    const std::array<VkVertexInputBindingDescription, 2> bindings = {{
        {0, sizeof(GridVertex), VK_VERTEX_INPUT_RATE_VERTEX},
        {1, sizeof(NodeInstance), VK_VERTEX_INPUT_RATE_INSTANCE},
    }};
    const std::array<VkVertexInputAttributeDescription, 4> attributes = {{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GridVertex, x)},
        {1, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(NodeInstance, originX)},
        {2, 1, VK_FORMAT_R32_UINT, offsetof(NodeInstance, lod)},
        {3, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(NodeInstance, morphStart)},
    }};
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0.0f, static_cast<float>(inputs.extent.height), static_cast<float>(inputs.extent.width),
                        -static_cast<float>(inputs.extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, inputs.extent};
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo rasterizer =
        BuildRasterizationState(render::CullMode::Back, render::FrontFaceMode::CW);
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth = BuildDepthStencilState(render::DepthMode::Normal);
    VkPipelineColorBlendAttachmentState blend = BuildColorBlendAttachmentState(render::BlendMode::Opaque);
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blend;
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &assembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depth;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.layout = _rasterPipelineLayout;
    pipelineInfo.renderPass = inputs.renderPass;
    const VkResult result = vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_rasterPipeline);
    vkDestroyShaderModule(_device, fragmentModule, nullptr);
    vkDestroyShaderModule(_device, vertexModule, nullptr);
    if (result != VK_SUCCESS)
    {
        error = "vkCreateGraphicsPipelines failed (VkResult=" + std::to_string(static_cast<int>(result)) + ")";
        DestroyRasterPipeline(_device);
        return false;
    }
    return true;
}

void TerrainVK::DestroyRasterPipeline(VkDevice device)
{
    if (device && _rasterPipeline) vkDestroyPipeline(device, _rasterPipeline, nullptr);
    if (device && _rasterPipelineLayout) vkDestroyPipelineLayout(device, _rasterPipelineLayout, nullptr);
    _rasterPipeline = VK_NULL_HANDLE;
    _rasterPipelineLayout = VK_NULL_HANDLE;
}

bool TerrainVK::RecordInstancedGridDraw(VkCommandBuffer commandBuffer) const
{
    if (commandBuffer == VK_NULL_HANDLE || _gridVertices.buffer == VK_NULL_HANDLE || _gridIndices.buffer == VK_NULL_HANDLE ||
        _instances.buffer == VK_NULL_HANDLE || _gridIndexCount == 0 || _visible.empty())
        return false;
    const VkBuffer buffers[] = {_gridVertices.buffer, _instances.buffer};
    const VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 2, buffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, _gridIndices.buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(commandBuffer, _gridIndexCount, static_cast<std::uint32_t>(_visible.size()), 0, 0, 0);
    return true;
}

bool TerrainVK::RecreateMapImages(const render::frame::TerrainOpaque& terrain)
{
    // The WGPU reference intentionally extends the self-shadow grid to 2x the
    // heightfield, never shrinking below it, then caps the allocation.  A map
    // wider than the cap cannot retain that visual contract, so do not silently
    // produce a lower-resolution shadow.
    if (terrain.heightWidth > _shadowMaskMaxDimension || terrain.heightHeight > _shadowMaskMaxDimension)
        return false;
    const std::uint32_t shadowWidth = std::clamp(terrain.heightWidth * kShadowMaskScale, terrain.heightWidth,
                                                 _shadowMaskMaxDimension);
    const std::uint32_t shadowHeight = std::clamp(terrain.heightHeight * kShadowMaskScale, terrain.heightHeight,
                                                  _shadowMaskMaxDimension);
    DestroyImage(_device, _heightmap); DestroyImage(_device, _indexMap); DestroyImage(_device, _jitterMap);
    DestroyImage(_device, _selfShadowMask); DestroyImage(_device, _skyVisibilityMask);
    _selfShadowLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    _selfShadowPopulated = false;
    _visualDescriptorsReady = false;
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (CreateImage2D(_physicalDevice, _device, terrain.heightWidth, terrain.heightHeight, 1, VK_FORMAT_R32_SFLOAT, usage,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _heightmap) != VK_SUCCESS ||
        CreateImage2D(_physicalDevice, _device, terrain.indexWidth, terrain.indexHeight, 1, VK_FORMAT_R16_UINT, usage,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _indexMap) != VK_SUCCESS ||
        CreateImage2D(_physicalDevice, _device, terrain.indexWidth, terrain.indexHeight, 1, VK_FORMAT_R8G8_SNORM, usage,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _jitterMap) != VK_SUCCESS ||
        CreateImage2D(_physicalDevice, _device, shadowWidth, shadowHeight, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      _selfShadowMask) != VK_SUCCESS)
        return false;
    if (UploadImage2D(_physicalDevice, _device, _commandPool, _queue, _heightmap, terrain.heights.data(),
                      terrain.heights.size() * sizeof(float)) != VK_SUCCESS ||
        UploadImage2D(_physicalDevice, _device, _commandPool, _queue, _indexMap, terrain.textureIndices.data(),
                      terrain.textureIndices.size() * sizeof(std::uint16_t)) != VK_SUCCESS ||
        UploadImage2D(_physicalDevice, _device, _commandPool, _queue, _jitterMap, terrain.jitterOffsets.data(),
                      terrain.jitterOffsets.size() * sizeof(std::int8_t)) != VK_SUCCESS)
        return false;
    _skyVisibilitySource = terrain.heights;
    _skyVisibilitySourceWidth = terrain.heightWidth;
    _skyVisibilitySourceHeight = terrain.heightHeight;
    _skyVisibilitySourceGrid = terrain.terrainGrid;
    _shadowSweep.worldOrigin[0] = _params.worldOrigin[0];
    _shadowSweep.worldOrigin[1] = _params.worldOrigin[1];
    _shadowSweep.terrainGrid = terrain.terrainGrid;
    _shadowSweep.invScale[0] = static_cast<float>(terrain.heightWidth) / shadowWidth;
    _shadowSweep.invScale[1] = static_cast<float>(terrain.heightHeight) / shadowHeight;
    _shadowSweep.heightWidth = terrain.heightWidth;
    _shadowSweep.heightHeight = terrain.heightHeight;
    _shadowSweep.maskWidth = shadowWidth;
    _shadowSweep.maskHeight = shadowHeight;
    _shadowSweep.maxSteps = 512;
    _shadowSweep.strength = 1.0f;
    _shadowSweep.sunDirection[3] = *std::max_element(terrain.heights.begin(), terrain.heights.end());
    _shadowDirty = true;
    return RebuildSkyVisibility() && UpdateShadowComputeDescriptors() &&
           (!_detailDescriptor.imageView || UpdateVisualDescriptors());
}

bool TerrainVK::RebuildSkyVisibility()
{
    if (_skyVisibilitySource.empty() || _skyVisibilitySourceWidth == 0 || _skyVisibilitySourceHeight == 0)
        return false;
    const std::uint32_t downsample = std::max(1u, _skyVisibilityOptions.downsample);
    const std::uint32_t outWidth = std::max(1u, (_skyVisibilitySourceWidth + downsample - 1u) / downsample);
    const std::uint32_t outHeight = std::max(1u, (_skyVisibilitySourceHeight + downsample - 1u) / downsample);
    const std::uint32_t azimuths = std::max(1u, _skyVisibilityOptions.azimuths);
    const float grid = std::max(_skyVisibilitySourceGrid, 1.0e-3f);
    const float sector = 6.28318530718f / static_cast<float>(azimuths);
    std::vector<float> visibility(static_cast<std::size_t>(outWidth) * outHeight);
    constexpr float stepGrowth = 1.35f;
    for (std::uint32_t y = 0; y < outHeight; ++y)
        for (std::uint32_t x = 0; x < outWidth; ++x)
        {
            const float hx = (static_cast<float>(x) + 0.5f) * downsample;
            const float hz = (static_cast<float>(y) + 0.5f) * downsample;
            const float h0 = SampleHeightBilinear(_skyVisibilitySource, _skyVisibilitySourceWidth,
                                                   _skyVisibilitySourceHeight, hx, hz);
            const float rotation = Hash2(x, y) * sector;
            float sum = 0.0f;
            for (std::uint32_t i = 0; i < azimuths; ++i)
            {
                const float phi = sector * i + rotation;
                const float dx = std::cos(phi);
                const float dz = std::sin(phi);
                const float jitter = Hash2(x + i * 7919u, y + i * 104729u);
                float maxSlope = 0.0f;
                float distance = grid * (0.5f + jitter);
                float step = grid;
                while (distance <= _skyVisibilityOptions.radiusMeters)
                {
                    const float height = SampleHeightBilinear(_skyVisibilitySource, _skyVisibilitySourceWidth,
                                                              _skyVisibilitySourceHeight, hx + dx * distance / grid,
                                                              hz + dz * distance / grid);
                    maxSlope = std::max(maxSlope, (height - h0) / distance);
                    step *= stepGrowth;
                    distance += step;
                }
                sum += 1.0f / (1.0f + maxSlope * maxSlope);
            }
            visibility[static_cast<std::size_t>(y) * outWidth + x] = sum / azimuths;
        }
    const std::vector<float> filtered = BlurSkyVisibility(visibility, outWidth, outHeight, _skyVisibilityOptions.blurRadius);
    std::vector<std::uint8_t> bytes(filtered.size());
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>(std::clamp(filtered[i], 0.0f, 1.0f) * 255.0f + 0.5f);
    DestroyImage(_device, _skyVisibilityMask);
    if (CreateImage2D(_physicalDevice, _device, outWidth, outHeight, 1, VK_FORMAT_R8_UNORM,
                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      _skyVisibilityMask) != VK_SUCCESS)
        return false;
    return UploadImage2D(_physicalDevice, _device, _commandPool, _queue, _skyVisibilityMask, bytes.data(), bytes.size()) ==
           VK_SUCCESS;
}

bool TerrainVK::SetSkyVisibilityOptions(const SkyVisibilityOptions& options)
{
    const SkyVisibilityOptions normalized{std::max(1u, options.downsample), std::max(1u, options.azimuths),
                                          std::max(0.0f, options.radiusMeters), options.blurRadius};
    if (_skyVisibilityOptions.downsample == normalized.downsample && _skyVisibilityOptions.azimuths == normalized.azimuths &&
        std::abs(_skyVisibilityOptions.radiusMeters - normalized.radiusMeters) <= 1.0e-3f &&
        _skyVisibilityOptions.blurRadius == normalized.blurRadius)
        return true;
    _skyVisibilityOptions = normalized;
    _visualDescriptorsReady = false;
    return _skyVisibilitySource.empty() || (RebuildSkyVisibility() &&
                                            (!_detailDescriptor.imageView || UpdateVisualDescriptors()));
}

bool TerrainVK::RecordSelfShadowPass(VkCommandBuffer commandBuffer, float sunToLightX, float sunToLightY,
                                     float sunToLightZ)
{
    if (commandBuffer == VK_NULL_HANDLE || !_shadowComputePipeline || !_shadowComputeDescriptorSet ||
        !_selfShadowMask.image || !_heightmap.image || _shadowSweep.maskWidth == 0 || _shadowSweep.maskHeight == 0)
        return false;
    const float length = std::sqrt(sunToLightX * sunToLightX + sunToLightY * sunToLightY + sunToLightZ * sunToLightZ);
    if (length <= 1.0e-5f)
        return false;
    sunToLightX /= length;
    sunToLightY /= length;
    sunToLightZ /= length;
    const bool moved = _lastSunToLight[0] * sunToLightX + _lastSunToLight[1] * sunToLightY +
                           _lastSunToLight[2] * sunToLightZ <
                       kSunMoveCos;
    if (!_shadowDirty && !moved)
        return false;
    _shadowSweep.sunDirection[0] = sunToLightX;
    _shadowSweep.sunDirection[1] = sunToLightY;
    _shadowSweep.sunDirection[2] = sunToLightZ;
    UploadMappedBuffer(_shadowSweepBuffer, &_shadowSweep, sizeof(_shadowSweep));

    VkImageMemoryBarrier imageBarrier{};
    imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageBarrier.oldLayout = _selfShadowLayout;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.image = _selfShadowMask.image;
    imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarrier.subresourceRange.levelCount = 1;
    imageBarrier.subresourceRange.layerCount = 1;
    imageBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    const VkPipelineStageFlags sourceStage = _selfShadowLayout == VK_IMAGE_LAYOUT_UNDEFINED
                                                 ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                                 : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    imageBarrier.srcAccessMask = _selfShadowLayout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, sourceStage, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &imageBarrier);
    VkBufferMemoryBarrier sweepBarrier{};
    sweepBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    sweepBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sweepBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sweepBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    sweepBarrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
    sweepBarrier.buffer = _shadowSweepBuffer.buffer;
    sweepBarrier.size = sizeof(_shadowSweep);
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
                         &sweepBarrier, 0, nullptr);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _shadowComputePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _shadowComputePipelineLayout, 0, 1,
                            &_shadowComputeDescriptorSet, 0, nullptr);
    vkCmdDispatch(commandBuffer, (_shadowSweep.maskWidth + 7u) / 8u, (_shadowSweep.maskHeight + 7u) / 8u, 1);

    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &imageBarrier);
    _selfShadowLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    _lastSunToLight[0] = sunToLightX;
    _lastSunToLight[1] = sunToLightY;
    _lastSunToLight[2] = sunToLightZ;
    _shadowDirty = false;
    _selfShadowPopulated = true;
    _visualDescriptorsReady = _detailDescriptorsReady;
    return true;
}

bool TerrainVK::Upload(const render::frame::TerrainOpaque& terrain)
{
    if (!_ready || !terrain.Valid()) return false;
    if (!ValidateLayerIndices(terrain)) return false;
    const bool needsRebuild = _revision.NeedsRebuild(terrain.revision);
    if (needsRebuild && (!RecreateMapImages(terrain) || !UpdateStaticDescriptors())) return false;
    _params.landGrid = terrain.landGrid; _params.terrainGrid = terrain.terrainGrid;
    _params.heightWidth = terrain.heightWidth; _params.heightHeight = terrain.heightHeight;
    _params.landRange = terrain.indexWidth; _params.layerCount = static_cast<std::uint32_t>(terrain.textureLayers.size());
    _params.seaLevel = terrain.seaLevel;
    UploadMappedBuffer(_paramsBuffer, &_params, sizeof(_params));
    if (needsRebuild)
    {
        auto bounds = [&](int ox, int oz, int span, float& mn, float& mx)
        {
            for (int z = oz; z <= oz + span; ++z) for (int x = ox; x <= ox + span; ++x)
            {
                const int cx = std::clamp(x, 0, int(terrain.heightWidth) - 1);
                const int cz = std::clamp(z, 0, int(terrain.heightHeight) - 1);
                const float h = terrain.heights[std::size_t(cz) * terrain.heightWidth + cx]; mn = std::min(mn, h); mx = std::max(mx, h);
            }
        };
        const int rootTexels = CdlodRootTexels(int(terrain.heightWidth), int(kGridN));
        BuildCdlodTree(rootTexels, 0, 0, terrain.terrainGrid, kGridN, bounds, _tree, _root, _levels, _leafSize);
        ComputeCdlodRanges(_leafSize * 4.0f, 2.0f, _levels, _ranges);
        _revision.Commit(terrain.revision);
    }
    return true;
}

bool TerrainVK::UpdateLayerDescriptors(const std::vector<LayerBinding>& layers)
{
    _telemetry.boundLayers = 0;
    _telemetry.fallbackLayers = 0;
    _telemetry.invalidLayers = 0;
    if (!_ready || !_descriptorSet || layers.empty() || layers.size() > _layerCapacity)
        return false;

    std::vector<VkDescriptorImageInfo> images;
    images.reserve(layers.size());
    for (const LayerBinding& layer : layers)
    {
        if (layer.image.imageView == VK_NULL_HANDLE || layer.image.imageLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            ++_telemetry.invalidLayers;
            return false;
        }
        // Binding 6 is VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE. Layer TextureVK
        // samplers are intentionally discarded; bit 15 selects one of the
        // TerrainVK-owned repeat/clamp samplers in the shader.
        VkDescriptorImageInfo image = layer.image;
        image.sampler = VK_NULL_HANDLE;
        images.push_back(image);
    }

    const std::uint32_t activeCount = static_cast<std::uint32_t>(images.size());
    if (activeCount < _layerCapacity)
    {
        images.resize(_layerCapacity, images[0]);
    }

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = _descriptorSet;
    write.dstBinding = kTextureLayersBinding;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.descriptorCount = static_cast<std::uint32_t>(images.size());
    write.pImageInfo = images.data();
    vkUpdateDescriptorSets(_device, 1, &write, 0, nullptr);
    _telemetry.boundLayers = activeCount;
    return true;
}

void TerrainVK::Select(float cameraX, float cameraY, float cameraZ, float x0, float z0, float x1, float z1)
{
    _visible.clear();
    if (_root < 0) return;
    SelectCdlod(_tree, _root, _levels - 1, cameraX, cameraY, cameraZ, _ranges, 0.5f,
                [&](const CdlodNode& n) { return n.originX + n.size > x0 && n.originX < x1 && n.originZ + n.size > z0 && n.originZ < z1; },
                [&](const CdlodSelection& n) { _visible.push_back({n.originX, n.originZ, n.size, std::uint32_t(n.level), n.morphStart, n.morphEnd}); });
    if (_visible.size() <= 8192) UploadMappedBuffer(_instances, _visible.data(), _visible.size() * sizeof(NodeInstance));
}

void TerrainVK::Destroy(VkDevice device)
{
    _ready = false; _visible.clear(); _tree.clear(); _ranges.clear(); _revision.Invalidate();
    DestroyRasterPipeline(device);
    DestroyShadowComputeResources(device);
    DestroyVisualDescriptorResources(device);
    DestroyDescriptorResources(device);
    DestroyBuffer(device, _gridVertices); DestroyBuffer(device, _gridIndices); DestroyBuffer(device, _instances); DestroyBuffer(device, _paramsBuffer);
    DestroyImage(device, _heightmap); DestroyImage(device, _indexMap); DestroyImage(device, _jitterMap);
    DestroyImage(device, _selfShadowMask); DestroyImage(device, _skyVisibilityMask);
    _skyVisibilitySource.clear();
    _detailDescriptor = {};
    _visualDescriptorsReady = false;
    _detailDescriptorsReady = false;
    _selfShadowPopulated = false;
    _shadowDirty = true;
    _selfShadowLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

void TerrainVK::DestroyDescriptorResources(VkDevice device)
{
    if (!device)
        return;
    if (_heightSampler) vkDestroySampler(device, _heightSampler, nullptr);
    if (_indexSampler) vkDestroySampler(device, _indexSampler, nullptr);
    if (_jitterSampler) vkDestroySampler(device, _jitterSampler, nullptr);
    if (_maskSampler) vkDestroySampler(device, _maskSampler, nullptr);
    if (_layerRepeatSampler) vkDestroySampler(device, _layerRepeatSampler, nullptr);
    if (_layerClampSampler) vkDestroySampler(device, _layerClampSampler, nullptr);
    _heightSampler = _indexSampler = _jitterSampler = _maskSampler = VK_NULL_HANDLE;
    _layerRepeatSampler = _layerClampSampler = VK_NULL_HANDLE;
    if (_descriptorPool) vkDestroyDescriptorPool(device, _descriptorPool, nullptr);
    _descriptorPool = VK_NULL_HANDLE;
    _descriptorSet = VK_NULL_HANDLE;
    if (_descriptorSetLayout) vkDestroyDescriptorSetLayout(device, _descriptorSetLayout, nullptr);
    _descriptorSetLayout = VK_NULL_HANDLE;
    _layerCapacity = 0;
    _telemetry = {};
    _gridIndexCount = 0;
}

void TerrainVK::DestroyVisualDescriptorResources(VkDevice device)
{
    _visualDescriptorsReady = false;
    if (!device)
        return;
    if (_visualDescriptorPool)
        vkDestroyDescriptorPool(device, _visualDescriptorPool, nullptr);
    _visualDescriptorPool = VK_NULL_HANDLE;
    _visualDescriptorSet = VK_NULL_HANDLE;
    if (_visualDescriptorSetLayout)
        vkDestroyDescriptorSetLayout(device, _visualDescriptorSetLayout, nullptr);
    _visualDescriptorSetLayout = VK_NULL_HANDLE;
}

void TerrainVK::DestroyShadowComputeResources(VkDevice device)
{
    if (!device)
        return;
    if (_shadowComputePipeline)
        vkDestroyPipeline(device, _shadowComputePipeline, nullptr);
    _shadowComputePipeline = VK_NULL_HANDLE;
    if (_shadowComputePipelineLayout)
        vkDestroyPipelineLayout(device, _shadowComputePipelineLayout, nullptr);
    _shadowComputePipelineLayout = VK_NULL_HANDLE;
    if (_shadowComputeDescriptorPool)
        vkDestroyDescriptorPool(device, _shadowComputeDescriptorPool, nullptr);
    _shadowComputeDescriptorPool = VK_NULL_HANDLE;
    _shadowComputeDescriptorSet = VK_NULL_HANDLE;
    if (_shadowComputeDescriptorSetLayout)
        vkDestroyDescriptorSetLayout(device, _shadowComputeDescriptorSetLayout, nullptr);
    _shadowComputeDescriptorSetLayout = VK_NULL_HANDLE;
    DestroyBuffer(device, _shadowSweepBuffer);
}
} // namespace Poseidon::vk
