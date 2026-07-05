#include <catch2/catch_test_macros.hpp>

#include <Poseidon/Graphics/GraphicsEngineFactory.hpp>

#include <string>
#include <vector>

using Poseidon::GraphicsBackend;

using Poseidon::GraphicsBackendDescriptor;
using Poseidon::GraphicsEngineFactory;

using Poseidon::Engine;
using Poseidon::GraphicsEngineParams;

namespace
{
int g_primarySentinel = 1;
int g_secondarySentinel = 2;
int g_dummySentinel = 3;

bool g_primaryAvailable = true;
bool g_secondaryAvailable = true;
bool g_dummyAvailable = true;
int g_primaryCreateCalls = 0;
int g_secondaryCreateCalls = 0;
int g_dummyCreateCalls = 0;

void ResetTestState()
{
    GraphicsEngineFactory::ResetForTesting();
    g_primaryAvailable = true;
    g_secondaryAvailable = true;
    g_dummyAvailable = true;
    g_primaryCreateCalls = 0;
    g_secondaryCreateCalls = 0;
    g_dummyCreateCalls = 0;
}

bool PrimaryAvailable()
{
    return g_primaryAvailable;
}

bool SecondaryAvailable()
{
    return g_secondaryAvailable;
}

bool DummyAvailable()
{
    return g_dummyAvailable;
}

Engine* CreatePrimary(const GraphicsEngineParams&)
{
    ++g_primaryCreateCalls;
    return reinterpret_cast<Engine*>(&g_primarySentinel);
}

Engine* CreateSecondary(const GraphicsEngineParams&)
{
    ++g_secondaryCreateCalls;
    return reinterpret_cast<Engine*>(&g_secondarySentinel);
}

Engine* CreateDummyForTest(const GraphicsEngineParams&)
{
    ++g_dummyCreateCalls;
    return reinterpret_cast<Engine*>(&g_dummySentinel);
}
} // namespace

TEST_CASE("GraphicsEngineFactory rejects incomplete and duplicate descriptors", "[graphics][factory][unit]")
{
    ResetTestState();

    REQUIRE_FALSE(GraphicsEngineFactory::Register(GraphicsBackendDescriptor{
        nullptr,
        "Missing code",
        5,
        &CreatePrimary,
        &PrimaryAvailable,
    }));
    REQUIRE_FALSE(GraphicsEngineFactory::Register(GraphicsBackendDescriptor{
        "missing-create",
        "Missing create",
        5,
        nullptr,
        &PrimaryAvailable,
    }));

    REQUIRE(GraphicsEngineFactory::Register(GraphicsBackendDescriptor{
        "primary",
        "Primary",
        50,
        &CreatePrimary,
        &PrimaryAvailable,
    }));
    REQUIRE_FALSE(GraphicsEngineFactory::Register(GraphicsBackendDescriptor{
        "PRIMARY",
        "Duplicate primary",
        5,
        &CreateSecondary,
        &SecondaryAvailable,
    }));
}

TEST_CASE("GraphicsEngineFactory enumerates by priority and filters unavailable backends", "[graphics][factory][unit]")
{
    ResetTestState();
    g_secondaryAvailable = false;

    REQUIRE(GraphicsEngineFactory::Register(GraphicsBackendDescriptor{
        "secondary",
        "Secondary",
        10,
        &CreateSecondary,
        &SecondaryAvailable,
    }));
    REQUIRE(GraphicsEngineFactory::Register(GraphicsBackendDescriptor{
        "primary",
        "Primary",
        30,
        &CreatePrimary,
        &PrimaryAvailable,
    }));
    REQUIRE(GraphicsEngineFactory::Register(GraphicsBackendDescriptor{
        "dummy",
        "Dummy",
        0,
        &CreateDummyForTest,
        &DummyAvailable,
    }));

    const auto registered = GraphicsEngineFactory::EnumerateRegistered();
    REQUIRE(registered.size() == 3);
    CHECK(std::string(registered[0].codeName) == "primary");
    CHECK(std::string(registered[1].codeName) == "secondary");
    CHECK(std::string(registered[2].codeName) == "dummy");

    const auto available = GraphicsEngineFactory::EnumerateAvailable();
    REQUIRE(available.size() == 2);
    CHECK(std::string(available[0].codeName) == "primary");
    CHECK(std::string(available[1].codeName) == "dummy");
}

TEST_CASE("GraphicsEngineFactory auto-selects the highest-priority available backend", "[graphics][factory][unit]")
{
    ResetTestState();
    g_primaryAvailable = false;

    REQUIRE(GraphicsEngineFactory::Register(GraphicsBackendDescriptor{
        "primary",
        "Primary",
        100,
        &CreatePrimary,
        &PrimaryAvailable,
    }));
    REQUIRE(GraphicsEngineFactory::Register(GraphicsBackendDescriptor{
        "secondary",
        "Secondary",
        50,
        &CreateSecondary,
        &SecondaryAvailable,
    }));
    REQUIRE(GraphicsEngineFactory::Register(GraphicsBackendDescriptor{
        "dummy",
        "Dummy",
        0,
        &CreateDummyForTest,
        &DummyAvailable,
    }));

    const GraphicsEngineParams params;
    Engine* created = GraphicsEngineFactory::CreateAuto(params);
    REQUIRE(created == reinterpret_cast<Engine*>(&g_secondarySentinel));
    CHECK(g_primaryCreateCalls == 0);
    CHECK(g_secondaryCreateCalls == 1);
    CHECK(g_dummyCreateCalls == 0);
}

TEST_CASE("GraphicsEngineFactory resolves explicit backend codes", "[graphics][factory][unit]")
{
    ResetTestState();

    REQUIRE(GraphicsEngineFactory::Register(GraphicsBackendDescriptor{
        "primary",
        "Primary",
        10,
        &CreatePrimary,
        &PrimaryAvailable,
    }));
    REQUIRE(GraphicsEngineFactory::Register(GraphicsBackendDescriptor{
        "dummy",
        "Dummy",
        0,
        &CreateDummyForTest,
        &DummyAvailable,
    }));

    const GraphicsEngineParams params;
    REQUIRE(GraphicsEngineFactory::Create("primary", params) == reinterpret_cast<Engine*>(&g_primarySentinel));
    REQUIRE(GraphicsEngineFactory::Create("PRIMARY", params) == reinterpret_cast<Engine*>(&g_primarySentinel));
    REQUIRE(GraphicsEngineFactory::Create("missing", params) == nullptr);
    REQUIRE(GraphicsEngineFactory::Create(GraphicsBackend::Auto, params) ==
            reinterpret_cast<Engine*>(&g_primarySentinel));
    REQUIRE(GraphicsEngineFactory::IsBackendAvailable(GraphicsBackend::Auto));
}

TEST_CASE("GraphicsEngineFactory exposes Vulkan registration during Phase 1 stub", "[graphics][factory][vulkan]")
{
    ResetTestState();

    Poseidon::RegisterVulkanGraphicsBackend();

    const auto registered = GraphicsEngineFactory::EnumerateRegistered();
    REQUIRE(registered.size() == 1);
    CHECK(std::string(registered[0].codeName) == "vulkan");
    CHECK(std::string(registered[0].displayName) == "Vulkan");
    CHECK(registered[0].priority == 50);
    CHECK(registered[0].isAvailable == GraphicsEngineFactory::IsBackendAvailable(GraphicsBackend::Vulkan));

    CHECK(std::string(GraphicsEngineFactory::GetBackendName(GraphicsBackend::Vulkan)) == "Vulkan");
}
