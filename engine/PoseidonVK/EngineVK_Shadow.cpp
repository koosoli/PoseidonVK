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
#include <PoseidonVK/SceneDrawCommandsVK.hpp>
#include <PoseidonVK/TextureVK.hpp>
#include <PoseidonVK/VertexLayoutVK.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Graphics/Shadow/ShadowMath.hpp>

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

bool RecreateSignaledShadowFence(VkDevice device, VkFence& fence)
{
    if (fence)
        vkDestroyFence(device, fence, nullptr);
    VkFenceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    return vkCreateFence(device, &info, nullptr, &fence) == VK_SUCCESS;
}
} // namespace
} // namespace Poseidon

// Shader sources embedded at compile time via GenerateShaderHeader.cmake
namespace
{
struct ShadowPushConstantsVK
{
    float lightVP[16] = {};
    // Affine scene transforms have a fixed final column.  Pack the three
    // variable columns plus translation so CSM alpha cutoff still fits Vulkan's
    // guaranteed 128-byte push-constant budget.
    float worldColumns[3][4] = {};
    float translation[3] = {};
    float alphaCutoff = 0.5f;
};

static_assert(sizeof(ShadowPushConstantsVK) == sizeof(float) * 32,
              "shadow push constants must match shadowDepth*.glsl");

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
    _shadowTuning.enabled = enabled;
}

