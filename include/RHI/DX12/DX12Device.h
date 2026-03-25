#pragma once

#include "RHI/RHI.h"
#include "RHI/DX12/DX12Headers.h"
#include <vector>
#include <queue>

namespace Kiwi
{

    // ============================================================
    // DX12 辅助：格式转换
    // ============================================================

    inline DXGI_FORMAT DX12ToDXGIFormat(EFormat format)
    {
        switch (format)
        {
        case EFormat::R8G8B8A8_UNORM:     return DXGI_FORMAT_R8G8B8A8_UNORM;
        case EFormat::R16G16B16A16_FLOAT:  return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case EFormat::R16G16_FLOAT:        return DXGI_FORMAT_R16G16_FLOAT;
        case EFormat::R32G32B32A32_FLOAT:  return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case EFormat::R32G32B32_FLOAT:     return DXGI_FORMAT_R32G32B32_FLOAT;
        case EFormat::R32G32_FLOAT:        return DXGI_FORMAT_R32G32_FLOAT;
        case EFormat::R32_FLOAT:           return DXGI_FORMAT_R32_FLOAT;
        case EFormat::R32_UINT:            return DXGI_FORMAT_R32_UINT;
        case EFormat::R16_UINT:            return DXGI_FORMAT_R16_UINT;
        case EFormat::D24_UNORM_S8_UINT:   return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case EFormat::D32_FLOAT:           return DXGI_FORMAT_D32_FLOAT;
        case EFormat::R32_TYPELESS:        return DXGI_FORMAT_R32_TYPELESS;
        default:                           return DXGI_FORMAT_UNKNOWN;
        }
    }

    inline D3D12_PRIMITIVE_TOPOLOGY DX12ToDX12Topology(EPrimitiveTopology topology)
    {
        switch (topology)
        {
        case EPrimitiveTopology::TriangleList:  return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case EPrimitiveTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case EPrimitiveTopology::LineList:      return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case EPrimitiveTopology::LineStrip:     return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case EPrimitiveTopology::PointList:     return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        default:                                return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        }
    }

    // ============================================================
    // DX12 Resources
    // ============================================================

    class DX12Buffer : public RHIBuffer
    {
    public:
        DX12Buffer(ID3D12Resource* resource, const BufferDesc& desc,
                   void* mappedPtr = nullptr)
            : m_Resource(resource), m_Desc(desc), m_MappedPtr(mappedPtr) {}

        ~DX12Buffer() override
        {
            if (m_MappedPtr && m_Resource)
            {
                m_Resource->Unmap(0, nullptr);
                m_MappedPtr = nullptr;
            }
        }

        void* GetNativeHandle() const override { return m_Resource.Get(); }
        const BufferDesc& GetDesc() const override { return m_Desc; }

        void* Map(uint32_t subresource = 0) override
        {
            if (m_MappedPtr) return m_MappedPtr;
            D3D12_RANGE readRange = { 0, 0 };
            HRESULT hr = m_Resource->Map(0, &readRange, &m_MappedPtr);
            if (FAILED(hr)) return nullptr;
            return m_MappedPtr;
        }

        void Unmap(uint32_t subresource = 0) override
        {
            if (m_MappedPtr)
            {
                m_Resource->Unmap(0, nullptr);
                m_MappedPtr = nullptr;
            }
        }

        void UpdateData(const void* data, uint32_t size, uint32_t offset = 0) override
        {
            void* mapped = Map();
            if (mapped)
            {
                memcpy((uint8_t*)mapped + offset, data, size);
                Unmap();
            }
        }

        ID3D12Resource* GetD3DResource() const { return m_Resource.Get(); }
        D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const { return m_Resource->GetGPUVirtualAddress(); }

    private:
        ComPtr<ID3D12Resource> m_Resource;
        BufferDesc m_Desc;
        void* m_MappedPtr = nullptr;
    };

    class DX12Texture : public RHITexture
    {
    public:
        DX12Texture(ID3D12Resource* resource, const TextureDesc& desc)
            : m_Resource(resource), m_Desc(desc) {}

        void* GetNativeHandle() const override { return m_Resource.Get(); }
        const TextureDesc& GetDesc() const override { return m_Desc; }
        ID3D12Resource* GetD3DResource() const { return m_Resource.Get(); }

