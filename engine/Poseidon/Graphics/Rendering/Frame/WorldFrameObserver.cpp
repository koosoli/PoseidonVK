#include "WorldFrameObserver.hpp"

#include <Poseidon/Foundation/Platform/AppConfig.hpp>

#include "BuildFrame.hpp"
#include "FrameStats.hpp"
#include "RuntimeChecks.hpp"
#include "SceneExtractor.hpp"
#include "ValidateFrame.hpp"

#include <Poseidon/Graphics/Core/Engine.hpp>

namespace Poseidon
{

namespace render::frame
{

namespace
{
// Pass shape of the most recently observed frame — render thread only.
std::vector<ObservedPass> s_lastShape;
} // namespace

const std::vector<ObservedPass>& LastObservedFrameShape()
{
    return s_lastShape;
}

void ObserveRenderedFrame(Engine& engine, Scene& scene)
{
    // Diagnostics, not rendering: extraction walks every draw bucket
    // (O(draw calls) copies per frame) and the validators scan the result.
    // Sample every 16th frame in normal play — a real invariant violation
    // is a persistent state, so sampling still catches it within ~0.3 s.
    // The tri harness (--autotest) keeps full per-frame coverage because
    // tests assert on observer output (pass shape, viewport, near-black)
    // at exact frames.
    {
        static int s_sampleCountdown = 0;
        // POSEIDON_OBSERVER_SAMPLE=1 forces sampling under the harness so
        // the perf bench can measure normal-play behavior.
        static const bool s_everyFrame =
            Foundation::AppConfig::Instance().AutoTest() && getenv("POSEIDON_OBSERVER_SAMPLE") == nullptr;
        if (!s_everyFrame)
        {
            if (--s_sampleCountdown > 0)
                return;
            s_sampleCountdown = 16;
        }
    }
    // Frame-static state: tracks the GL error count across frames
    // so only *new* errors are reported; throttles violation logs
    // so a stable bug doesn't spam every frame; counts down to
    // the next stats summary line.
    static unsigned int s_lastGlErrorCount = 0;
    static int s_violationLogs = 0;
    static int s_statsCountdown = 0;
    // Transient-frame tolerance: a loading screen, scene swap,
    // pre-dawn cutscene (e.g. the demo intro at 5:35 AM with center
    // pixel ~(6,6,8)), or any short fade can produce a string of
    // legitimately near-black frames.  Only escalate to a logged
    // violation after roughly half a second of solid near-black so
    // we don't spam during transitions — a catastrophic clear-too-
    // late or scene-matrix bug holds the black for far longer and
    // still gets caught.
    static int s_consecutiveBlackFrames = 0;
    static int s_blackFrameLogs = 0;
    static int s_viewportMismatchLogs = 0;
    static int s_meshHandleLogs = 0;

    SceneInputs si = ExtractSceneInputs(engine, scene);
    si.lastObservedDebugErrorCount = s_lastGlErrorCount;
    s_lastGlErrorCount = si.currentDebugErrorCount;

    const Frame f = BuildFrame(si);

    // Snapshot the pass shape for the tri harness.
    s_lastShape.clear();
    for (const auto& p : f.passes)
        s_lastShape.push_back({p.kind, static_cast<int>(p.draws.size())});

    const auto vr = ValidateFrame(f);

    if (!vr.ok() && s_violationLogs < 5)
    {
        for (const auto& v : vr.violations)
            LOG_DEBUG(Graphics, "ValidateFrame {}: {}", v.ruleId, v.detail.c_str());
        ++s_violationLogs;
    }

    // Viewport runtime check — the GL viewport at the observation seam
    // must match the rect SceneInputs carried.  Mismatch indicates
    // a resize / scissor / blit-target bug that screenshot tests
    // can mask (rendered content still looks plausible).  Throttle
    // the log so a stuck-mismatch only fires five times.
    {
        int liveVp[4] = {0, 0, 0, 0};
        if (engine.GetGLViewport(liveVp))
        {
            // SceneInputs carries window-space coords; with SSAA the live GL
            // viewport is the render-scaled frame target.  Scale expectations
            // the same way RenderTargetSize does so scale 1.5/2 stays clean.
            const float rs = engine.GetRenderScale();
            const auto scaled = [rs](int v) { return static_cast<int>(v * rs + 0.5f); };
            const auto vio =
                DetectViewportMismatch(scaled(f.camera.viewport.x), scaled(f.camera.viewport.y),
                                       scaled(f.camera.viewport.width), scaled(f.camera.viewport.height), liveVp);
            if (vio && s_viewportMismatchLogs < 5)
            {
                LOG_WARN(Graphics, "frame runtime {}: {}", vio->ruleId, vio->detail.c_str());
                ++s_viewportMismatchLogs;
            }
        }
    }

    // Mesh-handle runtime check — every TL draw must reach the frame
    // layer with a resolvable backend mesh binding — EmitDraw silently skips
    // missing bindings.
    // Counts across all per-pass buckets so a missing capture site
    // anywhere in the engine is visible.
    {
        unsigned int tlCount = 0, missingMeshHandle = 0;
        auto count = [&](const std::vector<SceneDraw>& v)
        {
            for (const auto& d : v)
            {
                if (d.indexCount <= 0)
                    continue;
                ++tlCount;
                if (!d.mesh.HasBackendMesh())
                    ++missingMeshHandle;
            }
        };
        count(si.shadowDraws);
        count(si.skyDraws);
        count(si.worldOpaqueDraws);
        count(si.worldCutoutDraws);
        count(si.surfaceOverlayDraws);
        count(si.waterDraws);
        count(si.worldTransparentDraws);
        count(si.cockpitDraws);
        const auto vio = DetectMissingMeshHandles(tlCount, missingMeshHandle);
        if (vio && s_meshHandleLogs < 5)
        {
            LOG_WARN(Graphics, "frame runtime {}: {}", vio->ruleId, vio->detail.c_str());
            ++s_meshHandleLogs;
        }
    }

    // Near-black runtime check — a non-empty 3D scene should produce a
    // non-trivial centre pixel.  Catches catastrophic clear-too-late,
    // scene-matrix, or blit failures that the structural validators
    // can't see.  Counts opaque + transparent world buckets as the
    // "3D content was meant to be drawn" signal; HUD-only frames
    // (menus, options screen) are intentionally exempt.
    const bool had3DContent = !si.worldOpaqueDraws.empty() || !si.worldTransparentDraws.empty();
    if (had3DContent)
    {
        const int cx = f.camera.viewport.x + f.camera.viewport.width / 2;
        const int cy = f.camera.viewport.y + f.camera.viewport.height / 2;
        std::uint8_t rgb[3] = {255, 255, 255};
        if (engine.SamplePixel(cx, cy, rgb))
        {
            const auto vio = DetectBlackFrame(true, rgb);
            if (vio)
            {
                ++s_consecutiveBlackFrames;
                // Threshold of 30 (~0.5s at 60 FPS) lets transient
                // fades, dim pre-dawn cutscenes, and short scene
                // swaps pass without a warning; a real stuck-black
                // bug holds for far longer than half a second.
                // 5-log cap keeps a still-misbehaving world from
                // filling the log on every frame after that.
                if (s_consecutiveBlackFrames >= 30 && s_blackFrameLogs < 5)
                {
                    LOG_WARN(Graphics, "frame runtime {}: {} (consecutive frames: {})", vio->ruleId,
                             vio->detail.c_str(), s_consecutiveBlackFrames);
                    ++s_blackFrameLogs;
                }
            }
            else
            {
                s_consecutiveBlackFrames = 0;
            }
        }
    }
    else
    {
        s_consecutiveBlackFrames = 0;
    }

    // Optional per-frame stats summary — gated by AppConfig flag
    // so default gameplay logs nothing.  Fires on the first
    // observed frame (s_statsCountdown starts at 0) then every
    // 60th frame ≈ once a second at 60 Hz.
    if (AppConfig::Instance().RenderFrameLog())
    {
        if (s_statsCountdown <= 0)
        {
            const FrameStats s = CountFrameStats(f);
            LOG_INFO(Graphics,
                     "render frame: passes={} draws={} maxDrawsInPass={} uniqueTextures={} uniqueVertexBuffers={} uniqueIndexBuffers={} glErrorsThisFrame={}",
                     s.passCount, s.drawCount, s.maxDrawsInPass, s.uniqueTextureCount, s.uniqueVertexBufferCount,
                     s.uniqueIndexBufferCount, f.newDebugErrors);
            s_statsCountdown = 60;
        }
        else
        {
            --s_statsCountdown;
        }
    }
}

} // namespace render::frame

} // namespace Poseidon
