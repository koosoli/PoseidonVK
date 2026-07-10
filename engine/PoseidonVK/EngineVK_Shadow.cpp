// EngineVK_Shadow.cpp
// Cascade shadow-map implementation for PoseidonVK.
// Mirrors the GL33 path (EngineGL33_ShadowDepth.cpp) using Vulkan idioms:
//  - Depth-only 2D-array image (kShadowCascades layers)
//  - One VkRenderPass, per-layer VkFramebuffer
//  - Solid caster pipeline: vertex-only, push-constant lightVP
//  - Alpha caster pipeline: vertex + fragment (alpha-discard), same push constant
//  - One-shot command buffer per frame (submitted before the main CB)
//  - Shadow UBO fields filled in UpdateShadowFrameConstants -> UploadFrameConstants

#include <PoseidonVK/EngineVK.hpp>
#include <PoseidonVK/BufferVK.hpp>
#include <PoseidonVK/DescriptorBindingsVK.hpp>
#include <PoseidonVK/TextureVK.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace Poseidon
{
namespace
{
const char* VkResultName(VkResult result)
{
    switch (result)
    {
        case VK_SUCCESS:                    return "VK_SUCCESS";
        case VK_ERROR_OUT_OF_HOST_MEMORY:   return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_FEATURE_NOT_PRESENT:  return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_MEMORY_MAP_FAILED:    return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_LAYER_NOT_PRESENT:    return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:  return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_DEVICE_LOST:          return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_OUT_OF_DATE_KHR:      return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_OUT_OF_POOL_MEMORY:   return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_SUBOPTIMAL_KHR:             return "VK_SUBOPTIMAL_KHR";
        default:                            return "VkResult";
    }
}
} // namespace
} // namespace Poseidon

// Shader sources embedded at compile time via GenerateShaderHeader.cmake
namespace
{
constexpr const char kShadowDepthVertSrc[] =
#include <PoseidonVK/Shaders/shadowDepth.vert.glsl.hpp>
;

constexpr const char kShadowAlphaVertSrc[] =
#include <PoseidonVK/Shaders/shadowDepthAlpha.vert.glsl.hpp>
;

constexpr const char kShadowAlphaFragSrc[] =
#include <PoseidonVK/Shaders/shadowDepthAlpha.frag.glsl.hpp>
;
} // namespace

