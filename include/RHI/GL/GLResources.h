#pragma once

#include "RHI/RHI.h"
#include "RHI/GL/GLHeaders.h"
#include <vector>
#include <string>

namespace Kiwi
{

    // ============================================================
    // GL Buffer
    // ============================================================

    class GLBuffer : public RHIBuffer
    {
    public:
        GLBuffer(GLuint id, const BufferDesc& desc)
            : ID(id), Desc(desc) {}

        ~GLBuffer() override
        {
            if (ID) glDeleteBuffers(1, &ID);
        }

        void* GetNativeHandle() const override { return (void*)(uintptr_t)ID; }
        const BufferDesc& GetDesc() const override { return Desc; }

        void* Map(uint32_t subresource = 0) override
        {
            GLenum target = GetTarget();
            glBindBuffer(target, ID);
            return glMapBuffer(target, GL_WRITE_ONLY);
        }

        void Unmap(uint32_t subresource = 0) override
        {
            GLenum target = GetTarget();
            glBindBuffer(target, ID);
            glUnmapBuffer(target);
        }

        void UpdateData(const void* data, uint32_t size, uint32_t offset = 0) override
        {
            GLenum target = GetTarget();
            glBindBuffer(target, ID);
            glBufferSubData(target, offset, size, data);
        }

        GLuint GetID() const { return ID; }

        GLenum GetTarget() const
        {
            if (Desc.BindFlags & BUFFER_USAGE_VERTEX)   return GL_ARRAY_BUFFER;
            if (Desc.BindFlags & BUFFER_USAGE_INDEX)    return GL_ELEMENT_ARRAY_BUFFER;
            if (Desc.BindFlags & BUFFER_USAGE_CONSTANT) return GL_UNIFORM_BUFFER;
            return GL_ARRAY_BUFFER;
        }

    private:
        GLuint ID = 0;
        BufferDesc Desc;
    };

    // ============================================================
    // GL Texture
    // ============================================================

    class GLTexture : public RHITexture
    {
    public:
        GLTexture(GLuint id, const TextureDesc& desc)
            : ID(id), Desc(desc) {}

        ~GLTexture() override
        {
            if (ID) glDeleteTextures(1, &ID);
        }

        void* GetNativeHandle() const override { return (void*)(uintptr_t)ID; }
        const TextureDesc& GetDesc() const override { return Desc; }
        GLuint GetID() const { return ID; }

    private:
        GLuint ID = 0;
        TextureDesc Desc;
    };

    // ============================================================
    // GL Texture View (FBO attachment reference)
    // In OpenGL, "views" are really just texture IDs + attachment type.
    // We store both for SetRenderTargets / SetShaderResourceView.
    // ============================================================

    class GLTextureView : public RHITextureView
    {
    public:
        enum class Type { RTV, DSV, SRV };

        GLTextureView(GLuint textureID, Type type, GLenum internalFormat = GL_RGBA8)
            : TextureID(textureID), ViewType(type), InternalFormat(internalFormat) {}

        void* GetNativeHandle() const override { return (void*)(uintptr_t)TextureID; }
        GLuint GetTextureID() const { return TextureID; }
        Type GetViewType() const { return ViewType; }
        GLenum GetInternalFormat() const { return InternalFormat; }

    private:
        GLuint TextureID = 0;
        Type ViewType;
        GLenum InternalFormat;
    };

    // ============================================================
    // GL Shader (compiled program, not individual stage)
    // For the RHI interface, we store individual compiled shaders
    // and link them in CreateGraphicsPipelineState.
    // ============================================================

    class GLShader : public RHIShader
    {
    public:
        GLShader(EShaderType type, GLuint shaderID, const std::string& source)
            : Type(type), ShaderID(shaderID), Source(source) {}

        ~GLShader() override
        {
            if (ShaderID) glDeleteShader(ShaderID);
        }

        void* GetNativeHandle() const override { return (void*)(uintptr_t)ShaderID; }
        EShaderType GetType() const override { return Type; }
        GLuint GetShaderID() const { return ShaderID; }
        const std::string& GetSource() const { return Source; }

    private:
        EShaderType Type;
        GLuint ShaderID = 0;
        std::string Source;
    };

    // ============================================================
    // GL Input Layout (VAO description, stored as metadata)
    // The actual VAO is created per-draw or cached in pipeline state.
    // ============================================================

    class GLInputLayout : public RHIInputLayout
    {
    public:
        GLInputLayout(const std::vector<InputElementDesc>& elements)
            : Elements(elements) {}

        void* GetNativeHandle() const override { return nullptr; }
        const std::vector<InputElementDesc>& GetElements() const { return Elements; }

    private:
        std::vector<InputElementDesc> Elements;
    };

    // ============================================================
    // GL Pipeline State (linked shader program + state)
    // ============================================================

    class GLPipelineState : public RHIPipelineState
    {
    public:
        GLPipelineState() = default;

        ~GLPipelineState() override
        {
            if (Program) glDeleteProgram(Program);
        }

        void* GetNativeHandle() const override { return (void*)(uintptr_t)Program; }
        GLuint GetProgram() const { return Program; }
        void SetProgram(GLuint prog) { Program = prog; }

        bool DepthEnabled = true;
        bool DepthWrite = true;
        RasterizerStateDesc Rasterizer;

    private:
        GLuint Program = 0;
    };

    // ============================================================
    // GL Sampler
    // ============================================================

    class GLSampler : public RHISampler
    {
    public:
        GLSampler(GLuint id) : ID(id) {}

        ~GLSampler() override
        {
            if (ID) glDeleteSamplers(1, &ID);
        }

        void* GetNativeHandle() const override { return (void*)(uintptr_t)ID; }
        GLuint GetID() const { return ID; }

    private:
        GLuint ID = 0;
    };

} // namespace Kiwi
