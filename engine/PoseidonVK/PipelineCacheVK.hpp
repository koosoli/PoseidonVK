#pragma once

// PipelineCacheVK — lazy Vulkan graphics pipeline creation from
// RenderPassDescriptor-derived state keys.
//
// The scene uses a single pair of compiled shader modules (vertex + fragment)
// shared across all pipeline variants. The only variable parts per variant are
// cull, frontFace, depth, blend, and surface depth-bias state. These are hashed
// into a map key and a new VkPipeline is created on first use.
//
// Usage:
//   PipelineCacheVK cache;
//   cache.Init(device, renderPass, layout, vertModule, fragModule, vertInput,
//              inputAssembly, viewportState, multisampling);
//
//   VkPipeline pipeline = cache.Get(desc);   // creates lazily
//   cache.Destroy(device);                   // destroys all cached pipelines

#include <PoseidonVK/RenderStateVK.hpp>
#include <Poseidon/Graphics/Rendering/RenderPassDescriptor.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace Poseidon::vk
{

// Key: the pipeline-state axes that require distinct VkPipeline objects.
// Sampler, fog, lighting, texgen etc. are resolved in-shader via push constants
// or descriptors and do NOT require separate pipelines.
struct PipelineKeyVK
{
    render::CullMode cull = render::CullMode::Back;
    render::FrontFaceMode frontFace = render::FrontFaceMode::CW;
    render::DepthMode depth = render::DepthMode::Normal;
    render::BlendMode blend = render::BlendMode::Opaque;
    render::SurfaceMode surface = render::SurfaceMode::Default;

    bool operator==(const PipelineKeyVK& o) const noexcept
    {
        return cull == o.cull && frontFace == o.frontFace && depth == o.depth && blend == o.blend &&
               surface == o.surface;
    }
};

struct PipelineKeyHash
{
    std::size_t operator()(const PipelineKeyVK& k) const noexcept
    {
        // Pack five single-byte enum values and hash them together.
        std::uint64_t packed =
            (static_cast<std::uint64_t>(k.cull)) |
            (static_cast<std::uint64_t>(k.frontFace) << 8) |
            (static_cast<std::uint64_t>(k.depth) << 16) |
            (static_cast<std::uint64_t>(k.blend) << 24) |
            (static_cast<std::uint64_t>(k.surface) << 32);
        // FNV-1a mix
        std::size_t h = 2166136261u;
        h ^= packed;
        h *= 16777619u;
        return h;
    }
};

inline PipelineKeyVK KeyFromDescriptor(const render::RenderPassDescriptor& desc) noexcept
{
    PipelineKeyVK k;
    k.cull      = desc.cull;
    k.frontFace = desc.frontFace;
    k.depth     = desc.depth;
    k.blend     = desc.blend;
    k.surface   = desc.surface;
    return k;
}

class PipelineCacheVK
{
public:
    PipelineCacheVK() = default;

    // Must be called once before Get(). Stores all fixed pipeline state that
    // does not change between variants. Does NOT take ownership of the modules.
    void Init(VkDevice device,
              VkRenderPass renderPass,
              VkPipelineLayout layout,
              VkShaderModule vertModule,
              VkShaderModule fragModule,
              VkPipelineVertexInputStateCreateInfo vertexInput,
              VkPipelineInputAssemblyStateCreateInfo inputAssembly,
              VkPipelineViewportStateCreateInfo viewportState,
              VkPipelineMultisampleStateCreateInfo multisampling)
    {
        _device        = device;
        _renderPass    = renderPass;
        _layout        = layout;
        _vertModule    = vertModule;
        _fragModule    = fragModule;
        _vertexInput   = vertexInput;
        _inputAssembly = inputAssembly;
        _viewportState = viewportState;
        _multisampling = multisampling;

        // Make deep copies of pointers pointing to temporary stack variables
        if (vertexInput.pVertexBindingDescriptions && vertexInput.vertexBindingDescriptionCount > 0)
        {
            _bindingDescs.assign(vertexInput.pVertexBindingDescriptions,
                                 vertexInput.pVertexBindingDescriptions + vertexInput.vertexBindingDescriptionCount);
            _vertexInput.pVertexBindingDescriptions = _bindingDescs.data();
        }
        else
        {
            _vertexInput.pVertexBindingDescriptions = nullptr;
        }

        if (vertexInput.pVertexAttributeDescriptions && vertexInput.vertexAttributeDescriptionCount > 0)
        {
            _attributeDescs.assign(vertexInput.pVertexAttributeDescriptions,
                                   vertexInput.pVertexAttributeDescriptions + vertexInput.vertexAttributeDescriptionCount);
            _vertexInput.pVertexAttributeDescriptions = _attributeDescs.data();
        }
        else
        {
            _vertexInput.pVertexAttributeDescriptions = nullptr;
        }

        if (viewportState.pViewports && viewportState.viewportCount > 0)
        {
            _viewports.assign(viewportState.pViewports, viewportState.pViewports + viewportState.viewportCount);
            _viewportState.pViewports = _viewports.data();
        }
        else
        {
            _viewportState.pViewports = nullptr;
        }

        if (viewportState.pScissors && viewportState.scissorCount > 0)
        {
            _scissors.assign(viewportState.pScissors, viewportState.pScissors + viewportState.scissorCount);
            _viewportState.pScissors = _scissors.data();
        }
        else
        {
            _viewportState.pScissors = nullptr;
        }
    }

    // Returns (or creates) the VkPipeline for the given state key.
    // Returns VK_NULL_HANDLE on creation failure.
    VkPipeline Get(const PipelineKeyVK& key)
    {
        auto it = _cache.find(key);
        if (it != _cache.end())
            return it->second;

        VkPipeline pipeline = Create(key);
        _cache[key] = pipeline;
        return pipeline;
    }

    VkPipeline Get(const render::RenderPassDescriptor& desc)
    {
        return Get(KeyFromDescriptor(desc));
    }

    void Destroy(VkDevice device)
    {
        for (auto& [key, pipeline] : _cache)
        {
            if (device && pipeline)
                vkDestroyPipeline(device, pipeline, nullptr);
        }
        _cache.clear();
    }

private:
    VkPipeline Create(const PipelineKeyVK& key)
    {
        if (!_device || !_renderPass || !_layout || !_vertModule || !_fragModule)
            return VK_NULL_HANDLE;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = _vertModule;
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = _fragModule;
        stages[1].pName  = "main";

        VkPipelineRasterizationStateCreateInfo rasterizer =
            BuildRasterizationState(key.cull, key.frontFace, key.surface);
        VkPipelineDepthStencilStateCreateInfo depthStencil =
            BuildDepthStencilState(key.depth);
        VkPipelineColorBlendAttachmentState blendAttachment =
            BuildColorBlendAttachmentState(key.blend);

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments    = &blendAttachment;

        VkGraphicsPipelineCreateInfo info{};
        info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount          = 2;
        info.pStages             = stages;
        info.pVertexInputState   = &_vertexInput;
        info.pInputAssemblyState = &_inputAssembly;
        info.pViewportState      = &_viewportState;
        info.pRasterizationState = &rasterizer;
        info.pMultisampleState   = &_multisampling;
        info.pDepthStencilState  = &depthStencil;
        info.pColorBlendState    = &colorBlending;
        info.layout              = _layout;
        info.renderPass          = _renderPass;
        info.subpass             = 0;

        VkPipeline pipeline = VK_NULL_HANDLE;
        vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
        return pipeline;
    }

    VkDevice         _device        = VK_NULL_HANDLE;
    VkRenderPass     _renderPass    = VK_NULL_HANDLE;
    VkPipelineLayout _layout       = VK_NULL_HANDLE;
    VkShaderModule   _vertModule    = VK_NULL_HANDLE;
    VkShaderModule   _fragModule    = VK_NULL_HANDLE;

    VkPipelineVertexInputStateCreateInfo   _vertexInput{};
    VkPipelineInputAssemblyStateCreateInfo _inputAssembly{};
    VkPipelineViewportStateCreateInfo      _viewportState{};
    VkPipelineMultisampleStateCreateInfo   _multisampling{};

    std::vector<VkVertexInputBindingDescription>   _bindingDescs;
    std::vector<VkVertexInputAttributeDescription> _attributeDescs;
    std::vector<VkViewport>                        _viewports;
    std::vector<VkRect2D>                          _scissors;

    std::unordered_map<PipelineKeyVK, VkPipeline, PipelineKeyHash> _cache;
};

} // namespace Poseidon::vk
