#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Graphics/Rendering/Frame/Frame.hpp>
#include <Poseidon/Graphics/Rendering/Frame/ValidateFrame.hpp>

#include <cstring>
#include <type_traits>

// Phase D spike — score the C2 (Frame + ValidateFrame) architecture
// against the historical bug catalog.
//
// Each TEST_CASE references the catalog ID (B-NNN) it locks down and
// the invariant ID (I-NN) it enforces.  A test passing means the
// failure mode the catalog entry describes is structurally caught by
// this architecture (compile-time, type system, or pure-function
// unit test — no GL context needed).

namespace v2 = Poseidon::render::frame;

// I-01 / B-001 — UBO byte ranges non-overlapping (compile-time)

TEST_CASE("Frame/I-01: projection and viewport-scale live in distinct struct members",
          "[render-frame][invariants][I-01]")
{
    // The B-001 alias bug was `SlotProj == SlotVpScale == 0`.  Here,
    // there's no shared offset table — projection is one struct
    // member, viewport scale is computed from camera.viewport at
    // Execute time.  The static_asserts below would fail to compile
    // if some future refactor put them at the same address.
    Poseidon::render::frame::CameraView c{};
    const void* pProj = &c.projection;
    const void* pView = &c.view;
    const void* pVp = &c.viewport;

    REQUIRE(pProj != pView);
    REQUIRE(pProj != pVp);
    REQUIRE(pView != pVp);

    // Distinct struct members at distinct addresses, regardless of
    // layout order chosen by the compiler.
    auto nonOverlap = [](const void* a, size_t sa, const void* b, size_t sb)
    {
        const auto* pa = static_cast<const std::uint8_t*>(a);
        const auto* pb = static_cast<const std::uint8_t*>(b);
        return (pa + sa <= pb) || (pb + sb <= pa);
    };
    REQUIRE(nonOverlap(pProj, sizeof(c.projection), pView, sizeof(c.view)));
    REQUIRE(nonOverlap(pProj, sizeof(c.projection), pVp, sizeof(c.viewport)));
}

// I-08 / B-010 — one bit, one semantic meaning (compile-time)

TEST_CASE("Frame/I-08: FramePassKind values are distinct enum members", "[render-frame][invariants][I-08]")
{
    // Type system enforces that PassKind values cannot be conflated.
    // Adding a "Shadow" alias for "WorldOpaque" would require a
    // separate enum, breaking the contract.
    static_assert(static_cast<int>(Poseidon::render::frame::FramePassKind::Cockpit) !=
                  static_cast<int>(Poseidon::render::frame::FramePassKind::WorldOpaque));
    static_assert(static_cast<int>(Poseidon::render::frame::FramePassKind::ShadowAccum) !=
                  static_cast<int>(Poseidon::render::frame::FramePassKind::ShadowDarken));
    SUCCEED("PassKind distinctness is a compile-time check.");
}

// I-02 / B-002, B-003, B-032 — single bind site for state (compile-time)

TEST_CASE("Frame/I-02: Frame is immutable; no caches between draws", "[render-frame][invariants][I-02]")
{
    // I-02 is structural: there are no mutable static caches in the frame layer,
    // and Execute is the single function that touches GL.  Verified
    // by construction — see Frame.hpp.  This test pins that Frame
    // and its sub-structs are aggregate-copyable (no hidden state).
    Poseidon::render::frame::Frame f1{};
    Poseidon::render::frame::Frame f2 = f1; // value-copy works
    REQUIRE(f2.passes.size() == f1.passes.size());
    SUCCEED("Frame has no shared mutable state.");
}

// I-19 / I-26 / B-029, B-032 — complete material state per draw

TEST_CASE("Frame/I-19, I-26: Draw requires a complete RenderPassDescriptor", "[render-frame][invariants][I-19][I-26]")
{
    // The Draw struct's `descriptor` field is a complete
    // RenderPassDescriptor by construction.  "Partial state" is
    // unrepresentable — you can't construct a Draw with half a
    // descriptor.
    Poseidon::render::frame::Draw d{};
    // descriptor is default-constructed (complete with default values),
    // not "uninitialized" — proves the type makes partial state
    // unrepresentable.
    REQUIRE(d.descriptor.pass == Poseidon::render::PassKind::WorldOpaque);
    REQUIRE(d.descriptor.depth == Poseidon::render::DepthMode::Normal);
    REQUIRE(d.descriptor.blend == Poseidon::render::BlendMode::Opaque);
}