    private:
        ComPtr<ID3D12Resource> m_Resource;
        TextureDesc m_Desc;
    };

    class DX12TextureView : public RHITextureView
    {
    public:
        DX12TextureView(D3D12_CPU_DESCRIPTOR_HANDLE handle)
            : m_Handle(handle) {}

        void* GetNativeHandle() const override { return (void*)m_Handle.ptr; }
        D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle() const { return m_Handle; }

        // For SRV views: hold the CPU-only descriptor heap so it's not destroyed
        void SetSRVHeap(ComPtr<ID3D12DescriptorHeap> heap) { m_SRVHeap = std::move(heap); }

    private:
        D3D12_CPU_DESCRIPTOR_HANDLE m_Handle;
        ComPtr<ID3D12DescriptorHeap> m_SRVHeap; // Optional: owns the CPU descriptor heap for SRV
    };

    class DX12Shader : public RHIShader
    {
    public:
        DX12Shader(EShaderType type, ID3DBlob* blob)
            : m_Type(type), m_Blob(blob) {}

        void* GetNativeHandle() const override { return m_Blob->GetBufferPointer(); }
        EShaderType GetType() const override { return m_Type; }
        ID3DBlob* GetBlob() const { return m_Blob.Get(); }

    private:
        EShaderType m_Type;
        ComPtr<ID3DBlob> m_Blob;
    };

    class DX12InputLayout : public RHIInputLayout
    {
    public:
        DX12InputLayout(const std::vector<D3D12_INPUT_ELEMENT_DESC>& elements)
            : m_Elements(elements) {}

        void* GetNativeHandle() const override { return (void*)m_Elements.data(); }
        const std::vector<D3D12_INPUT_ELEMENT_DESC>& GetElements() const { return m_Elements; }

    private:
        std::vector<D3D12_INPUT_ELEMENT_DESC> m_Elements;
    };

    class DX12PipelineState : public RHIPipelineState
    {
    public:
        DX12PipelineState(ID3D12PipelineState* pso)
            : m_PSO(pso) {}

        void* GetNativeHandle() const override { return m_PSO.Get(); }
        ID3D12PipelineState* GetPSO() const { return m_PSO.Get(); }

    private:
        ComPtr<ID3D12PipelineState> m_PSO;
    };

    class DX12Sampler : public RHISampler
    {
    public:
        DX12Sampler(D3D12_CPU_DESCRIPTOR_HANDLE handle)
            : m_Handle(handle) {}

        void* GetNativeHandle() const override { return (void*)m_Handle.ptr; }

    private:
        D3D12_CPU_DESCRIPTOR_HANDLE m_Handle;
    };

    // ============================================================
    // DX12 SwapChain
    // ============================================================

    class DX12SwapChain : public RHISwapChain
    {
    public:
        DX12SwapChain(IDXGISwapChain3* swapChain,
                      ID3D12Device* device,
                      ID3D12CommandQueue* cmdQueue,
                      const SwapChainDesc& desc);
        ~DX12SwapChain() override;

        void* GetNativeHandle() const override { return m_SwapChain.Get(); }
        void Present(uint32_t syncInterval = 0) override;
        void ResizeBuffers(uint32_t width, uint32_t height) override;

        uint32_t GetCurrentBackBufferIndex() const override;
        RHITexture* GetBackBuffer(uint32_t index) override;
        RHITextureView* GetBackBufferRTV(uint32_t index) override;

        // DX12-specific
        IDXGISwapChain3* GetDXGISwapChain() const { return m_SwapChain.Get(); }

    private:
        void CreateRenderTargetViews();

        ComPtr<IDXGISwapChain3>    m_SwapChain;
        ComPtr<ID3D12Device>       m_Device;
        ComPtr<ID3D12CommandQueue>  m_CommandQueue;

        SwapChainDesc              m_Desc;
        ComPtr<ID3D12DescriptorHeap> m_RTVHeap;
        uint32_t                   m_RTVDescriptorSize = 0;

        std::vector<std::unique_ptr<DX12Texture>>    m_BackBuffers;
        std::vector<std::unique_ptr<DX12TextureView>> m_RTVs;
    };

    // ============================================================
    // DX12 Device
    // ============================================================

