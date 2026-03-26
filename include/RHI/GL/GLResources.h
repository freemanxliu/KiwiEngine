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
            : m_ID(id), m_Desc(desc) {}

        ~GLBuffer() override
        {
            if (m_ID) glDeleteBuffers(1, &m_ID);
        }

        void* GetNativeHandle() const override { return (void*)(uintptr_t)m_ID; }
        const BufferDesc& GetDesc() const override { return m_Desc; }

        void* Map(uint32_t subresource = 0) override
        {
            GLenum target = GetTarget();
            glBindBuffer(target, m_ID);
            return glMapBuffer(target, GL_WRITE_ONLY);
        }

        void Unmap(uint32_t subresource = 0) override
        {
            GLenum target = GetTarget();
            glBindBuffer(target, m_ID);
            glUnmapBuffer(target);
        }

        void UpdateData(const void* data, uint32_t size, uint32_t offset = 0) override
        {
            GLenum target = GetTarget();
            glBindBuffer(target, m_ID);
            glBufferSubData(target, offset, size, data);
        }

        GLuint GetID() const { return m_ID; }

        GLenum GetTarget() const
        {
            if (m_Desc.BindFlags & BUFFER_USAGE_VERTEX)   return GL_ARRAY_BUFFER;
            if (m_Desc.BindFlags & BUFFER_USAGE_INDEX)    return GL_ELEMENT_ARRAY_BUFFER;
            if (m_Desc.BindFlags & BUFFER_USAGE_CONSTANT) return GL_UNIFORM_BUFFER;
            return GL_ARRAY_BUFFER;
        }

    private:
        GLuint m_ID = 0;
        BufferDesc m_Desc;
    };

    // ============================================================
    // GL Texture
    // ============================================================

    class GLTexture : public RHITexture
    {
    public:
        GLTexture(GLuint id, const TextureDesc& desc)
            : m_ID(id), m_Desc(desc) {}

        ~GLTexture() override
        {
            if (m_ID) glDeleteTextures(1, &m_ID);
        }

        void* GetNativeHandle() const override { return (void*)(uintptr_t)m_ID; }
        const TextureDesc& GetDesc() const override { return m_Desc; }
        GLuint GetID() const { return m_ID; }

    private:
        GLuint m_ID = 0;
        TextureDesc m_Desc;
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
            : m_TextureID(textureID), m_Type(type), m_InternalFormat(internalFormat) {}

        void* GetNativeHandle() const override { return (void*)(uintptr_t)m_TextureID; }
        GLuint GetTextureID() const { return m_TextureID; }
        Type GetViewType() const { return m_Type; }
        GLenum GetInternalFormat() const { return m_InternalFormat; }

    private:
        GLuint m_TextureID = 0;
        Type m_Type;
        GLenum m_InternalFormat;
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
            : m_Type(type), m_ShaderID(shaderID), m_Source(source) {}

        ~GLShader() override
        {
            if (m_ShaderID) glDeleteShader(m_ShaderID);
        }

        void* GetNativeHandle() const override { return (void*)(uintptr_t)m_ShaderID; }
        EShaderType GetType() const override { return m_Type; }
        GLuint GetShaderID() const { return m_ShaderID; }
        const std::string& GetSource() const { return m_Source; }

    private:
        EShaderType m_Type;
        GLuint m_ShaderID = 0;
        std::string m_Source;
    };

    // ============================================================
    // GL Input Layout (VAO description, stored as metadata)
    // The actual VAO is created per-draw or cached in pipeline state.
    // ============================================================

    class GLInputLayout : public RHIInputLayout
    {
    public:
        GLInputLayout(const std::vector<InputElementDesc>& elements)
            : m_Elements(elements) {}

        void* GetNativeHandle() const override { return nullptr; }
        const std::vector<InputElementDesc>& GetElements() const { return m_Elements; }

    private:
        std::vector<InputElementDesc> m_Elements;
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
            if (m_Program) glDeleteProgram(m_Program);
        }

        void* GetNativeHandle() const override { return (void*)(uintptr_t)m_Program; }
        GLuint GetProgram() const { return m_Program; }
        void SetProgram(GLuint prog) { m_Program = prog; }

        bool DepthEnabled = true;
        bool DepthWrite = true;
        bool CullEnabled = true;

    private:
        GLuint m_Program = 0;
    };

    // ============================================================
    // GL Sampler
    // ============================================================

    class GLSampler : public RHISampler
    {
    public:
        GLSampler(GLuint id) : m_ID(id) {}

        ~GLSampler() override
        {
            if (m_ID) glDeleteSamplers(1, &m_ID);
        }

        void* GetNativeHandle() const override { return (void*)(uintptr_t)m_ID; }
        GLuint GetID() const { return m_ID; }

    private:
        GLuint m_ID = 0;
    };

} // namespace Kiwi