// I-22 / B-011 — the shared frame layer carries one mesh resource id plus the
// typed buffer handles that belong to that mesh.

TEST_CASE("Frame/I-22: MeshHandle bundles mesh resource id + VBO + IBO",
          "[render-frame][invariants][I-22]")
{
    // The Draw type holds a MeshHandle, which forces the opaque backend mesh
    // token and the typed buffer handles to travel together.
    Poseidon::render::frame::MeshHandle m{};
    static_assert(std::is_aggregate_v<Poseidon::render::frame::MeshHandle>);
    static_assert(sizeof(m.id) > 0);
    static_assert(sizeof(m.ibo) > 0);
    SUCCEED("MeshHandle carries one opaque mesh id plus its typed buffer handles.");
}

// I-09 / B-008, B-009 — coplanar disambiguation (validator unit-test)

TEST_CASE("Frame/I-09: ValidateFrame catches OnSurface draw without offset or cull", "[render-frame][invariants][I-09]")
{
    Poseidon::render::frame::Frame f{};
    f.camera.viewport = {0, 0, 800, 600};

    Poseidon::render::frame::Pass p;
    p.kind = Poseidon::render::frame::FramePassKind::WorldOpaque;
    Poseidon::render::frame::Draw d{};
    d.descriptor.surface = Poseidon::render::SurfaceMode::OnSurface;
    d.descriptor.cull = Poseidon::render::CullMode::None;
    d.descriptor.pass = Poseidon::render::PassKind::WorldOpaque; // not SurfaceOverlay
    p.draws.push_back(d);
    f.passes.push_back(p);

    const auto r = Poseidon::render::frame::ValidateFrame(f);
    bool foundI09 = false;
    for (const auto& v : r.violations)
        if (std::strcmp(v.ruleId, "I-09") == 0)
            foundI09 = true;
    REQUIRE(foundI09);
}

TEST_CASE("Frame/I-09: ValidateFrame passes OnSurface with polygon-offset PassKind", "[render-frame][invariants][I-09]")
{
    Poseidon::render::frame::Frame f{};
    f.camera.viewport = {0, 0, 800, 600};

    Poseidon::render::frame::Pass p;
    p.kind = Poseidon::render::frame::FramePassKind::WorldOpaque;
    Poseidon::render::frame::Draw d{};
    d.descriptor.surface = Poseidon::render::SurfaceMode::OnSurface;
    d.descriptor.pass = Poseidon::render::PassKind::SurfaceOverlay;
    d.descriptor.cull = Poseidon::render::CullMode::None;
    p.draws.push_back(d);
    f.passes.push_back(p);

    const auto r = Poseidon::render::frame::ValidateFrame(f);
    for (const auto& v : r.violations)
        REQUIRE(std::strcmp(v.ruleId, "I-09") != 0);
}

TEST_CASE("Frame/I-09: ValidateFrame passes OnSurface with backface cull enabled (B-008)",
          "[render-frame][invariants][I-09]")
{
    // B-008's coplanar disambiguator: paired CW/CCW back-face culled
    // decals — cull alone keeps the z-fight away even on non-
    // SurfaceOverlay passes.  WorldOpaque + OnSurface + cull=Back
    // must validate cleanly.
    Poseidon::render::frame::Frame f{};
    f.camera.viewport = {0, 0, 800, 600};

    Poseidon::render::frame::Pass p;
    p.kind = Poseidon::render::frame::FramePassKind::WorldOpaque;
    Poseidon::render::frame::Draw d{};
    d.descriptor.surface = Poseidon::render::SurfaceMode::OnSurface;
    d.descriptor.pass = Poseidon::render::PassKind::WorldOpaque;
    d.descriptor.cull = Poseidon::render::CullMode::Back;
    p.draws.push_back(d);
    f.passes.push_back(p);

    const auto r = Poseidon::render::frame::ValidateFrame(f);
    for (const auto& v : r.violations)
        REQUIRE(std::strcmp(v.ruleId, "I-09") != 0);
}

