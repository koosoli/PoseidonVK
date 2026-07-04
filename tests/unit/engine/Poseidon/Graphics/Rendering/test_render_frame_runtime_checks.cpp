#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Graphics/Rendering/Frame/RuntimeChecks.hpp>

// Phase E.11.c — DetectBlackFrame is the pure decision function the
// production observer uses to fire I-29.  Tested standalone so its
// thresholding and "had 3D content" gating are locked down without
// any framebuffer / GL state.

namespace v2 = Poseidon::render::frame;

TEST_CASE("Frame/E.11.c: DetectBlackFrame skips check when no 3D content", "[render-frame][runtime][I-29][E.11.c]")
{
    const std::uint8_t black[3] = {0, 0, 0};
    REQUIRE_FALSE(Poseidon::render::frame::DetectBlackFrame(/*had3DContent=*/false, black).has_value());
}

TEST_CASE("Frame/E.11.c: DetectBlackFrame flags pure-black centre when 3D expected",
          "[render-frame][runtime][I-29][E.11.c]")
{
    const std::uint8_t black[3] = {0, 0, 0};
    const auto v = Poseidon::render::frame::DetectBlackFrame(true, black);
    REQUIRE(v.has_value());
    REQUIRE(std::string(v->ruleId) == "I-29");
}

TEST_CASE("Frame/E.11.c: DetectBlackFrame respects threshold boundary", "[render-frame][runtime][I-29][E.11.c]")
{
    // At the default threshold (8), (8,8,8) is still considered
    // black; (9,8,8) is not.  Asserts the inequality direction so
    // dim-but-non-black scenes pass.
    const std::uint8_t atBoundary[3] = {8, 8, 8};
    REQUIRE(Poseidon::render::frame::DetectBlackFrame(true, atBoundary).has_value());

    const std::uint8_t justOver[3] = {9, 8, 8};
    REQUIRE_FALSE(Poseidon::render::frame::DetectBlackFrame(true, justOver).has_value());
}

TEST_CASE("Frame/E.11.c: DetectBlackFrame passes lit scenes", "[render-frame][runtime][I-29][E.11.c]")
{
    // The forest-weapon-visible center pixel reads ~(120,122,118)
    // in production.  A lit world should never flag.
    const std::uint8_t litForest[3] = {120, 122, 118};
    REQUIRE_FALSE(Poseidon::render::frame::DetectBlackFrame(true, litForest).has_value());
}

TEST_CASE("Frame/E.11.c: DetectBlackFrame uses custom threshold", "[render-frame][runtime][I-29][E.11.c]")
{
    // A stricter threshold of 4 lets (5,5,5) through; a looser
    // threshold of 32 still flags (16,16,16) as dark enough to be
    // suspicious.  Tests that the parameter actually drives the
    // decision.
    const std::uint8_t dim[3] = {5, 5, 5};
    REQUIRE_FALSE(Poseidon::render::frame::DetectBlackFrame(true, dim, /*threshold=*/4).has_value());

    const std::uint8_t darker[3] = {16, 16, 16};
    REQUIRE(Poseidon::render::frame::DetectBlackFrame(true, darker, /*threshold=*/32).has_value());
}

TEST_CASE("Frame/E.11.c: DetectBlackFrame detail mentions sampled values", "[render-frame][runtime][I-29][E.11.c]")
{
    // Detail string is what an operator sees in the log; it must
    // include the actual readback so the bug class (depth-clear vs
    // matrix vs blit) is diagnosable from the line alone.
    const std::uint8_t black[3] = {1, 2, 3};
    const auto v = Poseidon::render::frame::DetectBlackFrame(true, black);
    REQUIRE(v.has_value());
    REQUIRE(v->detail.find('1') != std::string::npos);
    REQUIRE(v->detail.find('2') != std::string::npos);
    REQUIRE(v->detail.find('3') != std::string::npos);
}

// I-24 — viewport parity at the observation seam.

TEST_CASE("Frame/E.11.d: DetectViewportMismatch passes when flat_quads align", "[render-frame][runtime][I-24][E.11.d]")
{
    const int live[4] = {0, 0, 800, 600};
    REQUIRE_FALSE(Poseidon::render::frame::DetectViewportMismatch(0, 0, 800, 600, live).has_value());
}

TEST_CASE("Frame/E.11.d: DetectViewportMismatch catches width drift", "[render-frame][runtime][I-24][E.11.d]")
{
    // Window resized to 1024 wide but SceneInputs still carries 800
    // — the bug class fullscreen-transition or scissor-leak produces.
    const int live[4] = {0, 0, 1024, 600};
    const auto v = Poseidon::render::frame::DetectViewportMismatch(0, 0, 800, 600, live);
    REQUIRE(v.has_value());
    REQUIRE(std::string(v->ruleId) == "I-24");
    REQUIRE(v->detail.find("1024") != std::string::npos);
    REQUIRE(v->detail.find("800") != std::string::npos);
}

