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

        void* GetNativeHandle() const override { return (void*)Hdc; }
        void Present(uint32_t syncInterval = 0) override;
        void ResizeBuffers(uint32_t width, uint32_t height) override;

        uint32_t GetCurrentBackBufferIndex() const override { return 0; }
        RHITexture* GetBackBuffer(uint32_t index) override { return &BackBuffer; }
        RHITextureView* GetBackBufferRTV(uint32_t index) override { return &BackBufferRTV; }

        uint32_t GetWidth() const { return Desc.Width; }
        uint32_t GetHeight() const { return Desc.Height; }

    private:
        HWND Hwnd = nullptr;
        HDC  Hdc  = nullptr;
        SwapChainDesc Desc;

        // Pseudo back-buffer (GL default framebuffer = 0)
        GLTexture     BackBuffer;
        GLTextureView BackBufferRTV;
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
        void* GetNativeDevice() const override { return (void*)Hglrc; }
        void* GetImmediateContext() const override { return (void*)Hglrc; }

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
            const GraphicsPipelineStateInitializer& initializer) override;
        std::unique_ptr<RHISampler> CreateSampler() override;
        std::unique_ptr<RHISampler> CreateComparisonSampler() override;

        bool IsFeatureSupported(const char* feature) const override { return true; }

        void InitImGui(void* windowHandle) override;
        void ShutdownImGui() override;
        void ImGuiNewFrame() override;
        void ImGuiUpdateTextures(ImDrawData* DrawData) override;
        void ImGuiRenderDrawData(RHICommandContext* Ctx, ImDrawData* DrawData) override;

    private:
        HWND  Hwnd  = nullptr;
        HDC   Hdc   = nullptr;
        HGLRC Hglrc = nullptr;
        bool  EnableDebug;
        bool  ImGuiInitialized = false;

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
        void SetCullMode(ECullMode mode) override;
        void ClearCullModeOverride() override;

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
        bool   GLResourcesReady = false;
        GLuint FBO = 0;       // Current framebuffer object (0 = default/backbuffer)
        GLuint VAO = 0;       // Current vertex array object
        GLenum Topology = GL_TRIANGLES;

        // Cached state for draw calls
        GLShader*      CurrentVS = nullptr;
        GLShader*      CurrentPS = nullptr;
        GLInputLayout* CurrentLayout = nullptr;
        GLPipelineState* CurrentPSO = nullptr;
        bool CullOverride = false;
        ECullMode CullMode = ECullMode::Back;

        // Index buffer state
        GLenum IndexFormat = GL_UNSIGNED_INT;
        uint32_t IndexBufferOffset = 0;
    };

    // Factory function (called from CreateRHI)
    void CreateGLRHI(const RHIInitParams& params,
        std::unique_ptr<RHIDevice>& outDevice,
        std::unique_ptr<RHICommandContext>& outContext);

} // namespace Kiwi
