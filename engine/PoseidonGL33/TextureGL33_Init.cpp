#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>

#include <Poseidon/IO/Streams/QBStream.hpp>
#include <Poseidon/Foundation/Algorithms/Qsort.hpp>

using Poseidon::CmpStartStr;

#include <PoseidonGL33/TextureGL33.hpp>
#include <PoseidonGL33/GL33BindCache.hpp>
#include <PoseidonGL33/EngineGL33.hpp>
#include <Poseidon/IO/FileServer.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Graphics/Textures/LooseTextures.hpp>
#include <Poseidon/Graphics/Textures/PAADecoder.hpp>
#include <unordered_map>
#include <vector>
#include <cstring>

#include <glad/gl.h>

namespace
{

std::unordered_map<std::uint32_t, TextureGL33*>& TextureResourceRegistry()
{
    static std::unordered_map<std::uint32_t, TextureGL33*> registry;
    return registry;
}

std::uint32_t AllocateTextureResourceId()
{
    static std::uint32_t nextId = TextureGL33::FallbackResourceId() + 1;
    // Monotonic process-local ids: never reused, so a stale captured id cannot
    // alias a different texture later in the same run.
    return nextId++;
}

} // namespace

int MipmapSizeGL33(PacFormat format, int w, int h)
{
    switch (format)
    {
        case PacDXT1:
            return ((w + 3) / 4) * ((h + 3) / 4) * 8;
        case PacDXT2:
        case PacDXT3:
        case PacDXT4:
        case PacDXT5:
            return ((w + 3) / 4) * ((h + 3) / 4) * 16;
        case PacARGB8888:
            return w * h * 4;
        default:
            return w * h * 2; // 16-bit formats
    }
}

static PacFormat BasicFormat(const char* name)
{
    const char* ext = strrchr(name, '.');
    if (ext && !strcmpi(ext, ".paa"))
    {
        return PacARGB4444;
    }
    return PacARGB1555;
}

static PacFormat DstFormat(PacFormat srcFormat, int dxt)
{
    switch (srcFormat)
    {
        case PacARGB1555:
        case PacRGB565:
        case PacARGB4444:
            return srcFormat;
        case PacP8:
            return PacARGB1555;
        case PacAI88:
            if (static_cast<EngineGL33*>(GEngine)->Can88())
                return srcFormat;
            if (static_cast<EngineGL33*>(GEngine)->Can8888())
                return PacARGB8888;
            return PacARGB4444;
        case PacDXT1:
            if (static_cast<EngineGL33*>(GEngine)->CanDXT(1))
                return srcFormat;
            return PacARGB1555;
        default:
            LOG_DEBUG(Graphics, "Unsupported source format {}", (int)srcFormat);
            return srcFormat;
    }
}

void InitGLPixelFormat(TextureDescGL33& desc, PacFormat format, bool enableDXT)
{
    desc.compressed = false;
    switch (format)
    {
        case PacDXT1:
            desc.internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
            desc.compressed = true;
            break;
        case PacDXT3:
        case PacDXT2:
            desc.internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
            desc.compressed = true;
            break;
        case PacDXT5:
        case PacDXT4:
            desc.internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
            desc.compressed = true;
            break;
        case PacARGB1555:
            desc.internalFormat = GL_RGB5_A1;
            desc.pixelFormat = GL_BGRA;
            desc.pixelType = GL_UNSIGNED_SHORT_1_5_5_5_REV;
            break;
        case PacRGB565:
            desc.internalFormat = GL_RGB565;
            desc.pixelFormat = GL_RGB;
            desc.pixelType = GL_UNSIGNED_SHORT_5_6_5;
            break;
        case PacARGB4444:
            desc.internalFormat = GL_RGBA4;
            desc.pixelFormat = GL_BGRA;
            desc.pixelType = GL_UNSIGNED_SHORT_4_4_4_4_REV;
            break;
        case PacAI88:
            desc.internalFormat = GL_RG8;
            desc.pixelFormat = GL_RG;
            desc.pixelType = GL_UNSIGNED_BYTE;
            break;
        case PacARGB8888:
            desc.internalFormat = GL_RGBA8;
            desc.pixelFormat = GL_BGRA;
            desc.pixelType = GL_UNSIGNED_INT_8_8_8_8_REV;
            break;
        case PacP8:
            desc.internalFormat = GL_R8;
            desc.pixelFormat = GL_RED;
            desc.pixelType = GL_UNSIGNED_BYTE;
            Fail("Palette textures obsolete");
            break;
        default:
            Poseidon::Foundation::ErrorMessage("Texture has bad pixel format (GL).");
            break;
    }
}

