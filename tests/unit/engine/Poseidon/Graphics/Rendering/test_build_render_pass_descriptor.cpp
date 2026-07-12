#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Core/Types.hpp>
#include <Poseidon/Graphics/Rendering/BuildRenderPassDescriptor.hpp>
#include <catch2/catch_message.hpp>
#include <initializer_list>

// Phase 2.2: pin the translation from `LegacySpec` to
// `RenderPassDescriptor`.  This function is the single place that
// decodes legacy bits in Phase 2; every downstream consumer reads
// descriptor fields.  These tests pin the mapping exactly - a future
// rename / recategorization of any legacy bit that drifts the
// descriptor output fails here loudly before it reaches the renderer.

using namespace Poseidon::render;

namespace
{

// Convenience: build a descriptor from a raw legacy int.
RenderPassDescriptor buildFromInt(int legacyInt, const BuildContext& ctx = {})
{
    return BuildRenderPassDescriptor(SplitLegacy(legacyInt), ctx);
}

} // namespace

TEST_CASE("BuildRenderPassDescriptor: default (empty spec) -> opaque world", "[build-descriptor][phase2]")
{
    const RenderPassDescriptor d = buildFromInt(0);
    REQUIRE(d.pass == PassKind::WorldOpaque);
    REQUIRE(d.depth == DepthMode::Normal);
    REQUIRE(d.blend == BlendMode::Opaque);
    REQUIRE(d.fog == FogMode::Enabled);
    REQUIRE(d.alpha == AlphaMode::Disabled);
    REQUIRE(d.shader == ShaderFamily::Normal);
    REQUIRE(d.lighting == LightingMode::Lit);
    REQUIRE(d.surface == SurfaceMode::Default);
    REQUIRE(d.texGen == TexGenMode::None);
    REQUIRE(d.sampler.filter == SamplerFilter::Linear);
    REQUIRE_FALSE(d.sampler.clampU);
    REQUIRE_FALSE(d.sampler.clampV);
    REQUIRE_FALSE(d.stencilExclusion);
}

TEST_CASE("BuildRenderPassDescriptor: NoDropdown disables fog but does NOT route passes",
          "[build-descriptor][phase2][I-06]")
{
    // Pass-family selection is the explicit PassKindHint's job — the
    // NoDropdown bit keeps its fog-off and legacy LOD/PassNum meanings
    // only.  A NoDropdown spec without the hint stays in the World*
    // families; the cockpit draw scopes (Scene::DrawSortObject, the
    // soldier first-person proxies) set the hint explicitly.
    const RenderPassDescriptor d = buildFromInt(NoDropdown | FogDisabled);
    REQUIRE(d.pass == PassKind::WorldOpaque);
    REQUIRE(d.fog == FogMode::Disabled);
    REQUIRE(d.blend == BlendMode::Opaque);
    REQUIRE(d.alpha == AlphaMode::Disabled);
}

TEST_CASE("BuildRenderPassDescriptor: NoDropdown|IsTransparent stays WorldCutout without the hint",
          "[build-descriptor][phase2][I-06]")
{
    const RenderPassDescriptor d = buildFromInt(NoDropdown | IsTransparent);
    REQUIRE(d.pass == PassKind::WorldCutout);
    REQUIRE(d.fog == FogMode::Disabled);
    REQUIRE(d.blend == BlendMode::Opaque);
    REQUIRE(d.alpha == AlphaMode::Test);
    REQUIRE(d.alphaRef == 0xc0);
}

TEST_CASE("BuildRenderPassDescriptor: NoDropdown|IsAlpha stays WorldTransparent without the hint",
          "[build-descriptor][phase2][I-06]")
{
    const RenderPassDescriptor d = buildFromInt(NoDropdown | IsAlpha);
    REQUIRE(d.pass == PassKind::WorldTransparent);
    REQUIRE(d.fog == FogMode::Disabled);
    REQUIRE(d.blend == BlendMode::AlphaBlend);
    REQUIRE(d.alpha == AlphaMode::Test);
    REQUIRE(d.alphaRef == 1);
}

