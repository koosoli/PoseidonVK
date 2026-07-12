#include <Poseidon/Graphics/Dummy/EngineDummy.hpp>
#include <Poseidon/Graphics/Dummy/TextBankDummy.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Foundation/Framework/AppFrame.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Memtype.h>

// SDL input buffer functions (InputProcessing_sdl.cpp) — shared with the GL33
// event window so injected events reach the same handlers a real device would.
extern void SDLInput_BufferKeyEvent(SDL_Scancode sc, bool down, DWORD timestamp);
extern void SDLInput_BufferMouseButton(int btn, bool down);
extern void SDLInput_BufferMouseMotion(float dx, float dy);
extern void SDLInput_BufferMouseWheel(float dy);
extern void SDLInput_BufferUIKeyEvent(SDL_Keycode key, bool down);
extern void SDLInput_BufferUICharEvent(const char* text);

namespace Poseidon
{

EngineDummy::EngineDummy()
{
    _bank = new TextBankDummy();
}

void EngineDummy::HandleEvents()
{
    // No window, so no resize / fullscreen / focus handling — but the SDL event
    // queue still fills (harness `key` injection, --auto-key, real input under a
    // hidden window).  Drain it and route input to the same buffers GL33 uses.
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (GApp)
                    GApp->m_closeRequest = true;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (!event.key.repeat)
                    SDLInput_BufferKeyEvent(event.key.scancode, true, Foundation::GlobalTickCount());
                SDLInput_BufferUIKeyEvent(event.key.key, true);
                break;
            case SDL_EVENT_KEY_UP:
                SDLInput_BufferKeyEvent(event.key.scancode, false, Foundation::GlobalTickCount());
                SDLInput_BufferUIKeyEvent(event.key.key, false);
                break;
            case SDL_EVENT_TEXT_INPUT:
                SDLInput_BufferUICharEvent(event.text.text);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                int btn = event.button.button - 1;
                if (btn == 1)
                    btn = 2;
                else if (btn == 2)
                    btn = 1;
                SDLInput_BufferMouseButton(btn, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                break;
            }
            case SDL_EVENT_MOUSE_MOTION:
                SDLInput_BufferMouseMotion(event.motion.xrel, event.motion.yrel);
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                SDLInput_BufferMouseWheel(event.wheel.y);
                break;
            default:
                break;
        }
    }
}

EngineDummy::~EngineDummy()
{
    if (_bank)
    {
        delete _bank, _bank = nullptr;
    }
}

RString EngineDummy::GetDebugName() const
{
    return "No";
}
RString EngineDummy::GetRendererName() const
{
    return "None";
}
void EngineDummy::InitDraw() {}
void EngineDummy::FinishDraw()
{
    Engine::FinishDraw();
}
void EngineDummy::Pause() {}
void EngineDummy::Restore() {}
void EngineDummy::DrawPicture555(unsigned short*) {}
void EngineDummy::FogColorChanged(const Color&) {}
void EngineDummy::LightChanged(const Color&, const Color&) {}
void EngineDummy::NightEffectChanged(float) {}

bool EngineDummy::SwitchRes(int w, int h, int bpp)
{
    return true;
}
bool EngineDummy::SwitchRefreshRate(int refresh)
{
    return true;
}
void EngineDummy::ListResolutions(FindArray<ResolutionInfo>& ret) {}
void EngineDummy::ListRefreshRates(FindArray<int>& ret) {}

bool EngineDummy::CanZBias() const
{
    return false;
}
bool EngineDummy::ZBiasExclusion() const
{
    return false;
}

int EngineDummy::PixelSize() const
{
    return 32;
}
int EngineDummy::RefreshRate() const
{
    return 0;
}
bool EngineDummy::CanBeWindowed() const
{
    return true;
}
bool EngineDummy::IsWindowed() const
{
    return true;
}

AbstractTextBank* EngineDummy::TextBank()
{
    return _bank;
}

void EngineDummy::ResetForRemount()
{
    if (_bank)
        _bank->ReleaseAllTextures();
}

float EngineDummy::ZShadowEpsilon() const
{
    return 0;
}
float EngineDummy::ZRoadEpsilon() const
{
    return 0;
}
float EngineDummy::ObjMipmapCoef() const
{
    return 1;
}
float EngineDummy::LandMipmapCoef() const
{
    return 1;
}
bool EngineDummy::ShadowsFirst() const
{
    return false;
}
bool EngineDummy::SortByShape() const
{
    return false;
}

int EngineDummy::GetBias()
{
    return 0;
}
void EngineDummy::SetBias(int) {}
void EngineDummy::GetZCoefs(float& zAdd, float& zMult)
{
    zAdd = 0, zMult = 1;
}

int EngineDummy::Width() const
{
    return 160;
}
int EngineDummy::Height() const
{
    return 120;
}
int EngineDummy::Width2D() const
{
    return 80;
}
int EngineDummy::Height2D() const
{
    return 60;
}
int EngineDummy::AFrameTime() const
{
    return 0;
}

void EngineDummy::PrepareTriangle(const PacLevelMem*, int) {}
void EngineDummy::DrawPolygon(const VertexIndex* i, int n) {}
void EngineDummy::DrawSection(const FaceArray&, Offset b, Offset e) {}

void EngineDummy::DrawDecal(Vector3Par pos, float rhw, float sizeX, float sizeY, PackedColor col, const MipInfo& mip,
                            int specFlags)
{
}

void EngineDummy::Draw2D(const PacLevelMem*, PackedColor, float, float, float, float, float, float, float, float) {}

void EngineDummy::Clear(bool, bool, PackedColor) {}
void EngineDummy::SetGamma(float) {}
float EngineDummy::GetGamma() const
{
    return 0;
}
void EngineDummy::PrepareTriangle(const MipInfo&, int) {}
void EngineDummy::DrawPolygon(TLVertexTable&, const short*, int) {}
void EngineDummy::Draw2D(const Draw2DPars&, const Rect2DAbs&, const Rect2DAbs&) {}
void EngineDummy::DrawLine(int beg, int end) {}
void EngineDummy::DrawLine(const Line2DAbs&, PackedColor, PackedColor, const Rect2DAbs&) {}
void EngineDummy::PrepareMesh(const render::LegacySpec& /*spec*/, ClipFlags /*clipFlags*/) {}
void EngineDummy::BeginMesh(TLVertexTable&, const render::LegacySpec& /*spec*/) {}
void EngineDummy::EndMesh(TLVertexTable&) {}
void EngineDummy::TextureDestroyed(Texture*) {}
float EngineDummy::GetZCoef() const
{
    return 1;
}

void EngineDummy::DrawPoly(const MipInfo& mip, const Vertex2DPixel* vertices, int n, const Rect2DPixel& clipRect,
                           int specFlags)
{
}

void EngineDummy::DrawPoly(const MipInfo& mip, const Vertex2DAbs* vertices, int n, const Rect2DAbs& clipRect,
                           int specFlags)
{
}

Engine* CreateEngineDummy()
{
    return new EngineDummy();
}

} // namespace Poseidon
