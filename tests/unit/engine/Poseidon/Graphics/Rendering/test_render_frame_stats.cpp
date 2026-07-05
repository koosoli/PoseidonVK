#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Graphics/Rendering/Frame/BuildFrame.hpp>
#include <Poseidon/Graphics/Rendering/Frame/FrameStats.hpp>

// CountFrameStats — the pure fold over the Frame value that feeds the
// --render-frame-log summary line.  Replaces the dispatch-walk stats
// backend: same numbers, no backend object, no call sequence.

namespace v2 = Poseidon::render::frame;

namespace
{

v2::SceneInputs makeMinimal()
{
    v2::SceneInputs s;
    s.camera.viewport = {0, 0, 800, 600};
    s.flags.hudEnabled = false;
    return s;
}

v2::SceneDraw makeDraw()
{
    v2::SceneDraw d;
    d.descriptor.pass = Poseidon::render::PassKind::WorldOpaque;
    d.descriptor.depth = Poseidon::render::DepthMode::Normal;
    d.descriptor.blend = Poseidon::render::BlendMode::Opaque;
    d.indexCount = 3;
    return d;
}

} // namespace

TEST_CASE("Frame/FrameStats: empty Frame folds to zeroes", "[render-frame][frame-stats]")
{
    const v2::Frame f; // truly empty — no passes at all
    const v2::FrameStats s = v2::CountFrameStats(f);

    REQUIRE(s.passCount == 0);
    REQUIRE(s.drawCount == 0);
    REQUIRE(s.maxDrawsInPass == 0);
    REQUIRE(s.uniqueTextureCount == 0);
    REQUIRE(s.uniqueVertexBufferCount == 0);
    REQUIRE(s.uniqueIndexBufferCount == 0);
}

TEST_CASE("Frame/FrameStats: counts passes and draws on a one-draw frame", "[render-frame][frame-stats]")
{
    auto si = makeMinimal();
    si.worldOpaqueDraws.push_back(makeDraw());

    const v2::FrameStats s = v2::CountFrameStats(v2::BuildFrame(si));

    REQUIRE(s.passCount == 1);
    REQUIRE(s.drawCount == 1);
    REQUIRE(s.maxDrawsInPass == 1);
}

TEST_CASE("Frame/FrameStats: maxDrawsInPass tracks the busiest pass", "[render-frame][frame-stats]")
{
    auto si = makeMinimal();
    // 3 draws in WorldOpaque, 1 in WorldCutout — max is 3.
    si.worldOpaqueDraws.push_back(makeDraw());
    si.worldOpaqueDraws.push_back(makeDraw());
    si.worldOpaqueDraws.push_back(makeDraw());

    v2::SceneDraw cd = makeDraw();
    cd.descriptor.pass = Poseidon::render::PassKind::WorldCutout;
    si.worldCutoutDraws.push_back(cd);

    const v2::FrameStats s = v2::CountFrameStats(v2::BuildFrame(si));

    REQUIRE(s.passCount == 2);
    REQUIRE(s.drawCount == 4);
    REQUIRE(s.maxDrawsInPass == 3);
}

TEST_CASE("Frame/FrameStats: counts unique non-zero texture and per-role buffer handles once", "[render-frame][frame-stats]")
{
    v2::Frame f;
    v2::Pass pass;

    v2::Draw a;
    a.mesh.vbo.id = 10;
    a.mesh.ibo.id = 20;
    a.textures[0].id = 100;
    a.textures[1].id = 200;

    v2::Draw b;
    b.mesh.vbo.id = 10;  // duplicate VBO
    b.mesh.ibo.id = 30;  // new IBO
    b.textures[0].id = 100; // duplicate texture
    b.textures[2].id = 300; // new texture

    v2::Draw c;
    // zero IDs are placeholders and should not count.
    c.mesh.vbo.id = 0;
    c.mesh.ibo.id = 0;

    pass.draws.push_back(a);
    pass.draws.push_back(b);
    pass.draws.push_back(c);
    f.passes.push_back(pass);

    const v2::FrameStats s = v2::CountFrameStats(f);

    REQUIRE(s.drawCount == 3);
    REQUIRE(s.uniqueVertexBufferCount == 1);
    REQUIRE(s.uniqueIndexBufferCount == 2);
    REQUIRE(s.uniqueTextureCount == 3);
}

TEST_CASE("Frame/FrameStats: same numeric id in VBO and IBO roles counts separately", "[render-frame][frame-stats]")
{
    v2::Frame f;
    v2::Pass pass;
    v2::Draw d;
    d.mesh.vbo.id = 7;
    d.mesh.ibo.id = 7;
    pass.draws.push_back(d);
    f.passes.push_back(pass);

    const v2::FrameStats s = v2::CountFrameStats(f);

    REQUIRE(s.uniqueVertexBufferCount == 1);
    REQUIRE(s.uniqueIndexBufferCount == 1);
}

TEST_CASE("Frame/FrameStats: mesh-only draws count as one VBO and IBO resource", "[render-frame][frame-stats]")
{
    v2::Frame f;
    v2::Pass pass;

    v2::Draw a;
    a.mesh.id = 77;

    v2::Draw b;
    b.mesh.id = 77;

    v2::Draw c;
    c.mesh.id = 88;

    pass.draws.push_back(a);
    pass.draws.push_back(b);
    pass.draws.push_back(c);
    f.passes.push_back(pass);

    const v2::FrameStats s = v2::CountFrameStats(f);

    REQUIRE(s.drawCount == 3);
    REQUIRE(s.uniqueVertexBufferCount == 2);
    REQUIRE(s.uniqueIndexBufferCount == 2);
}

TEST_CASE("Frame/FrameStats: a pure fold never accumulates across frames", "[render-frame][frame-stats]")
{
    auto s1 = makeMinimal();
    s1.worldOpaqueDraws.push_back(makeDraw());
    s1.worldOpaqueDraws.push_back(makeDraw());

    auto s2 = makeMinimal();
    s2.worldOpaqueDraws.push_back(makeDraw());

    REQUIRE(v2::CountFrameStats(v2::BuildFrame(s1)).drawCount == 2);
    REQUIRE(v2::CountFrameStats(v2::BuildFrame(s2)).drawCount == 1);
}