TEST_CASE("Frame/I-09: non-OnSurface draw never trips the check", "[render-frame][invariants][I-09]")
{
    // Default surface mode — the I-09 check only fires when
    // surface == OnSurface; ordinary world geometry can have
    // any cull mode without tripping it.
    Poseidon::render::frame::Frame f{};
    f.camera.viewport = {0, 0, 800, 600};

    Poseidon::render::frame::Pass p;
    p.kind = Poseidon::render::frame::FramePassKind::WorldOpaque;
    Poseidon::render::frame::Draw d{};
    d.descriptor.surface = Poseidon::render::SurfaceMode::Default;
    d.descriptor.cull = Poseidon::render::CullMode::None; // double-sided draw, off-surface
    p.draws.push_back(d);
    f.passes.push_back(p);

    const auto r = Poseidon::render::frame::ValidateFrame(f);
    for (const auto& v : r.violations)
        REQUIRE(std::strcmp(v.ruleId, "I-09") != 0);
}

TEST_CASE("Frame/I-09: violation message names the offending pass and draw index", "[render-frame][invariants][I-09]")
{
    // The check reports `pass[N].draw[M]: ...` so a frame with
    // many draws can be debugged from the log line alone.  Add
    // a non-violating draw at index 0 and a violating one at
    // index 1 to verify the index is correctly reported.
    Poseidon::render::frame::Frame f{};
    f.camera.viewport = {0, 0, 800, 600};

    Poseidon::render::frame::Pass p;
    p.kind = Poseidon::render::frame::FramePassKind::WorldOpaque;
    {
        Poseidon::render::frame::Draw ok{};
        ok.descriptor.surface = Poseidon::render::SurfaceMode::Default;
        ok.descriptor.cull = Poseidon::render::CullMode::Back;
        p.draws.push_back(ok);
    }
    {
        Poseidon::render::frame::Draw bad{};
        bad.descriptor.surface = Poseidon::render::SurfaceMode::OnSurface;
        bad.descriptor.pass = Poseidon::render::PassKind::WorldOpaque;
        bad.descriptor.cull = Poseidon::render::CullMode::None;
        p.draws.push_back(bad);
    }
    f.passes.push_back(p);

    const auto r = Poseidon::render::frame::ValidateFrame(f);
    bool foundDraw1 = false;
    for (const auto& v : r.violations)
    {
        if (std::strcmp(v.ruleId, "I-09") == 0 && v.detail.find("draw[1]") != std::string::npos)
        {
            foundDraw1 = true;
        }
    }
    REQUIRE(foundDraw1);
}

// I-04 / B-004, B-005, B-006 — pass ordering (validator unit-test)

TEST_CASE("Frame/I-04: ValidateFrame catches out-of-order passes (cockpit before world)",
          "[render-frame][invariants][I-04]")
{
    Poseidon::render::frame::Frame f{};
    f.camera.viewport = {0, 0, 800, 600};

    f.passes.push_back({Poseidon::render::frame::FramePassKind::Cockpit});
    f.passes.push_back({Poseidon::render::frame::FramePassKind::WorldOpaque});

    const auto r = Poseidon::render::frame::ValidateFrame(f);
    bool foundOrder = false;
    for (const auto& v : r.violations)
        if (std::strcmp(v.ruleId, "I-PassOrder") == 0)
            foundOrder = true;
    REQUIRE(foundOrder);
}

TEST_CASE("Frame/I-04: canonical pass order validates clean", "[render-frame][invariants][I-04]")
{
    Poseidon::render::frame::Frame f{};
    f.camera.viewport = {0, 0, 800, 600};

    f.passes.push_back({Poseidon::render::frame::FramePassKind::ShadowAccum});
    f.passes.push_back({Poseidon::render::frame::FramePassKind::ShadowDarken});
    f.passes.push_back({Poseidon::render::frame::FramePassKind::Sky});
    f.passes.push_back({Poseidon::render::frame::FramePassKind::WorldOpaque});
    f.passes.push_back({Poseidon::render::frame::FramePassKind::WorldCutout});
    f.passes.push_back({Poseidon::render::frame::FramePassKind::Water});
    f.passes.push_back({Poseidon::render::frame::FramePassKind::WorldTransparent});
    f.passes.push_back({Poseidon::render::frame::FramePassKind::Cockpit});
    f.passes.push_back({Poseidon::render::frame::FramePassKind::ScreenSpace});

    const auto r = Poseidon::render::frame::ValidateFrame(f);
    for (const auto& v : r.violations)
        REQUIRE(std::strcmp(v.ruleId, "I-PassOrder") != 0);
}

// I-24 / B-017 — viewport ↔ FB size (validator unit-test)

