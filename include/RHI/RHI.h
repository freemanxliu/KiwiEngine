#pragma once

#include "RHITypes.h"
#include <memory>

namespace Kiwi
{

    // Forward declarations
    class RHICommandContext;

    // ============================================================
    // RHI 资源接口 - 所有图形资源（缓冲、纹理等）的基类
    // ============================================================

    class RHIBuffer
    {
    public:
        virtual ~RHIBuffer() = default;

        virtual void* GetNativeHandle() const = 0;
        virtual const BufferDesc& GetDesc() const = 0;

        // Dynamic buffer: Map/Unmap
        virtual void* Map(uint32_t subresource = 0) = 0;
        virtual void  Unmap(uint32_t subresource = 0) = 0;
        virtual void  UpdateData(const void* data, uint32_t size, uint32_t offset = 0) = 0;
    };

    class RHITexture
    {
    public:
        virtual ~RHITexture() = default;

        virtual void* GetNativeHandle() const = 0;
        virtual const TextureDesc& GetDesc() const = 0;
    };

    // 渲染目标视图
    class RHITextureView
    {
    public:
        virtual ~RHITextureView() = default;
        virtual void* GetNativeHandle() const = 0;
    };

    // 着色器
    class RHIShader
    {
    public:
        virtual ~RHIShader() = default;
        virtual void* GetNativeHandle() const = 0;
        virtual EShaderType GetType() const = 0;
    };

    // 输入布局
    class RHIInputLayout
    {
    public:
        virtual ~RHIInputLayout() = default;
        virtual void* GetNativeHandle() const = 0;
    };

    // 管线状态
    class RHIPipelineState
    {
    public:
        virtual ~RHIPipelineState() = default;
        virtual void* GetNativeHandle() const = 0;
    };

    // 采样器
    class RHISampler
    {
    public:
        virtual ~RHISampler() = default;
        virtual void* GetNativeHandle() const = 0;
    };

    // ============================================================
    // SwapChain
    // ============================================================

    struct SwapChainDesc
    {
        void*      WindowHandle  = nullptr;   // HWND
        uint32_t   Width         = 1280;
        uint32_t   Height        = 720;
        uint32_t   BufferCount   = 2;
        EFormat    Format        = EFormat::R8G8B8A8_UNORM;
        bool       Windowed      = true;
    };

    class RHISwapChain
    {
    public:
        virtual ~RHISwapChain() = default;

        virtual void* GetNativeHandle() const = 0;
        virtual void  Present(uint32_t syncInterval = 0) = 0;
        virtual void  ResizeBuffers(uint32_t width, uint32_t height) = 0;

        virtual uint32_t GetCurrentBackBufferIndex() const = 0;
        virtual RHITexture* GetBackBuffer(uint32_t index) = 0;
        virtual RHITextureView* GetBackBufferRTV(uint32_t index) = 0;
    };

    // ============================================================
    // RHI Device - 创建所有图形资源
    // ============================================================

    // 深度缓冲绑定标志提示 (让后端自行处理)
    constexpr uint32_t TEXTURE_HINT_DEPTH_STENCIL = 0x8000;

    class RHIDevice
    {
    public:
        virtual ~RHIDevice() = default;

        virtual RHI_API_TYPE GetApiType() const = 0;
        virtual void* GetNativeDevice() const = 0;

        // 创建 SwapChain
        virtual std::unique_ptr<RHISwapChain> CreateSwapChain(const SwapChainDesc& desc) = 0;

        // 创建缓冲
        virtual std::unique_ptr<RHIBuffer> CreateBuffer(
            const BufferDesc& desc,
            const void* initialData = nullptr) = 0;

        // 创建纹理
        virtual std::unique_ptr<RHITexture> CreateTexture(
            const TextureDesc& desc,
            const void* initialData = nullptr) = 0;

        // 创建纹理视图（RTV/DSV/SRV/UAV）
        virtual std::unique_ptr<RHITextureView> CreateTextureView(
            RHITexture* texture,
            EDescriptorHeapType heapType,
            EFormat format = EFormat::Unknown,
            int mipSlice = -1,
            int arraySlice = -1) = 0;

        // 创建着色器（从字节码）
        virtual std::unique_ptr<RHIShader> CreateShader(
            EShaderType type,
            const void* byteCode,
            size_t byteCodeSize) = 0;

        // 从 HLSL 源码编译着色器（统一接口，后端自行处理编译细节）
        virtual std::unique_ptr<RHIShader> CompileShader(
            EShaderType type,
            const char* hlslSource,
            const char* entryPoint,
            const char* shaderModel,
            const ShaderMacro* macros = nullptr,
            uint32_t macroCount = 0) = 0;

