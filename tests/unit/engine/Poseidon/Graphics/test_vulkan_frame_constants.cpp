#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <PoseidonVK/FrameConstantsVK.hpp>
#include <Poseidon/Graphics/Rendering/Frame/BuildFrame.hpp>

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
    inputs.fogStart = 25.0f;
    inputs.fogEnd = 125.0f;
    inputs.fogColorRGBA = 0x336699ccu;
    return frame::BuildFrame(inputs);
}

} // namespace

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
