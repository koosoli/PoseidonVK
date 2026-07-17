#include <catch2/catch_test_macros.hpp>

#include <Poseidon/World/Terrain/TerrainCdlod.hpp>

#include <vector>

namespace
{
using namespace Poseidon;

struct Tree
{
    std::vector<CdlodNode> nodes;
    int root = -1;
    int levels = 0;
    float leafSize = 0.0f;
};

Tree makeTree()
{
    Tree tree;
    auto bounds = [](int x, int z, int span, float& minY, float& maxY)
    {
        minY = static_cast<float>(x + z);
        maxY = minY + static_cast<float>(span);
    };
    BuildCdlodTree(64, 0, 0, 1.0f, 16, bounds, tree.nodes, tree.root, tree.levels, tree.leafSize);
    return tree;
}

std::vector<CdlodSelection> select(const Tree& tree, float x, float z)
{
    std::vector<float> ranges;
    ComputeCdlodRanges(24.0f, 2.0f, tree.levels, ranges);
    std::vector<CdlodSelection> output;
    SelectCdlod(tree.nodes, tree.root, tree.levels - 1, x, 10.0f, z, ranges, 0.5f,
                [](const CdlodNode&) { return true; }, [&](const CdlodSelection& selection) { output.push_back(selection); });
    return output;
}
} // namespace

TEST_CASE("Terrain/CDLOD builds exact power-of-two coverage and aggregated bounds", "[terrain][cdlod]")
{
    const Tree tree = makeTree();
    REQUIRE(tree.root >= 0);
    REQUIRE(tree.levels == 3);
    REQUIRE(tree.leafSize == 16.0f);
    REQUIRE(tree.nodes[tree.root].originX == 0.0f);
    REQUIRE(tree.nodes[tree.root].originZ == 0.0f);
    REQUIRE(tree.nodes[tree.root].size == 64.0f);
    REQUIRE(tree.nodes[tree.root].minY == 0.0f);
    REQUIRE(tree.nodes[tree.root].maxY == 112.0f);
    REQUIRE(CdlodRootTexels(49, 16) == 64);
    REQUIRE(CdlodCenteredOrigin(128, 64, 16) == -32);
}

TEST_CASE("Terrain/CDLOD distance, frustum rejection, and morph bands preserve coverage", "[terrain][cdlod]")
{
    const Tree tree = makeTree();
    std::vector<float> ranges;
    ComputeCdlodRanges(24.0f, 2.0f, tree.levels, ranges);
    std::vector<CdlodSelection> visible;
    SelectCdlod(tree.nodes, tree.root, tree.levels - 1, 8.0f, 10.0f, 8.0f, ranges, 0.5f,
                [](const CdlodNode& node) { return node.originX < 32.0f; },
                [&](const CdlodSelection& selection) { visible.push_back(selection); });
    REQUIRE_FALSE(visible.empty());
    for (const CdlodSelection& selection : visible)
    {
        REQUIRE(selection.originX < 32.0f);
        REQUIRE(selection.morphStart <= selection.morphEnd);
        REQUIRE(selection.morphEnd == ranges[selection.level]);
    }
    float start = 0.0f, end = 0.0f;
    CdlodMorphBand(ranges, 1, 0.5f, start, end);
    REQUIRE(start == 36.0f);
    REQUIRE(end == 48.0f);
}

TEST_CASE("Terrain/CDLOD selection is deterministic and revision invalidation is explicit", "[terrain][cdlod]")
{
    const Tree tree = makeTree();
    const auto first = select(tree, 8.0f, 8.0f);
    const auto second = select(tree, 8.0f, 8.0f);
    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i)
    {
        REQUIRE(first[i].originX == second[i].originX);
        REQUIRE(first[i].originZ == second[i].originZ);
        REQUIRE(first[i].level == second[i].level);
    }
    CdlodRevisionCache cache;
    REQUIRE(cache.NeedsRebuild(7));
    cache.Commit(7);
    REQUIRE_FALSE(cache.NeedsRebuild(7));
    REQUIRE(cache.NeedsRebuild(8));
    cache.Invalidate();
    REQUIRE(cache.NeedsRebuild(7));
}
