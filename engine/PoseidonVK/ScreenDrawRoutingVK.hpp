#pragma once

#include <Poseidon/Core/Types.hpp>
#include <Poseidon/Graphics/Rendering/RenderFlags.hpp>

namespace Poseidon::vk
{

// These light/clip hints identify the legacy sky fallback. They must be emitted
// before world geometry, unlike NoZBuf cockpit and 2D draws which are overlays.
enum class ScreenDrawPhaseVK
{
    Background,
    Overlay,
};

constexpr ScreenDrawPhaseVK ScreenDrawPhaseFromLegacyContext(const render::LegacySpec& spec,
                                                             ClipFlags clipFlags) noexcept
{
    const ClipFlags lightHint = clipFlags & ClipLightMask;
    const bool isSkyBackdrop = lightHint == ClipLightCloud || lightHint == ClipLightStars;
    const bool isHorizon = (clipFlags & ClipUser0) != 0 && render::Has(spec.backend, render::Backend::NoZWrite) &&
                           !render::Has(spec.backend, render::Backend::NoZBuf);
    return isSkyBackdrop || isHorizon ? ScreenDrawPhaseVK::Background : ScreenDrawPhaseVK::Overlay;
}

} // namespace Poseidon::vk
