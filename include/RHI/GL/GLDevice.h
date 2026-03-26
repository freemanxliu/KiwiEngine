#pragma once

#include "RHI/RHI.h"
#include "RHI/GL/GLHeaders.h"
#include "RHI/GL/GLResources.h"
#include <vector>

namespace Kiwi
{

    // ============================================================
    // GL Format Helpers
    // ============================================================

    GLenum GLFormatToInternalFormat(EFormat format);
    GLenum GLFormatToBaseFormat(EFormat format);
    GLenum GLFormatToType(EFormat format);
    GLenum GLTopology(EPrimitiveTopology topology);

    // ============================================================
    // GL SwapChain (WGL-based, double-buffered)
    // ============================================================

    class GLSwapChain : public RHISwapChain
    {
    public:
        GLSwapChain(HWND hwnd, HDC hdc, const SwapChainDesc& desc);
        ~GLSwapChain() override;

        void* GetNativeHandle() const override { return (void*)m_HDC; }
        void Present(uint32_t syncInterval = 0) override;
        void ResizeBuffers(uint32_t width, uint32_t height) override;

        uint32_t GetCurrentBackBufferIndex() const override { return 0; }
        RHITexture* GetBackBuffer(uint32_t index) override { return &m_BackBuffer; }
        RHITextureView* GetBackBufferRTV(uint32_t index) override { return &m_BackBufferRTV; }

        uint32_t GetWidth() const { return m_Desc.Width; }
        uint32_t GetHeight() const { return m_Desc.Height; }

    private:
        HWND m_HWND = nullptr;
        HDC  m_HDC  = nullptr;
        SwapChainDesc m_Desc;

        // Pseudo back-buffer (GL default framebuffer = 0)
        GLTexture     m_BackBuffer;
        GLTextureView m_BackBufferRTV;
    };

    // ============================================================
    // GL Device
    // ============================================================

    class GLDevice : public RHIDevice
    {
    public:
        GLDevice(bool enableDebug);
        ~GLDevice() override;

        RHI_API_TYPE GetApiType() const override { return RHI_API_TYPE::OPENGL; }
        void* GetNativeDevice() const override { return (void*)m_HGLRC; }
        void* GetImmediateContext() const override { return (void*)m_HGLRC; }

        std::unique_ptr<RHISwapChain> CreateSwapChain(const SwapChainDesc& desc) override;
        std::unique_ptr<RHIBuffer> CreateBuffer(const BufferDesc& desc, const void* initialData = nullptr) override;
        std::unique_ptr<RHITexture> CreateTexture(const TextureDesc& desc, const void* initialData = nullptr) override;
        std::unique_ptr<RHITextureView> CreateTextureView(RHITexture* texture, EDescriptorHeapType heapType,
            EFormat format = EFormat::Unknown, int mipSlice = -1, int arraySlice = -1) override;
        std::unique_ptr<RHIShader> CreateShader(EShaderType type, const void* byteCode, size_t byteCodeSize) override;
        std::unique_ptr<RHIShader> CompileShader(EShaderType type, const char* hlslSource,
            const char* entryPoint, const char* shaderModel,
            const ShaderMacro* macros = nullptr, uint32_t macroCount = 0) override;
        std::unique_ptr<RHIInputLayout> CreateInputLayout(const InputElementDesc* elements, uint32_t elementCount,
            RHIShader* vertexShader) override;
        std::unique_ptr<RHIPipelineState> CreatePipelineState() override;
        std::unique_ptr<RHIPipelineState> CreateGraphicsPipelineState(
            RHIShader* vertexShader, RHIShader* pixelShader, RHIInputLayout* inputLayout) override;
        std::unique_ptr<RHIPipelineState> CreateGraphicsPipelineState(
            RHIShader* vertexShader, RHIShader* pixelShader, RHIInputLayout* inputLayout,
            const PipelineStateDesc& pipelineDesc) override;
        std::unique_ptr<RHISampler> CreateSampler() override;
        std::unique_ptr<RHISampler> CreateComparisonSampler() override;

        bool IsFeatureSupported(const char* feature) const override { return true; }

        void InitImGui(void* windowHandle) override;
        void ShutdownImGui() override;
        void ImGuiNewFrame() override;
        void ImGuiRenderDrawData(RHICommandContext* ctx) override;

    private:
        HWND  m_HWND  = nullptr;
        HDC   m_HDC   = nullptr;
        HGLRC m_HGLRC = nullptr;
        bool  m_EnableDebug;
        bool  m_ImGuiInitialized = false;

        void CreateWGLContext(HWND hwnd);
    };

    // ============================================================
    // GL Command Context
    // ============================================================

    class GLCommandContext : public RHICommandContext
    {
    public:
        GLCommandContext();
        ~GLCommandContext() override;

        void* GetNativeHandle() const override { return nullptr; }

        // Frame lifecycle (GL: mostly no-ops, no explicit command lists)
        void BeginFrame(RHISwapChain* swapChain) override {}
        void EndFrame(RHISwapChain* swapChain) override {}

        // Render Targets (FBO management)
        void SetRenderTargets(RHITextureView** rtvs, uint32_t rtvCount, RHITextureView* dsv = nullptr) override;
        void ClearRenderTargetView(RHITextureView* rtv, const ClearColorValue& color) override;
        void ClearDepthStencilView(RHITextureView* dsv, const ClearDepthStencilValue& value, uint8_t clearFlags) override;

        void SetPipelineState(RHIPipelineState* pso) override;

        void SetPrimitiveTopology(EPrimitiveTopology topology) override;
        void SetVertexBuffers(uint32_t startSlot, RHIBuffer* const* buffers, const VertexBufferView* views, uint32_t count) override;
        void SetIndexBuffer(RHIBuffer* buffer, const IndexBufferView* view) override;
        void SetVertexShader(RHIShader* shader) override;
        void SetPixelShader(RHIShader* shader) override;
        void SetGeometryShader(RHIShader* shader) override;
        void SetInputLayout(RHIInputLayout* layout) override;

        void SetConstantBuffer(uint32_t slot, RHIBuffer* buffer) override;
        void SetShaderResourceView(uint32_t slot, RHITextureView* srv) override;
        void SetSampler(uint32_t slot, RHISampler* sampler) override;

        void SetViewports(const Viewport* viewports, uint32_t count) override;
        void SetScissorRects(const ScissorRect* rects, uint32_t count) override;

        void Draw(uint32_t vertexCount, uint32_t vertexStart = 0) override;
        void DrawIndexed(uint32_t indexCount, uint32_t indexStart = 0, int32_t vertexOffset = 0) override;

        void Flush() override;

        void EnsureGLResources();

    private:
        bool   m_GLResourcesReady = false;
        GLuint m_FBO = 0;       // Current framebuffer object (0 = default/backbuffer)
        GLuint m_VAO = 0;       // Current vertex array object
        GLenum m_Topology = GL_TRIANGLES;

        // Cached state for draw calls
        GLShader*      m_CurrentVS = nullptr;
        GLShader*      m_CurrentPS = nullptr;
        GLInputLayout* m_CurrentLayout = nullptr;
        GLPipelineState* m_CurrentPSO = nullptr;

        // Index buffer state
        GLenum m_IndexFormat = GL_UNSIGNED_INT;
        uint32_t m_IndexBufferOffset = 0;
    };

    // Factory function (called from CreateRHI)
    void CreateGLRHI(const RHIInitParams& params,
        std::unique_ptr<RHIDevice>& outDevice,
        std::unique_ptr<RHICommandContext>& outContext);

} // namespace Kiwi
