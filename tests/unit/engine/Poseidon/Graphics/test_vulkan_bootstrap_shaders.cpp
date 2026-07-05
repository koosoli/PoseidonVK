#include <catch2/catch_test_macros.hpp>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

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
