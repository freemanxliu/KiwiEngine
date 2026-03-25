#pragma once

#include "RHI/RHI.h"
#include "RHI/DX11/DX11Headers.h"
#include "RHI/DX11/DX11Utils.h"
#include "RHI/DX11/DX11Resources.h"
#include <vector>

namespace Kiwi
{

    // ============================================================
    // DX11 SwapChain
    // ============================================================

    class DX11SwapChain : public RHISwapChain
    {
    public:
        DX11SwapChain(IDXGISwapChain* swapChain,
                      ID3D11Device* device,
                      ID3D11DeviceContext* context,
                      const SwapChainDesc& desc);

        ~DX11SwapChain() override;

        void* GetNativeHandle() const override { return m_SwapChain.Get(); }

        void Present(uint32_t syncInterval = 0) override;
        void ResizeBuffers(uint32_t width, uint32_t height) override;

        uint32_t GetCurrentBackBufferIndex() const override;
        RHITexture* GetBackBuffer(uint32_t index) override;
        RHITextureView* GetBackBufferRTV(uint32_t index) override;

    private:
        void CreateRenderTargetViews();

        ComPtr<IDXGISwapChain>         m_SwapChain;
        ComPtr<ID3D11Device>           m_Device;
        ComPtr<ID3D11DeviceContext>    m_Context;

        SwapChainDesc                  m_Desc;
        std::vector<std::unique_ptr<DX11Texture>>    m_BackBuffers;
        std::vector<std::unique_ptr<DX11TextureView>> m_RTVs;
    };

    // ============================================================
    // DX11 Device
    // ============================================================

    class DX11Device : public RHIDevice
    {
    public:
        DX11Device(bool enableDebug);
        ~DX11Device() override;

        RHI_API_TYPE GetApiType() const override { return RHI_API_TYPE::DX11; }
        void* GetNativeDevice() const override { return m_Device.Get(); }
        void* GetImmediateContext() const override { return m_Context.Get(); }

        // SwapChain
        std::unique_ptr<RHISwapChain> CreateSwapChain(const SwapChainDesc& desc) override;

        // Buffer
        std::unique_ptr<RHIBuffer> CreateBuffer(
            const BufferDesc& desc,
            const void* initialData = nullptr) override;

        // Texture
        std::unique_ptr<RHITexture> CreateTexture(
            const TextureDesc& desc,
            const void* initialData = nullptr) override;

        // Texture View
        std::unique_ptr<RHITextureView> CreateTextureView(
            RHITexture* texture,
            EDescriptorHeapType heapType,
            EFormat format = EFormat::Unknown,
            int mipSlice = -1,
            int arraySlice = -1) override;

        // Shader
        std::unique_ptr<RHIShader> CreateShader(
            EShaderType type,
            const void* byteCode,
            size_t byteCodeSize) override;

        // 从 HLSL 源码编译着色器
        std::unique_ptr<RHIShader> CompileShader(
            EShaderType type,
            const char* hlslSource,
            const char* entryPoint,
            const char* shaderModel,
            const ShaderMacro* macros = nullptr,
            uint32_t macroCount = 0) override;

        // Input Layout
        std::unique_ptr<RHIInputLayout> CreateInputLayout(
            const InputElementDesc* elements,
            uint32_t elementCount,
            RHIShader* vertexShader) override;

        // Pipeline State (empty — DX11 uses separate Set* calls)
        std::unique_ptr<RHIPipelineState> CreatePipelineState() override;

        // Graphics Pipeline State (DX11: stores VS+PS references for binding)
        std::unique_ptr<RHIPipelineState> CreateGraphicsPipelineState(
            RHIShader* vertexShader,
            RHIShader* pixelShader,
            RHIInputLayout* inputLayout) override;

        // MRT variant (DX11: same as above, RTV formats handled by SetRenderTargets)
        std::unique_ptr<RHIPipelineState> CreateGraphicsPipelineState(
            RHIShader* vertexShader,
            RHIShader* pixelShader,
            RHIInputLayout* inputLayout,
            const PipelineStateDesc& pipelineDesc) override;

        // Sampler
        std::unique_ptr<RHISampler> CreateSampler() override;

        bool IsFeatureSupported(const char* feature) const override { return true; }

        // ---- ImGui 集成 ----
        void InitImGui(void* windowHandle) override;
        void ShutdownImGui() override;
        void ImGuiNewFrame() override;
        void ImGuiRenderDrawData(RHICommandContext* ctx) override;

        // 获取原生指针
        ID3D11Device* GetD3DDevice() const { return m_Device.Get(); }
        ID3D11DeviceContext* GetD3DContext() const { return m_Context.Get(); }

    private:
        ComPtr<ID3D11Device>           m_Device;
        ComPtr<ID3D11DeviceContext>    m_Context;
        ComPtr<IDXGIDevice>            m_DXGIDevice;
        ComPtr<IDXGIAdapter>           m_Adapter;
        ComPtr<ID3D11Debug>            m_Debug;
        bool                           m_EnableDebug;
        bool                           m_ImGuiInitialized = false;
    };

    // ============================================================
    // DX11 Command Context
    // ============================================================

    class DX11CommandContext : public RHICommandContext
    {
    public:
        DX11CommandContext(ID3D11DeviceContext* context);
        ~DX11CommandContext() override;

        void* GetNativeHandle() const override { return m_Context.Get(); }

        // GPU debug annotations (RenderDoc / PIX)
        void BeginEvent(const char* name) override;
        void EndEvent() override;
        void SetMarker(const char* name) override;

        // Render Targets
        void SetRenderTargets(
            RHITextureView** rtvs, uint32_t rtvCount,
            RHITextureView* dsv = nullptr) override;

        void ClearRenderTargetView(RHITextureView* rtv, const ClearColorValue& color) override;
        void ClearDepthStencilView(
            RHITextureView* dsv,
            const ClearDepthStencilValue& value,
            uint8_t clearFlags) override;

        // Pipeline State
        void SetPipelineState(RHIPipelineState* pso) override;

        // Graphics Pipeline
        void SetPrimitiveTopology(EPrimitiveTopology topology) override;

        void SetVertexBuffers(
            uint32_t startSlot,
            RHIBuffer* const* buffers,
            const VertexBufferView* views,
            uint32_t count) override;

        void SetIndexBuffer(RHIBuffer* buffer, const IndexBufferView* view) override;

        void SetVertexShader(RHIShader* shader) override;
        void SetPixelShader(RHIShader* shader) override;
        void SetGeometryShader(RHIShader* shader) override;

        void SetInputLayout(RHIInputLayout* layout) override;

        // Constant Buffer
        void SetConstantBuffer(uint32_t slot, RHIBuffer* buffer) override;

        // Shader Resource View (SRV)
        void SetShaderResourceView(uint32_t slot, RHITextureView* srv) override;

        // Sampler
        void SetSampler(uint32_t slot, RHISampler* sampler) override;

        // Viewport and Scissor
        void SetViewports(const Viewport* viewports, uint32_t count) override;
        void SetScissorRects(const ScissorRect* rects, uint32_t count) override;

        // Draw
        void Draw(uint32_t vertexCount, uint32_t vertexStart = 0) override;
        void DrawIndexed(uint32_t indexCount, uint32_t indexStart = 0, int32_t vertexOffset = 0) override;

        // Flush
        void Flush() override;

    private:
        ComPtr<ID3D11DeviceContext> m_Context;
        ComPtr<ID3DUserDefinedAnnotation> m_Annotation;
    };

} // namespace Kiwi