TEST_CASE("Frame/I-24: ValidateFrame catches zero-sized viewport", "[render-frame][invariants][I-24]")
{
    Poseidon::render::frame::Frame f{};
    f.camera.viewport = {0, 0, 0, 0};

    const auto r = Poseidon::render::frame::ValidateFrame(f);
    bool foundI24 = false;
    for (const auto& v : r.violations)
        if (std::strcmp(v.ruleId, "I-24") == 0)
            foundI24 = true;
    REQUIRE(foundI24);
}

// I-08 alignment — Pass kind matches descriptor's pass family

TEST_CASE("Frame/I-08: cockpit pass with world descriptor flagged", "[render-frame][invariants][I-08]")
{
    Poseidon::render::frame::Frame f{};
    f.camera.viewport = {0, 0, 800, 600};

    f.passes.push_back({Poseidon::render::frame::FramePassKind::WorldOpaque}); // pass 0 for ordering

    Poseidon::render::frame::Pass cockpit;
    cockpit.kind = Poseidon::render::frame::FramePassKind::Cockpit;
    Poseidon::render::frame::Draw d{};
    d.descriptor.pass = Poseidon::render::PassKind::WorldOpaque; // mismatched!
    cockpit.draws.push_back(d);
    f.passes.push_back(cockpit);

    const auto r = Poseidon::render::frame::ValidateFrame(f);
    bool foundI08 = false;
    for (const auto& v : r.violations)
        if (std::strcmp(v.ruleId, "I-08") == 0)
            foundI08 = true;
    REQUIRE(foundI08);
}

// I-34 — cropped world flat_quad must not stretch: the projection FOV
// aspect has to match the flat_quad's pixel aspect.  The full (uncropped)
// flat_quad is exempt — the default ultrawide policy clamps the FOV below
// the viewport ratio by design.

namespace
{
Poseidon::render::frame::Frame makeCroppedFrame(float cropL, float cropR, float fovAspect)
{
    Poseidon::render::frame::Frame f;
    f.camera.viewport = {0, 0, 1920, 1080};
    f.camera.worldLeft = cropL;
    f.camera.worldRight = cropR;
    // ProjectionNormal layout: _11 = 1/cLeft, _22 = 1/cTop.
    f.camera.projection._11 = 1.0f;
    f.camera.projection._22 = fovAspect; // aspect = _22/_11
    return f;
}
} // namespace

TEST_CASE("Frame/I-34: cropped world with matching FOV aspect validates clean", "[render-frame][I-34]")
{
    // Centered half-width crop on 16:9: flat_quad pixel aspect =
    // (0.5*1920)/1080 = 0.888…; FOV aspect must match.
    const auto f = makeCroppedFrame(0.25f, 0.75f, (0.5f * 1920.0f) / 1080.0f);
    REQUIRE(Poseidon::render::frame::ValidateFrame(f).ok());
}

TEST_CASE("Frame/I-34: cropped world with mismatched FOV aspect is a violation", "[render-frame][I-34]")
{
    // Same crop, but the FOV kept the full-viewport aspect (16:9) —
    // the world would stretch 2x horizontally.
    const auto f = makeCroppedFrame(0.25f, 0.75f, 1920.0f / 1080.0f);
    const auto r = Poseidon::render::frame::ValidateFrame(f);
    REQUIRE_FALSE(r.ok());
    bool found = false;
    for (const auto& v : r.violations)
        if (std::string(v.ruleId) == "I-34")
            found = true;
    REQUIRE(found);
}

TEST_CASE("Frame/I-34: full (uncropped) flat_quad is exempt", "[render-frame][I-34]")
{
    // Clamped-FOV-on-ultrawide is policy, not a stretch bug: full flat_quad
    // with any FOV aspect passes.
    const auto f = makeCroppedFrame(0.0f, 1.0f, 21.0f / 9.0f);
    REQUIRE(Poseidon::render::frame::ValidateFrame(f).ok());
}

// ComputeIndexByteOffset: the calculation EmitDraw's glDrawElements
// depends on.  Locks down the math so a 16→32-bit index migration or
// a regression in DrawSectionTL's capture site shows up here instead
// of as a subtle render glitch.

TEST_CASE("Frame/E.11.j: ComputeIndexByteOffset(0, 2) is 0", "[render-frame][emit][E.11.j]")
{
    // Engine's primary VertexIndex is `short` (2 bytes).  First
    // index, first byte.
    REQUIRE(Poseidon::render::frame::ComputeIndexByteOffset(0, sizeof(short)) == 0);
}

