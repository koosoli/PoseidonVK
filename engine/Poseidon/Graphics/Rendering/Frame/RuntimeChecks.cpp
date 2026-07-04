#include <Poseidon/Graphics/Rendering/Frame/RuntimeChecks.hpp>

namespace Poseidon
{

namespace render::frame
{

std::optional<RuntimeViolation> DetectBlackFrame(bool had3DContent, const std::uint8_t centerPixelRGB[3], int threshold)
{
    if (!had3DContent)
        return std::nullopt;

    const int r = centerPixelRGB[0];
    const int g = centerPixelRGB[1];
    const int b = centerPixelRGB[2];
    if (r <= threshold && g <= threshold && b <= threshold)
    {
        RuntimeViolation v;
        v.ruleId = "I-29";
        v.detail = "scene had 3D draws but center pixel RGB=(" + std::to_string(r) + "," + std::to_string(g) + "," +
                   std::to_string(b) + ") <= " + std::to_string(threshold);
        return v;
    }
    return std::nullopt;
}

std::optional<RuntimeViolation> DetectMissingMeshHandles(unsigned int tlDrawCount,
                                                         unsigned int tlDrawsWithMissingMeshHandle)
{
    if (tlDrawsWithMissingMeshHandle == 0)
        return std::nullopt;
    RuntimeViolation v;
    v.ruleId = "I-22";
    v.detail = std::to_string(tlDrawsWithMissingMeshHandle) + " of " + std::to_string(tlDrawCount) +
               " TL draws reached the frame layer with mesh.id == 0";
    return v;
}

std::optional<RuntimeViolation> DetectViewportMismatch(int expectedX, int expectedY, int expectedW, int expectedH,
                                                       const int liveRect[4], int tolerancePx)
{
    auto absDiff = [](int a, int b) { return a > b ? a - b : b - a; };
    if (absDiff(expectedX, liveRect[0]) > tolerancePx || absDiff(expectedY, liveRect[1]) > tolerancePx ||
        absDiff(expectedW, liveRect[2]) > tolerancePx || absDiff(expectedH, liveRect[3]) > tolerancePx)
    {
        RuntimeViolation v;
        v.ruleId = "I-24";
        v.detail = "GL viewport (" + std::to_string(liveRect[0]) + "," + std::to_string(liveRect[1]) + "," +
                   std::to_string(liveRect[2]) + "," + std::to_string(liveRect[3]) + ") != SceneInputs (" +
                   std::to_string(expectedX) + "," + std::to_string(expectedY) + "," + std::to_string(expectedW) + "," +
                   std::to_string(expectedH) + ")";
        return v;
    }
    return std::nullopt;
}

std::optional<RuntimeViolation> DetectBindCacheDivergence(int unit, unsigned int cachedHandle,
                                                          unsigned int liveGLHandle)
{
    if (cachedHandle == liveGLHandle)
        return std::nullopt;
    RuntimeViolation v;
    v.ruleId = "B-007";
    v.detail = "bind cache says unit " + std::to_string(unit) + " holds texture " + std::to_string(cachedHandle) +
               " but GL has " + std::to_string(liveGLHandle) + " (deleted/recycled handle or bind outside the cache)";
    return v;
}

} // namespace render::frame

} // namespace Poseidon
