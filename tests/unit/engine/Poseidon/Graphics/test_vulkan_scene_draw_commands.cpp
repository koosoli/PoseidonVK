#include <catch2/catch_test_macros.hpp>

#include <PoseidonVK/SceneDrawCommandsVK.hpp>

#include <vector>

TEST_CASE("Vulkan scene draw commands keep drawable mesh/index ranges", "[vulkan][scene-draw-commands]")
{
    std::vector<Poseidon::vk::DrawConstantsVK> draws(3);
    draws[0].meshId = 10;
    draws[0].indexBegin = 6;
    draws[0].indexCount = 12;
    draws[1].meshId = 0;
    draws[1].indexCount = 9;
    draws[2].meshId = 20;
    draws[2].indexBegin = 30;
    draws[2].indexCount = 3;

    const std::vector<Poseidon::vk::SceneDrawCommandVK> commands =
        Poseidon::vk::BuildSceneDrawCommands(draws);

    REQUIRE(commands.size() == 2);
    CHECK(commands[0].drawIndex == 0);
    CHECK(commands[0].meshId == 10);
    CHECK(commands[0].firstIndex == 6);
    CHECK(commands[0].indexCount == 12);
    CHECK(commands[1].drawIndex == 2);
    CHECK(commands[1].meshId == 20);
    CHECK(commands[1].firstIndex == 30);
    CHECK(commands[1].indexCount == 3);
}

TEST_CASE("Vulkan scene draw commands skip empty index ranges", "[vulkan][scene-draw-commands]")
{
    Poseidon::vk::DrawConstantsVK draw;
    draw.meshId = 42;
    draw.indexCount = 0;

    CHECK_FALSE(Poseidon::vk::IsDrawableSceneDraw(draw));
    CHECK(Poseidon::vk::BuildSceneDrawCommands(std::vector<Poseidon::vk::DrawConstantsVK>{draw}).empty());
}

TEST_CASE("Vulkan scene draw commands obey command limit", "[vulkan][scene-draw-commands]")
{
    std::vector<Poseidon::vk::DrawConstantsVK> draws(2);
    draws[0].meshId = 10;
    draws[0].indexCount = 3;
    draws[1].meshId = 20;
    draws[1].indexCount = 6;

    const std::vector<Poseidon::vk::SceneDrawCommandVK> commands =
        Poseidon::vk::BuildSceneDrawCommands(draws, 1);

    REQUIRE(commands.size() == 1);
    CHECK(commands[0].drawIndex == 0);
    CHECK(commands[0].meshId == 10);
}