#define MIN_MIP_SIZE 4

void TextureGL33::SetMipmapRange(int min, int max)
{
    if (min < 0)
        min = 0;
    if (max > _nMipmaps - 1)
        max = _nMipmaps - 1;
    if (min > max)
        min = max;
    _largestUsed = min;
    _nMipmaps = max + 1;
}

int TextureGL33::Init(const char* name)
{
    SetName(name);

    _maxSize = 0x10000;

    RString resolved = Poseidon::Graphics::ResolveLooseTexturePath(name);
    ITextureSourceFactory* factory = SelectTextureSourceFactory(resolved);
    if (!factory || !factory->Check(resolved))
    {
        Poseidon::Foundation::WarningMessage("Cannot load texture %s.", static_cast<const char*>(GetName()));
        _nMipmaps = 0;
        return -1;
    }

    return 0;
}

void TextureGL33::PreloadHeaders()
{
    RString resolved = Poseidon::Graphics::ResolveLooseTexturePath(Name());
    ITextureSourceFactory* factory = SelectTextureSourceFactory(resolved);
    if (!factory)
        return;
    factory->PreInit(resolved);
}

void TextureGL33::DoLoadHeaders()
{
    PoseidonAssert(!_initialized);
    _initialized = true;

    int i = -1;

    PacFormat format = BasicFormat(GetName());
    bool isPaa = (format == PacARGB4444);

    TextBankGL33* bank = static_cast<TextBankGL33*>(GEngine->TextBank());

    if (_maxSize >= 0x10000)
    {
        if (!CmpStartStr(Name(), "fonts\\"))
            _maxSize = 1024;
        else if (!CmpStartStr(Name(), "merged\\"))
            _maxSize = 2048;
        else if (bank->AnimatedNumber(Name()) >= 0 && IsAlpha())
            _maxSize = ENGINE_CONFIG.maxAnimText;
        else
            _maxSize = ENGINE_CONFIG.maxObjText;
    }

    RString resolved2 = Poseidon::Graphics::ResolveLooseTexturePath(Name());
    ITextureSourceFactory* factory = SelectTextureSourceFactory(resolved2);
    if (!factory)
    {
        _nMipmaps = 0;
        return;
    }
    _src = factory->Create(resolved2, _mipmaps, MAX_MIPMAPS);

    if (_src)
    {
        format = _src->GetFormat();

        if (format == PacARGB4444 || format == PacAI88 || format == PacARGB8888)
            _src->ForceAlpha();

        int dxt = static_cast<EngineGL33*>(GEngine)->DXTSupport();
        PacFormat dFormat = DstFormat(format, dxt);

        if (!_src->IsTransparent() && _src->GetFormat() == PacARGB1555)
        {
            if (bank->_engine->Can565() && !isPaa)
                dFormat = PacRGB565;
        }

        _largestUsed = MAX_MIPMAPS;
        int nMipmaps = _src->GetMipmapCount();
        for (i = 0; i < nMipmaps; i++)
        {
            PacLevelMem& mip = _mipmaps[i];
            mip.SetDestFormat(dFormat, 8);

            if (!mip.TooLarge(_maxSize))
            {
                if (_largestUsed > i)
                    _largestUsed = i;
            }

            if (mip._w < MIN_MIP_SIZE)
                break;
            if (mip._h < MIN_MIP_SIZE)
                break;
        }

        _nMipmaps = i;
        _levelLoaded = i;
        _smallLoaded = i;
        _levelNeededThisFrame = _levelNeededLastFrame = i;

        return;
    }

    Poseidon::Foundation::WarningMessage("Cannot load texture %s.", static_cast<const char*>(GetName()));
    _nMipmaps = 0;
}

