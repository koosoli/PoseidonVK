#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Graphics/Rendering/Frame/BuildFrame.hpp>
#include <Poseidon/Graphics/Rendering/Frame/FrameStats.hpp>
#include <Poseidon/Graphics/Rendering/Frame/ValidateFrame.hpp>

#include <chrono>

// Perf budget — the frame validator runs on every rendered frame
// (ObserveRenderedFrame), so its pure stages must stay linear in the
// draw count.  This bounds BuildFrame + ValidateFrame + CountFrameStats
// against a synthetic frame ~2x denser than a real mission (the
// frame_shape probe measured ~300 cutout + ~5 transparent draws on
// command_menu_basic; the menu scene ~65 total).
//
// Teeth: an O(N^2) insertion or a per-draw allocation explosion pushes
// the per-frame cost to milliseconds at this density; the linear path
// sits at tens of microseconds.  The 2ms ceiling is generous against
// CI noise while catching the quadratic class.  Best-of-batches timing
// (minimum batch mean) filters scheduler spikes.

namespace v2 = Poseidon::render::frame;

namespace
{

v2::SceneDraw makeDraw(Poseidon::render::PassKind pass)
{
    v2::SceneDraw d;
    d.descriptor.pass = pass;
    d.descriptor.fog = Poseidon::render::FogMode::Disabled;
    d.mesh.id = 7;
    d.indexBegin = 0;
    d.indexCount = 36;
    return d;
}

v2::SceneInputs makeDenseScene()
{
    v2::SceneInputs s;
    s.camera.viewport = {0, 0, 1920, 1080};
    s.flags.shadowsEnabled = true;
    s.flags.inFirstPersonView = true;
    s.flags.hudEnabled = true;

    for (int i = 0; i < 50; ++i)
        s.skyDraws.push_back(makeDraw(Poseidon::render::PassKind::WorldOpaque));
    for (int i = 0; i < 200; ++i)
        s.worldOpaqueDraws.push_back(makeDraw(Poseidon::render::PassKind::WorldOpaque));
    for (int i = 0; i < 1000; ++i)
        s.worldCutoutDraws.push_back(makeDraw(Poseidon::render::PassKind::WorldCutout));
    for (int i = 0; i < 100; ++i)
        s.surfaceOverlayDraws.push_back(makeDraw(Poseidon::render::PassKind::SurfaceOverlay));
    for (int i = 0; i < 200; ++i)
        s.worldTransparentDraws.push_back(makeDraw(Poseidon::render::PassKind::WorldTransparent));
    for (int i = 0; i < 50; ++i)
        s.cockpitDraws.push_back(makeDraw(Poseidon::render::PassKind::CockpitOpaque));
    for (int i = 0; i < 30; ++i)
        s.hudDraws.push_back(makeDraw(Poseidon::render::PassKind::ScreenSpace3D));
    // Fix the SurfaceOverlay descriptors so ValidateFrame stays clean.
    for (auto& d : s.surfaceOverlayDraws)
        d.descriptor.surface = Poseidon::render::SurfaceMode::OnSurface;
    return s;
}

} // namespace

TEST_CASE("Frame/perf: validator stages stay linear on a dense frame", "[render-frame][perf-budget]")
{
    const v2::SceneInputs s = makeDenseScene();

    // Warm-up (allocators, caches).
    {
        const v2::Frame f = v2::BuildFrame(s);
        REQUIRE(v2::ValidateFrame(f).ok());
        REQUIRE(v2::CountFrameStats(f).drawCount == 1630u);
    }

    constexpr int kBatches = 5;
    constexpr int kPerBatch = 10;
    double bestBatchMeanUs = 1e9;
    for (int b = 0; b < kBatches; ++b)
    {
        const auto t0 = std::chrono::steady_clock::now();
        unsigned int sink = 0;
        for (int i = 0; i < kPerBatch; ++i)
        {
            const v2::Frame f = v2::BuildFrame(s);
            const auto vr = v2::ValidateFrame(f);
            sink += static_cast<unsigned int>(vr.violations.size());
            sink += v2::CountFrameStats(f).drawCount;
        }
        const auto t1 = std::chrono::steady_clock::now();
        REQUIRE(sink > 0); // keep the loop observable
        const double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / kPerBatch;
        if (us < bestBatchMeanUs)
            bestBatchMeanUs = us;
    }

    INFO("best batch mean: " << bestBatchMeanUs << " us per frame");
    REQUIRE(bestBatchMeanUs < 2000.0);
}
