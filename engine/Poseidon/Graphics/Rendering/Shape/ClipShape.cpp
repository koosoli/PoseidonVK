#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Core/TLVertex.hpp>
#include <Poseidon/World/Terrain/Landscape.hpp>
#include <Poseidon/World/World.hpp>
#include <Poseidon/World/Entities/Vehicles/Plane.hpp>
#include <Poseidon/Foundation/Math/PolyClip.hpp>

#include <Poseidon/Graphics/Rendering/Shape/ClipShape.hpp>
#include <Poseidon/Foundation/Common/FltOpts.hpp>
#include <Poseidon/Foundation/Containers/StaticArray.hpp>
#include <Poseidon/Foundation/Containers/StreamArray.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Math/Math3DP.hpp>
#include <Poseidon/Foundation/Math/MathDefs.hpp>
#include <Poseidon/Foundation/Memory/MemAlloc.hpp>

extern bool DisableTextures;

namespace Poseidon
{
using Poseidon::Foundation::MStorage;

#ifdef _DEBUG
inline int offsetToInt(Offset o)
{
    return o.GetOffset();
}
#else
inline int offsetToInt(Offset o)
{
    return o;
}
#endif

static StaticStorage<char> CharStorageF; // storage for faces
static StaticStorage<char> CharStorageS; // storage for sections

#define VERIFY_TXT_PTR 0

#if VERIFY_TXT_PTR
} // namespace Poseidon
#include "win.h"
namespace Poseidon
{
#endif

bool FaceArray::VerifyStructure() const
{
#if VERIFY_TXT_PTR
    // verify all face textures are valid
    for (Offset f = Begin(); f < End(); Next(f))
    {
        const Poly& face = (*this)[f];
        // force access to texture
        // chech if pointer is valid
        Texture* texture = face.GetTexture();
        if (::IsBadReadPtr(texture, sizeof(Texture)))
        {
            LOG_DEBUG(Graphics, "FaceArray::VerifyStructure: Bad read pointer");
            return false;
        }
        if (::IsBadWritePtr(texture, sizeof(Texture)))
        {
            LOG_DEBUG(Graphics, "FaceArray::VerifyStructure: Bad write pointer");
            return false;
        }
    }
#endif

    // verify section structure
    Offset lastOffset = Offset(0);

    for (int s = 0; s < _sections.Size(); s++)
    {
        const ShapeSection& sec = _sections[s];
        if (sec.beg != lastOffset)
        {
            LOG_ERROR(Graphics, "sec.beg!=lastOffset : {}!={}", offsetToInt(sec.beg), offsetToInt(lastOffset));
            return false;
        }
        lastOffset = sec.end;
    }

    return true;
}

void FaceArray::ReserveFaces(int size, bool dynamic)
{
    if (!dynamic)
    {
        GetData().SetStorage(CharStorageF.Init(64 * 1024));
        _sections.SetStorage(CharStorageS.Init(1024));
    }
    base::Reserve(size);
}

FaceArray::FaceArray(int size, bool dynamic)
{
    if (!dynamic)
    {
        GetData().SetStorage(CharStorageF.Init(64 * 1024));
        _sections.SetStorage(CharStorageS.Init(1024));
    }
    if (size)
    {
        base::Realloc(size);
    }
}

void FaceArray::Clip(const FaceArray& faces, TLVertexTable& tlMesh, const Camera& camera, ClipFlags clipFlags,
                     bool doCull)
{
    // note: clipFlags should be set by CheckClipping
    // copy all faces and perform per-face clipping
    Clear();
    // transfer section information from faces
    // process section by section
    _sections.Realloc(faces._sections.Size());
    for (int i = 0; i < faces._sections.Size(); i++)
    {
        const ShapeSection& srcSec = faces._sections[i];
        ShapeSection& sec = _sections.Append();
        // copy all properites from source section
        sec = srcSec;
        // keep track of changed offsets
        sec.beg = End();
        if (!clipFlags)
        {
            // we guarantee no clipping
            for (Offset si = srcSec.beg, se = srcSec.end; si < se; faces.Next(si))
            {
                const Poly& sf = faces[si];
                if (doCull && sf.BackfaceCull(tlMesh))
                {
                    continue;
                }
                Add(sf);
            }
        }
        else
        {
            for (Offset si = srcSec.beg, se = srcSec.end; si < se; faces.Next(si))
            {
                const Poly& sf = faces[si];
                if (doCull && sf.BackfaceCull(tlMesh))
                {
                    continue;
                }
                Poly df = sf;
                df.Clip(tlMesh, camera, clipFlags);
                if (df.N() < 3)
                {
                    continue;
                }
                Add(df);
            }
        }
        // keep track of changed offsets
        sec.end = End();
    }
    if (_sections.Size() > 0)
    {
        DoAssert(_sections[_sections.Size() - 1].end == End());
    }
}

Poly* FaceArray::AddClipped(const Poly& face, TLVertexTable& tlMesh, Scene& scene, ClipFlags clipFlags)
{
    if (face.BackfaceCull(tlMesh))
    {
        return nullptr;
    }
    Poly df = face;
    df.Clip(tlMesh, *scene.GetCamera(), clipFlags);
    if (df.N() < 3)
    {
        return nullptr;
    }
    Offset o = Add(df);
    return &Set(o);
}

Poly* FaceArray::AddNoClip(const Poly& face, TLVertexTable& tlMesh, Scene& scene)
{
    if (face.BackfaceCull(tlMesh))
    {
        return nullptr;
    }
    Offset o = Add(face);
    return &Set(o);
}

/*
\patch_internal 1.50 Date 4/7/2002 by Ondra
- Optimized: SW T&L renderstate setting is now done per-section.
*/

void FaceArray::Draw(const IAnimator* matSource, TLVertexTable& tlTable, const LightList& lights, const Shape& mesh,
                     ClipFlags clipFlags, int spec, const Matrix4& invTransform) const
{
    DoAssert(_sections.Size() == 0 || _sections[_sections.Size() - 1].end == End());
    Engine* engine = GEngine;
    const ClipFlags drawClipFlags = clipFlags;
    if ((spec & OnSurface) == 0)
    {
        ClipFlags andClip = clipFlags;
        clipFlags &= tlTable.CheckClipping(*GScene->GetCamera(), clipFlags, andClip);
        if (andClip)
        {
            return;
        }
    }

    ClipFlags backendClipFlags = drawClipFlags;
    const ClipFlags lightHint = mesh.GetAndHints() & ClipLightMask;
    if (lightHint == ClipLightCloud || lightHint == ClipLightStars)
        backendClipFlags |= lightHint;
    engine->PrepareMesh(render::SplitLegacy(spec), backendClipFlags);
    if (spec & (OnSurface | IsOnSurface))
    {
        engine->SetBias(0x10);
    }
    else
    {
        int bias = (spec & ZBiasMask) / ZBiasStep;
        // max. bias value is 3
        engine->SetBias(bias * 5);
    }

    tlTable.DoLighting(matSource, invTransform, lights, mesh, spec);

    // optimize for common case
    // no clipping - we can draw directly
    const FaceArray* drawFaces = this;
    FaceArray clippedFaces;
    if ((clipFlags & ClipAll) || (spec & OnSurface))
    {
        clippedFaces.ReserveFaces(0, false);
        if (spec & OnSurface)
        {
            float y = ((spec & IsShadow) ? engine->ZShadowEpsilon() : engine->ZRoadEpsilon());
            clippedFaces.SurfaceSplit(*this, tlTable, *GScene, clipFlags, y);
        }
        else
        {
            clippedFaces.Clip(*this, tlTable, *GScene->GetCamera(), clipFlags);
        }
        drawFaces = &clippedFaces;
    }

    if (drawFaces->Begin() < drawFaces->End())
    {
        tlTable.DoPerspective(*GScene->GetCamera(), clipFlags);
        engine->BeginMesh(tlTable, render::SplitLegacy(spec));
        Texture* lastTexture = (Texture*)-1; // something that is not equal to any valid texture
        int lastSpec = -1;
        int nSections = drawFaces->_sections.Size();
        // verify there are some sections
        DoAssert(nSections != 0);
        // verify all source shape sections are still there
        DoAssert(mesh.NSections() == nSections);
        // verify whole FaceArray is covered by sections
        DoAssert(nSections != 0 && drawFaces->_sections[nSections - 1].end == drawFaces->End());
        for (int s = 0; s < nSections; s++)
        {
            const ShapeSection& sec = drawFaces->_sections[s];
            Texture* texture = sec.properties.GetTexture();
            if (DisableTextures)
            {
                texture = nullptr;
            }
            int spec = sec.properties.Special();
            if (spec & (IsHidden | IsHiddenProxy))
            {
                continue;
            }
            if (texture != lastTexture || spec != lastSpec)
            {
                lastSpec = spec;
                lastTexture = texture;
                sec.properties.Prepare(texture, spec);
            }

            GEngine->DrawSection(*drawFaces, sec.beg, sec.end);
        }
        engine->EndMesh(tlTable);
        engine->SetBias(0);
        int idleMs = engine->HowLongIdle();
        if (idleMs >= 0 && GLOB_WORLD)
        {
            GLOB_WORLD->PrimaryAllowSwitch(idleMs);
        }
    }
}

inline void swap(Poly*& a, Poly*& b)
{
    Poly* t = a;
    a = b;
    b = t;
}

void FaceArray::SurfaceSplit(const FaceArray& faces, TLVertexTable& tlMesh, Scene& scene, ClipFlags clipFlags, float y)
{
    // note: it may be clipped by any plane
    clipFlags = ClipAll;
    // clear the object data
    Matrix3Val nTrans = scene.CamNormalTrans();
    Matrix4Val pTrans = scene.ScaledInvTransform();

    Clear();

    // fill with surface split
    const Camera& camera = *scene.GetCamera();
    Realloc(faces.Size() * 2 + 8);

    // determine range of landscape squares polygon is in
    Vector3Val sNo = nTrans.DirectionAside();
    Vector3Val uNo = nTrans.Direction();
    Vector3 cNo(VMultiply, nTrans, Vector3(+H_SQRT2 / 2, 0, +H_SQRT2 / 2));

    Poly zTemp1, zTemp2;
    Poly xTemp1, xTemp2;
    Poly splitA, splitB;
    for (int s = 0; s < faces._sections.Size(); s++)
    {
        const ShapeSection& sec = faces._sections[s];
        ShapeSection& dstSec = _sections.Append();
        dstSec = sec;
        dstSec.beg = End();
        for (Offset i = sec.beg, e = sec.end; i < e; faces.Next(i))
        {
            Poly source;
            source = faces[i];

            if (!(source.Special() & OnSurface))
            {
                source.CheckClip(tlMesh, camera, clipFlags);
                if (source.N() >= 3)
                {
                    Add(source);
                }
                continue;
            }

            Vector3 pos(VFastTransform, scene.CamInvTrans(), tlMesh.TransPosA(source.GetVertex(0)));
            float xFMin = pos.X(), xFMax = xFMin;
            float yFMin = pos.Y(), yFMax = yFMin;
            float zFMin = pos.Z(), zFMax = zFMin;
            for (int ii = 1; ii < source.N(); ii++)
            {
                Vector3 pos(VFastTransform, scene.CamInvTrans(), tlMesh.TransPosA(source.GetVertex(ii)));
                xFMin = floatMin(xFMin, pos.X());
                xFMax = floatMax(xFMax, pos.X());
                yFMin = floatMin(yFMin, pos.Y());
                yFMax = floatMax(yFMax, pos.Y());
                zFMin = floatMin(zFMin, pos.Z());
                zFMax = floatMax(zFMax, pos.Z());
            }

            int xMin = toIntFloor(xFMin * InvTerrainGrid);
            int zMin = toIntFloor(zFMin * InvTerrainGrid);
            int xMax = toIntFloor(xFMax * InvTerrainGrid);
            int zMax = toIntFloor(zFMax * InvTerrainGrid);

            // optimization impossible due to triangulation

            Poly* zRest = &source;
            Poly* zSplit = &zTemp1;
            Poly* zFree = &zTemp2;
            for (int z = zMin; z <= zMax; z++)
            {
                float zT = z * TerrainGrid;
                if (z < zMax)
                {
                    float zB = (z + 1) * TerrainGrid;
                    Vector3 ptB(VFastTransform, pTrans, Vector3(0, 0, zB));
                    Plane bottom(-uNo, ptB);
                    // cut a part from zRest and use it for x-splitting
                    zRest->Split(tlMesh, *zSplit, *zFree, bottom.Normal(), bottom.D());
                    zSplit->CopyProperties(*zRest);
                    zFree->CopyProperties(*zRest);
                    swap(zFree, zRest);
                }
                else
                {
                    swap(zRest, zSplit); // use whole zRest
                }
                Poly* xRest = zSplit; // zSplit will be destroyed during x splitting
                Poly* xSplit = &xTemp1;
                Poly* xFree = &xTemp2;
                for (int x = xMin; x <= xMax; x++)
                {
                    // four clipping planes
                    float xR = (x + 1) * TerrainGrid;
                    Vector3 ptRT(VFastTransform, pTrans, Vector3(xR, 0, zT));
                    if (x < xMax)
                    {
                        // left and right side clipping
                        Plane right(-sNo, ptRT);
                        xRest->Split(tlMesh, *xSplit, *xFree, right.Normal(), right.D());
                        xSplit->CopyProperties(*xRest);
                        xFree->CopyProperties(*xRest);
                        swap(xFree, xRest);
                    }
                    else
                    {
                        swap(xSplit, xRest);
                    }

                    // split 'xSplit' to two part (by A/B triangles)
                    Plane cutA(-cNo, ptRT);
                    xSplit->Split(tlMesh, splitA, splitB, cutA.Normal(), cutA.D());

                    if (splitA.N() >= 3)
                    {
                        splitA.CopyProperties(*xSplit);
                        splitA.FitToLandscape(tlMesh, scene, y);
                        splitA.CheckClip(tlMesh, camera, clipFlags);
                        if (splitA.N() >= 3)
                        {
                            Add(splitA);
                        }
                    }

                    if (splitB.N() >= 3)
                    {
                        splitB.CopyProperties(*xSplit);
                        splitB.FitToLandscape(tlMesh, scene, y);
                        splitB.CheckClip(tlMesh, camera, clipFlags);
                        if (splitB.N() >= 3)
                        {
                            Add(splitB);
                        }
                    }
                }
            }
        }
        dstSec.end = End();
    }
    DoAssert(_sections.Size() == 0 || _sections[_sections.Size() - 1].end == End());
}

void FaceArray::Draw(const IAnimator* matSource, const LightList& lights, const Shape& mesh, ClipFlags clip, int spec,
                     const Matrix4& transform, const Matrix4& invTransform) const
{
    Matrix4 pointView = GScene->ScaledInvTransform() * transform;
    TLVertexTable tlTable(matSource, mesh, pointView);
    Draw(matSource, tlTable, lights, mesh, clip, spec, invTransform);
}
} // namespace Poseidon