TEST_CASE("BuildRenderPassDescriptor: shadow spec sets shader family + stencil", "[build-descriptor][phase2]")
{
    BuildContext ctx;
    ctx.shadowAlphaRef = 0x40;

    const RenderPassDescriptor d = buildFromInt(IsShadow, ctx);
    REQUIRE(d.pass == PassKind::WorldShadow);
    REQUIRE(d.depth == DepthMode::Shadow);
    REQUIRE(d.blend == BlendMode::Shadow);
    REQUIRE(d.fog == FogMode::Disabled);
    REQUIRE(d.shader == ShaderFamily::Shadow);
    REQUIRE(d.alpha == AlphaMode::Test);
    REQUIRE(d.alphaRef == 0x40);
    REQUIRE(d.stencilExclusion);
    REQUIRE(d.lighting == LightingMode::ShadowDarkPolygon);
}

// Regression guard for the stencil-shadow dual-path bug (RenderDoc:
// shadows drawing as ALWAYS/REPLACE stencil-only with color writes off
// in some frames, never reaching the framebuffer).  The contract: a
// shadow spec ALWAYS resolves to the per-poly path — DepthMode::Shadow
// (EQUAL 0 / INCR), BlendMode::Shadow, color writes on — regardless of
// the 3D-pass flag.  The broken-state delta this guards against was a
// second path selected when isIn3DPass was true that wrote stencil only
// (color mask off) and left the actual darkening to a separate pass; if
// anyone reintroduces a flag-conditional shadow depth mode, the in3D
// case below diverges from the non-in3D case and this fails.
TEST_CASE("BuildRenderPassDescriptor: shadow path is always per-poly (no accumulator)",
          "[build-descriptor][phase2][stencil-shadow]")
{
    for (bool in3D : {true, false})
    {
        BuildContext ctx;
        ctx.isIn3DPass = in3D;

        const RenderPassDescriptor d = buildFromInt(IsShadow, ctx);
        INFO("isIn3DPass=" << in3D);
        REQUIRE(d.depth == DepthMode::Shadow);
        REQUIRE(d.blend == BlendMode::Shadow);
        REQUIRE(d.stencilExclusion);
    }
}

TEST_CASE("BuildRenderPassDescriptor: water path", "[build-descriptor][phase2]")
{
    const RenderPassDescriptor d = buildFromInt(IsWater);
    REQUIRE(d.pass == PassKind::WorldWater);
    REQUIRE(d.shader == ShaderFamily::Water);
    REQUIRE(d.blend == BlendMode::Opaque);
    REQUIRE(d.fog == FogMode::Enabled);
    REQUIRE(d.texGen == TexGenMode::Water);
}

TEST_CASE("BuildRenderPassDescriptor: water with FogDisabled honors routing", "[build-descriptor][phase2]")
{
    const RenderPassDescriptor d = buildFromInt(IsWater | FogDisabled);
    REQUIRE(d.pass == PassKind::WorldWater);
    REQUIRE(d.fog == FogMode::Disabled);
}

TEST_CASE("BuildRenderPassDescriptor: alpha-fog path", "[build-descriptor][phase2][I-07]")
{
    const RenderPassDescriptor d = buildFromInt(IsAlphaFog);
    REQUIRE(d.pass == PassKind::WorldTransparent);
    REQUIRE(d.blend == BlendMode::AlphaBlend);
    REQUIRE(d.fog == FogMode::AlphaFog);
    REQUIRE(d.alpha == AlphaMode::Test);
    REQUIRE(d.alphaRef == 1);
}

TEST_CASE("BuildRenderPassDescriptor: light volume path", "[build-descriptor][phase2]")
{
    const RenderPassDescriptor d = buildFromInt(IsLight);
    REQUIRE(d.pass == PassKind::WorldLight);
    REQUIRE(d.blend == BlendMode::Additive);
    REQUIRE(d.fog == FogMode::Disabled);
    REQUIRE(d.lighting == LightingMode::Unlit);
}

