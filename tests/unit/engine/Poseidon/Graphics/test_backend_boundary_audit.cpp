#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

std::filesystem::path PoseidonDir()
{
    return std::filesystem::path(TESTS_ROOT_DIR).parent_path() / "engine" / "Poseidon";
}

std::string ReadTextFile(const std::filesystem::path& p)
{
    std::ifstream f(p);
    if (!f.is_open())
        return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool IsSourceFile(const std::filesystem::path& p)
{
    const auto ext = p.extension().string();
    return ext == ".cpp" || ext == ".hpp" || ext == ".h";
}

} // namespace

TEST_CASE("Backend boundary: shared Poseidon sources do not include GL33 headers", "[Graphics][boundary][backend]")
{
    const auto root = PoseidonDir();
    REQUIRE(std::filesystem::exists(root));

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file() || !IsSourceFile(entry.path()))
            continue;

        const std::string body = ReadTextFile(entry.path());
        INFO(entry.path().lexically_relative(root).generic_string());
        REQUIRE(body.find("#include <PoseidonGL33/") == std::string::npos);
    }
}