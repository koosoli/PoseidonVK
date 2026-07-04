#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <stddef.h>
#include <algorithm>
#include <catch2/catch_message.hpp>

// I-02 / B-002 / B-003 / B-032: Every GL state field has exactly
// one bind site.
//
// B-002 was cull state with multiple owners; B-003 was nested
// state caches falling out of sync; B-032 was the 2D path
// inheriting leftover texture-sampler state.  The shipping fix:
// `EngineGL33::ApplyPipeline` (in `EngineGL33_State.cpp`) is the
// sole per-draw owner of depth / blend / cull / sampler / fog /
// alpha-test state.  The frame-start init in
// `EngineGL33_Queue.cpp::BeginPass` resets a small subset of state
// to canonical defaults so the first ApplyPipeline of a pass lands
// on known ground.
//
// This audit walks every `.cpp` in the GL33 backend, locates calls
// to the managed GL state symbols, and asserts each callsite lives
// in the audited owning file + context.  A rogue
// `glBlendFuncSeparate` in `EngineGL33_Draw.cpp` (or any other file
// outside `EngineGL33_State.cpp`) fails the audit.

namespace
{

std::string ReadTextFile(const std::filesystem::path& p)
{
    std::ifstream f(p);
    if (!f.is_open())
        return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::filesystem::path Gl33Dir()
{
    return std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "PoseidonGL33";
}

struct OwnerEntry
{
    const char* file;
    const char* note;
};

struct StateOwnership
{
    const char* symbol; // GL call to scan for
    int expectedCount;
    const OwnerEntry* allowedFiles;
    size_t allowedFileCount;
};

// Post-I-02-T1: every managed GL state call lives in a GL33 helper.
// The GL33 backend's `.cpp` files contain ZERO raw calls — the per-
// mode bundles in `Poseidon::render::blend::*`, `Poseidon::render::depthstencil::*`,
// `Poseidon::render::cull::*` are the unique callsites of each underlying GL
// symbol.  The allow-lists below are empty intentionally; any
// backend `.cpp` containing the audited symbol fails the test.
constexpr OwnerEntry kEmptyOwners[] = {{"<none>", "no backend file"}};

constexpr StateOwnership kAudits[] = {
    {"glBlendFunc(", 0, kEmptyOwners, 0},
    {"glBlendFuncSeparate(", 0, kEmptyOwners, 0},
    {"glCullFace(", 0, kEmptyOwners, 0},
    {"glFrontFace(", 0, kEmptyOwners, 0},
    {"glDepthMask(", 0, kEmptyOwners, 0},
    {"glDepthFunc(", 0, kEmptyOwners, 0},
    {"glStencilFunc(", 0, kEmptyOwners, 0},
    {"glStencilOp(", 0, kEmptyOwners, 0},
    {"glColorMask(", 0, kEmptyOwners, 0},
    {"glPolygonOffset(", 0, kEmptyOwners, 0},
    {"glEnable(GL_POLYGON_OFFSET_FILL)", 0, kEmptyOwners, 0},
    {"glDisable(GL_POLYGON_OFFSET_FILL)", 0, kEmptyOwners, 0},
    {"glEnable(GL_DEPTH_TEST)", 0, kEmptyOwners, 0},
    {"glDisable(GL_DEPTH_TEST)", 0, kEmptyOwners, 0},
    {"glEnable(GL_DEPTH_CLAMP)", 0, kEmptyOwners, 0},
    {"glDisable(GL_DEPTH_CLAMP)", 0, kEmptyOwners, 0},
    {"glClearColor(", 0, kEmptyOwners, 0},
};

bool IsAllowedFile(const std::string& name, const OwnerEntry* allowed, size_t allowedCount)
{
    for (size_t i = 0; i < allowedCount; ++i)
        if (name == allowed[i].file)
            return true;
    return false;
}

} // namespace

TEST_CASE("I-02 T1: managed GL state symbols never appear in backend .cpps (B-002 / B-003)",
          "[Graphics][GL33][StateOwnership][I-02]")
{
    // T1 lift: every audited GL state symbol now lives in a GL33
    // helper, not in the backend.  Any backend `.cpp` containing the
    // symbol is a regression — the per-mode bundle was bypassed.
    for (const auto& audit : kAudits)
    {
        for (const auto& entry : std::filesystem::directory_iterator(Gl33Dir()))
        {
            if (!entry.is_regular_file())
                continue;
            if (entry.path().extension() != ".cpp")
                continue;

            const std::string body = ReadTextFile(entry.path());
            const std::string name = entry.path().filename().string();
            if (body.find(audit.symbol) == std::string::npos)
                continue;

            CAPTURE(audit.symbol);
            CAPTURE(name);
            // Symbol present in a backend file — must be in the allow
            // list (which is empty post-T1, so any presence fails).
            REQUIRE(IsAllowedFile(name, audit.allowedFiles, audit.allowedFileCount));
        }
    }
}

TEST_CASE("I-02 T1: Core/ helpers expose the named per-mode bundles", "[Graphics][GL33][StateOwnership][I-02]")
{
    const std::filesystem::path gl33Dir = std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "PoseidonGL33";

    const std::string blend = ReadTextFile(gl33Dir / "GLBlendState.hpp");
    REQUIRE_FALSE(blend.empty());
    REQUIRE(blend.find("Opaque()") != std::string::npos);
    REQUIRE(blend.find("AlphaBlend()") != std::string::npos);
    REQUIRE(blend.find("Additive()") != std::string::npos);
    REQUIRE(blend.find("Shadow()") != std::string::npos);

    const std::string ds = ReadTextFile(gl33Dir / "GLDepthStencilState.hpp");
    REQUIRE_FALSE(ds.empty());
    REQUIRE(ds.find("Normal(") != std::string::npos);
    REQUIRE(ds.find("ReadOnly(") != std::string::npos);
    REQUIRE(ds.find("Disabled(") != std::string::npos);
    REQUIRE(ds.find("Shadow(") != std::string::npos);

    const std::string cull = ReadTextFile(gl33Dir / "GLCullState.hpp");
    REQUIRE_FALSE(cull.empty());
    REQUIRE(cull.find("Back()") != std::string::npos);
    REQUIRE(cull.find("Front()") != std::string::npos);
    REQUIRE(cull.find("None()") != std::string::npos);
    REQUIRE(cull.find("FrontFaceCW()") != std::string::npos);
    REQUIRE(cull.find("FrontFaceCCW()") != std::string::npos);
}

TEST_CASE("I-02: ApplyPipeline owns the per-draw state path (one stop-shop)", "[Graphics][GL33][StateOwnership][I-02]")
{
    // The descriptor-driven `ApplyPipeline(d)` in EngineGL33_State.cpp
    // calls the per-state helpers (ApplyDepthMode, ApplyBlendMode,
    // ApplySamplerState, etc.).  Any per-draw GL state mutation
    // outside ApplyPipeline / its helpers would break the
    // "one bind site" invariant.  Spot-check that ApplyPipeline
    // exists and routes through the named helpers — a refactor that
    // bypasses one of them gets caught by the helper-name presence
    // assertion.
    const std::string body = ReadTextFile(Gl33Dir() / "EngineGL33_State.cpp");
    REQUIRE_FALSE(body.empty());

    const size_t applyPos =
        body.find("void EngineGL33::ApplyPipeline(const Poseidon::render::RenderPassDescriptor& d)");
    REQUIRE(applyPos != std::string::npos);

    // Scan the function body for each helper.  Wide window because
    // ApplyPipeline is a long dispatcher.
    const size_t scanEnd = std::min(body.size(), applyPos + 8000);
    const std::string region = body.substr(applyPos, scanEnd - applyPos);

    REQUIRE(region.find("ApplySamplerState") != std::string::npos);
    REQUIRE(region.find("ApplyDepthMode") != std::string::npos);
    REQUIRE(region.find("ApplyBlendMode") != std::string::npos);
    REQUIRE(region.find("SetShaderFogEnabled") != std::string::npos);
    REQUIRE(region.find("SetAlphaTest") != std::string::npos);
}

TEST_CASE("I-02: queued TL shadows bind screen vertex input explicitly", "[Graphics][GL33][StateOwnership][Shadow]")
{
    // RenderDoc regression: projected tree shadows are queued from TLVertex
    // data and FlushQueue binds _vaoScreen.  If shader selection is inferred
    // from the ambient active pass instead of the queued vertex layout, a
    // shadow can run through VSShadow/VSTransform, lose TL vertex alpha, and
    // stamp full black into the framebuffer.
    const std::string queue = ReadTextFile(Gl33Dir() / "EngineGL33_Queue.cpp");
    REQUIRE_FALSE(queue.empty());

    const size_t flushPos = queue.find("void EngineGL33::FlushQueue(QueueGL33& queue, int index)");
    REQUIRE(flushPos != std::string::npos);
    const size_t prepareTlPos = queue.find("void EngineGL33::PrepareTriangleTL");
    REQUIRE(prepareTlPos != std::string::npos);

    const std::string flushRegion = queue.substr(flushPos, prepareTlPos - flushPos);
    REQUIRE(flushRegion.find("PipelineVertexInput::Screen") != std::string::npos);
    REQUIRE(flushRegion.find("GL33Bind::Vao(_vaoScreen)") != std::string::npos);

    const std::string prepareTlRegion = queue.substr(prepareTlPos, std::min<size_t>(queue.size() - prepareTlPos, 1200));
    REQUIRE(prepareTlRegion.find("PipelineVertexInput::Mesh") != std::string::npos);

    const size_t beginPassPos = queue.find("void EngineGL33::BeginPass(PassId passId)");
    REQUIRE(beginPassPos != std::string::npos);
    const size_t beginScreenPassPos = queue.find("void EngineGL33::BeginScreenPass()", beginPassPos);
    REQUIRE(beginScreenPassPos != std::string::npos);
    const std::string beginPassRegion = queue.substr(beginPassPos, beginScreenPassPos - beginPassPos);

    const size_t defaultVsPos = beginPassRegion.find("SelectVertexShader(VSTransform)");
    REQUIRE(defaultVsPos != std::string::npos);
    const size_t invalidatePos = beginPassRegion.find("InvalidatePipelineCache()", defaultVsPos);
    REQUIRE(invalidatePos != std::string::npos);

    const std::string state = ReadTextFile(Gl33Dir() / "EngineGL33_State.cpp");
    REQUIRE_FALSE(state.empty());
    const size_t applyPos =
        state.find("void EngineGL33::ApplyPipeline(const Poseidon::render::RenderPassDescriptor& d)");
    REQUIRE(applyPos != std::string::npos);
    const size_t scanEnd = std::min(state.size(), applyPos + 9000);
    const std::string applyRegion = state.substr(applyPos, scanEnd - applyPos);

    REQUIRE(applyRegion.find("meshVertexInput") != std::string::npos);
    const size_t shadowPos = applyRegion.find("ShaderFamily::Shadow");
    REQUIRE(shadowPos != std::string::npos);
    const size_t waterPos = applyRegion.find("ShaderFamily::Water", shadowPos);
    REQUIRE(waterPos != std::string::npos);
    const std::string shadowRegion = applyRegion.substr(shadowPos, waterPos - shadowPos);
    REQUIRE(shadowRegion.find("if (meshVertexInput)") != std::string::npos);
    REQUIRE(shadowRegion.find("if (IsIn3DPass())") == std::string::npos);
}
