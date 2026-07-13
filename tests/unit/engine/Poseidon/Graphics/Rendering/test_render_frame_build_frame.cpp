#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Graphics/Rendering/Frame/BuildFrame.hpp>
#include <Poseidon/Graphics/Rendering/Frame/ValidateFrame.hpp>

#include <cstring>
#include <vector>

// Phase E.4 — `BuildFrame(SceneInputs)` is the central pure decision
// point of the frame layer.  These tests pin its contract against synthetic
// inputs.  No engine, no GL, no scene-graph: every test constructs
// the minimal `SceneInputs` it needs inline.

namespace v2 = Poseidon::render::frame;

namespace
{

// Minimal valid SceneInputs — used as a base by each test that
// only cares about specific fields.
Poseidon::render::frame::SceneInputs makeMinimal()
{
    Poseidon::render::frame::SceneInputs s;
    s.camera.viewport = {0, 0, 800, 600};
    s.flags.hudEnabled = false; // most tests don't need HUD
    return s;
}

// Synthetic draw with a sensible default descriptor.
Poseidon::render::frame::SceneDraw makeDraw()
{
    Poseidon::render::frame::SceneDraw d;
    d.descriptor.pass = Poseidon::render::PassKind::WorldOpaque;
    d.descriptor.depth = Poseidon::render::DepthMode::Normal;
    d.descriptor.blend = Poseidon::render::BlendMode::Opaque;
    d.indexCount = 3; // one triangle
    return d;
}

// Cockpit-family draw; Phase 4 validator requires FogMode::Disabled.
Poseidon::render::frame::SceneDraw makeCockpitDraw()
{
    Poseidon::render::frame::SceneDraw d = makeDraw();
    d.descriptor.pass = Poseidon::render::PassKind::CockpitOpaque;
    d.descriptor.fog = Poseidon::render::FogMode::Disabled;
    return d;
}

// Transparent draw; Phase 4 validator requires alpha info when
// BlendMode::AlphaBlend is set.
Poseidon::render::frame::SceneDraw makeTransparentDraw()
{
    Poseidon::render::frame::SceneDraw d = makeDraw();
    d.descriptor.pass = Poseidon::render::PassKind::WorldTransparent;
    d.descriptor.blend = Poseidon::render::BlendMode::AlphaBlend;
    d.descriptor.alpha = Poseidon::render::AlphaMode::Test;
    d.descriptor.alphaRef = 1;
    return d;
}

// Surface overlay (road/decal) — disambiguates against terrain
// via SurfaceMode::OnSurface + PassKind::SurfaceOverlay.
Poseidon::render::frame::SceneDraw makeSurfaceOverlayDraw()
{
    Poseidon::render::frame::SceneDraw d = makeDraw();
    d.descriptor.pass = Poseidon::render::PassKind::SurfaceOverlay;
    d.descriptor.surface = Poseidon::render::SurfaceMode::OnSurface;
    return d;
}

// Water draw — Water pass needs PassKind::WorldWater.
Poseidon::render::frame::SceneDraw makeWaterDraw()
{
    Poseidon::render::frame::SceneDraw d = makeDraw();
    d.descriptor.pass = Poseidon::render::PassKind::WorldWater;
    d.descriptor.shader = Poseidon::render::ShaderFamily::Water;
    return d;
}

} // namespace

// Empty inputs -> fallback clear pass

TEST_CASE("Frame/BuildFrame: empty inputs produce a single clear-only pass", "[render-frame][build-frame]")
{
    const auto f = Poseidon::render::frame::BuildFrame(makeMinimal());

    REQUIRE(f.passes.size() == 1);
    REQUIRE(f.passes[0].kind == Poseidon::render::frame::FramePassKind::ScreenSpace);
    REQUIRE(f.passes[0].clearColor);
    REQUIRE(f.passes[0].clearDepth);
    REQUIRE(f.passes[0].draws.empty());
}

TEST_CASE("Frame/BuildFrame: empty-inputs frame validates clean", "[render-frame][build-frame]")
{
    const auto f = Poseidon::render::frame::BuildFrame(makeMinimal());
    const auto r = Poseidon::render::frame::ValidateFrame(f);
    REQUIRE(r.ok());
}