namespace Poseidon
{

// ---------------------------------------------------------------------------
// Public overrides -- shadow enable / tuning / sun factor
// ---------------------------------------------------------------------------

void EngineVK::SetShadowMapsEnabled(bool enabled)
{
    _shadowEnabled = enabled;
}

bool EngineVK::ShadowMapsEnabled() const
{
    return _shadowEnabled;
}

Engine::ShadowMapTuning EngineVK::GetShadowMapTuning() const
{
    return _shadowTuning;
}

void EngineVK::SetShadowMapTuning(const ShadowMapTuning& tuning)
{
    _shadowTuning = tuning;
}

void EngineVK::SetShadowMapSunFactor(float f)
{
    _shadowSunFactor = f;
}

bool EngineVK::DumpShadowMap(const char* /*path*/)
{
    // Not implemented; readback requires staging buffer + host-visible copy.
    return false;
}

bool EngineVK::ShadowMapCacheSelfTest()
{
    return true;
}

// ---------------------------------------------------------------------------
// UpdateShadowFrameConstants
// Fills the shadow UBO fields in _lastFrameConstants before UploadFrameConstants
// copies them to the GPU-visible buffer.
// ---------------------------------------------------------------------------

void EngineVK::UpdateShadowFrameConstants()
{
    const bool active = _shadowEnabled && _shadowMapActive && _shadowSunFactor > 0.01f;

    _lastFrameConstants.shadowCtl[0] = active ? 1.0f : 0.0f;
    _lastFrameConstants.shadowCtl[1] = _shadowTuning.biasBase;
    _lastFrameConstants.shadowCtl[2] = _shadowTuning.darkness * _shadowSunFactor;
    _lastFrameConstants.shadowCtl[3] =
        (_shadowMapRes > 0) ? (1.0f / static_cast<float>(_shadowMapRes)) : 0.0f;

    if (active)
    {
        const int nC = std::min(_shadowCascades, kShadowCascades);
        for (int c = 0; c < nC; ++c)
            std::memcpy(_lastFrameConstants.cascadeVP[c], _shadowMapVP + c * 16,
                        sizeof(float) * 16);

        for (int c = 0; c < kShadowCascades; ++c)
            _lastFrameConstants.cascadeSplits[c] = (c < nC) ? _shadowSplits[c] : 0.0f;

        _lastFrameConstants.cascadeCtl[0] = static_cast<float>(nC);
        _lastFrameConstants.cascadeCtl[1] = _shadowTuning.fadeRange;
        _lastFrameConstants.cascadeCtl[2] = _shadowTuning.biasBase;
        _lastFrameConstants.cascadeCtl[3] = static_cast<float>(_shadowOmniCount);

        _lastFrameConstants.camFwd[0] = _shadowCamFwd[0];
        _lastFrameConstants.camFwd[1] = _shadowCamFwd[1];
        _lastFrameConstants.camFwd[2] = _shadowCamFwd[2];
        _lastFrameConstants.camFwd[3] = 0.0f;
    }
}

// ---------------------------------------------------------------------------
// EnsureShadowResources
// Idempotent: returns true immediately if res/layers match current allocation.
// Otherwise destroys old resources and re-allocates.
// ---------------------------------------------------------------------------

bool EngineVK::EnsureShadowResources(int res, int layers)
{
    if (!_device || !_physicalDevice)
        return false;

    layers = std::min(layers, kShadowCascades);

    // Already allocated at the right dimensions?
    if (_shadowDepthImage.image != VK_NULL_HANDLE &&
        _shadowMapRes == res && _shadowCascades == layers)
        return true;

    DestroyShadowResources();

    // 1. Depth format
    const VkFormat depthFormat = FindDepthFormat();
    if (depthFormat == VK_FORMAT_UNDEFINED)
    {
        LOG_ERROR(Graphics, "VK shadow: no depth format available");
        return false;
    }

    // 2. Depth image array
    {
        VkImageCreateInfo ii{};
        ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType     = VK_IMAGE_TYPE_2D;
        ii.format        = depthFormat;
        ii.extent        = {static_cast<uint32_t>(res), static_cast<uint32_t>(res), 1};
        ii.mipLevels     = 1;
        ii.arrayLayers   = static_cast<uint32_t>(layers);
        ii.samples       = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ii.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkResult r = vkCreateImage(_device, &ii, nullptr, &_shadowDepthImage.image);
        if (r != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "VK shadow: vkCreateImage failed: {}", VkResultName(r));
            return false;
        }
        _shadowDepthImage.format    = depthFormat;
        _shadowDepthImage.width     = static_cast<uint32_t>(res);
        _shadowDepthImage.height    = static_cast<uint32_t>(res);
        _shadowDepthImage.mipLevels = 1;

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(_device, _shadowDepthImage.image, &req);

        const uint32_t memIdx = vk::FindMemoryType(_physicalDevice, req.memoryTypeBits,
                                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memIdx == vk::kInvalidMemoryType)
        {
            LOG_ERROR(Graphics, "VK shadow: no device-local memory for depth array");
            vkDestroyImage(_device, _shadowDepthImage.image, nullptr);
            _shadowDepthImage.image = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryAllocateInfo mai{};
        mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize  = req.size;
        mai.memoryTypeIndex = memIdx;
        r = vkAllocateMemory(_device, &mai, nullptr, &_shadowDepthImage.memory);
        if (r != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "VK shadow: vkAllocateMemory failed: {}", VkResultName(r));
            vkDestroyImage(_device, _shadowDepthImage.image, nullptr);
            _shadowDepthImage.image = VK_NULL_HANDLE;
            return false;
        }

        r = vkBindImageMemory(_device, _shadowDepthImage.image, _shadowDepthImage.memory, 0);
        if (r != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "VK shadow: vkBindImageMemory failed: {}", VkResultName(r));
            DestroyShadowResources();
            return false;
        }

        // Array view -- sampled in the fragment shader as sampler2DArray
        VkImageViewCreateInfo vi{};
        vi.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image                           = _shadowDepthImage.image;
        vi.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        vi.format                          = depthFormat;
        vi.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
        vi.subresourceRange.baseMipLevel   = 0;
        vi.subresourceRange.levelCount     = 1;
        vi.subresourceRange.baseArrayLayer = 0;
        vi.subresourceRange.layerCount     = static_cast<uint32_t>(layers);