        // 创建输入布局
        virtual std::unique_ptr<RHIInputLayout> CreateInputLayout(
            const InputElementDesc* elements,
            uint32_t elementCount,
            RHIShader* vertexShader) = 0;

        // 创建空管线状态（DX11 用）
        virtual std::unique_ptr<RHIPipelineState> CreatePipelineState() = 0;

        // 创建图形管线状态（带 VS + PS，DX12 创建完整 PSO，DX11 返回轻量包装）
        virtual std::unique_ptr<RHIPipelineState> CreateGraphicsPipelineState(
            RHIShader* vertexShader,
            RHIShader* pixelShader,
            RHIInputLayout* inputLayout) = 0;

        // 创建采样器
        virtual std::unique_ptr<RHISampler> CreateSampler() = 0;

        // 查询特性
        virtual bool IsFeatureSupported(const char* feature) const = 0;

        // 获取当前创建的命令上下文（for immediate mode APIs like DX11）
        virtual void* GetImmediateContext() const = 0;

        // ---- ImGui 集成（后端自行处理初始化/关闭/渲染）----
        virtual void InitImGui(void* windowHandle) = 0;
        virtual void ShutdownImGui() = 0;
        virtual void ImGuiNewFrame() = 0;
        virtual void ImGuiRenderDrawData(RHICommandContext* ctx) = 0;
    };

    // ============================================================
    // RHI Command Context - 记录渲染命令
    // ============================================================

    class RHICommandContext
    {
    public:
        virtual ~RHICommandContext() = default;
        virtual void* GetNativeHandle() const = 0;

        // ---- 帧生命周期（DX12: Reset/RootSig/DescriptorHeaps/Barrier；DX11: 空操作）----
        virtual void BeginFrame(RHISwapChain* swapChain) {}
        virtual void EndFrame(RHISwapChain* swapChain) {}

        // 资源屏障（DX12/Vulkan 用，DX11 空实现）
        virtual void ResourceBarrier(RHITexture* texture, int stateBefore, int stateAfter) {}

        // ---- 渲染目标 ----
        virtual void SetRenderTargets(
            RHITextureView** rtvs, uint32_t rtvCount,
            RHITextureView* dsv = nullptr) = 0;

        virtual void ClearRenderTargetView(RHITextureView* rtv, const ClearColorValue& color) = 0;
        virtual void ClearDepthStencilView(
            RHITextureView* dsv,
            const ClearDepthStencilValue& value,
            uint8_t clearFlags) = 0;

        // ---- 管线状态 ----
        virtual void SetPipelineState(RHIPipelineState* pso) = 0;

        // ---- 图形管线 ----
        virtual void SetPrimitiveTopology(EPrimitiveTopology topology) = 0;

        virtual void SetVertexBuffers(
            uint32_t startSlot,
            RHIBuffer* const* buffers,
            const VertexBufferView* views,
            uint32_t count) = 0;

        virtual void SetIndexBuffer(
            RHIBuffer* buffer,
            const IndexBufferView* view) = 0;

        virtual void SetVertexShader(RHIShader* shader) = 0;
        virtual void SetPixelShader(RHIShader* shader) = 0;
        virtual void SetGeometryShader(RHIShader* shader) = 0;

        virtual void SetInputLayout(RHIInputLayout* layout) = 0;

        // ---- 常量缓冲 ----
        virtual void SetConstantBuffer(uint32_t slot, RHIBuffer* buffer) = 0;

        // ---- 采样器 ----
        virtual void SetSampler(uint32_t slot, RHISampler* sampler) = 0;

        // ---- 视口和裁剪 ----
        virtual void SetViewports(const Viewport* viewports, uint32_t count) = 0;
        virtual void SetScissorRects(const ScissorRect* rects, uint32_t count) = 0;

        // ---- 绘制 ----
        virtual void Draw(uint32_t vertexCount, uint32_t vertexStart = 0) = 0;
        virtual void DrawIndexed(uint32_t indexCount, uint32_t indexStart = 0, int32_t vertexOffset = 0) = 0;

        // ---- 提交 ----
        virtual void Flush() = 0;
    };

    // ============================================================
    // RHI Factory - 工厂模式创建指定后端
    // ============================================================

    struct RHIInitParams
    {
        RHI_API_TYPE ApiType      = RHI_API_TYPE::DX11;
        bool        EnableDebug   = true;
        uint32_t    AdapterIndex  = 0;
    };

    // 创建 RHI Device 和主 Command Context
    // 返回 device 和 mainContext（通过输出参数）
    void CreateRHI(
        const RHIInitParams& params,
        std::unique_ptr<RHIDevice>& outDevice,
        std::unique_ptr<RHICommandContext>& outContext);

} // namespace Kiwi
