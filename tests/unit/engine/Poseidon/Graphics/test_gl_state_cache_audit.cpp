#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include <stddef.h>
#include <algorithm>
#include <catch2/catch_message.hpp>
#include <utility>

// I-03 / B-007: state caches invalidate on side-effecting calls.
// I-04 / B-022: default-state slots stay default.
//
// Both are GL-state-cache invariants enforced today by *not* having
// the regression that would violate them — the current GL33 backend
// re-applies depth / blend / sampler state every call without a
// short-circuit cache, so `glClear`-induced state desync is fixed
// by the next `ApplyDepthMode` call regardless.  Sampler slot 1
// is bound once at init to the default linear-wrap sampler and
// never touched per-draw.
//
// That correctness is structural — but a future "skip the apply
// if mode hasn't changed" optimisation would re-introduce B-007,
// and a future "bind a per-material sampler to slot 1" would re-
// introduce B-022.  These tests pin the structure by source-text
// audit so the regressing PR has to delete or amend an audit
// entry rather than silently introduce the problem.

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

// Source files in the GL33 backend the audits walk.  Every other
// file in the backend is also subject to the invariant, but glClear
// / glBindSampler are infrequent enough that we list the files that
// legitimately call them.
struct AllowedCallsite
{
    const char* relPath; // relative to engine/PoseidonGL33/
    const char* context; // human-readable owning code path
    int expectedCount;   // expected occurrences of the audited symbol
};

// Post-I-03-T1: every glClear in the engine goes through
// `Poseidon::render::clear::ColorDepthStencil` / `WithMask` in
// `engine/PoseidonGL33/GLClear.hpp`.  The helpers issue
// `glDepthMask(GL_TRUE)` before the depth-touching clear so the
// B-007 precondition is structurally enforced.  Backend `.cpp` files
// contain zero raw `glClear(` callsites.
constexpr AllowedCallsite kGlClearAllowed[] = {
    // (intentionally empty — every backend callsite routes through
    // the named helpers in GLClear.hpp)
};

constexpr AllowedCallsite kGlBindSamplerSlot1Allowed[] = {
    {"EngineGL33_State.cpp", "CreateSamplerStates init binding (slot 1 = default)", 1},
};

int CountOccurrences(const std::string& haystack, const std::string& needle)
{
    if (needle.empty())
        return 0;
    int n = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos)
    {
        ++n;
        pos += needle.size();
    }
    return n;
}

std::filesystem::path Gl33Dir()
{
    // TESTS_ROOT_DIR is defined by tests/unit/engine/Poseidon/CMakeLists.txt
    // and points at <repo>/tests.  Walk up one level and into engine/...
    return std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "PoseidonGL33";
}

// Read the union of all *.cpp / *.hpp under the GL33 backend so a
// rogue callsite anywhere in the directory shows up in the total.
struct BackendSourceCorpus
{
    std::vector<std::pair<std::string, std::string>> files;
};

BackendSourceCorpus ReadGL33Corpus()
{
    BackendSourceCorpus corpus;
    for (const auto& entry : std::filesystem::directory_iterator(Gl33Dir()))
    {
        if (!entry.is_regular_file())
            continue;
        if (entry.path().filename() == "GLClear.hpp")
            continue;
        const auto ext = entry.path().extension().string();
        if (ext != ".cpp" && ext != ".hpp")
            continue;
        corpus.files.emplace_back(entry.path().filename().string(), ReadTextFile(entry.path()));
    }
    return corpus;
}

} // namespace

TEST_CASE("I-03 T1: GL33 backend does not call glClear directly (B-007)", "[Graphics][GL33][StateCacheAudit][I-03]")
{
    // Post-I-03-T1: every glClear in the backend routes through the
    // named helpers in `engine/PoseidonGL33/GLClear.hpp`,
    // which bundle `glDepthMask(GL_TRUE)` with the depth-touching
    // clear so the B-007 precondition is structurally enforced.
    auto corpus = ReadGL33Corpus();
    REQUIRE_FALSE(corpus.files.empty());

    for (const auto& [name, body] : corpus.files)
    {
        const int n = CountOccurrences(body, "glClear(");
        CAPTURE(name);
        REQUIRE(n == 0);
    }
}