    class DX12Device : public RHIDevice
    {
    public:
        DX12Device(bool enableDebug);
        ~DX12Device() override;

        RHI_API_TYPE GetApiType() const override { return RHI_API_TYPE::DX12; }
        void* GetNativeDevice() const override { return m_Device.Get(); }
        void* GetImmediateContext() const override { return m_CommandQueue.Get(); }

        std::unique_ptr<RHISwapChain> CreateSwapChain(const SwapChainDesc& desc) override;
        std::unique_ptr<RHIBuffer> CreateBuffer(const BufferDesc& desc, const void* initialData = nullptr) override;
        std::unique_ptr<RHITexture> CreateTexture(const TextureDesc& desc, const void* initialData = nullptr) override;
        std::unique_ptr<RHITextureView> CreateTextureView(RHITexture* texture, EDescriptorHeapType heapType,
            EFormat format = EFormat::Unknown, int mipSlice = -1, int arraySlice = -1) override;
        std::unique_ptr<RHIShader> CreateShader(EShaderType type, const void* byteCode, size_t byteCodeSize) override;
        std::unique_ptr<RHIInputLayout> CreateInputLayout(const InputElementDesc* elements, uint32_t elementCount,
            RHIShader* vertexShader) override;
        std::unique_ptr<RHIPipelineState> CreatePipelineState() override;
        std::unique_ptr<RHISampler> CreateSampler() override;

        bool IsFeatureSupported(const char* feature) const override { return true; }

        // 从 HLSL 源码编译着色器
        std::unique_ptr<RHIShader> CompileShader(EShaderType type, const char* hlslSource,
            const char* entryPoint, const char* shaderModel,
            const ShaderMacro* macros = nullptr, uint32_t macroCount = 0) override;

        // 创建图形管线状态（完整 DX12 PSO）
        std::unique_ptr<RHIPipelineState> CreateGraphicsPipelineState(
            RHIShader* vertexShader,
            RHIShader* pixelShader,
            RHIInputLayout* inputLayout) override;

        // 创建图形管线状态（MRT — 指定渲染目标格式）
        std::unique_ptr<RHIPipelineState> CreateGraphicsPipelineState(
            RHIShader* vertexShader,
            RHIShader* pixelShader,
            RHIInputLayout* inputLayout,
            const PipelineStateDesc& pipelineDesc) override;

        // ---- ImGui 集成 ----
        void InitImGui(void* windowHandle) override;
        void ShutdownImGui() override;
        void ImGuiNewFrame() override;
        void ImGuiRenderDrawData(RHICommandContext* ctx) override;

        // DX12 native access
        ID3D12Device* GetD3DDevice() const { return m_Device.Get(); }
        ID3D12CommandQueue* GetCommandQueue() const { return m_CommandQueue.Get(); }

        // Fence & sync
        void WaitForGPU();
        uint64_t Signal();
        void WaitForFenceValue(uint64_t fenceValue);

        // Descriptor heap for CBV/SRV/UAV (used by ImGui)
        ID3D12DescriptorHeap* GetSRVHeap() const { return m_SRVHeap.Get(); }

    private:
        ComPtr<ID3D12Device>        m_Device;
        ComPtr<ID3D12CommandQueue>  m_CommandQueue;
        ComPtr<IDXGIFactory4>       m_DXGIFactory;
        ComPtr<ID3D12Fence>         m_Fence;
        uint64_t                    m_FenceValue = 0;
        HANDLE                      m_FenceEvent = nullptr;
        bool                        m_EnableDebug;

        // SRV heap for ImGui + post-process + G-Buffer (32 descriptors, index 0 = ImGui)
        ComPtr<ID3D12DescriptorHeap> m_SRVHeap;
        uint32_t m_SRVDescriptorSize = 0;
        uint32_t m_SRVAllocated = 1; // index 0 reserved for ImGui

        // RTV heap for offscreen render targets + G-Buffer (separate from SwapChain's RTV heap)
        ComPtr<ID3D12DescriptorHeap> m_OffscreenRTVHeap;
        uint32_t m_OffscreenRTVDescriptorSize = 0;
        uint32_t m_OffscreenRTVAllocated = 0;