// Camera passthrough — Frame.camera mirrors SceneInputs.camera

TEST_CASE("Frame/BuildFrame: camera viewport and matrices pass through unchanged", "[render-frame][build-frame]")
{
    auto s = makeMinimal();
    s.camera.viewport = {10, 20, 1280, 720};
    s.camera.nearPlane = 0.01f;
    s.camera.farPlane = 5000.0f;

    const auto f = Poseidon::render::frame::BuildFrame(s);
    REQUIRE(f.camera.viewport.x == 10);
    REQUIRE(f.camera.viewport.y == 20);
    REQUIRE(f.camera.viewport.width == 1280);
    REQUIRE(f.camera.viewport.height == 720);
    REQUIRE(f.camera.nearPlane == 0.01f);
    REQUIRE(f.camera.farPlane == 5000.0f);
}

// Single-pass scenes — minimum non-trivial frames

TEST_CASE("Frame/BuildFrame: sun direction passes through unchanged", "[render-frame][build-frame]")
{
    auto s = makeMinimal();
    s.sunEnabled = true;
    s.sunDirection[0] = 0.25f;
    s.sunDirection[1] = -0.75f;
    s.sunDirection[2] = 0.5f;

    const auto f = Poseidon::render::frame::BuildFrame(s);
    REQUIRE(f.sunEnabled);
    REQUIRE(f.sunDirection[0] == 0.25f);
    REQUIRE(f.sunDirection[1] == -0.75f);
    REQUIRE(f.sunDirection[2] == 0.5f);
}

TEST_CASE("Frame/BuildFrame: local lights pass through unchanged", "[render-frame][build-frame]")
{
    auto s = makeMinimal();
    s.localLightScale = 0.6f;
    s.localLightCount = 1;
    s.localLights[0].position[0] = 1.0f;
    s.localLights[0].position[1] = 2.0f;
    s.localLights[0].position[2] = 3.0f;
    s.localLights[0].direction[0] = 0.0f;
    s.localLights[0].direction[1] = -1.0f;
    s.localLights[0].direction[2] = 0.0f;
    s.localLights[0].diffuse[0] = 0.7f;
    s.localLights[0].ambient[1] = 0.2f;
    s.localLights[0].startAtten = 50.0f;
    s.localLights[0].kind = v2::LocalLightKind::Spot;

    const auto f = Poseidon::render::frame::BuildFrame(s);
    REQUIRE(f.localLightScale == 0.6f);
    REQUIRE(f.localLightCount == 1);
    REQUIRE(f.localLights[0].position[0] == 1.0f);
    REQUIRE(f.localLights[0].position[1] == 2.0f);
    REQUIRE(f.localLights[0].position[2] == 3.0f);
    REQUIRE(f.localLights[0].direction[1] == -1.0f);
    REQUIRE(f.localLights[0].diffuse[0] == 0.7f);
    REQUIRE(f.localLights[0].ambient[1] == 0.2f);
    REQUIRE(f.localLights[0].startAtten == 50.0f);
    REQUIRE(f.localLights[0].kind == v2::LocalLightKind::Spot);
}

TEST_CASE("Frame/BuildFrame: one WorldOpaque draw -> single WorldOpaque pass with clear", "[render-frame][build-frame]")
{
    auto s = makeMinimal();
    s.worldOpaqueDraws.push_back(makeDraw());

    const auto f = Poseidon::render::frame::BuildFrame(s);
    REQUIRE(f.passes.size() == 1);
    REQUIRE(f.passes[0].kind == Poseidon::render::frame::FramePassKind::WorldOpaque);
    REQUIRE(f.passes[0].draws.size() == 1);
    REQUIRE(f.passes[0].clearColor); // first emitted pass gets the clear
    REQUIRE(f.passes[0].clearDepth);
}

TEST_CASE("Frame/BuildFrame: empty-cockpit + WorldOpaque -> cockpit pass not emitted", "[render-frame][build-frame]")
{
    auto s = makeMinimal();
    s.worldOpaqueDraws.push_back(makeDraw());
    s.flags.inFirstPersonView = true;
    // cockpitDraws stays empty

    const auto f = Poseidon::render::frame::BuildFrame(s);
    REQUIRE(f.passes.size() == 1);
    REQUIRE(f.passes[0].kind == Poseidon::render::frame::FramePassKind::WorldOpaque);
}