TEST_CASE("Frame/E.11.d: DetectViewportMismatch catches origin drift", "[render-frame][runtime][I-24][E.11.d]")
{
    // A scissor leak typically shifts the flat_quad origin; size matches
    // but x/y differ.  Defaults to zero tolerance so a single-pixel
    // drift is loud.
    const int live[4] = {16, 32, 800, 600};
    REQUIRE(Poseidon::render::frame::DetectViewportMismatch(0, 0, 800, 600, live).has_value());
}

TEST_CASE("Frame/E.11.d: DetectViewportMismatch tolerance window", "[render-frame][runtime][I-24][E.11.d]")
{
    // GL spec allows minor implementation slack on some drivers,
    // and HiDPI scaling can introduce 1-pixel rounding.  A
    // tolerance of 1 should swallow that without losing real
    // mismatches.
    const int liveTight[4] = {0, 0, 801, 600};
    REQUIRE_FALSE(Poseidon::render::frame::DetectViewportMismatch(0, 0, 800, 600, liveTight, 1).has_value());

    const int liveLoose[4] = {0, 0, 803, 600};
    REQUIRE(Poseidon::render::frame::DetectViewportMismatch(0, 0, 800, 600, liveLoose, 1).has_value());
}

// I-22 — every TL draw must carry a non-zero backend mesh resource id.

TEST_CASE("Frame/E.11.h: DetectMissingMeshHandles passes when every TL has a mesh resource id",
          "[render-frame][runtime][I-22][E.11.h]")
{
    // 198 forest-frame TL draws, all with backendMeshResourceId set
    // by EngineGL33_VertexBuffer.cpp's DrawSectionTL.
    REQUIRE_FALSE(Poseidon::render::frame::DetectMissingMeshHandles(198, 0).has_value());
}

TEST_CASE("Frame/E.11.h: DetectMissingMeshHandles flags any missing mesh resource id",
          "[render-frame][runtime][I-22][E.11.h]")
{
    // Even one bad draw is structurally wrong — a TL draw arrived
    // at the frame layer without a resolvable mesh, which means the capture
    // site (DrawSectionTL or any future TL-path) is missing the
    // backendMeshResourceId assignment.
    const auto v = Poseidon::render::frame::DetectMissingMeshHandles(100, 1);
    REQUIRE(v.has_value());
    REQUIRE(std::string(v->ruleId) == "I-22");
    REQUIRE(v->detail.find("1 of 100") != std::string::npos);
}

TEST_CASE("Frame/E.11.h: DetectMissingMeshHandles passes when no TL draws at all",
          "[render-frame][runtime][I-22][E.11.h]")
{
    // HUD-only frame: all draws went through the queued path, no
    // TL draws.  The check vacuously passes — nothing to validate.
    REQUIRE_FALSE(Poseidon::render::frame::DetectMissingMeshHandles(0, 0).has_value());
}

// B-007 — GL bind-cache skip must agree with the live GL binding.
// This is the pure decision the debug tripwire in GL33Bind::Tex2D uses:
// when the cache skips a glBindTexture, it reads back GL_TEXTURE_BINDING_2D
// and feeds (cachedHandle, liveGLHandle) here.  Catches the
// divergence class — a deleted/recycled handle still matching a stale skip.

TEST_CASE("Frame/B-007: DetectBindCacheDivergence passes when cache matches GL", "[render-frame][runtime][B-007]")
{
    // The common case: the skip is legitimate — GL really holds what the
    // cache claims, so no glBindTexture was needed.
    REQUIRE_FALSE(Poseidon::render::frame::DetectBindCacheDivergence(0, 42, 42).has_value());
}

TEST_CASE("Frame/B-007: DetectBindCacheDivergence flags a stale cache entry", "[render-frame][runtime][B-007]")
{
    // Cache claims unit 0 holds texture 42 but GL has 99 — something rebound
    // the unit outside the cache without invalidating.  The skip would leave 99
    // sampled instead of 42.
    const auto v = Poseidon::render::frame::DetectBindCacheDivergence(0, 42, 99);
    REQUIRE(v.has_value());
    REQUIRE(std::string(v->ruleId) == "B-007");
    REQUIRE(v->detail.find("unit 0") != std::string::npos);
    REQUIRE(v->detail.find("42") != std::string::npos);
    REQUIRE(v->detail.find("99") != std::string::npos);
}

TEST_CASE("Frame/B-007: DetectBindCacheDivergence catches the recycled-handle shape", "[render-frame][runtime][B-007]")
{
    // The recycled-handle manifestation: the overview texture (GL id 7) was deleted and
    // its id recycled for a terrain tile, but the engine's stale skip-gate still
    // matched 7 while the unit actually sat on the GL default (id 0, the 1x1
    // white). The cache claims 7; GL reads back 0 → divergence.
    const auto v = Poseidon::render::frame::DetectBindCacheDivergence(0, 7, 0);
    REQUIRE(v.has_value());
    REQUIRE(std::string(v->ruleId) == "B-007");
    REQUIRE(v->detail.find("7") != std::string::npos);
}