        // DSV heap
        ComPtr<ID3D12DescriptorHeap> m_DSVHeap;
        uint32_t m_DSVDescriptorSize = 0;
        uint32_t m_DSVAllocated = 0;

        // Root signature (shared for all PSOs in this simple engine)
        ComPtr<ID3D12RootSignature> m_RootSignature;

        void CreateRootSignature();
        bool m_ImGuiInitialized = false;

    public:
        ID3D12RootSignature* GetRootSignature() const { return m_RootSignature.Get(); }
    };

    // ============================================================
    // DX12 Command Context
    // ============================================================

    class DX12CommandContext : public RHICommandContext
    {
    public:
        DX12CommandContext(ID3D12Device* device, ID3D12CommandQueue* cmdQueue,
                           ID3D12RootSignature* rootSignature, ID3D12DescriptorHeap* srvHeap);
        ~DX12CommandContext() override;

        void* GetNativeHandle() const override { return m_CommandList.Get(); }

        // Frame lifecycle
        void BeginFrame(RHISwapChain* swapChain) override;
        void EndFrame(RHISwapChain* swapChain) override;

        // GPU debug annotations (RenderDoc / PIX)
        void BeginEvent(const char* name) override;
        void EndEvent() override;
        void SetMarker(const char* name) override;

        // Resource barriers
        void ResourceBarrier(RHITexture* texture, int stateBefore, int stateAfter) override;

        // Render Targets
        void SetRenderTargets(RHITextureView** rtvs, uint32_t rtvCount, RHITextureView* dsv = nullptr) override;
        void ClearRenderTargetView(RHITextureView* rtv, const ClearColorValue& color) override;
        void ClearDepthStencilView(RHITextureView* dsv, const ClearDepthStencilValue& value, uint8_t clearFlags) override;

        // Pipeline State
        void SetPipelineState(RHIPipelineState* pso) override;

        // Graphics Pipeline
        void SetPrimitiveTopology(EPrimitiveTopology topology) override;
        void SetVertexBuffers(uint32_t startSlot, RHIBuffer* const* buffers, const VertexBufferView* views, uint32_t count) override;
        void SetIndexBuffer(RHIBuffer* buffer, const IndexBufferView* view) override;
        void SetVertexShader(RHIShader* shader) override;
        void SetPixelShader(RHIShader* shader) override;
        void SetGeometryShader(RHIShader* shader) override;
        void SetInputLayout(RHIInputLayout* layout) override;

        // Constant Buffer
        void SetConstantBuffer(uint32_t slot, RHIBuffer* buffer) override;

        // Shader Resource View
        void SetShaderResourceView(uint32_t slot, RHITextureView* srv) override;

        // Sampler
        void SetSampler(uint32_t slot, RHISampler* sampler) override;

        // Viewport and Scissor
        void SetViewports(const Viewport* viewports, uint32_t count) override;
        void SetScissorRects(const ScissorRect* rects, uint32_t count) override;

        // Draw
        void Draw(uint32_t vertexCount, uint32_t vertexStart = 0) override;
        void DrawIndexed(uint32_t indexCount, uint32_t indexStart = 0, int32_t vertexOffset = 0) override;

        // Flush (execute & wait)
        void Flush() override;

        // DX12-specific
        ID3D12GraphicsCommandList* GetCommandList() const { return m_CommandList.Get(); }
        void Reset();
        void Execute();

        // Set root signature + descriptor heaps
        void SetRootSignature(ID3D12RootSignature* rootSig);
        void SetDescriptorHeaps(ID3D12DescriptorHeap* const* heaps, uint32_t count);

    private:
        ComPtr<ID3D12CommandAllocator>       m_CommandAllocator;
        ComPtr<ID3D12GraphicsCommandList>    m_CommandList;
        ComPtr<ID3D12CommandQueue>           m_CommandQueue;
        ComPtr<ID3D12Device>                 m_Device;
        ComPtr<ID3D12Fence>                  m_Fence;
        uint64_t                             m_FenceValue = 0;
        HANDLE                               m_FenceEvent = nullptr;
        bool                                 m_IsOpen = false;
        ID3D12RootSignature*                 m_RootSignature = nullptr;
        ID3D12DescriptorHeap*                m_SRVHeap = nullptr;
    };

} // namespace Kiwi