        r = vkCreateImageView(_device, &vi, nullptr, &_shadowDepthImage.view);
        if (r != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "VK shadow: vkCreateImageView (array) failed: {}", VkResultName(r));
            DestroyShadowResources();
            return false;
        }
    }

    // 3. Per-layer image views (used as framebuffer attachments)
    for (int i = 0; i < layers; ++i)
    {
        VkImageViewCreateInfo vi{};
        vi.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image                           = _shadowDepthImage.image;
        vi.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        vi.format                          = _shadowDepthImage.format;
        vi.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
        vi.subresourceRange.baseMipLevel   = 0;
        vi.subresourceRange.levelCount     = 1;
        vi.subresourceRange.baseArrayLayer = static_cast<uint32_t>(i);
        vi.subresourceRange.layerCount     = 1;

        const VkResult r = vkCreateImageView(_device, &vi, nullptr, &_shadowCascadeViews[i]);
        if (r != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "VK shadow: vkCreateImageView (layer {}) failed: {}", i,
                      VkResultName(r));
            DestroyShadowResources();
            return false;
        }
    }

    // 4. Shadow sampler (linear, clamp-to-edge, no comparison for manual PCF)
    {
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_LINEAR;
        si.minFilter    = VK_FILTER_LINEAR;
        si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.minLod       = 0.0f;
        si.maxLod       = 0.0f;
        si.compareEnable = VK_FALSE;

        const VkResult r = vkCreateSampler(_device, &si, nullptr, &_shadowSampler);
        if (r != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "VK shadow: vkCreateSampler failed: {}", VkResultName(r));
            DestroyShadowResources();
            return false;
        }
    }

    // 5. Depth-only render pass
    //    initialLayout = UNDEFINED -> clear on first use
    //    finalLayout   = SHADER_READ_ONLY_OPTIMAL -> ready for sampling
    {
        VkAttachmentDescription depth{};
        depth.format         = _shadowDepthImage.format;
        depth.samples        = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 0;
        depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.pDepthStencilAttachment = &depthRef;

        // Ensure depth writes complete before any fragment shader reads them
        VkSubpassDependency dep{};
        dep.srcSubpass     = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass     = 0;
        dep.srcStageMask   = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep.dstStageMask   = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.srcAccessMask  = VK_ACCESS_SHADER_READ_BIT;
        dep.dstAccessMask  = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkSubpassDependency dep2{};
        dep2.srcSubpass     = 0;
        dep2.dstSubpass     = VK_SUBPASS_EXTERNAL;
        dep2.srcStageMask   = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dep2.dstStageMask   = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep2.srcAccessMask  = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dep2.dstAccessMask  = VK_ACCESS_SHADER_READ_BIT;
        dep2.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        const VkSubpassDependency deps[2] = {dep, dep2};

        VkRenderPassCreateInfo rpi{};
        rpi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpi.attachmentCount = 1;
        rpi.pAttachments    = &depth;
        rpi.subpassCount    = 1;
        rpi.pSubpasses      = &subpass;
        rpi.dependencyCount = 2;
        rpi.pDependencies   = deps;

        const VkResult r = vkCreateRenderPass(_device, &rpi, nullptr, &_shadowRenderPass);
        if (r != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "VK shadow: vkCreateRenderPass failed: {}", VkResultName(r));
            DestroyShadowResources();
            return false;
        }
    }

    // 6. Per-cascade framebuffers
    for (int i = 0; i < layers; ++i)
    {
        VkFramebufferCreateInfo fbi{};
        fbi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbi.renderPass      = _shadowRenderPass;
        fbi.attachmentCount = 1;
        fbi.pAttachments    = &_shadowCascadeViews[i];
        fbi.width           = static_cast<uint32_t>(res);
        fbi.height          = static_cast<uint32_t>(res);
        fbi.layers          = 1;

        const VkResult r = vkCreateFramebuffer(_device, &fbi, nullptr, &_shadowFramebuffers[i]);
        if (r != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "VK shadow: vkCreateFramebuffer (cascade {}) failed: {}", i,
                      VkResultName(r));
            DestroyShadowResources();
            return false;
        }
    }

    // 7. Shadow depth pipelines (solid + alpha)
    if (!CreateShadowDepthPipeline())
    {
        DestroyShadowResources();
        return false;
    }

    // 8. Update frame descriptor set binding 2 with the shadow sampler+image
    if (_frameDescriptorSet != VK_NULL_HANDLE)
    {
        VkDescriptorImageInfo imgInfo{};
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgInfo.imageView   = _shadowDepthImage.view;
        imgInfo.sampler     = _shadowSampler;

        VkWriteDescriptorSet w = vk::MakeShadowMapDescriptorWrite(_frameDescriptorSet, &imgInfo);
        vkUpdateDescriptorSets(_device, 1, &w, 0, nullptr);
    }

    _shadowMapRes   = res;
    _shadowCascades = layers;
    LOG_INFO(Graphics, "VK shadow: allocated {}x{} depth array ({} cascades, format {})",
             res, res, layers, static_cast<int>(_shadowDepthImage.format));
    return true;
}