// Canonical pass ordering

TEST_CASE("Frame/BuildFrame: pass order is Opaque -> Cutout -> Transparent -> Cockpit -> HUD",
          "[render-frame][build-frame]")
{
    auto s = makeMinimal();
    s.flags.hudEnabled = true;
    s.flags.inFirstPersonView = true;

    Poseidon::render::frame::SceneDraw od = makeDraw();
    od.descriptor.pass = Poseidon::render::PassKind::WorldOpaque;
    s.worldOpaqueDraws.push_back(od);

    Poseidon::render::frame::SceneDraw cd = makeDraw();
    cd.descriptor.pass = Poseidon::render::PassKind::WorldCutout;
    s.worldCutoutDraws.push_back(cd);

    s.worldTransparentDraws.push_back(makeTransparentDraw());
    s.cockpitDraws.push_back(makeCockpitDraw());

    Poseidon::render::frame::SceneDraw hd = makeDraw();
    hd.descriptor.pass = Poseidon::render::PassKind::ScreenSpace3D;
    hd.descriptor.fog = Poseidon::render::FogMode::Disabled; // 3D UI draws after the world
    s.hudDraws.push_back(hd);

    const auto f = Poseidon::render::frame::BuildFrame(s);
    REQUIRE(f.passes.size() == 5);
    REQUIRE(f.passes[0].kind == Poseidon::render::frame::FramePassKind::WorldOpaque);
    REQUIRE(f.passes[1].kind == Poseidon::render::frame::FramePassKind::WorldCutout);
    REQUIRE(f.passes[2].kind == Poseidon::render::frame::FramePassKind::WorldTransparent);
    REQUIRE(f.passes[3].kind == Poseidon::render::frame::FramePassKind::Cockpit);
    REQUIRE(f.passes[4].kind == Poseidon::render::frame::FramePassKind::ScreenSpace);

    // Clear lives on the first emitted 3D pass only.
    REQUIRE(f.passes[0].clearColor);
    REQUIRE_FALSE(f.passes[1].clearColor);
    REQUIRE_FALSE(f.passes[2].clearColor);

    // Frame must validate clean — canonical ordering, descriptor
    // alignment with pass kinds, viewport > 0, OnSurface absent.
    const auto r = Poseidon::render::frame::ValidateFrame(f);
    REQUIRE(r.ok());
}

// Pass-skipping behaviour

TEST_CASE("Frame/BuildFrame: skipping middle passes preserves order of remaining", "[render-frame][build-frame]")
{
    auto s = makeMinimal();
    s.worldOpaqueDraws.push_back(makeDraw());
    s.worldTransparentDraws.push_back(makeTransparentDraw());
    // worldCutoutDraws empty — should be skipped, not present as empty Pass

    const auto f = Poseidon::render::frame::BuildFrame(s);
    REQUIRE(f.passes.size() == 2);
    REQUIRE(f.passes[0].kind == Poseidon::render::frame::FramePassKind::WorldOpaque);
    REQUIRE(f.passes[1].kind == Poseidon::render::frame::FramePassKind::WorldTransparent);
    REQUIRE(Poseidon::render::frame::ValidateFrame(f).ok());
}

TEST_CASE("Frame/BuildFrame: HUD-only frame produces ScreenSpace pass with HUD draws and clear",
          "[render-frame][build-frame]")
{
    auto s = makeMinimal();
    s.flags.hudEnabled = true;

    Poseidon::render::frame::SceneDraw hd = makeDraw();
    hd.descriptor.pass = Poseidon::render::PassKind::ScreenSpace3D;
    hd.descriptor.fog = Poseidon::render::FogMode::Disabled; // 3D UI draws after the world
    s.hudDraws.push_back(hd);

    const auto f = Poseidon::render::frame::BuildFrame(s);
    REQUIRE(f.passes.size() == 1);
    REQUIRE(f.passes[0].kind == Poseidon::render::frame::FramePassKind::ScreenSpace);
    REQUIRE(f.passes[0].draws.size() == 1);
    // No 3D pass emitted, so clear lands on the only Pass (the HUD).
    REQUIRE(f.passes[0].clearColor);
}