TEST_CASE("BuildRenderPassDescriptor: flare decal NoZBuf keeps light additive but disables depth",
          "[build-descriptor][phase2][flare]")
{
    const RenderPassDescriptor d = buildFromInt(NoZBuf | IsLight | ClampU | ClampV | IsAlphaFog);
    REQUIRE(d.pass == PassKind::WorldLight);
    REQUIRE(d.depth == DepthMode::Disabled);
    REQUIRE(d.blend == BlendMode::Additive);
    REQUIRE(d.fog == FogMode::Disabled);
    REQUIRE(d.alpha == AlphaMode::Test);
    REQUIRE(d.alphaRef == 1);
}

TEST_CASE("BuildRenderPassDescriptor: world alpha-blend (no cockpit, no alphafog)", "[build-descriptor][phase2]")
{
    const RenderPassDescriptor d = buildFromInt(IsAlpha);
    REQUIRE(d.pass == PassKind::WorldTransparent);
    REQUIRE(d.blend == BlendMode::AlphaBlend);
    REQUIRE(d.alpha == AlphaMode::Test);
    REQUIRE(d.alphaRef == 1);
    REQUIRE(d.fog == FogMode::Enabled);
}

TEST_CASE("BuildRenderPassDescriptor: cutout (IsTransparent without IsAlpha)", "[build-descriptor][phase2]")
{
    const RenderPassDescriptor d = buildFromInt(IsTransparent);
    REQUIRE(d.pass == PassKind::WorldCutout);
    REQUIRE(d.blend == BlendMode::Opaque);
    REQUIRE(d.alpha == AlphaMode::Test);
    REQUIRE(d.alphaRef == 0xc0);
}

TEST_CASE("BuildRenderPassDescriptor: depth-disabled (NoZBuf, e.g. sky)", "[build-descriptor][phase2]")
{
    const RenderPassDescriptor d = buildFromInt(NoZBuf);
    REQUIRE(d.depth == DepthMode::Disabled);
}

TEST_CASE("BuildRenderPassDescriptor: depth-read-only (NoZWrite)", "[build-descriptor][phase2]")
{
    const RenderPassDescriptor d = buildFromInt(NoZWrite);
    REQUIRE(d.depth == DepthMode::ReadOnly);
}

TEST_CASE("BuildRenderPassDescriptor: surface overlay routing", "[build-descriptor][phase2]")
{
    SECTION("OnSurface")
    {
        const RenderPassDescriptor d = buildFromInt(OnSurface);
        REQUIRE(d.surface == SurfaceMode::OnSurface);
        REQUIRE(d.pass == PassKind::SurfaceOverlay);
    }
    SECTION("IsOnSurface")
    {
        const RenderPassDescriptor d = buildFromInt(IsOnSurface);
        REQUIRE(d.surface == SurfaceMode::OnSurface);
        REQUIRE(d.pass == PassKind::SurfaceOverlay);
    }
    SECTION("Surface overlay does NOT override Shadow pass")
    {
        const RenderPassDescriptor d = buildFromInt(IsShadow | OnSurface);
        REQUIRE(d.surface == SurfaceMode::OnSurface);
        REQUIRE(d.pass == PassKind::WorldShadow); // shadow wins
    }
}

TEST_CASE("BuildRenderPassDescriptor: sampler bits", "[build-descriptor][phase2]")
{
    SECTION("PointSampling")
    {
        const RenderPassDescriptor d = buildFromInt(PointSampling);
        REQUIRE(d.sampler.filter == SamplerFilter::Point);
    }
    SECTION("ClampU only")
    {
        const RenderPassDescriptor d = buildFromInt(ClampU);
        REQUIRE(d.sampler.clampU);
        REQUIRE_FALSE(d.sampler.clampV);
    }
    SECTION("ClampU + ClampV")
    {
        const RenderPassDescriptor d = buildFromInt(ClampU | ClampV);
        REQUIRE(d.sampler.clampU);
        REQUIRE(d.sampler.clampV);
    }
}

