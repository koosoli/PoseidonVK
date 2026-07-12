#include <catch2/catch_test_macros.hpp>

#include <PoseidonVK/DrawConstantsVK.hpp>
#include <PoseidonVK/ScenePushConstantsVK.hpp>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

std::filesystem::path RepoRoot()
{
    return std::filesystem::path(__FILE__)
        .parent_path()
        .parent_path()
        .parent_path()
        .parent_path()
        .parent_path()
        .parent_path();
}

std::string ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

struct GlslangInit
{
    GlslangInit() { glslang::InitializeProcess(); }
    ~GlslangInit() { glslang::FinalizeProcess(); }
};

struct CompileOutcome
{
    bool success = false;
    std::string info;
    std::size_t spirvWordCount = 0;
};

CompileOutcome CompileVulkanGLSL(const std::string& source, EShLanguage stage)
{
    glslang::TShader shader(stage);
    const char* strings[] = {source.c_str()};
    shader.setStrings(strings, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);

    const EShMessages messages = static_cast<EShMessages>(EShMsgVulkanRules | EShMsgSpvRules);
    if (!shader.parse(GetDefaultResources(), 450, false, messages))
        return {false, shader.getInfoLog(), 0};

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages))
        return {false, program.getInfoLog(), 0};

    std::vector<unsigned int> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);
    return {!spirv.empty(), shader.getInfoLog(), spirv.size()};
}

} // namespace

TEST_CASE("Vulkan scene shaders compile under Vulkan GLSL rules", "[vulkan][scene-shaders]")
{
    GlslangInit init;

    const std::filesystem::path shaderDir = RepoRoot() / "engine" / "PoseidonVK" / "Shaders";
    const std::string vertexSource = ReadTextFile(shaderDir / "scene.vert.glsl");
    const std::string fragmentSource = ReadTextFile(shaderDir / "scene.frag.glsl");
    REQUIRE_FALSE(vertexSource.empty());
    REQUIRE_FALSE(fragmentSource.empty());

    const CompileOutcome vertex = CompileVulkanGLSL(vertexSource, EShLangVertex);
    CAPTURE(vertex.info);
    REQUIRE(vertex.success);
    REQUIRE(vertex.spirvWordCount > 0);

    const CompileOutcome fragment = CompileVulkanGLSL(fragmentSource, EShLangFragment);
    CAPTURE(fragment.info);
    REQUIRE(fragment.success);
    REQUIRE(fragment.spirvWordCount > 0);
}

TEST_CASE("Vulkan scene vertex shader matches the scene vertex input contract", "[vulkan][scene-shaders]")
{
    const std::filesystem::path shaderDir = RepoRoot() / "engine" / "PoseidonVK" / "Shaders";
    const std::string vertexSource = ReadTextFile(shaderDir / "scene.vert.glsl");

    CHECK(vertexSource.find("layout(location = 0) in vec3 inPosition;") != std::string::npos);
    CHECK(vertexSource.find("layout(location = 1) in vec3 inNormal;") != std::string::npos);
    CHECK(vertexSource.find("layout(location = 2) in vec2 inTexcoord;") != std::string::npos);
}

TEST_CASE("Vulkan scene shaders share the frame descriptor contract", "[vulkan][scene-shaders]")
{
    const std::filesystem::path shaderDir = RepoRoot() / "engine" / "PoseidonVK" / "Shaders";
    const std::string vertexSource = ReadTextFile(shaderDir / "scene.vert.glsl");
    const std::string fragmentSource = ReadTextFile(shaderDir / "scene.frag.glsl");

    CHECK(vertexSource.find("layout(set = 0, binding = 0, std140) uniform FrameConstants") != std::string::npos);
    CHECK(vertexSource.find("mat4 view;") != std::string::npos);
    CHECK(vertexSource.find("mat4 projection;") != std::string::npos);
    CHECK(vertexSource.find("mat4 sunMatrix;") != std::string::npos);
    CHECK(vertexSource.find("vec4 sunDirection;") != std::string::npos);
    CHECK(vertexSource.find("vec4 localLightPosition[8];") != std::string::npos);
    CHECK(vertexSource.find("vec4 localLightDiffuse[8];") != std::string::npos);
    CHECK(vertexSource.find("vec4 localLightAmbient[8];") != std::string::npos);
    CHECK(vertexSource.find("vec4 localLightDirection[8];") != std::string::npos);
    CHECK(fragmentSource.find("layout(set = 0, binding = 0, std140) uniform FrameConstants") != std::string::npos);
    CHECK(fragmentSource.find("vec4 sunDirection;") != std::string::npos);
    CHECK(fragmentSource.find("vec4 localLightPosition[8];") != std::string::npos);
    CHECK(fragmentSource.find("vec4 localLightDiffuse[8];") != std::string::npos);
    CHECK(fragmentSource.find("vec4 localLightAmbient[8];") != std::string::npos);
    CHECK(fragmentSource.find("vec4 localLightDirection[8];") != std::string::npos);
}