void TextureGL33::LoadHeaders()
{
    if (_initialized)
        return;
    DoLoadHeaders();
}

Color TextureGL33::GetPixel(int level, float u, float v) const
{
    LoadHeadersNV();

    QIFStream in;
    GFileServer->Open(in, Name());
    if (in.fail())
        return HWhite;

    Color icol;
    QIFStream ipol;
    if (_interpolate)
    {
        GFileServer->Open(ipol, _interpolate->Name());
        if (ipol.fail())
            return HWhite;
    }

    PacLevelMem mip = _mipmaps[level];

    AUTO_STATIC_ARRAY(char, mem, 256 * 256 * 2);
    mem.Realloc(mip._pitch * mip._h);
    mem.Resize(mip._pitch * mip._h);

    _src->GetMipmapData(mem.Data(), mip, level);

    if (_interpolate)
    {
        AUTO_STATIC_ARRAY(char, imem, 256 * 256 * 2);
        PacLevelMem& imip = _interpolate->_mipmaps[level];
        imem.Realloc(imip._pitch * imip._h);
        imem.Resize(imip._pitch * imip._h);
        _interpolate->_src->GetMipmapData(imem.Data(), imip, level);
        icol = imip.GetPixel(imem.Data(), u, v);
    }
    Color col = mip.GetPixel(mem.Data(), u, v);
    if (_interpolate)
    {
        col = col * (1 - _iFactor) + icol * _iFactor;
    }
    return col;
}

// Decode the top (full-resolution) mip once and classify its alpha channel.
// Reads the texture's bytes through the VFS (so it works for PBO-packed textures) and
// decodes via the shared DecodePAABuffer, which handles every PAA/PAC pixel format
// (DXT1/3/5, ARGB8888/4444/1555, AI88, P8) — unlike PacLevelMem::GetPixelInt, which
// only covers a subset and would Fail per texel on the rest. Top mip on purpose: a
// smaller mip blurs a cutout's crisp 0/255 holes into false partial-alpha, which would
// mis-route a pole/fence to the blend pass.
Poseidon::AlphaStats::Kind TextureGL33::ScanTopMipAlphaClass()
{
    LoadHeadersNV();
    if (!_src)
        return Poseidon::AlphaStats::Opaque;

    QIFStream in;
    GFileServer->Open(in, Name());
    const int size = in.fail() ? 0 : in.rest();
    if (size <= 0)
        return Poseidon::AlphaStats::Opaque;

    AUTO_STATIC_ARRAY(char, fileData, 256 * 1024);
    fileData.Realloc(size);
    fileData.Resize(size);
    in.read(fileData.Data(), size);

    const char* name = Name();
    const size_t len = name ? strlen(name) : 0;
    const bool isPaa = len >= 4 && (name[len - 1] == 'a' || name[len - 1] == 'A'); // .paa vs .pac

    const Poseidon::DecodedImage img = Poseidon::DecodePAABuffer(fileData.Data(), static_cast<size_t>(size), isPaa);
    if (!img.valid())
        return Poseidon::AlphaStats::Opaque;

    return Poseidon::ClassifyAlpha(img.rgba.data(), static_cast<size_t>(img.width) * static_cast<size_t>(img.height))
        .kind;
}

Poseidon::AlphaStats::Kind TextureGL33::GetAlphaClass()
{
    if (_alphaClass >= 0)
        return static_cast<Poseidon::AlphaStats::Kind>(_alphaClass);

    LoadHeadersNV();
    Poseidon::AlphaStats::Kind kind = Poseidon::AlphaStats::Opaque;
    if (_src)
    {
        const bool hasAlpha = _src->IsAlpha();
        const bool chroma = _src->IsTransparent();
        const bool oneBit = _src->GetFormat() == PacDXT1; // 1-bit alpha: punch-through only
        // Only multi-bit-alpha formats need the (cached) decode to tell cutout from blend.
        Poseidon::AlphaStats decoded;
        const Poseidon::AlphaStats* decodedPtr = nullptr;
        if (hasAlpha && !oneBit)
        {
            decoded.kind = ScanTopMipAlphaClass();
            decodedPtr = &decoded;
        }
        kind = Poseidon::ClassifyTextureAlpha(hasAlpha, chroma, oneBit, decodedPtr);
    }
    _alphaClass = static_cast<signed char>(kind);
    return kind;
}