// ---------------------------------------------------------------------------
// CreateShadowDepthPipeline
// ---------------------------------------------------------------------------

bool EngineVK::CreateShadowDepthPipeline()
{
    // Push-constant: one mat4 (lightVP) = 64 bytes, vertex-stage only
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(float) * 16;

    // --- Solid pipeline layout (no descriptor sets needed) ---
    {
        VkPipelineLayoutCreateInfo li{};
        li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges    = &pcRange;

        const VkResult r = vkCreatePipelineLayout(_device, &li, nullptr,
                                                   &_shadowDepthPipelineLayout);
        if (r != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "VK shadow: solid pipeline layout failed: {}", VkResultName(r));
            return false;
        }
    }

    // --- Alpha pipeline layout (set 0 = per-texture sampler) ---
    {
        VkPipelineLayoutCreateInfo li{};
        li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.setLayoutCount         = 1;
        li.pSetLayouts            = &_textureDescriptorSetLayout;
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges    = &pcRange;

        const VkResult r = vkCreatePipelineLayout(_device, &li, nullptr,
                                                   &_shadowAlphaPipelineLayout);
        if (r != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "VK shadow: alpha pipeline layout failed: {}", VkResultName(r));
            return false;
        }
    }

    // --- Compile shaders (CompileShader takes EShLanguage cast to int) ---
    // EShLangVertex = 0, EShLangFragment = 4
    constexpr int kVertex   = 0;
    constexpr int kFragment = 4;

    auto makeModule = [&](const char* src, int stage,
                          VkShaderModule& mod, const char* name) -> bool
    {
        std::vector<uint32_t> spirv;
        std::string err;
        if (!CompileShader(src, stage, spirv, err))
        {
            LOG_ERROR(Graphics, "VK shadow: {} compile failed: {}", name, err);
            return false;
        }
        VkShaderModuleCreateInfo ci{};
        ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = spirv.size() * sizeof(uint32_t);
        ci.pCode    = spirv.data();
        const VkResult r = vkCreateShaderModule(_device, &ci, nullptr, &mod);
        if (r != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "VK shadow: vkCreateShaderModule ({}) failed: {}", name,
                      VkResultName(r));
            return false;
        }
        return true;
    };

    if (!makeModule(kShadowDepthVertSrc, kVertex,   _shadowDepthVertexModule,  "shadowDepth.vert"))
        return false;
    if (!makeModule(kShadowAlphaVertSrc, kVertex,   _shadowAlphaVertexModule,  "shadowAlpha.vert"))
        return false;
    if (!makeModule(kShadowAlphaFragSrc, kFragment, _shadowAlphaFragmentModule,"shadowAlpha.frag"))
        return false;

    // --- Common fixed-function state ---
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_FRONT_BIT; // front-face culling reduces shadow acne
    rs.frontFace   = VK_FRONT_FACE_CLOCKWISE; // flipped due to negative viewport height
    rs.lineWidth   = 1.0f;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth-only pass -- no colour attachments
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

    const VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates    = dynStates;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    // --- Solid pipeline: vec3 xyz only ---
    {
        VkVertexInputBindingDescription bind{};
        bind.binding   = 0;
        bind.stride    = sizeof(float) * 3;
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attr{};
        attr.binding  = 0;
        attr.location = 0;
        attr.format   = VK_FORMAT_R32G32B32_SFLOAT;
        attr.offset   = 0;

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount   = 1;
        vi.pVertexBindingDescriptions      = &bind;
        vi.vertexAttributeDescriptionCount = 1;
        vi.pVertexAttributeDescriptions    = &attr;

        VkPipelineShaderStageCreateInfo stage{};
        stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stage.module = _shadowDepthVertexModule;
        stage.pName  = "main";

        VkGraphicsPipelineCreateInfo gpi{};
        gpi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gpi.stageCount          = 1;
        gpi.pStages             = &stage;
        gpi.pVertexInputState   = &vi;
        gpi.pInputAssemblyState = &ia;
        gpi.pViewportState      = &vp;
        gpi.pRasterizationState = &rs;
        gpi.pMultisampleState   = &ms;
        gpi.pDepthStencilState  = &ds;
        gpi.pColorBlendState    = &cb;
        gpi.pDynamicState       = &dyn;
        gpi.layout              = _shadowDepthPipelineLayout;
        gpi.renderPass          = _shadowRenderPass;
        gpi.subpass             = 0;

        const VkResult r = vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &gpi,
                                                      nullptr, &_shadowDepthPipeline);
        if (r != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "VK shadow: solid pipeline failed: {}", VkResultName(r));
            return false;
        }
    }

    // --- Alpha pipeline: vec3 xyz + vec2 uv ---
    {
        VkVertexInputBindingDescription bind{};
        bind.binding   = 0;
        bind.stride    = sizeof(float) * 5;
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attrs[2]{};
        attrs[0].binding  = 0;
        attrs[0].location = 0;
        attrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset   = 0;
        attrs[1].binding  = 0;
        attrs[1].location = 1;
        attrs[1].format   = VK_FORMAT_R32G32_SFLOAT;
        attrs[1].offset   = sizeof(float) * 3;

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount   = 1;
        vi.pVertexBindingDescriptions      = &bind;
        vi.vertexAttributeDescriptionCount = 2;
        vi.pVertexAttributeDescriptions    = attrs;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = _shadowAlphaVertexModule;
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = _shadowAlphaFragmentModule;
        stages[1].pName  = "main";

        VkGraphicsPipelineCreateInfo gpi{};
        gpi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gpi.stageCount          = 2;
        gpi.pStages             = stages;
        gpi.pVertexInputState   = &vi;
        gpi.pInputAssemblyState = &ia;
        gpi.pViewportState      = &vp;
        gpi.pRasterizationState = &rs;
        gpi.pMultisampleState   = &ms;
        gpi.pDepthStencilState  = &ds;
        gpi.pColorBlendState    = &cb;
        gpi.pDynamicState       = &dyn;
        gpi.layout              = _shadowAlphaPipelineLayout;
        gpi.renderPass          = _shadowRenderPass;
        gpi.subpass             = 0;

        const VkResult r = vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &gpi,
                                                      nullptr, &_shadowAlphaPipeline);
        if (r != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "VK shadow: alpha pipeline failed: {}", VkResultName(r));
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// DestroyShadowResources
// ---------------------------------------------------------------------------