TEST_CASE("Vulkan scene vertex shader reads per-draw constants from the SSBO", "[vulkan][scene-shaders]")
{
    const std::filesystem::path shaderDir = RepoRoot() / "engine" / "PoseidonVK" / "Shaders";
    const std::string vertexSource = ReadTextFile(shaderDir / "scene.vert.glsl");

    CHECK(vertexSource.find("layout(set = 0, binding = 1, std430) readonly buffer DrawConstantsBuffer") !=
          std::string::npos);
    CHECK(vertexSource.find("mat4 world;") != std::string::npos);
    CHECK(vertexSource.find("uint drawIndex;") != std::string::npos);
    CHECK(vertexSource.find("min(draw.drawIndex, drawConstants.draws.length() - 1u)") != std::string::npos);
    CHECK(vertexSource.find("drawConstants.draws[drawIndex].world") != std::string::npos);
}

TEST_CASE("Vulkan DrawConstants SSBO element stride matches the shader struct", "[vulkan][scene-shaders]")
{
    // The shader declares DrawConstants as a std430 SSBO element; its size must
    // match Poseidon::vk::DrawConstantsVK so draws[0].world reads the right bytes.
    STATIC_REQUIRE(sizeof(Poseidon::vk::DrawConstantsVK) == 176);
    STATIC_REQUIRE(offsetof(Poseidon::vk::DrawConstantsVK, world) == 0);
}

TEST_CASE("Vulkan scene shaders drive fog from the uploaded frame constants", "[vulkan][scene-shaders]")
{
    const std::filesystem::path shaderDir = RepoRoot() / "engine" / "PoseidonVK" / "Shaders";
    const std::string vertexSource = ReadTextFile(shaderDir / "scene.vert.glsl");
    const std::string fragmentSource = ReadTextFile(shaderDir / "scene.frag.glsl");

    // Vertex shader computes the fog factor from fogParams and emits a varying.
    CHECK(vertexSource.find("layout(location = 4) out float vFogFactor;") != std::string::npos);
    CHECK(vertexSource.find("frame.fogParams.y") != std::string::npos);
    CHECK(vertexSource.find("frame.fogParams.w") != std::string::npos);
    CHECK(vertexSource.find("pow(u, 3.0)") != std::string::npos);
    CHECK(vertexSource.find("vFogFactor =") != std::string::npos);

    // Fragment shader consumes the varying and mixes toward frame.fogColor.
    CHECK(fragmentSource.find("layout(location = 4) in float vFogFactor;") != std::string::npos);
    CHECK(fragmentSource.find("frame.fogColor.rgb") != std::string::npos);
    CHECK(fragmentSource.find("mix(frame.fogColor.rgb, baseColor, vFogFactor)") != std::string::npos);
}

TEST_CASE("Vulkan scene fragment shader applies GL33 night-eye parity", "[vulkan][scene-shaders]")
{
    const std::filesystem::path shaderDir = RepoRoot() / "engine" / "PoseidonVK" / "Shaders";
    const std::string fragmentSource = ReadTextFile(shaderDir / "scene.frag.glsl");

    CHECK(fragmentSource.find("frame.lightingParams.w") != std::string::npos);
    CHECK(fragmentSource.find("dot(baseColor, vec3(0.2, 0.9, 0.4))") != std::string::npos);
    CHECK(fragmentSource.find("baseColor = mix(vec3(luminance), baseColor, nightBlend)") != std::string::npos);
}

TEST_CASE("Vulkan scene fragment shader drives sun lighting from frame constants", "[vulkan][scene-shaders]")
{
    const std::filesystem::path shaderDir = RepoRoot() / "engine" / "PoseidonVK" / "Shaders";
    const std::string fragmentSource = ReadTextFile(shaderDir / "scene.frag.glsl");

    CHECK(fragmentSource.find("frame.sunDirection.xyz") != std::string::npos);
    CHECK(fragmentSource.find("frame.lightingParams.x") != std::string::npos);
    CHECK(fragmentSource.find("-sunDir") != std::string::npos);
    CHECK(fragmentSource.find("* sunOn") != std::string::npos);
}

TEST_CASE("Vulkan scene fragment shader applies per-draw tint", "[vulkan][scene-shaders]")
{
    const std::filesystem::path shaderDir = RepoRoot() / "engine" / "PoseidonVK" / "Shaders";
    const std::string fragmentSource = ReadTextFile(shaderDir / "scene.frag.glsl");

    CHECK(fragmentSource.find("vec4 tint;") != std::string::npos);
    CHECK(fragmentSource.find("baseColor *= tint.rgb") != std::string::npos);
    CHECK(fragmentSource.find("baseAlpha *= tint.a") != std::string::npos);
}