DEFINE_FAST_ALLOCATOR(TextureGL33);

TextureGL33::TextureGL33()
    : _nMipmaps(0), _levelLoaded(63), _smallLoaded(63), _levelNeededThisFrame(0), _levelNeededLastFrame(0),
      _isDetail(false), _useDetail(false), _cache(nullptr), _inUse(0), _interpolate(nullptr), _maxSize(256),
      _initialized(false)
{
    _textureResourceId = AllocateTextureResourceId();
    TextureResourceRegistry()[_textureResourceId] = this;
}

TextureGL33::~TextureGL33()
{
    TextureResourceRegistry().erase(_textureResourceId);
    ReleaseMemory(false);
    ReleaseSmall(false);
    GEngine->TextureDestroyed(this);
}

unsigned int TextureGL33::ResolveHandle(std::uint32_t resourceId)
{
    if (resourceId == FallbackResourceId())
        return 0;
    const auto it = TextureResourceRegistry().find(resourceId);
    return it != TextureResourceRegistry().end() && it->second ? it->second->GetHandle() : 0;
}

void TextureGL33::SetMaxSize(int size)
{
    LoadHeadersNV();
    if (size >= _maxSize)
        return;
    _maxSize = size;
}

void TextureGL33::SetMultitexturing(int type)
{
    _useDetail = (type != 0);
}

bool TextureGL33::VerifyChecksum(const MipInfo&) const
{
    return true;
}

void TextureGL33::ASetNMipmaps(int n)
{
    LoadHeadersNV();
    if (n > _nMipmaps)
    {
        LOG_ERROR(Graphics, "Out of range ASetNMipmaps in {}", static_cast<const char*>(GetName()));
        n = _nMipmaps;
    }
    _nMipmaps = n;
    PacLevelMem& mip = _mipmaps[n - 1];
    saturateMax(_maxSize, mip._w);
    saturateMax(_maxSize, mip._h);
    if (_largestUsed > _nMipmaps - 1)
        _largestUsed = _nMipmaps - 1;
    if (_interpolate)
        _interpolate->ASetNMipmaps(n);
}

int SurfaceInfoGL33::_nextId = 0;

int SurfaceInfoGL33::CalculateSize(const TextureDescGL33& desc, PacFormat format, int totalSize)
{
    if (totalSize >= 0)
        return totalSize;

    int size = 0;
    int w = desc.w, h = desc.h;
    for (int i = 0; i < desc.nMipmaps; i++)
    {
        size += MipmapSizeGL33(format, w, h);
        w = (w > 1) ? w / 2 : 1;
        h = (h > 1) ? h / 2 : 1;
    }
    return size;
}

