#include <PoseidonVK/GraphicsBackendVK.hpp>

#include <Poseidon/Graphics/GraphicsEngineFactory.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>

namespace
{

Poseidon::Engine* CreateVulkanBackend(const Poseidon::GraphicsEngineParams&)
{
    LOG_WARN(Core, "Vulkan graphics backend is registered but not implemented yet");
    return nullptr;
}

bool IsVulkanAvailable()
{
    return false;
}

} // namespace

namespace Poseidon
{

void RegisterVulkanGraphicsBackend()
{
    GraphicsEngineFactory::Register(GraphicsBackendDescriptor{
        "vulkan",
        "Vulkan",
        200,
        &CreateVulkanBackend,
        &IsVulkanAvailable,
    });
}

} // namespace Poseidon
