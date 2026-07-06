#include <catch2/catch_test_macros.hpp>

#include <PoseidonVK/BootstrapPushConstantsVK.hpp>

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

TEST_CASE("Vulkan bootstrap shaders compile under Vulkan GLSL rules", "[vulkan][shaders]")
{
    GlslangInit init;

    const std::filesystem::path shaderDir = RepoRoot() / "engine" / "PoseidonVK" / "Shaders";
    const std::string vertexSource = ReadTextFile(shaderDir / "bootstrap_triangle.vert.glsl");
    const std::string fragmentSource = ReadTextFile(shaderDir / "bootstrap_triangle.frag.glsl");
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

TEST_CASE("Vulkan bootstrap push constants match shader contract", "[vulkan][shaders]")
{
    using Poseidon::vk::BootstrapPushConstantsVK;

    STATIC_REQUIRE(offsetof(BootstrapPushConstantsVK, viewport) == 0);
    STATIC_REQUIRE(offsetof(BootstrapPushConstantsVK, clearColor) == 16);
    STATIC_REQUIRE(sizeof(BootstrapPushConstantsVK) == 32);
    STATIC_REQUIRE(Poseidon::vk::kBootstrapPushConstantsSize == sizeof(BootstrapPushConstantsVK));

    const std::filesystem::path shaderDir = RepoRoot() / "engine" / "PoseidonVK" / "Shaders";
    const std::string vertexSource = ReadTextFile(shaderDir / "bootstrap_triangle.vert.glsl");
    const std::string fragmentSource = ReadTextFile(shaderDir / "bootstrap_triangle.frag.glsl");

    CHECK(vertexSource.find("layout(push_constant) uniform BootstrapConstants") != std::string::npos);
    CHECK(vertexSource.find("layout(location = 0) in vec2 inPosition;") != std::string::npos);
    CHECK(vertexSource.find("layout(location = 1) in vec3 inColor;") != std::string::npos);
    CHECK(vertexSource.find("vec4 viewport;") != std::string::npos);
    CHECK(vertexSource.find("vec4 clearColor;") != std::string::npos);
    CHECK(vertexSource.find("layout(set = 0, binding = 0, std140) uniform FrameConstants") != std::string::npos);
    CHECK(vertexSource.find("mat4 view;") != std::string::npos);
    CHECK(vertexSource.find("mat4 projection;") != std::string::npos);
    CHECK(vertexSource.find("mat4 sunMatrix;") != std::string::npos);
    CHECK(vertexSource.find("vec4 clipPlanes;") != std::string::npos);
    CHECK(vertexSource.find("vec4 worldRect;") != std::string::npos);
    CHECK(vertexSource.find("vec4 fogParams;") != std::string::npos);
    CHECK(vertexSource.find("vec4 fogColor;") != std::string::npos);
    CHECK(vertexSource.find("vec4 lightingParams;") != std::string::npos);
    CHECK(vertexSource.find("vec4 sunDirection;") != std::string::npos);
    CHECK(vertexSource.find("gl_VertexIndex") == std::string::npos);
    CHECK(fragmentSource.find("layout(push_constant) uniform BootstrapConstants") != std::string::npos);
    CHECK(fragmentSource.find("vec4 viewport;") != std::string::npos);
    CHECK(fragmentSource.find("vec4 clearColor;") != std::string::npos);
}

TEST_CASE("Vulkan bootstrap push constants copy viewport and clear color", "[vulkan][shaders]")
{
    const float clearColor[4] = {0.04f, 0.09f, 0.16f, 1.0f};

    const Poseidon::vk::BootstrapPushConstantsVK constants =
        Poseidon::vk::BuildBootstrapPushConstants(3441, 1440, clearColor);

    CHECK(constants.viewport[0] == 0.0f);
    CHECK(constants.viewport[1] == 0.0f);
    CHECK(constants.viewport[2] == 3441.0f);
    CHECK(constants.viewport[3] == 1440.0f);
    CHECK(constants.clearColor[0] == 0.04f);
    CHECK(constants.clearColor[1] == 0.09f);
    CHECK(constants.clearColor[2] == 0.16f);
    CHECK(constants.clearColor[3] == 1.0f);
}

TEST_CASE("Vulkan bootstrap push constants can use frame constants viewport", "[vulkan][shaders]")
{
    Poseidon::vk::FrameConstantsVK frameConstants;
    frameConstants.viewport[0] = 8.0f;
    frameConstants.viewport[1] = 16.0f;
    frameConstants.viewport[2] = 1280.0f;
    frameConstants.viewport[3] = 720.0f;
    const float clearColor[4] = {0.2f, 0.3f, 0.4f, 1.0f};

    const Poseidon::vk::BootstrapPushConstantsVK constants =
        Poseidon::vk::BuildBootstrapPushConstants(frameConstants, clearColor);

    CHECK(constants.viewport[0] == 8.0f);
    CHECK(constants.viewport[1] == 16.0f);
    CHECK(constants.viewport[2] == 1280.0f);
    CHECK(constants.viewport[3] == 720.0f);
    CHECK(constants.clearColor[0] == 0.2f);
    CHECK(constants.clearColor[1] == 0.3f);
    CHECK(constants.clearColor[2] == 0.4f);
    CHECK(constants.clearColor[3] == 1.0f);
}