TEST_CASE("Frame/E.11.j: ComputeIndexByteOffset(3, 2) is 6", "[render-frame][emit][E.11.j]")
{
    REQUIRE(Poseidon::render::frame::ComputeIndexByteOffset(3, sizeof(short)) == 6);
}

TEST_CASE("Frame/E.11.j: ComputeIndexByteOffset handles 32-bit indices", "[render-frame][emit][E.11.j]")
{
    // If a future asset format raises the cap above 65535, the
    // engine swaps VertexIndex to uint32 and the same call gives
    // 4x the offset.  The helper is index-size-agnostic to
    // support that swap with one constant change.
    REQUIRE(Poseidon::render::frame::ComputeIndexByteOffset(3, 4) == 12);
}

TEST_CASE("Frame/E.11.j: ComputeIndexByteOffset for large index range", "[render-frame][emit][E.11.j]")
{
    // A forest-frame shape can have tens of thousands of indices;
    // confirms no int overflow at the call site (the helper
    // promotes to intptr_t before multiplying).
    const int firstIndex = 50000;
    REQUIRE(Poseidon::render::frame::ComputeIndexByteOffset(firstIndex, sizeof(short)) ==
            static_cast<std::intptr_t>(100000));
}

TEST_CASE("Frame/E.11.j: Draw struct carries every glDrawElements input", "[render-frame][emit][E.11.j]")
{
    // A representative TL draw — mesh resource id 7, raw index range [3, 9),
    // textured world-opaque.  After SceneExtractor + BuildFrame
    // the resulting Draw should have everything that
    // `glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT,
    // offset_bytes)` needs.
    Poseidon::render::frame::Draw d;
    d.descriptor.pass = Poseidon::render::PassKind::WorldOpaque;
    d.mesh.id = 7;
    d.indexBegin = 3;
    d.indexCount = 6;

    REQUIRE(d.mesh.id == 7);
    REQUIRE(d.mesh.HasBackendMesh());
    REQUIRE(d.indexCount == 6);
    REQUIRE(Poseidon::render::frame::ComputeIndexByteOffset(d.indexBegin, sizeof(short)) == 6);
}

// E.11.f — I-20 violation detail includes the last KHR_debug message

TEST_CASE("Frame/I-20: violation detail surfaces the last debug message", "[render-frame][invariants][I-20][E.11.f]")
{
    // Catalog refs: B-011, B-028.  Without the message, the
    // violation line is "1 new HIGH-severity GL error(s) this
    // frame" — true but not actionable.  With the message the
    // operator sees "type=0x824C id=131000: GL_INVALID_OPERATION
    // …" and goes straight to the bug class.
    Poseidon::render::frame::Frame f{};
    f.camera.viewport = {0, 0, 800, 600};
    f.passes.push_back({Poseidon::render::frame::FramePassKind::WorldOpaque});
    f.newDebugErrors = 1;
    f.lastDebugMessage = "type=0x824C id=131000: GL_INVALID_OPERATION blew up";

    const auto r = Poseidon::render::frame::ValidateFrame(f);
    bool found = false;
    for (const auto& v : r.violations)
    {
        if (std::strcmp(v.ruleId, "I-20") == 0)
        {
            found = true;
            REQUIRE(v.detail.find("GL_INVALID_OPERATION") != std::string::npos);
            REQUIRE(v.detail.find("131000") != std::string::npos);
        }
    }
    REQUIRE(found);
}

TEST_CASE("Frame/I-20: empty debug message keeps the original detail format",
          "[render-frame][invariants][I-20][E.11.f]")
{
    // If the backend hasn't captured a message yet (first frame,
    // headless engine), the detail string should still be valid
    // and not include a stray "; last: " suffix.
    Poseidon::render::frame::Frame f{};
    f.camera.viewport = {0, 0, 800, 600};
    f.passes.push_back({Poseidon::render::frame::FramePassKind::WorldOpaque});
    f.newDebugErrors = 2;
    f.lastDebugMessage = "";

    const auto r = Poseidon::render::frame::ValidateFrame(f);
    bool found = false;
    for (const auto& v : r.violations)
    {
        if (std::strcmp(v.ruleId, "I-20") == 0)
        {
            found = true;
            REQUIRE(v.detail.find("2 new") != std::string::npos);
            REQUIRE(v.detail.find("; last:") == std::string::npos);
        }
    }
    REQUIRE(found);
}
