#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <PoseidonVK/BufferVK.hpp>
#include <PoseidonVK/DrawConstantsVK.hpp>
#include <PoseidonVK/FrameConstantsVK.hpp>
#include <Poseidon/Graphics/Rendering/Frame/BuildFrame.hpp>

#include <cstddef>
#include <cstdint>

namespace frame = Poseidon::render::frame;

namespace
{

frame::Frame makeFrame()
{
    frame::SceneInputs inputs;
    inputs.flags.hudEnabled = false;
    inputs.camera.viewport = {8, 16, 1280, 720};
    inputs.camera.nearPlane = 0.125f;
    inputs.camera.farPlane = 2500.0f;
    inputs.camera.worldLeft = 0.1f;
    inputs.camera.worldTop = 0.2f;
    inputs.camera.worldRight = 0.9f;
    inputs.camera.worldBottom = 0.8f;
    inputs.camera.view._11 = 1.0f;
    inputs.camera.view._22 = 2.0f;
    inputs.camera.view._33 = 3.0f;
    inputs.camera.view._44 = 4.0f;
    inputs.camera.projection._11 = 5.0f;
    inputs.camera.projection._22 = 6.0f;
    inputs.camera.projection._33 = 7.0f;
    inputs.camera.projection._44 = 8.0f;
    inputs.sunMatrix._11 = 9.0f;
    inputs.sunMatrix._22 = 10.0f;
    inputs.sunMatrix._33 = 11.0f;
    inputs.sunMatrix._44 = 12.0f;
    inputs.sunEnabled = true;
    inputs.fogStart = 25.0f;
    inputs.fogEnd = 125.0f;
    inputs.fogColorRGBA = 0x336699ccu;
    return frame::BuildFrame(inputs);
}

} // namespace

TEST_CASE("Vulkan frame constants match std140 descriptor layout", "[vulkan][frame-constants]")
{
    using Poseidon::vk::FrameConstantsVK;

    STATIC_REQUIRE(sizeof(Poseidon::GfxMatrix) == 64);
    STATIC_REQUIRE(offsetof(FrameConstantsVK, view) == 0);
    STATIC_REQUIRE(offsetof(FrameConstantsVK, projection) == 64);
    STATIC_REQUIRE(offsetof(FrameConstantsVK, sunMatrix) == 128);
    STATIC_REQUIRE(offsetof(FrameConstantsVK, viewport) == 192);
    STATIC_REQUIRE(offsetof(FrameConstantsVK, clipPlanes) == 208);
    STATIC_REQUIRE(offsetof(FrameConstantsVK, worldRect) == 224);
    STATIC_REQUIRE(offsetof(FrameConstantsVK, fogParams) == 240);
    STATIC_REQUIRE(offsetof(FrameConstantsVK, fogColor) == 256);
    STATIC_REQUIRE(offsetof(FrameConstantsVK, lightingParams) == 272);
    STATIC_REQUIRE(sizeof(FrameConstantsVK) == 288);
}

TEST_CASE("Vulkan mapped buffer upload copies frame constants bytes", "[vulkan][frame-constants]")
{
    Poseidon::vk::FrameConstantsVK source;
    source.viewport[0] = 8.0f;
    source.viewport[1] = 16.0f;
    source.viewport[2] = 1280.0f;
    source.viewport[3] = 720.0f;
    source.fogParams[0] = 25.0f;

    Poseidon::vk::FrameConstantsVK destination;
    Poseidon::vk::BufferVK buffer;
    buffer.mapped = &destination;
    buffer.size = sizeof(destination);

    Poseidon::vk::UploadMappedBuffer(buffer, &source, sizeof(source));

    CHECK(destination.viewport[0] == 8.0f);
    CHECK(destination.viewport[1] == 16.0f);
    CHECK(destination.viewport[2] == 1280.0f);
    CHECK(destination.viewport[3] == 720.0f);
    CHECK(destination.fogParams[0] == 25.0f);
}

TEST_CASE("Vulkan frame constants preserve frame camera matrices", "[vulkan][frame-constants]")
{
    const frame::Frame source = makeFrame();
    const Poseidon::vk::FrameConstantsVK constants = Poseidon::vk::BuildFrameConstants(source);

    CHECK(constants.view._11 == 1.0f);
    CHECK(constants.view._22 == 2.0f);
    CHECK(constants.view._33 == 3.0f);
    CHECK(constants.view._44 == 4.0f);
    CHECK(constants.projection._11 == 5.0f);
    CHECK(constants.projection._22 == 6.0f);
    CHECK(constants.projection._33 == 7.0f);
    CHECK(constants.projection._44 == 8.0f);
    CHECK(constants.sunMatrix._11 == 9.0f);
    CHECK(constants.sunMatrix._22 == 10.0f);
    CHECK(constants.sunMatrix._33 == 11.0f);
    CHECK(constants.sunMatrix._44 == 12.0f);
}