int SurfaceInfoGL33::CreateSurface(const TextureDescGL33& desc, PacFormat format, int totalSize)
{
    _w = desc.w;
    _h = desc.h;
    _nMipmaps = desc.nMipmaps;
    _format = format;
    _id = _nextId++;

    _totalSize = CalculateSize(desc, format, totalSize);
    _usedSize = _totalSize;

    glGenTextures(1, &_texture);
    if (!_texture)
    {
        Poseidon::Foundation::ErrorMessage("Failed to create GL texture");
        return -1;
    }

    // All texture-storage and pixel-upload calls go via the dedicated upload
    // unit so unit 0/1's engine-tracked binding stays accurate — see
    // EngineGL33.hpp / kUploadUnit.
    GL33Bind::Tex2D(EngineGL33::kUploadUnit - GL_TEXTURE0, _texture);

    // Allocate immutable storage for all mipmap levels in a single call.
    // glTexStorage2D is core in GL 4.2 (and via ARB_texture_storage in GL 3.3
    // — required by our context).  It allocates all mip levels at once,
    // makes the format immutable (no driver re-validation per upload), and
    // reports allocation failure once instead of per-mip.  Subsequent
    // pixel uploads use glTex(Compressed)SubImage2D in the loader.
    glTexStorage2D(GL_TEXTURE_2D, desc.nMipmaps, desc.internalFormat, desc.w, desc.h);
    GLenum allocErr = glGetError();
    if (allocErr != GL_NO_ERROR)
    {
        LOG_ERROR(Graphics, "GL33: glTexStorage2D FAILED err=0x{:04X} tex={} {}x{} mips={} fmt=0x{:04X}", allocErr,
                  _texture, desc.w, desc.h, desc.nMipmaps, desc.internalFormat);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, desc.nMipmaps - 1);

    // Default filter: linear min/mag, nearest mip (matches D3D11 MIN_MAG_LINEAR_MIP_POINT)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // PacAI88 (Alpha-Intensity) → GL_RG8: D3D A8L8 samples as (L,L,L,A)
    // GL RG8 samples as (R,G,0,1) where R=luminance, G=alpha
    // Set swizzle to replicate D3D behavior
    if (format == PacAI88)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_GREEN);
    }

    // Restore the engine's normal active unit; the upload-unit binding
    // persists harmlessly (no shader samples from it).
    GL33Bind::ActiveUnit(0);

    return 0;
}

void SurfaceInfoGL33::Free(bool lastRef, int)
{
    if (lastRef && _texture)
    {
        GL33Bind::OnTexDeleted(_texture);
        glDeleteTextures(1, &_texture);
    }
    _texture = 0;
    _totalSize = 0;
    _usedSize = 0;
    _w = 0;
    _h = 0;
    _nMipmaps = 0;
}

bool TextureGL33::InitFromRGBA(int w, int h, const void* rgba, uint32_t size, bool mipmap)
{
    if (!rgba)
        return false;

    _initialized = true;
    _dynamicMipmapped = mipmap;

    int maxLevel = 0;
    if (mipmap)
    {
        int dim = w > h ? w : h;
        while (dim > 1)
        {
            dim >>= 1;
            maxLevel++;
        }
    }
    _nMipmaps = maxLevel + 1;
    _mipmaps[0]._w = static_cast<short>(w);
    _mipmaps[0]._h = static_cast<short>(h);
    _mipmaps[0]._pitch = static_cast<short>(w * 4);
    _maxSize = w > h ? w : h;

    unsigned int tex = 0;
    glGenTextures(1, &tex);
    if (!tex)
        return false;

    _surface.SetTexture(tex);
    GL33Bind::Tex2D(EngineGL33::kUploadUnit - GL_TEXTURE0, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, maxLevel);
    if (mipmap)
    {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        // Anisotropic filter keeps tilted-surface sampling crisp — isotropic LOD
        // over-blurs because the per-fragment UV footprint is elongated.
        float maxAniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        if (maxAniso > 1.0f)
        {
            float aniso = maxAniso < 8.0f ? maxAniso : 8.0f;
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
        }
    }
    else
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GL33Bind::ActiveUnit(0);

    _surface._w = w;
    _surface._h = h;
    _surface._nMipmaps = 1;
    _surface._totalSize = w * h * 4;
    _surface._usedSize = w * h * 4;

    // Mark as fully loaded so UseMipmap won't attempt demand-loading
    _levelLoaded = 0;
    _smallLoaded = 0;
    _largestUsed = 0;

    return true;
}

void TextureGL33::UpdateRGBA(const void* rgba, uint32_t size)
{
    if (!_surface.GetTexture() || !rgba)
        return;

    GL33Bind::Tex2D(EngineGL33::kUploadUnit - GL_TEXTURE0, _surface.GetTexture());
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, _surface._w, _surface._h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (_dynamicMipmapped)
        glGenerateMipmap(GL_TEXTURE_2D);
    GL33Bind::ActiveUnit(0);
}