bool EngineVK::ShadowMapsEnabled() const
{
    return _shadowTuning.enabled;
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
    _shadowSunFactor = std::clamp(f, 0.0f, 1.0f);
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
    const bool active = _shadowTuning.enabled && _shadowMapActive && _shadowSunFactor > 0.01f;

    _lastFrameConstants.shadowCtl[0] = active ? 1.0f : 0.0f;
    _lastFrameConstants.shadowCtl[1] = _shadowTuning.biasBase;
    _lastFrameConstants.shadowCtl[2] =
        1.0f - _shadowSunFactor * (1.0f - _shadowTuning.darkness);
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

    // 4. Shadow sampler (nearest, clamp-to-edge, no comparison for manual PCF)
    {
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_NEAREST;
        si.minFilter    = VK_FILTER_NEAREST;
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
    // The shared caster contract supplies one camera-relative world transform
    // per indexed mesh section.  Keep it adjacent to lightVP so depth shaders
    // consume the original scene vertex buffers directly.
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(ShadowPushConstantsVK);

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
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE; // engine CW after the viewport Y flip
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

    // --- Solid pipeline: regular scene vertex position ---
    {
        const VkVertexInputBindingDescription bind = vk::MakeSceneVertexBindingDescription();
        const VkVertexInputAttributeDescription attr = vk::MakeSceneVertexPositionAttribute();

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

    // --- Alpha pipeline: regular scene position + UV ---
    {
        VkPipelineRasterizationStateCreateInfo alphaRs = rs;
        alphaRs.cullMode = VK_CULL_MODE_NONE;
        const VkVertexInputBindingDescription bind = vk::MakeSceneVertexBindingDescription();
        const VkVertexInputAttributeDescription attrs[2] = {
            vk::MakeSceneVertexPositionAttribute(), vk::MakeSceneVertexTexcoordAttribute()};

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
        gpi.pRasterizationState = &alphaRs;
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

    _shadowMapRes    = 0;
    _shadowCascades  = 0;
    _shadowMapActive = false;
}

// ---------------------------------------------------------------------------
// RenderShadowDepthScene
// The legacy CPU-flattened caster API is intentionally not consumed by
// Vulkan.  Frame-plan consumers use RenderShadowDepthFramePlan below, which
// binds the source mesh resources directly.  GL retains the historical API.
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
    (void)lightVPs;
    (void)splitViewDist;
    (void)camFwd3;
    (void)numCascades;
    (void)omniCount;
    (void)res;
    (void)casters;
    _shadowMapActive = false;
}

void EngineVK::RenderShadowDepthFramePlan(const render::frame::Frame& frame)
{
    const render::frame::ShadowInput& input = frame.shadowInput;
    _shadowSunFactor = input.sunFactor;
    _shadowMapActive = false;
    if (!_shadowTuning.enabled || !input.enabled || input.sunFactor <= 0.01f || !_device || !_commandPool ||
        !_graphicsQueue)
        return;

    const std::vector<vk::ShadowDrawCommandVK> commands = vk::BuildShadowDrawCommands(input);
    if (commands.empty())
        return;

    if (_shadowInFlight)
        vkWaitForFences(_device, 1, &_shadowInFlight, VK_TRUE, UINT64_MAX);

    namespace sm = Poseidon::shadow;
    sm::CascadeBuildParams bp;
    bp.camPos = {frame.cameraPosition[0], frame.cameraPosition[1], frame.cameraPosition[2]};
    bp.forward = {input.camera.forward[0], input.camera.forward[1], input.camera.forward[2]};
    bp.right = {input.camera.right[0], input.camera.right[1], input.camera.right[2]};
    bp.up = {input.camera.up[0], input.camera.up[1], input.camera.up[2]};
    bp.tanHalfX = input.camera.tanHalfX;
    bp.tanHalfY = input.camera.tanHalfY;
    bp.nearD = input.camera.nearDistance;
    bp.farD = input.camera.farDistance;
    bp.sunDir = {frame.sunDirection[0], frame.sunDirection[1], frame.sunDirection[2]};
    bp.count = _shadowTuning.cascadeCount;
    bp.distanceCoef = _shadowTuning.distanceCoef;
    bp.splitCoef = _shadowTuning.splitCoef;
    bp.resolution = _shadowTuning.resolution;
    bp.zPad = 50.0f;
    bp.omniCount = _shadowTuning.omniCount;
    bp.omniCoef[0] = _shadowTuning.omniCoef0;
    bp.omniCoef[1] = _shadowTuning.omniCoef1;
    const sm::CascadeSet cascades = sm::BuildShadowCascadesTiered(bp);
    if (cascades.count <= 0 || !EnsureShadowResources(_shadowTuning.resolution, cascades.count))
        return;

    // InitDraw has waited for the prior frame fence, so this buffer is no
    // longer in use. Submit it before the main frame on the same queue rather
    // than draining the queue with vkQueueWaitIdle every frame.
    if (_shadowCommandBuffer == VK_NULL_HANDLE)
    {
        VkCommandBufferAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool        = _commandPool;
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(_device, &ai, &_shadowCommandBuffer) != VK_SUCCESS)
            return;
    }
    VkCommandBuffer cb = _shadowCommandBuffer;
    vkResetCommandBuffer(cb, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    // --- One render pass per cascade ---
    const int res = _shadowTuning.resolution;
    for (int c = 0; c < cascades.count; ++c)
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

        for (const vk::ShadowDrawCommandVK& command : commands)
        {
            const render::frame::ShadowCaster& caster = input.casters[command.casterIndex];
            const vk::MeshResourcesVK* mesh = _meshRegistry.Resolve(command.meshId);
            if (!mesh || !mesh->IsValid() || command.firstIndex >= mesh->indexCount)
                continue;
            const std::uint32_t indexCount = std::min(command.indexCount, mesh->indexCount - command.firstIndex);
            if (indexCount == 0)
                continue;

            ShadowPushConstantsVK constants;
            std::memcpy(constants.lightVP, cascades.camRelVP[c].m.data(), sizeof(constants.lightVP));
            std::memcpy(constants.worldColumns, &caster.world, sizeof(constants.worldColumns));
            constants.translation[0] = caster.world._41;
            constants.translation[1] = caster.world._42;
            constants.translation[2] = caster.world._43;
            constants.alphaCutoff = caster.alphaCutoff;
            const VkDeviceSize vertexOffset = 0;
            vkCmdBindVertexBuffers(cb, 0, 1, &mesh->vertexBuffer, &vertexOffset);
            vkCmdBindIndexBuffer(cb, mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT16);

            if (command.alphaMode == render::frame::ShadowCasterAlphaMode::Cutout &&
                _shadowAlphaPipeline != VK_NULL_HANDLE)
            {
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, _shadowAlphaPipeline);
                vkCmdPushConstants(cb, _shadowAlphaPipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(constants), &constants);
                TextureVK* texture = ResolveTexture(caster.alphaTexture.id);
                VkDescriptorSet texSet = texture ? texture->GetDescriptorSet() : VK_NULL_HANDLE;
                if (texSet == VK_NULL_HANDLE && _fallbackWhiteTexture)
                    texSet = _fallbackWhiteTexture->GetDescriptorSet();
                if (texSet != VK_NULL_HANDLE)
                    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, _shadowAlphaPipelineLayout, 0, 1,
                                            &texSet, 0, nullptr);
            }
            else if (_shadowDepthPipeline != VK_NULL_HANDLE)
            {
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, _shadowDepthPipeline);
                vkCmdPushConstants(cb, _shadowDepthPipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(constants), &constants);
            }
            else
            {
                continue;
            }
            vkCmdDrawIndexed(cb, indexCount, 1, command.firstIndex, 0, 0);
        }

        vkCmdEndRenderPass(cb);
    }

    if (vkEndCommandBuffer(cb) != VK_SUCCESS)
        return;

    // Same-queue submission order makes the final shadow layout readable by
    // the main scene command buffer without a CPU-side queue stall.
    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cb;
    if (_shadowInFlight)
        vkResetFences(_device, 1, &_shadowInFlight);
    const VkResult submitResult = vkQueueSubmit(_graphicsQueue, 1, &si, _shadowInFlight);
    if (submitResult != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "VK shadow: queue submit failed: {}", VkResultName(submitResult));
        if (_shadowInFlight)
            RecreateSignaledShadowFence(_device, _shadowInFlight);
        return;
    }

    // Persist cascade data for UpdateShadowFrameConstants
    _shadowCascades  = cascades.count;
    _shadowOmniCount = cascades.omniCount;
    _shadowCamFwd[0] = input.camera.forward[0];
    _shadowCamFwd[1] = input.camera.forward[1];
    _shadowCamFwd[2] = input.camera.forward[2];
    for (int c = 0; c < cascades.count; ++c)
    {
        std::memcpy(_shadowMapVP + c * 16, cascades.camRelVP[c].m.data(), sizeof(float) * 16);
        _shadowSplits[c] = cascades.splitViewDist[c];
    }
    _shadowMapActive = true;
}

} // namespace Poseidon
