#pragma once

#include <Poseidon/Graphics/Rendering/RenderFlags.hpp>
#include <Poseidon/Graphics/Rendering/RenderPassDescriptor.hpp>
#include <cstdint>

// `BuildRenderPassDescriptor` is the *single* translation function from typed
// `LegacySpec` + scene context to the `RenderPassDescriptor` consumed by the
// backend.  All spec-bit interpretation lives here — downstream code reads
// descriptor fields, not bitmasks.

namespace Poseidon
{
namespace render
{

// Minimal context the descriptor build needs beyond the spec.  Bigger
// contexts (object identity, scene phase, etc.) can extend this without
// changing the descriptor itself — they only affect *how* `PassKind` is
// chosen.  Defaults match the common case (in 3D pass, no
// multitexturing, opaque shadow blend ref).
struct BuildContext
{
    bool isIn3DPass = true;
    bool isMultitexturing = false;

    // Alpha-test reference for the shadow path (`(shadowFactor * 7) >> 4`
    // in the legacy ApplyPassState body).  0..255.
    std::uint8_t shadowAlphaRef = 0;

    // Explicit pass routing.  Cockpit / ScreenSpace3D pass families are
    // selected only through this hint — set by the draw scopes that own
    // the decision (Scene::DrawSortObject for inside-view LODs, the
    // soldier first-person proxies).  `Routing::NoDropdown` is not used for
    // pass routing; it remains input metadata for fog and the LOD/PassNum logic.
    PassKindHint passKindHint = PassKindHint::None;
};

// Pure, header-implemented to keep the seam visible.  Inline so callers
// can specialize for their context inputs at compile time when possible.
inline RenderPassDescriptor BuildRenderPassDescriptor(const LegacySpec& spec, const BuildContext& ctx = {})
{
    const Routing routing = spec.routing;
    const Material material = spec.material;
    const Backend backend = spec.backend;

    RenderPassDescriptor d;

    // Sampler.
    d.sampler.filter = Has(backend, Backend::PointSampling) ? SamplerFilter::Point : SamplerFilter::Linear;
    d.sampler.clampU = Has(backend, Backend::ClampU);
    d.sampler.clampV = Has(backend, Backend::ClampV);

    // Surface (decal / road polygon-offset).
    d.surface = IsOnSurfaceRouting(routing) ? SurfaceMode::OnSurface : SurfaceMode::Default;

    // Depth + Stencil.
    const bool isShadow = Has(backend, Backend::IsShadow);
    if (isShadow)
    {
        // Single per-poly path: stencil EQUAL 0 / INCR with color writes on,
        // so each shadow draw darkens the framebuffer directly (1-srcA blend).
        d.depth = DepthMode::Shadow;
    }
    else if (Has(backend, Backend::NoZBuf))
    {
        d.depth = DepthMode::Disabled;
    }
    else if (Has(backend, Backend::NoZWrite))
    {
        // Depth-write is disabled ONLY for surfaces explicitly flagged NoZWrite
        // (roads, decals, craters, cloudlets, grass, landscape sky) — matching the
        // original engine (engD3D.cpp: ZWRITEENABLE=FALSE for IsShadow/NoZBuf/
        // NoZWrite, TRUE otherwise).  IsAlpha / IsAlphaFog do NOT disable
        // depth-write: IsAlpha only means "texture has an alpha channel", not
        // "translucent", so opaque props with an alpha-channel texture (poles,
        // fences, signs) must still write depth or geometry behind them leaks
        // through.
        d.depth = DepthMode::ReadOnly;
    }
    else
    {
        d.depth = DepthMode::Normal;
    }

    // Lighting (sun gate).
    if (Has(material, Material::DisableSun))
        d.lighting = LightingMode::SunDisabled;
    // (Other lighting modes are decided per-shader-family below.)

    // Pass-family branch.
    // Order mirrors `EngineGL33::ApplyPassState` so the descriptor build
    // is bit-for-bit equivalent to that switch.
    if (isShadow)
    {
        d.pass = PassKind::WorldShadow;
        d.shader = ShaderFamily::Shadow;
        d.blend = BlendMode::Shadow;
        d.fog = FogMode::Disabled;
        d.alpha = AlphaMode::Test;
        d.alphaRef = ctx.shadowAlphaRef;
        d.stencilExclusion = true;
        d.lighting = LightingMode::ShadowDarkPolygon;
    }
    else if (Has(backend, Backend::IsLight))
    {
        d.pass = PassKind::WorldLight;
        d.shader = ShaderFamily::Normal;
        d.blend = BlendMode::Additive;
        d.fog = FogMode::Disabled;
        d.alpha = AlphaMode::Test;
        d.alphaRef = 1;
        d.lighting = LightingMode::Unlit;
    }
    else if (Has(backend, Backend::IsWater))
    {
        d.pass = PassKind::WorldWater;
        d.shader = ShaderFamily::Water;
        // Water is composited after the opaque/cutout depth has established
        // the coastline.  It must test that depth without replacing it: the
        // animated cosmetic surface is not gameplay geometry and must not
        // occlude later world receivers with its displaced crests.
        d.blend = BlendMode::AlphaBlend;
        d.depth = DepthMode::ReadOnly;
        d.fog = Has(routing, Routing::FogDisabled) ? FogMode::Disabled : FogMode::Enabled;
        d.alpha = AlphaMode::Disabled;
        d.texGen = ctx.isIn3DPass ? TexGenMode::Water : TexGenMode::None;
    }
    else if (Has(backend, Backend::IsAlphaFog))
    {
        d.pass = PassKind::WorldTransparent;
        d.shader = ShaderFamily::Normal;
        d.blend = BlendMode::AlphaBlend;
        d.fog = FogMode::AlphaFog;
        d.alpha = AlphaMode::Test;
        d.alphaRef = 1;
    }
    else
    {
        // Generic opaque / cutout / transparent path.  Fog depends on
        // Backend + Routing bits; PassKind comes from the explicit
        // hint only.
        constexpr Routing fogOffMask = Routing::NoDropdown | Routing::FogDisabled;
        d.fog = ((routing & fogOffMask) == Routing::None) ? FogMode::Enabled : FogMode::Disabled;

        const bool routeAsCockpit = (ctx.passKindHint == PassKindHint::Cockpit);
        const bool routeAsScreenSpace = (ctx.passKindHint == PassKindHint::ScreenSpace3D);
        const bool routeAsTerrain = (ctx.passKindHint == PassKindHint::Terrain);

        if (Has(backend, Backend::IsAlpha))
        {
            d.blend = BlendMode::AlphaBlend;
            d.alpha = AlphaMode::Test;
            d.alphaRef = 1;
            d.pass = routeAsScreenSpace ? PassKind::ScreenSpace3D
                     : routeAsCockpit   ? PassKind::CockpitTransparent
                                        : PassKind::WorldTransparent;
        }
        else if (Has(backend, Backend::IsTransparent))
        {
            d.blend = BlendMode::Opaque;
            d.alpha = AlphaMode::Test;
            d.alphaRef = 0xc0;
            d.pass = routeAsScreenSpace ? PassKind::ScreenSpace3D
                     : routeAsCockpit   ? PassKind::CockpitCutout
                                        : PassKind::WorldCutout;
        }
        else
        {
            d.blend = BlendMode::Opaque;
            d.alpha = AlphaMode::Disabled;
            d.alphaRef = 0xc0;
            d.pass = routeAsScreenSpace ? PassKind::ScreenSpace3D
                      : routeAsCockpit   ? PassKind::CockpitOpaque
                      : routeAsTerrain   ? PassKind::TerrainOpaque
                                         : PassKind::WorldOpaque;
        }

        // Multitexturing shader family + texGen — only for the generic
        // (non-special) path; water/shadow/light all set their own.
        constexpr Backend mtMask = Backend::DetailTexture | Backend::SpecularTexture | Backend::GrassTexture;
        if (ctx.isMultitexturing && (backend & mtMask) != Backend::None)
        {
            const bool grass = Has(backend, Backend::GrassTexture);
            d.shader = grass ? ShaderFamily::Grass : ShaderFamily::Detail;
            if (ctx.isIn3DPass)
                d.texGen = grass ? TexGenMode::Grass : TexGenMode::Detail;
        }
        else
        {
            d.shader = ShaderFamily::Normal;
        }
    }

    // Surface overlay overrides PassKind.
    // Roads / decals attach to terrain regardless of their alpha / blend.
    if (d.surface == SurfaceMode::OnSurface && d.pass != PassKind::WorldShadow)
    {
        d.pass = PassKind::SurfaceOverlay;
    }

    // NoZBuf draws (sky, cockpit, first-person HUD) must never fog, and
    // need backface culling disabled because the camera views them from
    // inside (sky dome, cockpit interior).
    // GL33's ApplyPassState explicitly disables fog for PassId::Sky and
    // PassId::Cockpit — match that here so the descriptor is correct
    // regardless of backend overrides.
    if (d.depth == DepthMode::Disabled)
    {
        d.fog = FogMode::Disabled;
        d.cull = CullMode::None;
        if (Has(backend, Backend::IsAlphaFog) && d.pass != PassKind::WorldLight)
            d.pass = PassKind::Sky;
    }

    return d;
}

} // namespace render

} // namespace Poseidon