TEST_CASE("Vulkan frame constants expose viewport and world rect", "[vulkan][frame-constants]")
{
    const Poseidon::vk::FrameConstantsVK constants = Poseidon::vk::BuildFrameConstants(makeFrame());

    CHECK(constants.viewport[0] == 8.0f);
    CHECK(constants.viewport[1] == 16.0f);
    CHECK(constants.viewport[2] == 1280.0f);
    CHECK(constants.viewport[3] == 720.0f);
    CHECK(constants.clipPlanes[0] == 0.125f);
    CHECK(constants.clipPlanes[1] == 2500.0f);
    CHECK(constants.clipPlanes[2] == 0.1f);
    CHECK(constants.clipPlanes[3] == 0.2f);
    CHECK(constants.worldRect[0] == 0.1f);
    CHECK(constants.worldRect[1] == 0.2f);
    CHECK(constants.worldRect[2] == 0.9f);
    CHECK(constants.worldRect[3] == 0.8f);
}

TEST_CASE("Vulkan frame constants normalize RGBA fog color", "[vulkan][frame-constants]")
{
    const Poseidon::vk::FrameConstantsVK constants = Poseidon::vk::BuildFrameConstants(makeFrame());

    CHECK(constants.fogParams[0] == 25.0f);
    CHECK(constants.fogParams[1] == 125.0f);
    CHECK(constants.fogParams[2] == Catch::Approx(0.01f));
    CHECK(constants.fogParams[3] == 1.0f);
    CHECK(constants.fogColor[0] == Catch::Approx(0x33 / 255.0f));
    CHECK(constants.fogColor[1] == Catch::Approx(0x66 / 255.0f));
    CHECK(constants.fogColor[2] == Catch::Approx(0x99 / 255.0f));
    CHECK(constants.fogColor[3] == Catch::Approx(0xcc / 255.0f));
    CHECK(constants.lightingParams[0] == 1.0f);
}

TEST_CASE("Vulkan frame constants disable fog for non-positive ranges", "[vulkan][frame-constants]")
{
    frame::SceneInputs inputs;
    inputs.flags.hudEnabled = false;
    inputs.fogStart = 125.0f;
    inputs.fogEnd = 25.0f;

    const Poseidon::vk::FrameConstantsVK constants = Poseidon::vk::BuildFrameConstants(frame::BuildFrame(inputs));

    CHECK(constants.fogParams[2] == 0.0f);
    CHECK(constants.fogParams[3] == 0.0f);
}

TEST_CASE("Vulkan frame constants expose disabled sun state", "[vulkan][frame-constants]")
{
    frame::SceneInputs inputs;
    inputs.flags.hudEnabled = false;
    inputs.sunEnabled = false;

    const Poseidon::vk::FrameConstantsVK constants = Poseidon::vk::BuildFrameConstants(frame::BuildFrame(inputs));

    CHECK(constants.lightingParams[0] == 0.0f);
}

TEST_CASE("Vulkan draw constants match std140-friendly layout", "[vulkan][draw-constants]")
{
    using Poseidon::vk::DrawConstantsVK;

    STATIC_REQUIRE(sizeof(Poseidon::GfxMatrix) == 64);
    STATIC_REQUIRE(offsetof(DrawConstantsVK, world) == 0);
    STATIC_REQUIRE(offsetof(DrawConstantsVK, textureIds) == 64);
    STATIC_REQUIRE(offsetof(DrawConstantsVK, meshId) == 80);
    STATIC_REQUIRE(offsetof(DrawConstantsVK, depth) == 96);
    STATIC_REQUIRE(offsetof(DrawConstantsVK, frontFace) == 112);
    STATIC_REQUIRE(offsetof(DrawConstantsVK, surface) == 128);
    STATIC_REQUIRE(offsetof(DrawConstantsVK, alphaRef) == 144);
    STATIC_REQUIRE(sizeof(DrawConstantsVK) == 160);
}

