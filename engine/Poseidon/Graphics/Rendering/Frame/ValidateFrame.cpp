#include <Poseidon/Graphics/Rendering/Frame/ValidateFrame.hpp>

#include <Poseidon/Graphics/Rendering/RenderPassDescriptor.hpp>
#include <Poseidon/Graphics/Rendering/ValidateRenderPassDescriptor.hpp>

#include <sstream>
#include <stddef.h>

namespace Poseidon
{

namespace render::frame
{

namespace
{

void check_I06_descriptor_validity(const Frame& f, ValidationResult& r)
{
    // Every Draw's descriptor must be internally consistent — reuses
    // the per-draw descriptor validator.
    for (size_t pi = 0; pi < f.passes.size(); ++pi)
    {
        const Pass& p = f.passes[pi];
        for (size_t di = 0; di < p.draws.size(); ++di)
        {
            const char* err = render::ValidateRenderPassDescriptor(p.draws[di].descriptor);
            if (err)
            {
                std::ostringstream os;
                os << "pass[" << pi << "].draw[" << di << "]: " << err;
                r.violations.push_back({"I-06", os.str()});
            }
        }
    }
}

void check_I08_pass_kind_descriptor_alignment(const Frame& f, ValidationResult& r)
{
    // One value, one meaning: a Pass tagged FramePassKind::Cockpit must
    // only contain draws whose descriptor routes to a Cockpit-family
    // PassKind.  Misalignment means the spec→PassKind translation drifted.
    for (size_t pi = 0; pi < f.passes.size(); ++pi)
    {
        const Pass& p = f.passes[pi];
        for (size_t di = 0; di < p.draws.size(); ++di)
        {
            const render::PassKind dpk = p.draws[di].descriptor.pass;
            const bool drawIsCockpit = dpk == render::PassKind::CockpitOpaque ||
                                       dpk == render::PassKind::CockpitCutout ||
                                       dpk == render::PassKind::CockpitTransparent;
            if (p.kind == FramePassKind::Cockpit && !drawIsCockpit)
            {
                std::ostringstream os;
                os << "pass[" << pi << "] is Cockpit but draw[" << di << "] descriptor.pass=" << static_cast<int>(dpk);
                r.violations.push_back({"I-08", os.str()});
            }
        }
    }
}

void check_I09_onsurface_disambiguation(const Frame& f, ValidationResult& r)
{
    // Coplanar disambiguation: an OnSurface draw must have either
    // polygon-offset or backface cull enabled.  Leaving both off
    // invites z-fighting.
    for (size_t pi = 0; pi < f.passes.size(); ++pi)
    {
        const Pass& p = f.passes[pi];
        for (size_t di = 0; di < p.draws.size(); ++di)
        {
            const auto& d = p.draws[di].descriptor;
            if (d.surface != render::SurfaceMode::OnSurface)
                continue;
            const bool hasOffset = (d.pass == render::PassKind::SurfaceOverlay);
            const bool hasCull = (d.cull != render::CullMode::None);
            if (!hasOffset && !hasCull)
            {
                std::ostringstream os;
                os << "pass[" << pi << "].draw[" << di << "]: OnSurface with no polygon-offset and no backface cull";
                r.violations.push_back({"I-09", os.str()});
            }
        }
    }
}

void check_I04_pass_ordering(const Frame& f, ValidationResult& r)
{
    // Pass topology: shadows precede world, world precedes cockpit,
    // screen space is last.  Out-of-order passes mean BuildFrame
    // mis-sequenced.
    auto rank = [](FramePassKind k) -> int
    {
        switch (k)
        {
            case FramePassKind::ShadowAccum:
                return 0;
            case FramePassKind::ShadowDarken:
                return 1;
            case FramePassKind::Sky:
                return 2;
            case FramePassKind::TerrainOpaque:
                return 3;
            case FramePassKind::WorldOpaque:
                return 4;
            case FramePassKind::WorldCutout:
                return 5;
            case FramePassKind::SurfaceOverlay:
                return 6;
            case FramePassKind::Water:
                return 7;
            case FramePassKind::WorldTransparent:
                return 8;
            case FramePassKind::Cockpit:
                return 9;
            case FramePassKind::ScreenSpace:
                return 10;
        }
        return 99;
    };
    int prev = -1;
    for (size_t pi = 0; pi < f.passes.size(); ++pi)
    {
        const int cur = rank(f.passes[pi].kind);
        if (cur < prev)
        {
            std::ostringstream os;
            os << "pass[" << pi << "] kind=" << static_cast<int>(f.passes[pi].kind)
               << " out of canonical order (rank=" << cur << " after " << prev << ")";
            r.violations.push_back({"I-PassOrder", os.str()});
        }
        prev = cur;
    }
}

void check_I20_no_new_gl_errors(const Frame& f, ValidationResult& r)
{
    // GL_INVALID_* count must not grow during a rendered frame.  Such
    // errors surface as HIGH-severity KHR_debug callbacks; SceneExtractor
    // compares the count against the last frame's snapshot and BuildFrame
    // carries the delta.
    if (f.newDebugErrors > 0)
    {
        std::ostringstream os;
        os << f.newDebugErrors << " new HIGH-severity GL error(s) this frame";
        if (!f.lastDebugMessage.empty())
            os << "; last: " << f.lastDebugMessage;
        r.violations.push_back({"I-20", os.str()});
    }
}

void check_I34_world_rect_no_stretch(const Frame& f, ValidationResult& r)
{
    // When the world renders into a cropped sub-rect (aspect pillarbox
    // / manual crop), the projection's FOV aspect must match the rect's
    // pixel aspect or every object stretches.  The full (uncropped)
    // rect is exempt: the ultrawide policy clamps the FOV below the
    // viewport ratio by design.
    const auto& c = f.camera;
    const float rectW = (c.worldRight - c.worldLeft) * static_cast<float>(c.viewport.width);
    const float rectH = (c.worldBottom - c.worldTop) * static_cast<float>(c.viewport.height);
    const bool cropped = c.worldLeft > 0.0f || c.worldTop > 0.0f || c.worldRight < 1.0f || c.worldBottom < 1.0f;
    if (!cropped || rectW <= 0.0f || rectH <= 0.0f)
        return;
    // ProjectionNormal: _11 = 1/cLeft, _22 = 1/cTop -> FOV aspect = _22/_11.
    if (c.projection._11 <= 0.0f || c.projection._22 <= 0.0f)
        return;
    const float fovAspect = c.projection._22 / c.projection._11;
    const float rectAspect = rectW / rectH;
    const float rel = fovAspect / rectAspect;
    if (rel < 0.98f || rel > 1.02f)
    {
        std::ostringstream os;
        os << "world rect aspect " << rectAspect << " vs FOV aspect " << fovAspect
           << " (cropped world must not stretch)";
        r.violations.push_back({"I-34", os.str()});
    }
}

void check_I24_viewport_nonempty(const Frame& f, ValidationResult& r)
{
    // Viewport must be non-empty.  Zero-sized means FB / camera setup
    // didn't propagate.
    if (f.camera.viewport.width <= 0 || f.camera.viewport.height <= 0)
    {
        std::ostringstream os;
        os << "viewport is " << f.camera.viewport.width << "x" << f.camera.viewport.height;
        r.violations.push_back({"I-24", os.str()});
    }
}

} // namespace

ValidationResult ValidateFrame(const Frame& f)
{
    ValidationResult r;
    check_I06_descriptor_validity(f, r);
    check_I08_pass_kind_descriptor_alignment(f, r);
    check_I09_onsurface_disambiguation(f, r);
    check_I04_pass_ordering(f, r);
    check_I20_no_new_gl_errors(f, r);
    check_I24_viewport_nonempty(f, r);
    check_I34_world_rect_no_stretch(f, r);
    return r;
}

} // namespace render::frame

} // namespace Poseidon