// Visibility flags

TEST_CASE("Frame/BuildFrame: HUD-disabled flag drops the HUD pass", "[render-frame][build-frame]")
{
    auto s = makeMinimal();
    s.flags.hudEnabled = false;
    Poseidon::render::frame::SceneDraw hd = makeDraw();
    s.hudDraws.push_back(hd);
    s.worldOpaqueDraws.push_back(makeDraw());

    const auto f = Poseidon::render::frame::BuildFrame(s);
    REQUIRE(f.passes.size() == 1);
    REQUIRE(f.passes[0].kind == Poseidon::render::frame::FramePassKind::WorldOpaque);
}

TEST_CASE("Frame/BuildFrame: cockpit pass omitted when not in first-person", "[render-frame][build-frame]")
{
    auto s = makeMinimal();
    s.flags.inFirstPersonView = false;
    s.cockpitDraws.push_back(makeCockpitDraw());
    s.worldOpaqueDraws.push_back(makeDraw());

    const auto f = Poseidon::render::frame::BuildFrame(s);
    // Only WorldOpaque pass.  Cockpit suppressed by flag.
    REQUIRE(f.passes.size() == 1);
    REQUIRE(f.passes[0].kind == Poseidon::render::frame::FramePassKind::WorldOpaque);
}

// Frame validates clean across a representative set of inputs

// Phase E.8 — new pass buckets

TEST_CASE("Frame/BuildFrame: SurfaceOverlay draws produce a SurfaceOverlay pass", "[render-frame][build-frame][E.8]")
{
    auto s = makeMinimal();
    s.worldOpaqueDraws.push_back(makeDraw());
    s.surfaceOverlayDraws.push_back(makeSurfaceOverlayDraw());

    const auto f = Poseidon::render::frame::BuildFrame(s);
    REQUIRE(f.passes.size() == 2);
    REQUIRE(f.passes[0].kind == Poseidon::render::frame::FramePassKind::WorldOpaque);
    REQUIRE(f.passes[1].kind == Poseidon::render::frame::FramePassKind::SurfaceOverlay);
    REQUIRE(Poseidon::render::frame::ValidateFrame(f).ok());
}

TEST_CASE("Frame/BuildFrame: Water draws produce a Water pass", "[render-frame][build-frame][E.8]")
{
    auto s = makeMinimal();
    s.worldOpaqueDraws.push_back(makeDraw());
    s.waterDraws.push_back(makeWaterDraw());

    const auto f = Poseidon::render::frame::BuildFrame(s);
    REQUIRE(f.passes.size() == 2);
    REQUIRE(f.passes[0].kind == Poseidon::render::frame::FramePassKind::WorldOpaque);
    REQUIRE(f.passes[1].kind == Poseidon::render::frame::FramePassKind::Water);
    REQUIRE(Poseidon::render::frame::ValidateFrame(f).ok());
}

TEST_CASE("Frame/BuildFrame: Sky pass emits first among 3D passes (before WorldOpaque)",
          "[render-frame][build-frame][E.8]")
{
    auto s = makeMinimal();
    Poseidon::render::frame::SceneDraw sky = makeDraw();
    sky.descriptor.depth = Poseidon::render::DepthMode::Disabled; // sky's typical setup
    s.skyDraws.push_back(sky);
    s.worldOpaqueDraws.push_back(makeDraw());

    const auto f = Poseidon::render::frame::BuildFrame(s);
    REQUIRE(f.passes.size() == 2);
    REQUIRE(f.passes[0].kind == Poseidon::render::frame::FramePassKind::Sky);
    REQUIRE(f.passes[1].kind == Poseidon::render::frame::FramePassKind::WorldOpaque);
    // Clear placed on Sky (first emitted).
    REQUIRE(f.passes[0].clearColor);
    REQUIRE_FALSE(f.passes[1].clearColor);
    REQUIRE(Poseidon::render::frame::ValidateFrame(f).ok());
}

