// Backend-neutral CDLOD quadtree construction and selection.  This file has no
// renderer dependency so CPU and eventual GPU selection can share its rules.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Poseidon
{
struct CdlodNode
{
    float originX = 0.0f, originZ = 0.0f;
    float size = 0.0f;
    float minY = 0.0f, maxY = 0.0f;
    int level = 0;
    int child[4] = {-1, -1, -1, -1};
};

struct CdlodSelection
{
    float originX = 0.0f, originZ = 0.0f;
    float size = 0.0f;
    int level = 0;
    float morphStart = 0.0f, morphEnd = 0.0f;
};

// A terrain cache is keyed by map content revision, not Landscape address.
// Reloads can reuse addresses and editor tools mutate existing maps in place.
struct CdlodRevisionCache
{
    std::uint64_t revision = 0;
    bool valid = false;

    bool NeedsRebuild(std::uint64_t incomingRevision) const noexcept
    {
        return !valid || revision != incomingRevision;
    }
    void Commit(std::uint64_t incomingRevision) noexcept
    {
        revision = incomingRevision;
        valid = true;
    }
    void Invalidate() noexcept { valid = false; }
};

inline void ComputeCdlodRanges(float baseRange, float ratio, int numLevels, std::vector<float>& ranges)
{
    ranges.resize(std::max(0, numLevels));
    for (int level = 0; level < numLevels; ++level)
    {
        ranges[level] = baseRange;
        baseRange *= ratio;
    }
}

inline float CdlodNodeDistanceSq(const CdlodNode& node, float x, float y, float z)
{
    const float maxX = node.originX + node.size;
    const float maxZ = node.originZ + node.size;
    const float dx = x < node.originX ? node.originX - x : (x > maxX ? x - maxX : 0.0f);
    const float dy = y < node.minY ? node.minY - y : (y > node.maxY ? y - node.maxY : 0.0f);
    const float dz = z < node.originZ ? node.originZ - z : (z > maxZ ? z - maxZ : 0.0f);
    return dx * dx + dy * dy + dz * dz;
}

inline void CdlodMorphBand(const std::vector<float>& ranges, int level, float morphRegion, float& start, float& end)
{
    end = ranges[level];
    const float previous = level > 0 ? ranges[level - 1] : 0.0f;
    start = end - (end - previous) * std::clamp(morphRegion, 0.0f, 1.0f);
}

template <typename EmitFn>
inline void EmitCdlodNode(const CdlodNode& node, int level, const std::vector<float>& ranges, float morphRegion,
                          EmitFn&& emit)
{
    float start = 0.0f, end = 0.0f;
    CdlodMorphBand(ranges, level, morphRegion, start, end);
    emit(CdlodSelection{node.originX, node.originZ, node.size, level, start, end});
}

template <typename VisibleFn, typename EmitFn>
bool SelectCdlod(const std::vector<CdlodNode>& nodes, int index, int lodLevel, float cameraX, float cameraY,
                 float cameraZ, const std::vector<float>& ranges, float morphRegion, VisibleFn&& visible,
                 EmitFn&& emit)
{
    if (index < 0 || index >= static_cast<int>(nodes.size()) || lodLevel < 0 || lodLevel >= static_cast<int>(ranges.size()))
        return false;
    const CdlodNode& node = nodes[index];
    const float distanceSq = CdlodNodeDistanceSq(node, cameraX, cameraY, cameraZ);
    if (distanceSq > ranges[lodLevel] * ranges[lodLevel])
        return false;
    if (!visible(node))
        return true;
    if (lodLevel == 0 || node.child[0] < 0)
    {
        EmitCdlodNode(node, lodLevel, ranges, morphRegion, emit);
        return true;
    }
    if (distanceSq > ranges[lodLevel - 1] * ranges[lodLevel - 1])
    {
        EmitCdlodNode(node, lodLevel, ranges, morphRegion, emit);
        return true;
    }
    for (int child = 0; child < 4; ++child)
    {
        const int childIndex = node.child[child];
        if (!SelectCdlod(nodes, childIndex, lodLevel - 1, cameraX, cameraY, cameraZ, ranges, morphRegion, visible, emit) &&
            childIndex >= 0 && visible(nodes[childIndex]))
            EmitCdlodNode(nodes[childIndex], lodLevel - 1, ranges, morphRegion, emit);
    }
    return true;
}

template <typename LeafBoundsFn>
inline int BuildCdlodNode(std::vector<CdlodNode>& tree, float grid, int leafTexels, int originX, int originZ,
                          int spanTexels, int level, LeafBoundsFn& leafBounds)
{
    CdlodNode node{};
    node.originX = originX * grid;
    node.originZ = originZ * grid;
    node.size = spanTexels * grid;
    node.level = level;
    if (spanTexels <= leafTexels)
    {
        node.minY = 1e30f;
        node.maxY = -1e30f;
        leafBounds(originX, originZ, spanTexels, node.minY, node.maxY);
    }
    else
    {
        const int half = spanTexels / 2;
        const int offsets[4][2] = {{0, 0}, {half, 0}, {0, half}, {half, half}};
        node.minY = 1e30f;
        node.maxY = -1e30f;
        for (int child = 0; child < 4; ++child)
        {
            node.child[child] = BuildCdlodNode(tree, grid, leafTexels, originX + offsets[child][0],
                                                originZ + offsets[child][1], half, level - 1, leafBounds);
            node.minY = std::min(node.minY, tree[node.child[child]].minY);
            node.maxY = std::max(node.maxY, tree[node.child[child]].maxY);
        }
    }
    const int index = static_cast<int>(tree.size());
    tree.push_back(node);
    return index;
}

inline int CdlodRootTexels(int coverageTexels, int leafTexels)
{
    int root = leafTexels;
    while (root < coverageTexels)
        root *= 2;
    return root;
}

inline int CdlodCenteredOrigin(int rootTexels, int mapTexels, int leafTexels)
{
    int margin = (rootTexels - mapTexels) / 2;
    return -(margin - margin % leafTexels);
}

template <typename LeafBoundsFn>
inline void BuildCdlodTree(int rootTexels, int originTexelX, int originTexelZ, float grid, int leafTexels,
                           LeafBoundsFn leafBounds, std::vector<CdlodNode>& tree, int& rootIndex, int& numLevels,
                           float& leafSize)
{
    tree.clear();
    rootIndex = -1;
    numLevels = 0;
    leafSize = 0.0f;
    if (rootTexels <= 0 || leafTexels <= 0 || grid <= 0.0f || rootTexels % leafTexels != 0)
        return;
    for (int span = leafTexels; span < rootTexels; span *= 2)
        ++numLevels;
    ++numLevels;
    leafSize = leafTexels * grid;
    rootIndex = BuildCdlodNode(tree, grid, leafTexels, originTexelX, originTexelZ, rootTexels, numLevels - 1, leafBounds);
}
} // namespace Poseidon