void EngineVK::DestroyShadowResources()
{
    if (!_device)
        return;

    vkDeviceWaitIdle(_device);

    if (_shadowAlphaPipeline)
    {
        vkDestroyPipeline(_device, _shadowAlphaPipeline, nullptr);
        _shadowAlphaPipeline = VK_NULL_HANDLE;
    }
    if (_shadowAlphaPipelineLayout)
    {
        vkDestroyPipelineLayout(_device, _shadowAlphaPipelineLayout, nullptr);
        _shadowAlphaPipelineLayout = VK_NULL_HANDLE;
    }
    if (_shadowAlphaFragmentModule)
    {
        vkDestroyShaderModule(_device, _shadowAlphaFragmentModule, nullptr);
        _shadowAlphaFragmentModule = VK_NULL_HANDLE;
    }
    if (_shadowAlphaVertexModule)
    {
        vkDestroyShaderModule(_device, _shadowAlphaVertexModule, nullptr);
        _shadowAlphaVertexModule = VK_NULL_HANDLE;
    }
    if (_shadowDepthPipeline)
    {
        vkDestroyPipeline(_device, _shadowDepthPipeline, nullptr);
        _shadowDepthPipeline = VK_NULL_HANDLE;
    }
    if (_shadowDepthPipelineLayout)
    {
        vkDestroyPipelineLayout(_device, _shadowDepthPipelineLayout, nullptr);
        _shadowDepthPipelineLayout = VK_NULL_HANDLE;
    }
    if (_shadowDepthVertexModule)
    {
        vkDestroyShaderModule(_device, _shadowDepthVertexModule, nullptr);
        _shadowDepthVertexModule = VK_NULL_HANDLE;
    }

    for (int i = 0; i < kShadowCascades; ++i)
    {
        if (_shadowFramebuffers[i])
        {
            vkDestroyFramebuffer(_device, _shadowFramebuffers[i], nullptr);
            _shadowFramebuffers[i] = VK_NULL_HANDLE;
        }
        if (_shadowCascadeViews[i])
        {
            vkDestroyImageView(_device, _shadowCascadeViews[i], nullptr);
            _shadowCascadeViews[i] = VK_NULL_HANDLE;
        }
    }

    if (_shadowSampler)
    {
        vkDestroySampler(_device, _shadowSampler, nullptr);
        _shadowSampler = VK_NULL_HANDLE;
    }

    if (_shadowRenderPass)
    {
        vkDestroyRenderPass(_device, _shadowRenderPass, nullptr);
        _shadowRenderPass = VK_NULL_HANDLE;
    }

    vk::DestroyImage(_device, _shadowDepthImage);
    vk::DestroyBuffer(_device, _shadowVertexBuffer);

    _shadowMapRes    = 0;
    _shadowCascades  = 0;
    _shadowMapActive = false;
}

