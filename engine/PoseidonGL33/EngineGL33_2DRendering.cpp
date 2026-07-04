#include <PoseidonGL33/EngineGL33.hpp>
#include <PoseidonGL33/GL33BindCache.hpp>
#include <PoseidonGL33/TextureGL33.hpp>

#include <PoseidonGL33/GLIndexBuffer.hpp>
#include <PoseidonGL33/GLVertexAttribLayouts.hpp>

#include <glad/gl.h>

void EngineGL33::CreateTextBank()
{
    _textBank = new TextBankGL33(this);
}

void EngineGL33::CreateVB()
{
    if (!_glContext)
        return;

    // Core profile requires a non-zero VAO bound for any
    // GL_ELEMENT_ARRAY_BUFFER bind (IBO binding is part of VAO state).
    // Gen everything up front, then bind each VAO and configure the
    // shared VBO/IBO inside it.
    glGenBuffers(1, &_vbo);
    glGenBuffers(1, &_ibo);
    glGenVertexArrays(1, &_vaoScreen);
    glGenVertexArrays(1, &_vaoMesh);

    size_t stride = sizeof(TLVertex);

    // --- VAO for screen-space rendering (vsScreen) ---
    // TLVertex layout: pos(vec3), rhw(float), color(BGRA), specular(BGRA), uv0(vec2), uv1(vec2)
    GL33Bind::Vao(_vaoScreen);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, MeshBufferLength * sizeof(TLVertex), nullptr, GL_DYNAMIC_DRAW);
    Poseidon::render::ibo::BindOnActiveVao(_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, IndexBufferLength * sizeof(WORD), nullptr, GL_DYNAMIC_DRAW);

    Poseidon::render::vao::SetupTLVertexLayout();

    // --- VAO for 3D mesh rendering (vsTransform) ---
    // Reads TLVertex data but interprets as: SVertex (pos, norm, uv).
    // Normal reads (rhw + color + specular) as 3 floats — same junk as D3D11.
    GL33Bind::Vao(_vaoMesh);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    Poseidon::render::ibo::BindOnActiveVao(_ibo);

    Poseidon::render::vao::SetupSVertexLayout();

    GL33Bind::Vao(0);

    _queueNo._vertexBufferUsed = 0;
    _queueNo._indexBufferUsed = 0;
}

void EngineGL33::DestroyVB()
{
    if (_ibo)
    {
        glDeleteBuffers(1, &_ibo);
        _ibo = 0;
    }
    if (_vbo)
    {
        glDeleteBuffers(1, &_vbo);
        _vbo = 0;
    }
    if (_vaoScreen)
    {
        GL33Bind::OnVaoDeleted(_vaoScreen);
        glDeleteVertexArrays(1, &_vaoScreen);
        _vaoScreen = 0;
    }
    if (_vaoMesh)
    {
        GL33Bind::OnVaoDeleted(_vaoMesh);
        glDeleteVertexArrays(1, &_vaoMesh);
        _vaoMesh = 0;
    }
    _lastQueueSource = nullptr;
}

void EngineGL33::CreateVBTL() {}
void EngineGL33::DestroyVBTL() {}

void EngineGL33::RestoreVB() {}
