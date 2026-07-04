#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <stddef.h>
#include <catch2/catch_message.hpp>

// I-22 / B-011: `glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ...)` requires
// a non-default VAO to be currently bound.  GL stores the IBO
// binding inside whichever VAO is active at bind time; binding the
// IBO with VAO 0 either silently no-ops (core profile) or attaches
// to the global default VAO and never reaches the draw VAO that
// needs it — `GL_INVALID_OPERATION` floods the driver log either way.
//
// The T1 lift adds `engine/PoseidonGL33/GLIndexBuffer.hpp`
// with `Poseidon::render::ibo::BindOnActiveVao(GLuint ibo)`.  The helper is
// the unique callsite of `glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ...)`
// in the engine and, in debug builds, asserts the current VAO is
// non-zero before issuing the bind so a missing `glBindVertexArray`
// upstream halts at the actual offending callsite rather than as a
// delayed `GL_INVALID_OPERATION` from the next draw.

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

std::filesystem::path IndexBufferHelper()
{
    return std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "PoseidonGL33" / "GLIndexBuffer.hpp";
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

TEST_CASE("I-22 T1: GL33 backend does not call glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ...) directly (B-011)",
          "[Graphics][GL33][IboBindAudit][I-22]")
{
    for (const auto& entry : std::filesystem::directory_iterator(Gl33Dir()))
    {
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension() != ".cpp")
            continue;

        const std::string body = ReadTextFile(entry.path());
        const std::string name = entry.path().filename().string();
        const int n = CountOccurrences(body, "glBindBuffer(GL_ELEMENT_ARRAY_BUFFER");
        CAPTURE(name);
        REQUIRE(n == 0);
    }
}

TEST_CASE("I-22 T1: GLIndexBuffer.hpp exposes the named bind helper", "[Graphics][GL33][IboBindAudit][I-22]")
{
    const std::string body = ReadTextFile(IndexBufferHelper());
    REQUIRE_FALSE(body.empty());

    REQUIRE(body.find("BindOnActiveVao") != std::string::npos);
    REQUIRE(body.find("glBindBuffer(GL_ELEMENT_ARRAY_BUFFER") != std::string::npos);

    // Debug-build VAO-non-zero precondition check is present.  The
    // helper queries `GL_VERTEX_ARRAY_BINDING` and traps on zero so
    // a missing upstream `glBindVertexArray` halts at the offending
    // callsite rather than at the next `glDrawElements`.
    REQUIRE(body.find("GL_VERTEX_ARRAY_BINDING") != std::string::npos);
}

TEST_CASE("I-22 T1: GLIndexBuffer.hpp is the unique IBO bind callsite", "[Graphics][GL33][IboBindAudit][I-22]")
{
    const std::string helperBody = ReadTextFile(IndexBufferHelper());
    REQUIRE_FALSE(helperBody.empty());

    // The helper file mentions `glBindBuffer(GL_ELEMENT_ARRAY_BUFFER` in
    // its doc comment too; the actual code-line ends with `, ibo);`.
    // Match that to count call statements, not comment mentions.
    REQUIRE(CountOccurrences(helperBody, "glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);") == 1);
}

TEST_CASE("I-22 T1: backend callsites use the named helper", "[Graphics][GL33][IboBindAudit][I-22]")
{
    // Known IBO bind sites: 2DRendering create-VAO x2, Queue flush,
    // VertexBuffer Init.  All route through
    // `Poseidon::render::ibo::BindOnActiveVao`.
    int total = 0;
    for (const auto& entry : std::filesystem::directory_iterator(Gl33Dir()))
    {
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension() != ".cpp")
            continue;

        const std::string body = ReadTextFile(entry.path());
        total += CountOccurrences(body, "Poseidon::render::ibo::BindOnActiveVao");
    }
    REQUIRE(total >= 4);
}