TEST_CASE("BuildRenderPassDescriptor: lighting category - DisableSun", "[build-descriptor][phase2]")
{
    const RenderPassDescriptor d = buildFromInt(DisableSun);
    REQUIRE(d.lighting == LightingMode::SunDisabled);
    REQUIRE(d.pass == PassKind::WorldOpaque); // DisableSun alone doesn't change routing
}

TEST_CASE("BuildRenderPassDescriptor: multitexturing detail / grass", "[build-descriptor][phase2]")
{
    BuildContext ctx;
    ctx.isMultitexturing = true;

    SECTION("DetailTexture -> ShaderFamily::Detail + TexGen::Detail")
    {
        const RenderPassDescriptor d = buildFromInt(DetailTexture, ctx);
        REQUIRE(d.shader == ShaderFamily::Detail);
        REQUIRE(d.texGen == TexGenMode::Detail);
    }
    SECTION("GrassTexture -> ShaderFamily::Grass + TexGen::Grass")
    {
        const RenderPassDescriptor d = buildFromInt(GrassTexture, ctx);
        REQUIRE(d.shader == ShaderFamily::Grass);
        REQUIRE(d.texGen == TexGenMode::Grass);
    }
    SECTION("Multitexturing disabled in engine -> no detail shader")
    {
        BuildContext ctxNoMt = ctx;
        ctxNoMt.isMultitexturing = false;
        const RenderPassDescriptor d = buildFromInt(DetailTexture, ctxNoMt);
        REQUIRE(d.shader == ShaderFamily::Normal);
        REQUIRE(d.texGen == TexGenMode::None);
    }
}

TEST_CASE("BuildRenderPassDescriptor: terrain hint routes opaque ground", "[build-descriptor][phase2]")
{
    BuildContext ctx;
    ctx.passKindHint = PassKindHint::Terrain;

    const RenderPassDescriptor d = buildFromInt(0, ctx);
    REQUIRE(d.pass == PassKind::TerrainOpaque);
    REQUIRE(d.shader == ShaderFamily::Normal);
}

TEST_CASE("BuildRenderPassDescriptor: in-2D pass drops Water texGen", "[build-descriptor][phase2]")
{
    BuildContext ctx;
    ctx.isIn3DPass = false;
    const RenderPassDescriptor d = buildFromInt(IsWater, ctx);
    REQUIRE(d.pass == PassKind::WorldWater);
    REQUIRE(d.texGen == TexGenMode::None);
}

TEST_CASE("BuildRenderPassDescriptor: combined cockpit / surface / disable-light_disc", "[build-descriptor][phase2]")
{
    // Approximates a soldier weapon proxy in a cockpit view with light_disc
    // disabled — the routing/material/backend signals all combine, and
    // the cockpit family comes from the explicit hint the proxy draw
    // scope sets.
    BuildContext ctx;
    ctx.passKindHint = PassKindHint::Cockpit;
    const RenderPassDescriptor d =
        BuildRenderPassDescriptor(SplitLegacy(NoDropdown | FogDisabled | DisableSun | IsAlpha), ctx);
    REQUIRE(d.pass == PassKind::CockpitTransparent);
    REQUIRE(d.fog == FogMode::Disabled);
    REQUIRE(d.lighting == LightingMode::SunDisabled);
    REQUIRE(d.blend == BlendMode::AlphaBlend);
    REQUIRE(d.alpha == AlphaMode::Test);
}

// Phase 3: explicit cockpit routing via PassKindHint.

TEST_CASE("BuildRenderPassDescriptor: PassKindHint::Cockpit promotes opaque to CockpitOpaque",
          "[build-descriptor][phase3]")
{
    BuildContext ctx;
    ctx.passKindHint = PassKindHint::Cockpit;
    // No NoDropdown bit in the spec - without the hint this would
    // route as WorldOpaque.
    const RenderPassDescriptor d = buildFromInt(0, ctx);
    REQUIRE(d.pass == PassKind::CockpitOpaque);
}