TEST_CASE("Vulkan scene fragment shader refines terrain normals", "[vulkan][scene-shaders]")
{
    const std::filesystem::path shaderDir = RepoRoot() / "engine" / "PoseidonVK" / "Shaders";
    const std::string fragmentSource = ReadTextFile(shaderDir / "scene.frag.glsl");

    CHECK(fragmentSource.find("kPassTerrainOpaque = 12u") != std::string::npos);
    CHECK(fragmentSource.find("cross(dFdx(vWorldPos), dFdy(vWorldPos))") != std::string::npos);
}

TEST_CASE("Vulkan scene fragment shader consumes uploaded local lights", "[vulkan][scene-shaders]")
{
    const std::filesystem::path shaderDir = RepoRoot() / "engine" / "PoseidonVK" / "Shaders";
    const std::string fragmentSource = ReadTextFile(shaderDir / "scene.frag.glsl");

    CHECK(fragmentSource.find("frame.lightingParams.y") != std::string::npos);
    CHECK(fragmentSource.find("frame.lightingParams.z") != std::string::npos);
    CHECK(fragmentSource.find("frame.localLightPosition[i].xyz - vWorldPos") != std::string::npos);
    CHECK(fragmentSource.find("frame.localLightPosition[i].w * frame.localLightPosition[i].w") !=
          std::string::npos);
    CHECK(fragmentSource.find("frame.localLightDirection[i].w > 0.5") != std::string::npos);
    CHECK(fragmentSource.find("frame.localLightDiffuse[i].rgb  * localLightScale") != std::string::npos);
    CHECK(fragmentSource.find("frame.localLightAmbient[i].rgb  * localLightScale") != std::string::npos);
}

TEST_CASE("Vulkan scene shaders declare the world push constant", "[vulkan][scene-shaders]")
{
    const std::filesystem::path shaderDir = RepoRoot() / "engine" / "PoseidonVK" / "Shaders";
    const std::string vertexSource = ReadTextFile(shaderDir / "scene.vert.glsl");

    CHECK(vertexSource.find("layout(push_constant) uniform SceneDraw") != std::string::npos);
    CHECK(vertexSource.find("mat4 world;") != std::string::npos);
}

TEST_CASE("Vulkan scene vertex shader applies the full camera projection", "[vulkan][scene-shaders]")
{
    const std::filesystem::path shaderDir = RepoRoot() / "engine" / "PoseidonVK" / "Shaders";
    const std::string vertexSource = ReadTextFile(shaderDir / "scene.vert.glsl");

    // The scene pipeline must apply the real proj*view*world transform, mirroring
    // the GL33 vsTransform convention (gl_Position = proj * view * world * pos).
    // The bring-up hack that bypassed the camera (gl_Position = vec4(worldPos.xy,
    // 0.0, 1.0)) must be gone so real world-space meshes render correctly.
    CHECK(vertexSource.find("gl_Position = frame.projection * viewPos;") != std::string::npos);
    CHECK(vertexSource.find("vec4(worldPos.xy, 0.0, 1.0)") == std::string::npos);
}

TEST_CASE("Vulkan scene push constants match the shader contract", "[vulkan][scene-shaders]")
{
    using Poseidon::vk::ScenePushConstantsVK;

    STATIC_REQUIRE(offsetof(ScenePushConstantsVK, world) == 0);
    STATIC_REQUIRE(offsetof(ScenePushConstantsVK, useDrawConstants) == 64);
    STATIC_REQUIRE(offsetof(ScenePushConstantsVK, drawIndex) == 68);
    STATIC_REQUIRE(sizeof(ScenePushConstantsVK) == 80);
    STATIC_REQUIRE(Poseidon::vk::kScenePushConstantsSize == sizeof(ScenePushConstantsVK));
}

TEST_CASE("Vulkan scene push constants build identity and world matrices", "[vulkan][scene-shaders]")
{
    {
        const Poseidon::vk::ScenePushConstantsVK constants = Poseidon::vk::BuildIdentityScenePushConstants();
        CHECK(constants.world._11 == 1.0f);
        CHECK(constants.world._22 == 1.0f);
        CHECK(constants.world._33 == 1.0f);
        CHECK(constants.world._44 == 1.0f);
        CHECK(constants.world._12 == 0.0f);
        CHECK(constants.world._21 == 0.0f);
        CHECK(constants.useDrawConstants == 0u);
        CHECK(constants.drawIndex == 0u);
    }
    {
        Poseidon::GfxMatrix world;
        world._11 = 2.0f;
        world._22 = 3.0f;
        world._33 = 4.0f;
        world._44 = 5.0f;
        const Poseidon::vk::ScenePushConstantsVK constants =
            Poseidon::vk::BuildScenePushConstants(world, true, 3u);
        CHECK(constants.world._11 == 2.0f);
        CHECK(constants.world._22 == 3.0f);
        CHECK(constants.world._33 == 4.0f);
        CHECK(constants.world._44 == 5.0f);
        CHECK(constants.useDrawConstants == 1u);
        CHECK(constants.drawIndex == 3u);
    }
}
