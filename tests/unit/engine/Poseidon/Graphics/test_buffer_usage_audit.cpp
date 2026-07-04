#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <stddef.h>
#include <algorithm>
#include <catch2/catch_message.hpp>

// I-21 / B-028: Buffer-mapping flags must match the buffer's declared
// usage hint.  `GL_MAP_INVALIDATE_BUFFER_BIT` on a `GL_STATIC_DRAW`
// buffer contradicts the static promise — NVIDIA reacts by demoting
// the buffer to host memory and spams KHR_debug 131186 perf warnings.
//
// The T1 lift: backend code never calls `glMapBufferRange` directly.
// All buffer-mapping flows through one of two named helpers in
// `engine/PoseidonGL33/GLBufferMap.hpp`:
//
//   Poseidon::render::buf::MapDynamicWriteInvalidate(target, offset, length)
//     — calls glMapBufferRange with WRITE|INVALIDATE; the only API
//       that issues INVALIDATE.  Suitable for GL_DYNAMIC_DRAW buffers
//       only — by name and by contract.
//
//   Poseidon::render::buf::MapStaticWriteOnce(target)
//     — calls glMapBuffer with WRITE_ONLY; never INVALIDATE.
//       Suitable for GL_STATIC_DRAW buffers only.
//
// There is no third public helper.  The bug class is the runtime
// flag-bitwise combination "STATIC buffer + INVALIDATE flag"; with
// these helpers, getting INVALIDATE requires calling the
// `Dynamic`-named function and that name is the contract.
//
// This audit pins:
//   - the GL33 backend does not call `glMapBufferRange` directly,
//   - the helper file exists and exposes the two named functions,
//   - the helper file is the unique callsite of `glMapBufferRange`,
//   - no alternative `Map*` helper exists that breaks the rule.

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

std::filesystem::path BufferMapHelper()
{
    return std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "PoseidonGL33" / "GLBufferMap.hpp";
}

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

} // namespace

TEST_CASE("I-21 T1: GL33 backend does not call glMapBufferRange directly (B-028)",
          "[Graphics][GL33][BufferUsageAudit][I-21]")
{
    // The pre-T1 audit allowed glMapBufferRange in
    // `EngineGL33_VertexBuffer.cpp` because the call was gated on
    // `_dynamic`.  The T1 lift removes the direct call entirely —
    // there is no longer a `_dynamic`-shaped runtime branch on a
    // raw GL flag bundle.  Any glMapBufferRange callsite in the
    // backend means a regression: someone bypassed the helper.
    for (const auto& entry : std::filesystem::directory_iterator(Gl33Dir()))
    {
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension() != ".cpp")
            continue;

        const std::string body = ReadTextFile(entry.path());
        const std::string name = entry.path().filename().string();
        const int n = CountOccurrences(body, "glMapBufferRange(");
        CAPTURE(name);
        REQUIRE(n == 0);
    }
}

TEST_CASE("I-21 T1: GLBufferMap.hpp exposes the two named helpers", "[Graphics][GL33][BufferUsageAudit][I-21]")
{
    const std::string body = ReadTextFile(BufferMapHelper());
    REQUIRE_FALSE(body.empty());

    // Both helpers are present and inline.
    REQUIRE(body.find("MapDynamicWriteInvalidate") != std::string::npos);
    REQUIRE(body.find("MapStaticWriteOnce") != std::string::npos);

    // Exactly the right flag bundle inside each helper.  The Dynamic
    // helper is the unique site that mentions INVALIDATE_BUFFER_BIT;
    // the Static helper uses glMapBuffer (no INVALIDATE possible).
    REQUIRE(body.find("GL_MAP_INVALIDATE_BUFFER_BIT") != std::string::npos);
    REQUIRE(body.find("glMapBufferRange") != std::string::npos);
    REQUIRE(body.find("glMapBuffer(target, GL_WRITE_ONLY)") != std::string::npos);

    // No third helper smuggled in that would weaken the contract.
    REQUIRE(body.find("MapDynamicNoInvalidate") == std::string::npos);
    REQUIRE(body.find("MapStaticInvalidate") == std::string::npos);
    REQUIRE(body.find("MapStaticWriteInvalidate") == std::string::npos);
}

TEST_CASE("I-21 T1: GLBufferMap.hpp is the unique glMapBufferRange callsite",
          "[Graphics][GL33][BufferUsageAudit][I-21]")
{
    const std::string helperBody = ReadTextFile(BufferMapHelper());
    REQUIRE_FALSE(helperBody.empty());

    // Exactly one glMapBufferRange in the helper (inside
    // MapDynamicWriteInvalidate).
    REQUIRE(CountOccurrences(helperBody, "glMapBufferRange(") == 1);
}

TEST_CASE("I-21 T1: backend callsites pick a named helper based on _dynamic",
          "[Graphics][GL33][BufferUsageAudit][I-21]")
{
    // The remaining structural property: backend code that maps a
    // buffer either calls MapDynamicWriteInvalidate (and is gated
    // on `_dynamic`) or MapStaticWriteOnce.  Spot-check the only
    // shipping callsite — `VertexBufferGL33::CopyVertices` — picks
    // between the two on the `_dynamic` field.
    const std::string body = ReadTextFile(Gl33Dir() / "EngineGL33_VertexBuffer.cpp");
    REQUIRE_FALSE(body.empty());

    const size_t copyVerts = body.find("void VertexBufferGL33::CopyVertices");
    REQUIRE(copyVerts != std::string::npos);

    const size_t scanEnd = std::min(body.size(), copyVerts + 2000);
    const std::string region = body.substr(copyVerts, scanEnd - copyVerts);

    REQUIRE(region.find("MapDynamicWriteInvalidate") != std::string::npos);
    REQUIRE(region.find("MapStaticWriteOnce") != std::string::npos);
    REQUIRE(region.find("_dynamic") != std::string::npos);
}