// ---------------------------------------------------------------------------
// RenderShadowDepthScene
// Called from the CPU-side scene pass (SceneShadowPass.cpp) before the main
// frame CB is recorded.  We record + submit a one-shot CB so the depth map
// is ready when the scene CB samples it.
// ---------------------------------------------------------------------------

void EngineVK::RenderShadowDepthScene(
    const float*           lightVPs,
    const float*           splitViewDist,
    const float*           camFwd3,
    int                    numCascades,
    int                    omniCount,
    int                    res,
    const ShadowCasterSet& casters)
{
    if (!_shadowEnabled || !_device || !_commandPool || !_graphicsQueue)
        return;

    numCascades = std::min(numCascades, kShadowCascades);
    if (numCascades <= 0)
        return;

    if (!EnsureShadowResources(res, numCascades))
        return;

    // --- Upload vertex data (solid then alpha) to host-visible buffer ---
    const VkDeviceSize solidBytes = static_cast<VkDeviceSize>(casters.solidVertexCount) *
                                    sizeof(float) * 3;
    const VkDeviceSize alphaBytes = static_cast<VkDeviceSize>(casters.alphaVertexCount) *
                                    sizeof(float) * 5;
    const VkDeviceSize totalBytes = solidBytes + alphaBytes;

    if (totalBytes > 0)
    {
        if (_shadowVertexBuffer.size < totalBytes)
        {
            vk::DestroyBuffer(_device, _shadowVertexBuffer);
            const VkResult r = vk::CreateHostVisibleBuffer(
                _physicalDevice, _device, totalBytes,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, _shadowVertexBuffer);
            if (r != VK_SUCCESS)
            {
                LOG_ERROR(Graphics, "VK shadow: vertex buffer alloc failed: {}", VkResultName(r));
                return;
            }
        }

        auto* dst = static_cast<uint8_t*>(_shadowVertexBuffer.mapped);
        if (solidBytes > 0 && casters.solidXYZ)
            std::memcpy(dst, casters.solidXYZ, solidBytes);
        if (alphaBytes > 0 && casters.alphaXYZUV)
            std::memcpy(dst + solidBytes, casters.alphaXYZUV, alphaBytes);
    }

    // --- One-shot command buffer ---
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = _commandPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(_device, &ai, &cb) != VK_SUCCESS)
        return;

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    // --- One render pass per cascade ---
    for (int c = 0; c < numCascades; ++c)
    {
        VkClearValue clearVal{};
        clearVal.depthStencil.depth   = 1.0f;
        clearVal.depthStencil.stencil = 0;

        VkRenderPassBeginInfo rpi{};
        rpi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpi.renderPass        = _shadowRenderPass;
        rpi.framebuffer       = _shadowFramebuffers[c];
        rpi.renderArea.offset = {0, 0};
        rpi.renderArea.extent = {static_cast<uint32_t>(res), static_cast<uint32_t>(res)};
        rpi.clearValueCount   = 1;
        rpi.pClearValues      = &clearVal;

        vkCmdBeginRenderPass(cb, &rpi, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vkVP{};
        vkVP.x        = 0.0f;
        vkVP.y        = static_cast<float>(res);
        vkVP.width    = static_cast<float>(res);
        vkVP.height   = -static_cast<float>(res);
        vkVP.minDepth = 0.0f;
        vkVP.maxDepth = 1.0f;
        vkCmdSetViewport(cb, 0, 1, &vkVP);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {static_cast<uint32_t>(res), static_cast<uint32_t>(res)};
        vkCmdSetScissor(cb, 0, 1, &scissor);

        const float* vp16 = lightVPs + c * 16;

        // Solid casters
        if (casters.solidVertexCount > 0 &&
            _shadowDepthPipeline != VK_NULL_HANDLE &&
            _shadowVertexBuffer.buffer != VK_NULL_HANDLE)
        {
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, _shadowDepthPipeline);
            vkCmdPushConstants(cb, _shadowDepthPipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16, vp16);
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cb, 0, 1, &_shadowVertexBuffer.buffer, &offset);
            vkCmdDraw(cb, static_cast<uint32_t>(casters.solidVertexCount), 1, 0, 0);
        }

        // Alpha-cutout casters
        if (casters.alphaBatchCount > 0 &&
            _shadowAlphaPipeline != VK_NULL_HANDLE &&
            _shadowVertexBuffer.buffer != VK_NULL_HANDLE)
        {
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, _shadowAlphaPipeline);
            vkCmdPushConstants(cb, _shadowAlphaPipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16, vp16);
            const VkDeviceSize alphaOff = solidBytes;
            vkCmdBindVertexBuffers(cb, 0, 1, &_shadowVertexBuffer.buffer, &alphaOff);

            for (int b = 0; b < casters.alphaBatchCount; ++b)
            {
                const ShadowCasterBatch& batch = casters.alphaBatches[b];
                if (batch.vertexCount <= 0)
                    continue;

                // Bind the caster texture descriptor set
                VkDescriptorSet texSet = VK_NULL_HANDLE;
                if (batch.texture)
                {
                    const auto* tex = static_cast<const TextureVK*>(batch.texture);
                    texSet = tex->GetDescriptorSet();
                }
                if (texSet == VK_NULL_HANDLE && _fallbackWhiteTexture)
                    texSet = _fallbackWhiteTexture->GetDescriptorSet();
                if (texSet != VK_NULL_HANDLE)
                    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            _shadowAlphaPipelineLayout, 0, 1,
                                            &texSet, 0, nullptr);

                vkCmdDraw(cb, static_cast<uint32_t>(batch.vertexCount), 1,
                          static_cast<uint32_t>(batch.firstVertex), 0);
            }
        }

        vkCmdEndRenderPass(cb);
    }

    vkEndCommandBuffer(cb);

    // Submit and wait -- depth map must be readable before the main CB runs
    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cb;
    vkQueueSubmit(_graphicsQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(_graphicsQueue);

    vkFreeCommandBuffers(_device, _commandPool, 1, &cb);

    // Persist cascade data for UpdateShadowFrameConstants
    _shadowCascades  = numCascades;
    _shadowOmniCount = omniCount;
    _shadowCamFwd[0] = camFwd3[0];
    _shadowCamFwd[1] = camFwd3[1];
    _shadowCamFwd[2] = camFwd3[2];
    for (int c = 0; c < numCascades; ++c)
    {
        std::memcpy(_shadowMapVP + c * 16, lightVPs + c * 16, sizeof(float) * 16);
        _shadowSplits[c] = splitViewDist[c];
    }
    _shadowMapActive = true;
}

} // namespace Poseidon