TEST_CASE("Vulkan draw constants preserve draw resource and descriptor state", "[vulkan][draw-constants]")
{
    frame::Draw draw;
    draw.world._11 = 2.0f;
    draw.world._44 = 3.0f;
    draw.mesh.id = 42;
    draw.indexBegin = 5;
    draw.indexCount = 18;
    draw.textures[0].id = 100;
    draw.textures[1].id = 101;
    draw.descriptor.pass = Poseidon::render::PassKind::WorldTransparent;
    draw.descriptor.depth = Poseidon::render::DepthMode::ReadOnly;
    draw.descriptor.blend = Poseidon::render::BlendMode::AlphaBlend;
    draw.descriptor.fog = Poseidon::render::FogMode::AlphaFog;
    draw.descriptor.cull = Poseidon::render::CullMode::None;
    draw.descriptor.frontFace = Poseidon::render::FrontFaceMode::CCW;
    draw.descriptor.alpha = Poseidon::render::AlphaMode::Blend;
    draw.descriptor.lighting = Poseidon::render::LightingMode::Unlit;
    draw.descriptor.texGen = Poseidon::render::TexGenMode::Detail;
    draw.descriptor.surface = Poseidon::render::SurfaceMode::OnSurface;
    draw.descriptor.sampler.filter = Poseidon::render::SamplerFilter::Point;
    draw.descriptor.sampler.clampU = true;
    draw.descriptor.sampler.clampV = true;
    draw.descriptor.shader = Poseidon::render::ShaderFamily::Detail;
    draw.descriptor.alphaRef = 127;
    draw.descriptor.stencilExclusion = true;

    const Poseidon::vk::DrawConstantsVK constants = Poseidon::vk::BuildDrawConstants(draw);

    CHECK(constants.world._11 == 2.0f);
    CHECK(constants.world._44 == 3.0f);
    CHECK(constants.textureIds[0] == 100);
    CHECK(constants.textureIds[1] == 101);
    CHECK(constants.meshId == 42);
    CHECK(constants.indexBegin == 5);
    CHECK(constants.indexCount == 18);
    CHECK(constants.pass == static_cast<std::uint32_t>(Poseidon::render::PassKind::WorldTransparent));
    CHECK(constants.depth == static_cast<std::uint32_t>(Poseidon::render::DepthMode::ReadOnly));
    CHECK(constants.blend == static_cast<std::uint32_t>(Poseidon::render::BlendMode::AlphaBlend));
    CHECK(constants.fog == static_cast<std::uint32_t>(Poseidon::render::FogMode::AlphaFog));
    CHECK(constants.cull == static_cast<std::uint32_t>(Poseidon::render::CullMode::None));
    CHECK(constants.frontFace == static_cast<std::uint32_t>(Poseidon::render::FrontFaceMode::CCW));
    CHECK(constants.alpha == static_cast<std::uint32_t>(Poseidon::render::AlphaMode::Blend));
    CHECK(constants.lighting == static_cast<std::uint32_t>(Poseidon::render::LightingMode::Unlit));
    CHECK(constants.texGen == static_cast<std::uint32_t>(Poseidon::render::TexGenMode::Detail));
    CHECK(constants.surface == static_cast<std::uint32_t>(Poseidon::render::SurfaceMode::OnSurface));
    CHECK(constants.samplerFilter == static_cast<std::uint32_t>(Poseidon::render::SamplerFilter::Point));
    CHECK(constants.samplerClamp == 3);
    CHECK(constants.shader == static_cast<std::uint32_t>(Poseidon::render::ShaderFamily::Detail));
    CHECK(constants.alphaRef == 127);
    CHECK(constants.stencilExclusion == 1);
}

TEST_CASE("Vulkan frame draw constants preserve pass order", "[vulkan][draw-constants]")
{
    frame::SceneInputs inputs;
    inputs.flags.hudEnabled = false;

    frame::SceneDraw world;
    world.mesh.id = 10;
    world.indexCount = 3;

    frame::SceneDraw transparent;
    transparent.descriptor.pass = Poseidon::render::PassKind::WorldTransparent;
    transparent.mesh.id = 20;
    transparent.indexCount = 6;

    inputs.worldOpaqueDraws.push_back(world);
    inputs.worldTransparentDraws.push_back(transparent);

    const frame::Frame built = frame::BuildFrame(inputs);
    const std::vector<Poseidon::vk::DrawConstantsVK> constants = Poseidon::vk::BuildDrawConstants(built);

    REQUIRE(constants.size() == 2);
    CHECK(constants[0].meshId == 10);
    CHECK(constants[0].indexCount == 3);
    CHECK(constants[1].meshId == 20);
    CHECK(constants[1].indexCount == 6);
}