TEST_CASE("BuildRenderPassDescriptor: PassKindHint::Cockpit + IsAlpha -> CockpitTransparent",
          "[build-descriptor][phase3]")
{
    BuildContext ctx;
    ctx.passKindHint = PassKindHint::Cockpit;
    const RenderPassDescriptor d = buildFromInt(IsAlpha, ctx);
    REQUIRE(d.pass == PassKind::CockpitTransparent);
    REQUIRE(d.alpha == AlphaMode::Test);
}

TEST_CASE("BuildRenderPassDescriptor: PassKindHint::Cockpit + IsTransparent -> CockpitCutout",
          "[build-descriptor][phase3]")
{
    BuildContext ctx;
    ctx.passKindHint = PassKindHint::Cockpit;
    const RenderPassDescriptor d = buildFromInt(IsTransparent, ctx);
    REQUIRE(d.pass == PassKind::CockpitCutout);
}

TEST_CASE("BuildRenderPassDescriptor: PassKindHint::ScreenSpace3D overrides everything", "[build-descriptor][phase3]")
{
    BuildContext ctx;
    ctx.passKindHint = PassKindHint::ScreenSpace3D;
    // Even with NoDropdown set (which would normally route as Cockpit)
    // the explicit ScreenSpace3D hint wins.
    const RenderPassDescriptor d = buildFromInt(NoDropdown | IsAlpha, ctx);
    REQUIRE(d.pass == PassKind::ScreenSpace3D);
    REQUIRE(d.alpha == AlphaMode::Test);
}

TEST_CASE("BuildRenderPassDescriptor: PassKindHint default does not change legacy routing",
          "[build-descriptor][phase3]")
{
    // With hint == None and no NoDropdown bit, draws stay on the
    // World* family - verifies Phase 3 is opt-in and unmigrated
    // producers see no behavior change.
    BuildContext ctx;
    REQUIRE(ctx.passKindHint == PassKindHint::None);
    const RenderPassDescriptor d = buildFromInt(IsAlpha, ctx);
    REQUIRE(d.pass == PassKind::WorldTransparent);
}

TEST_CASE("BuildRenderPassDescriptor: NoDropdown without the hint never routes Cockpit", "[build-descriptor][phase3]")
{
    // The legacy inference is deleted: with hint == None, a NoDropdown
    // spec stays in the World* families.  Cockpit routing happens only
    // through the explicit hint set by the owning draw scopes
    // (Scene::DrawSortObject for inside-view LODs, the soldier
    // first-person proxies).  Reintroducing the `|| noDropdown`
    // fallback in BuildRenderPassDescriptor fails this case.
    BuildContext ctx;
    const RenderPassDescriptor d = buildFromInt(NoDropdown, ctx);
    REQUIRE(d.pass == PassKind::WorldOpaque);
}

TEST_CASE("BuildRenderPassDescriptor: special pass branches ignore PassKindHint", "[build-descriptor][phase3]")
{
    // Hint only routes through the generic branch.  Shadow / Light /
    // Water / AlphaFog are special-path passes whose family is fixed
    // by the backend bits; hint doesn't override them.
    BuildContext ctx;
    ctx.passKindHint = PassKindHint::Cockpit;

    SECTION("Shadow stays Shadow")
    {
        const RenderPassDescriptor d = buildFromInt(IsShadow, ctx);
        REQUIRE(d.pass == PassKind::WorldShadow);
    }
    SECTION("Light stays Light")
    {
        const RenderPassDescriptor d = buildFromInt(IsLight, ctx);
        REQUIRE(d.pass == PassKind::WorldLight);
    }
    SECTION("Water stays Water")
    {
        const RenderPassDescriptor d = buildFromInt(IsWater, ctx);
        REQUIRE(d.pass == PassKind::WorldWater);
    }
    SECTION("AlphaFog stays Transparent")
    {
        const RenderPassDescriptor d = buildFromInt(IsAlphaFog, ctx);
        REQUIRE(d.pass == PassKind::WorldTransparent);
    }
}