TEST_CASE("Frame/BuildFrame: ShadowAccum emits when shadowsEnabled + non-empty bucket",
          "[render-frame][build-frame][E.8]")
{
    auto s = makeMinimal();
    s.flags.shadowsEnabled = true;
    Poseidon::render::frame::SceneDraw sd = makeDraw();
    // Shadow descriptors have stricter Phase 4 requirements; for
    // the bucket-emit test we just need a draw present — Phase 4
    // validator will fail on a malformed shadow descriptor in
    // the descriptor-validity test, not here.
    sd.descriptor.pass = Poseidon::render::PassKind::WorldShadow;
    sd.descriptor.blend = Poseidon::render::BlendMode::Shadow;
    sd.descriptor.depth = Poseidon::render::DepthMode::Shadow;
    sd.descriptor.shader = Poseidon::render::ShaderFamily::Shadow;
    sd.descriptor.stencilExclusion = true;
    sd.descriptor.lighting = Poseidon::render::LightingMode::ShadowDarkPolygon;
    s.shadowDraws.push_back(sd);
    s.worldOpaqueDraws.push_back(makeDraw());

    const auto f = Poseidon::render::frame::BuildFrame(s);
    REQUIRE(f.passes.size() == 2);
    REQUIRE(f.passes[0].kind == Poseidon::render::frame::FramePassKind::ShadowAccum);
    REQUIRE(f.passes[1].kind == Poseidon::render::frame::FramePassKind::WorldOpaque);
    REQUIRE(Poseidon::render::frame::ValidateFrame(f).ok());
}

TEST_CASE("Frame/BuildFrame: shadowsEnabled=false suppresses ShadowAccum even with draws",
          "[render-frame][build-frame][E.8]")
{
    auto s = makeMinimal();
    s.flags.shadowsEnabled = false;
    s.shadowDraws.push_back(makeDraw()); // present but should not emit
    s.worldOpaqueDraws.push_back(makeDraw());

    const auto f = Poseidon::render::frame::BuildFrame(s);
    REQUIRE(f.passes.size() == 1);
    REQUIRE(f.passes[0].kind == Poseidon::render::frame::FramePassKind::WorldOpaque);
}

TEST_CASE("Frame/BuildFrame: CSM depth schedules before receivers without clearing them",
           "[render-frame][build-frame][shadow]")
{
    auto s = makeMinimal();
    s.flags.shadowsEnabled = true;
    s.shadowInput.enabled = true;
    s.shadowInput.sunFactor = 1.0f;
    Poseidon::render::frame::ShadowCaster caster;
    caster.mesh.id = 41;
    caster.indexCount = 3;
    caster.world._11 = caster.world._22 = caster.world._33 = caster.world._44 = 1.0f;
    s.shadowInput.casters.push_back(caster);
    s.worldOpaqueDraws.push_back(makeDraw());

    const auto f = Poseidon::render::frame::BuildFrame(s);
    REQUIRE(f.passes.size() == 2);
    CHECK(f.passes[0].kind == Poseidon::render::frame::FramePassKind::ShadowDepth);
    CHECK(f.passes[0].draws.empty());
    CHECK_FALSE(f.passes[0].clearColor);
    CHECK(f.passes[1].kind == Poseidon::render::frame::FramePassKind::WorldOpaque);
    CHECK(f.passes[1].clearColor);
    REQUIRE(f.shadowInput.casters.size() == 1);
    CHECK(f.shadowInput.casters[0].mesh.id == 41);
    REQUIRE(Poseidon::render::frame::ValidateFrame(f).ok());
}