TEST_CASE("I-03 T1: GLClear.hpp helpers bundle glDepthMask with depth clears",
          "[Graphics][GL33][StateCacheAudit][I-03]")
{
    const std::filesystem::path helperPath =
        std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "PoseidonGL33" / "GLClear.hpp";
    const std::string body = ReadTextFile(helperPath);
    REQUIRE_FALSE(body.empty());

    REQUIRE(body.find("ColorDepthStencil") != std::string::npos);
    REQUIRE(body.find("WithMask") != std::string::npos);

    // Both helpers issue `glDepthMask(GL_TRUE)` before any depth-
    // touching `glClear` — the structural fix for B-007.
    REQUIRE(body.find("glDepthMask(GL_TRUE)") != std::string::npos);
    REQUIRE(body.find("GL_DEPTH_BUFFER_BIT") != std::string::npos);
}

TEST_CASE("I-04: glBindSampler(1, ...) only at init binding default (B-022)", "[Graphics][GL33][StateCacheAudit][I-04]")
{
    auto corpus = ReadGL33Corpus();
    REQUIRE_FALSE(corpus.files.empty());

    int allowedTotal = 0;
    for (const auto& a : kGlBindSamplerSlot1Allowed)
        allowedTotal += a.expectedCount;

    int globalTotal = 0;
    for (const auto& [name, body] : corpus.files)
    {
        const int n = CountOccurrences(body, "glBindSampler(1,");
        if (n == 0)
            continue;

        bool listed = false;
        for (const auto& a : kGlBindSamplerSlot1Allowed)
        {
            if (name == a.relPath)
            {
                CAPTURE(name);
                CAPTURE(a.context);
                CAPTURE(a.expectedCount);
                CAPTURE(n);
                REQUIRE(n == a.expectedCount);
                listed = true;
                break;
            }
        }
        if (!listed)
        {
            CAPTURE(name);
            FAIL("glBindSampler(1, ...) introduced outside the I-04 audit list — "
                 "slot 1 must hold the default linear-wrap sampler at every draw boundary");
        }

        globalTotal += n;
    }
    REQUIRE(globalTotal == allowedTotal);
}

TEST_CASE("I-04: init bind targets the default sampler (_samplerObjects[0])", "[Graphics][GL33][StateCacheAudit][I-04]")
{
    // The single allowed slot-1 bind must point at the default
    // sampler (index 0 in the cache).  Anything else means slot 1
    // would inherit a per-call configuration the rest of the
    // engine assumes is default — B-022.
    auto corpus = ReadGL33Corpus();
    for (const auto& [name, body] : corpus.files)
    {
        if (name != "EngineGL33_State.cpp")
            continue;

        const size_t pos = body.find("glBindSampler(1,");
        REQUIRE(pos != std::string::npos);
        const size_t end = body.find(')', pos);
        REQUIRE(end != std::string::npos);
        const std::string call = body.substr(pos, end - pos + 1);
        CAPTURE(call);
        REQUIRE(call.find("_samplerObjects[0]") != std::string::npos);
    }
}

TEST_CASE("I-04 T1: per-draw sampler routes through Poseidon::render::sampler::BindSlot0",
          "[Graphics][GL33][StateCacheAudit][I-04]")
{
    // The T1 lift: per-draw sampler bind goes through a named helper
    // that physically can only touch slot 0.  `ApplySamplerState` in
    // EngineGL33_State.cpp calls `Poseidon::render::sampler::BindSlot0`; there
    // is no `BindSlot1` API, so a future "bind a per-material sampler
    // to slot 1" refactor would have to either re-introduce a raw
    // glBindSampler(1, ...) (caught by the count audit above) or add
    // a new helper to GLSampler.hpp (which is reviewable as the audit
    // entry it requires).
    const std::filesystem::path helperPath =
        std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "PoseidonGL33" / "GLSampler.hpp";
    const std::string helper = ReadTextFile(helperPath);
    REQUIRE_FALSE(helper.empty());

    REQUIRE(helper.find("BindSlot0") != std::string::npos);
    REQUIRE(helper.find("glBindSampler(0,") != std::string::npos);
    // No BindSlot1 / BindSlotN helpers — only slot 0 is per-draw-configurable.
    REQUIRE(helper.find("BindSlot1") == std::string::npos);

    // ApplySamplerState uses the helper.
    auto corpus = ReadGL33Corpus();
    for (const auto& [name, body] : corpus.files)
    {
        if (name != "EngineGL33_State.cpp")
            continue;

        const size_t apsPos = body.find("void EngineGL33::ApplySamplerState()");
        REQUIRE(apsPos != std::string::npos);

        const size_t scanEnd = std::min(body.size(), apsPos + 600);
        const std::string region = body.substr(apsPos, scanEnd - apsPos);
        REQUIRE(region.find("Poseidon::render::sampler::BindSlot0") != std::string::npos);
    }
}