TEST_CASE("Frame/BuildFrame: full canonical-order frame with all 7 emittable passes",
          "[render-frame][build-frame][E.8]")
{
    auto s = makeMinimal();
    s.flags.shadowsEnabled = true;
    s.flags.inFirstPersonView = true;
    s.flags.hudEnabled = true;

    // Sky
    Poseidon::render::frame::SceneDraw sky = makeDraw();
    sky.descriptor.depth = Poseidon::render::DepthMode::Disabled;
    s.skyDraws.push_back(sky);

    // Shadow (valid Phase 4 descriptor — see test above)
    Poseidon::render::frame::SceneDraw sd = makeDraw();
    sd.descriptor.pass = Poseidon::render::PassKind::WorldShadow;
    sd.descriptor.blend = Poseidon::render::BlendMode::Shadow;
    sd.descriptor.depth = Poseidon::render::DepthMode::Shadow;
    sd.descriptor.shader = Poseidon::render::ShaderFamily::Shadow;
    sd.descriptor.stencilExclusion = true;
    sd.descriptor.lighting = Poseidon::render::LightingMode::ShadowDarkPolygon;
    s.shadowDraws.push_back(sd);

    s.worldOpaqueDraws.push_back(makeDraw());
    {
        Poseidon::render::frame::SceneDraw cd = makeDraw();
        cd.descriptor.pass = Poseidon::render::PassKind::WorldCutout;
        s.worldCutoutDraws.push_back(cd);
    }
    s.surfaceOverlayDraws.push_back(makeSurfaceOverlayDraw());
    s.waterDraws.push_back(makeWaterDraw());
    s.worldTransparentDraws.push_back(makeTransparentDraw());
    s.cockpitDraws.push_back(makeCockpitDraw());
    {
        Poseidon::render::frame::SceneDraw hd = makeDraw();
        hd.descriptor.pass = Poseidon::render::PassKind::ScreenSpace3D;
        hd.descriptor.fog = Poseidon::render::FogMode::Disabled; // 3D UI draws after the world
        s.hudDraws.push_back(hd);
    }

    const auto f = Poseidon::render::frame::BuildFrame(s);
    REQUIRE(f.passes.size() == 9);
    REQUIRE(f.passes[0].kind == Poseidon::render::frame::FramePassKind::ShadowAccum);
    REQUIRE(f.passes[1].kind == Poseidon::render::frame::FramePassKind::Sky);
    REQUIRE(f.passes[2].kind == Poseidon::render::frame::FramePassKind::WorldOpaque);
    REQUIRE(f.passes[3].kind == Poseidon::render::frame::FramePassKind::WorldCutout);
    REQUIRE(f.passes[4].kind == Poseidon::render::frame::FramePassKind::SurfaceOverlay);
    REQUIRE(f.passes[5].kind == Poseidon::render::frame::FramePassKind::Water);
    REQUIRE(f.passes[6].kind == Poseidon::render::frame::FramePassKind::WorldTransparent);
    REQUIRE(f.passes[7].kind == Poseidon::render::frame::FramePassKind::Cockpit);
    REQUIRE(f.passes[8].kind == Poseidon::render::frame::FramePassKind::ScreenSpace);

    // Clear on first emitted only.
    REQUIRE(f.passes[0].clearColor);
    for (size_t i = 1; i < f.passes.size(); ++i)
        REQUIRE_FALSE(f.passes[i].clearColor);

    REQUIRE(Poseidon::render::frame::ValidateFrame(f).ok());
}

TEST_CASE("Frame/BuildFrame: output frame validates clean for representative scenes", "[render-frame][build-frame]")
{
    // Empty.
    REQUIRE(Poseidon::render::frame::ValidateFrame(Poseidon::render::frame::BuildFrame(makeMinimal())).ok());

    // World only.
    auto a = makeMinimal();
    a.worldOpaqueDraws.push_back(makeDraw());
    REQUIRE(Poseidon::render::frame::ValidateFrame(Poseidon::render::frame::BuildFrame(a)).ok());

    // Full pipeline.
    auto b = makeMinimal();
    b.flags.hudEnabled = true;
    b.flags.inFirstPersonView = true;
    b.worldOpaqueDraws.push_back(makeDraw());
    {
        Poseidon::render::frame::SceneDraw d = makeDraw();
        d.descriptor.pass = Poseidon::render::PassKind::WorldCutout;
        b.worldCutoutDraws.push_back(d);
    }
    b.cockpitDraws.push_back(makeCockpitDraw());
    {
        Poseidon::render::frame::SceneDraw d = makeDraw();
        d.descriptor.pass = Poseidon::render::PassKind::ScreenSpace3D;
        d.descriptor.fog = Poseidon::render::FogMode::Disabled;
        b.hudDraws.push_back(d);
    }
    REQUIRE(Poseidon::render::frame::ValidateFrame(Poseidon::render::frame::BuildFrame(b)).ok());
}
