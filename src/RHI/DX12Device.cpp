#include "RHI/DX12/DX12Device.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>
#include <stdexcept>
#include <vector>
#include <cstring>

namespace Kiwi
{

    // ============================================================
    // DX12SwapChain
    // ============================================================

    DX12SwapChain::DX12SwapChain(IDXGISwapChain3* swapChain,
                                 ID3D12Device* device,
                                 ID3D12CommandQueue* cmdQueue,
                                 const SwapChainDesc& desc)
        : m_SwapChain(swapChain)
        , m_Device(device)
        , m_CommandQueue(cmdQueue)
        , m_Desc(desc)
    {
        // Create RTV descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = desc.BufferCount;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        HRESULT hr = m_Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_RTVHeap));
        if (FAILED(hr))
            throw std::runtime_error("Failed to create DX12 RTV descriptor heap");

        m_RTVDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        CreateRenderTargetViews();
    }

    DX12SwapChain::~DX12SwapChain()
    {
        m_BackBuffers.clear();
        m_RTVs.clear();
    }

    void DX12SwapChain::CreateRenderTargetViews()
    {
        m_BackBuffers.clear();
        m_RTVs.clear();

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_RTVHeap->GetCPUDescriptorHandleForHeapStart();

        for (uint32_t i = 0; i < m_Desc.BufferCount; i++)
        {
            ComPtr<ID3D12Resource> backBuffer;
            HRESULT hr = m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer));
            if (FAILED(hr))
                throw std::runtime_error("Failed to get DX12 swap chain back buffer");

            m_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);

            TextureDesc texDesc;
            texDesc.Width = m_Desc.Width;
            texDesc.Height = m_Desc.Height;
            texDesc.Format = m_Desc.Format;
            m_BackBuffers.push_back(std::make_unique<DX12Texture>(backBuffer.Get(), texDesc));
            m_RTVs.push_back(std::make_unique<DX12TextureView>(rtvHandle));

            rtvHandle.ptr += m_RTVDescriptorSize;
        }
    }

    void DX12SwapChain::Present(uint32_t syncInterval)
    {
        m_SwapChain->Present(syncInterval, 0);
    }

    void DX12SwapChain::ResizeBuffers(uint32_t width, uint32_t height)
    {
        m_BackBuffers.clear();
        m_RTVs.clear();

        HRESULT hr = m_SwapChain->ResizeBuffers(
            m_Desc.BufferCount, width, height,
            DX12ToDXGIFormat(m_Desc.Format),
            DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
        if (FAILED(hr))
            throw std::runtime_error("Failed to resize DX12 swap chain buffers");

        m_Desc.Width = width;
        m_Desc.Height = height;

        CreateRenderTargetViews();
    }

    uint32_t DX12SwapChain::GetCurrentBackBufferIndex() const
    {
        return m_SwapChain->GetCurrentBackBufferIndex();
    }

    RHITexture* DX12SwapChain::GetBackBuffer(uint32_t index)
    {
        if (index >= m_BackBuffers.size()) return nullptr;
        return m_BackBuffers[index].get();
    }

    RHITextureView* DX12SwapChain::GetBackBufferRTV(uint32_t index)
    {
        if (index >= m_RTVs.size()) return nullptr;
        return m_RTVs[index].get();
    }

    // ============================================================
    // DX12Device
    // ============================================================

    DX12Device::DX12Device(bool enableDebug)
        : m_EnableDebug(enableDebug)
    {
        // Enable debug layer
        if (enableDebug)
        {
            ComPtr<ID3D12Debug> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
            {
                debugController->EnableDebugLayer();
            }
        }

        // Create DXGI Factory
        UINT dxgiFlags = enableDebug ? DXGI_CREATE_FACTORY_DEBUG : 0;
        HRESULT hr = CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&m_DXGIFactory));
        if (FAILED(hr))
            throw std::runtime_error("Failed to create DXGI Factory for DX12");

        // Find hardware adapter
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; m_DXGIFactory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(&m_Device))))
            {
                break;
            }
        }

        if (!m_Device)
            throw std::runtime_error("Failed to create D3D12 device - no compatible GPU found");

        // Create command queue
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

        hr = m_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_CommandQueue));
        if (FAILED(hr))
            throw std::runtime_error("Failed to create DX12 command queue");

        // Create fence
        hr = m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence));
        if (FAILED(hr))
            throw std::runtime_error("Failed to create DX12 fence");
        m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        // Create SRV heap for ImGui + post-process + G-Buffer + shadow maps (64 descriptors)
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = 64;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = m_Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_SRVHeap));
        if (FAILED(hr))
            throw std::runtime_error("Failed to create DX12 SRV descriptor heap");
        m_SRVDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // Create offscreen RTV heap (for post-process render targets)
        D3D12_DESCRIPTOR_HEAP_DESC offscreenRTVDesc = {};
        offscreenRTVDesc.NumDescriptors = 16;
        offscreenRTVDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        offscreenRTVDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = m_Device->CreateDescriptorHeap(&offscreenRTVDesc, IID_PPV_ARGS(&m_OffscreenRTVHeap));
        if (FAILED(hr))
            throw std::runtime_error("Failed to create DX12 offscreen RTV descriptor heap");
        m_OffscreenRTVDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // Create DSV heap
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.NumDescriptors = 8;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = m_Device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_DSVHeap));
        if (FAILED(hr))
            throw std::runtime_error("Failed to create DX12 DSV descriptor heap");
        m_DSVDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        m_DSVAllocated = 0;

        // Create root signature
        CreateRootSignature();
    }

    DX12Device::~DX12Device()
    {
        WaitForGPU();
        if (m_FenceEvent)
        {
            CloseHandle(m_FenceEvent);
            m_FenceEvent = nullptr;
        }
    }

    void DX12Device::CreateRootSignature()
    {
        // Root parameter 0: CBV at b0 (main constant buffer)
        // Root parameter 1: Descriptor table with 8 SRVs at t0-t7 (G-Buffer + shadow maps + textures)
        // Root parameter 2: CBV at b1 (shadow constant buffer)
        D3D12_DESCRIPTOR_RANGE srvRange = {};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 8;
        srvRange.BaseShaderRegister = 0;
        srvRange.RegisterSpace = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER rootParams[3] = {};
        // Slot 0: CBV b0
        rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParams[0].Descriptor.ShaderRegister = 0;
        rootParams[0].Descriptor.RegisterSpace = 0;
        rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        // Slot 1: SRV descriptor table (8 descriptors: t0-t7)
        rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
        rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        // Slot 2: CBV b1 (shadow data)
        rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParams[2].Descriptor.ShaderRegister = 1;
        rootParams[2].Descriptor.RegisterSpace = 0;
        rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // Three static samplers:
        // s0 = linear clamp (post-process, G-Buffer sampling)
        // s1 = linear wrap (material textures)
        // s2 = comparison sampler (shadow mapping — PCF)
        D3D12_STATIC_SAMPLER_DESC staticSamplers[3] = {};

        // s0: Linear Clamp
        staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSamplers[0].MipLODBias = 0.0f;
        staticSamplers[0].MaxAnisotropy = 1;
        staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        staticSamplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        staticSamplers[0].MinLOD = 0.0f;
        staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
        staticSamplers[0].ShaderRegister = 0;
        staticSamplers[0].RegisterSpace = 0;
        staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // s1: Linear Wrap (for material textures)
        staticSamplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        staticSamplers[1].MipLODBias = 0.0f;
        staticSamplers[1].MaxAnisotropy = 1;
        staticSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        staticSamplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        staticSamplers[1].MinLOD = 0.0f;
        staticSamplers[1].MaxLOD = D3D12_FLOAT32_MAX;
        staticSamplers[1].ShaderRegister = 1;
        staticSamplers[1].RegisterSpace = 0;
        staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // s2: Comparison sampler (for shadow mapping — PCF)
        staticSamplers[2].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        staticSamplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        staticSamplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        staticSamplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        staticSamplers[2].MipLODBias = 0.0f;
        staticSamplers[2].MaxAnisotropy = 1;
        staticSamplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        staticSamplers[2].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        staticSamplers[2].MinLOD = 0.0f;
        staticSamplers[2].MaxLOD = D3D12_FLOAT32_MAX;
        staticSamplers[2].ShaderRegister = 2;
        staticSamplers[2].RegisterSpace = 0;
        staticSamplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
        rootSigDesc.NumParameters = 3;
        rootSigDesc.pParameters = rootParams;
        rootSigDesc.NumStaticSamplers = 3;
        rootSigDesc.pStaticSamplers = staticSamplers;
        rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
        if (FAILED(hr))
        {
            std::string errorMsg = "Failed to serialize root signature";
            if (error)
                errorMsg += std::string(": ") + (const char*)error->GetBufferPointer();
            throw std::runtime_error(errorMsg);
        }

        hr = m_Device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
            IID_PPV_ARGS(&m_RootSignature));
        if (FAILED(hr))
            throw std::runtime_error("Failed to create DX12 root signature");
    }

    void DX12Device::WaitForGPU()
    {
        if (!m_CommandQueue || !m_Fence) return;
        uint64_t fenceVal = Signal();
        WaitForFenceValue(fenceVal);
    }

    uint64_t DX12Device::Signal()
    {
        m_FenceValue++;
        m_CommandQueue->Signal(m_Fence.Get(), m_FenceValue);
        return m_FenceValue;
    }

    void DX12Device::WaitForFenceValue(uint64_t fenceValue)
    {
        if (m_Fence->GetCompletedValue() < fenceValue)
        {
            m_Fence->SetEventOnCompletion(fenceValue, m_FenceEvent);
            WaitForSingleObject(m_FenceEvent, INFINITE);
        }
    }

    std::unique_ptr<RHISwapChain> DX12Device::CreateSwapChain(const SwapChainDesc& desc)
    {
        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width = desc.Width;
        sd.Height = desc.Height;
        sd.Format = DX12ToDXGIFormat(desc.Format);
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = desc.BufferCount;
        sd.SampleDesc.Count = 1;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        ComPtr<IDXGISwapChain1> swapChain1;
        HRESULT hr = m_DXGIFactory->CreateSwapChainForHwnd(
            m_CommandQueue.Get(),
            (HWND)desc.WindowHandle,
            &sd,
            nullptr,
            nullptr,
            &swapChain1);
        if (FAILED(hr))
        {
            char msg[256];
            snprintf(msg, sizeof(msg), "Failed to create DX12 SwapChain (HRESULT: 0x%08X)", hr);
            throw std::runtime_error(msg);
        }

        m_DXGIFactory->MakeWindowAssociation((HWND)desc.WindowHandle, DXGI_MWA_NO_ALT_ENTER);

        ComPtr<IDXGISwapChain3> swapChain3;
        swapChain1.As(&swapChain3);

        return std::make_unique<DX12SwapChain>(swapChain3.Get(), m_Device.Get(), m_CommandQueue.Get(), desc);
    }

    std::unique_ptr<RHIBuffer> DX12Device::CreateBuffer(const BufferDesc& desc, const void* initialData)
    {
        uint32_t alignedSize = desc.SizeInBytes;
        // Constant buffers must be 256-byte aligned in DX12
        if (desc.BindFlags & BUFFER_USAGE_CONSTANT)
        {
            alignedSize = (alignedSize + 255) & ~255;
        }

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD; // All buffers on upload heap for simplicity

        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Width = alignedSize;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ComPtr<ID3D12Resource> resource;
        HRESULT hr = m_Device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&resource));
        if (FAILED(hr))
            throw std::runtime_error("Failed to create DX12 buffer");

        // Copy initial data
        if (initialData)
        {
            void* mapped = nullptr;
            D3D12_RANGE readRange = { 0, 0 };
            resource->Map(0, &readRange, &mapped);
            memcpy(mapped, initialData, desc.SizeInBytes);
            resource->Unmap(0, nullptr);
        }

        BufferDesc adjustedDesc = desc;
        adjustedDesc.SizeInBytes = alignedSize;

        return std::make_unique<DX12Buffer>(resource.Get(), adjustedDesc);
    }

    std::unique_ptr<RHITexture> DX12Device::CreateTexture(const TextureDesc& desc, const void* initialData)
    {
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        DXGI_FORMAT dxgiFormat = DX12ToDXGIFormat(desc.Format);

        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resDesc.Width = desc.Width;
        resDesc.Height = desc.Height;
        resDesc.DepthOrArraySize = (UINT16)desc.DepthOrArray;
        resDesc.MipLevels = (UINT16)desc.MipLevels;
        resDesc.Format = dxgiFormat;
        resDesc.SampleDesc.Count = desc.SampleCount;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        // 渲染目标纹理需要 ALLOW_RENDER_TARGET 标志
        if (desc.BindFlags & TEXTURE_BIND_RENDER_TARGET)
            resDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE* clearValue = nullptr;
        D3D12_CLEAR_VALUE depthClear = {};
        D3D12_CLEAR_VALUE rtClear = {};

        // Depth stencil
        if (desc.Format == EFormat::D24_UNORM_S8_UINT || desc.Format == EFormat::D32_FLOAT)
        {
            resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            depthClear.Format = dxgiFormat;
            depthClear.DepthStencil.Depth = 1.0f;
            depthClear.DepthStencil.Stencil = 0;
            clearValue = &depthClear;
        }
        else if (desc.Format == EFormat::R32_TYPELESS)
        {
            // Shadow map: typeless format for DSV (D32_FLOAT) + SRV (R32_FLOAT) dual use
            resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            depthClear.Format = DXGI_FORMAT_D32_FLOAT;
            depthClear.DepthStencil.Depth = 1.0f;
            depthClear.DepthStencil.Stencil = 0;
            clearValue = &depthClear;
        }
        else if (desc.BindFlags & TEXTURE_BIND_RENDER_TARGET)
        {
            // 渲染目标的优化清除值
            rtClear.Format = dxgiFormat;
            rtClear.Color[0] = 0.0f;
            rtClear.Color[1] = 0.0f;
            rtClear.Color[2] = 0.0f;
            rtClear.Color[3] = 1.0f;
            clearValue = &rtClear;
        }

        // 确定初始资源状态
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
        if (desc.Format == EFormat::D24_UNORM_S8_UINT || desc.Format == EFormat::D32_FLOAT ||
            desc.Format == EFormat::R32_TYPELESS)
            initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

        ComPtr<ID3D12Resource> resource;
        HRESULT hr = m_Device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resDesc,
            initialState,
            clearValue,
            IID_PPV_ARGS(&resource));
        if (FAILED(hr))
            throw std::runtime_error("Failed to create DX12 texture");

        return std::make_unique<DX12Texture>(resource.Get(), desc);
    }

    std::unique_ptr<RHITextureView> DX12Device::CreateTextureView(
        RHITexture* texture, EDescriptorHeapType heapType,
        EFormat format, int mipSlice, int arraySlice)
    {
        auto dxTexture = static_cast<DX12Texture*>(texture);
        ID3D12Resource* resource = dxTexture->GetD3DResource();

        switch (heapType)
        {
        case EDescriptorHeapType::RTV:
        {
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_OffscreenRTVHeap->GetCPUDescriptorHandleForHeapStart();
            rtvHandle.ptr += (SIZE_T)m_OffscreenRTVAllocated * m_OffscreenRTVDescriptorSize;
            m_OffscreenRTVAllocated++;

            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = DX12ToDXGIFormat(
                (format != EFormat::Unknown) ? format : texture->GetDesc().Format);
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Texture2D.MipSlice = (mipSlice >= 0) ? (UINT)mipSlice : 0;

            m_Device->CreateRenderTargetView(resource, &rtvDesc, rtvHandle);
            return std::make_unique<DX12TextureView>(rtvHandle);
        }
        case EDescriptorHeapType::CBV_SRV_UAV:
        {
            // Allocate from SRV heap (non-shader-visible CPU handle for source copy)
            // We create a separate non-shader-visible heap for the source descriptor
            D3D12_DESCRIPTOR_HEAP_DESC cpuSrvHeapDesc = {};
            cpuSrvHeapDesc.NumDescriptors = 1;
            cpuSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            cpuSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; // CPU-only

            ComPtr<ID3D12DescriptorHeap> cpuSrvHeap;
            HRESULT hr = m_Device->CreateDescriptorHeap(&cpuSrvHeapDesc, IID_PPV_ARGS(&cpuSrvHeap));
            if (FAILED(hr))
                throw std::runtime_error("Failed to create DX12 SRV CPU descriptor heap");

            D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = cpuSrvHeap->GetCPUDescriptorHandleForHeapStart();

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            EFormat srvFormat = (format != EFormat::Unknown) ? format : texture->GetDesc().Format;
            // For typeless depth textures, use R32_FLOAT as SRV format
            if (srvFormat == EFormat::R32_TYPELESS)
                srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
            else
                srvDesc.Format = DX12ToDXGIFormat(srvFormat);
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = texture->GetDesc().MipLevels;
            srvDesc.Texture2D.MostDetailedMip = 0;

            m_Device->CreateShaderResourceView(resource, &srvDesc, srvHandle);

            // Store the CPU heap so it doesn't get destroyed
            // We return a DX12TextureView that holds the CPU descriptor handle
            // The SetShaderResourceView will copy from this handle to the shader-visible heap
            auto view = std::make_unique<DX12TextureView>(srvHandle);
            view->SetSRVHeap(std::move(cpuSrvHeap));
            return view;
        }
        case EDescriptorHeapType::DSV:
        {
            D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_DSVHeap->GetCPUDescriptorHandleForHeapStart();
            dsvHandle.ptr += (SIZE_T)m_DSVAllocated * m_DSVDescriptorSize;
            m_DSVAllocated++;

            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
            EFormat dsvFormat = texture->GetDesc().Format;
            // For typeless depth textures, use D32_FLOAT as DSV format
            if (dsvFormat == EFormat::R32_TYPELESS)
                dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
            else
                dsvDesc.Format = DX12ToDXGIFormat(dsvFormat);
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            dsvDesc.Texture2D.MipSlice = 0;

            m_Device->CreateDepthStencilView(resource, &dsvDesc, dsvHandle);
            return std::make_unique<DX12TextureView>(dsvHandle);
        }
        default:
            throw std::runtime_error("Unsupported DX12 descriptor heap type for CreateTextureView");
        }
    }

    std::unique_ptr<RHIShader> DX12Device::CreateShader(EShaderType type, const void* byteCode, size_t byteCodeSize)
    {
        ComPtr<ID3DBlob> blob;
        HRESULT hr = D3DCreateBlob(byteCodeSize, &blob);
        if (FAILED(hr))
            throw std::runtime_error("Failed to create DX12 shader blob");
        memcpy(blob->GetBufferPointer(), byteCode, byteCodeSize);

        return std::make_unique<DX12Shader>(type, blob.Get());
    }

    std::unique_ptr<RHIShader> DX12Device::CompileShader(
        EShaderType type, const char* hlslSource, const char* entryPoint,
        const char* shaderModel, const ShaderMacro* macros, uint32_t macroCount)
    {
        ComPtr<ID3DBlob> shaderBlob;
        ComPtr<ID3DBlob> errorBlob;

        D3D_SHADER_MACRO* dxMacros = nullptr;
        std::vector<D3D_SHADER_MACRO> dxMacroVec;
        if (macros && macroCount > 0)
        {
            dxMacroVec.reserve(macroCount + 1);
            for (uint32_t i = 0; i < macroCount; i++)
                dxMacroVec.push_back({ macros[i].Name, macros[i].Definition });
            dxMacroVec.push_back({ nullptr, nullptr });
            dxMacros = dxMacroVec.data();
        }

        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        HRESULT hr = D3DCompile(hlslSource, strlen(hlslSource), nullptr, dxMacros, nullptr,
            entryPoint, shaderModel, flags, 0, &shaderBlob, &errorBlob);
        if (FAILED(hr))
        {
            std::string errorMsg = "DX12 Shader compilation failed: ";
            if (errorBlob)
                errorMsg += (const char*)errorBlob->GetBufferPointer();
            throw std::runtime_error(errorMsg);
        }

        return CreateShader(type, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());
    }

    std::unique_ptr<RHIInputLayout> DX12Device::CreateInputLayout(
        const InputElementDesc* elements, uint32_t elementCount, RHIShader* vertexShader)
    {
        std::vector<D3D12_INPUT_ELEMENT_DESC> d3dElements(elementCount);
        for (uint32_t i = 0; i < elementCount; i++)
        {
            d3dElements[i].SemanticName = elements[i].SemanticName;
            d3dElements[i].SemanticIndex = elements[i].SemanticIndex;
            d3dElements[i].Format = DX12ToDXGIFormat(elements[i].Format);
            d3dElements[i].AlignedByteOffset = elements[i].AlignedByteOffset;
            d3dElements[i].InputSlot = elements[i].InputSlot;
            d3dElements[i].InputSlotClass = (elements[i].InstanceDataStepRate > 0)
                ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            d3dElements[i].InstanceDataStepRate = elements[i].InstanceDataStepRate;
        }

        return std::make_unique<DX12InputLayout>(d3dElements);
    }

    std::unique_ptr<RHIPipelineState> DX12Device::CreatePipelineState()
    {
        return std::make_unique<DX12PipelineState>(nullptr);
    }

    std::unique_ptr<RHIPipelineState> DX12Device::CreateGraphicsPipelineState(
        RHIShader* vertexShader, RHIShader* pixelShader, RHIInputLayout* inputLayout)
    {
        auto dx12InputLayout = static_cast<DX12InputLayout*>(inputLayout);
        auto vsShader = static_cast<DX12Shader*>(vertexShader);
        auto psShader = static_cast<DX12Shader*>(pixelShader);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_RootSignature.Get();

        psoDesc.VS.pShaderBytecode = vsShader->GetBlob()->GetBufferPointer();
        psoDesc.VS.BytecodeLength = vsShader->GetBlob()->GetBufferSize();
        psoDesc.PS.pShaderBytecode = psShader->GetBlob()->GetBufferPointer();
        psoDesc.PS.BytecodeLength = psShader->GetBlob()->GetBufferSize();

        // inputLayout 可以为 nullptr（例如后处理全屏三角形使用 SV_VertexID，无需顶点输入）
        if (dx12InputLayout)
        {
            const auto& elements = dx12InputLayout->GetElements();
            psoDesc.InputLayout.pInputElementDescs = elements.data();
            psoDesc.InputLayout.NumElements = (UINT)elements.size();
        }
        else
        {
            psoDesc.InputLayout.pInputElementDescs = nullptr;
            psoDesc.InputLayout.NumElements = 0;
        }

        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = dx12InputLayout ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
        psoDesc.RasterizerState.DepthClipEnable = TRUE;

        psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        psoDesc.DepthStencilState.DepthEnable = (dx12InputLayout != nullptr); // 后处理不需要深度测试
        psoDesc.DepthStencilState.DepthWriteMask = (dx12InputLayout != nullptr) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        psoDesc.DepthStencilState.StencilEnable = FALSE;

        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat = dx12InputLayout ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_UNKNOWN;
        psoDesc.SampleDesc.Count = 1;

        ComPtr<ID3D12PipelineState> pso;
        HRESULT hr = m_Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
        if (FAILED(hr))
        {
            char msg[256];
            snprintf(msg, sizeof(msg), "Failed to create DX12 PSO (HRESULT: 0x%08X)", hr);
            throw std::runtime_error(msg);
        }

        return std::make_unique<DX12PipelineState>(pso.Get());
    }

    std::unique_ptr<RHIPipelineState> DX12Device::CreateGraphicsPipelineState(
        RHIShader* vertexShader, RHIShader* pixelShader, RHIInputLayout* inputLayout,
        const PipelineStateDesc& pipelineDesc)
    {
        auto dx12InputLayout = static_cast<DX12InputLayout*>(inputLayout);
        auto vsShader = static_cast<DX12Shader*>(vertexShader);
        auto psShader = static_cast<DX12Shader*>(pixelShader);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_RootSignature.Get();

        psoDesc.VS.pShaderBytecode = vsShader->GetBlob()->GetBufferPointer();
        psoDesc.VS.BytecodeLength = vsShader->GetBlob()->GetBufferSize();
        psoDesc.PS.pShaderBytecode = psShader->GetBlob()->GetBufferPointer();
        psoDesc.PS.BytecodeLength = psShader->GetBlob()->GetBufferSize();

        if (dx12InputLayout)
        {
            const auto& elements = dx12InputLayout->GetElements();
            psoDesc.InputLayout.pInputElementDescs = elements.data();
            psoDesc.InputLayout.NumElements = (UINT)elements.size();
        }
        else
        {
            psoDesc.InputLayout.pInputElementDescs = nullptr;
            psoDesc.InputLayout.NumElements = 0;
        }

        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = dx12InputLayout ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
        psoDesc.RasterizerState.DepthClipEnable = TRUE;

        for (uint32_t i = 0; i < pipelineDesc.NumRenderTargets; i++)
        {
            psoDesc.BlendState.RenderTarget[i].BlendEnable = FALSE;
            psoDesc.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }

        psoDesc.DepthStencilState.DepthEnable = pipelineDesc.DepthEnabled;
        psoDesc.DepthStencilState.DepthWriteMask = pipelineDesc.DepthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        psoDesc.DepthStencilState.StencilEnable = FALSE;

        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = pipelineDesc.NumRenderTargets;
        for (uint32_t i = 0; i < pipelineDesc.NumRenderTargets; i++)
            psoDesc.RTVFormats[i] = DX12ToDXGIFormat(pipelineDesc.RTVFormats[i]);
        psoDesc.DSVFormat = pipelineDesc.DepthEnabled ? DX12ToDXGIFormat(pipelineDesc.DSVFormat) : DXGI_FORMAT_UNKNOWN;
        psoDesc.SampleDesc.Count = 1;

        ComPtr<ID3D12PipelineState> pso;
        HRESULT hr = m_Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
        if (FAILED(hr))
        {
            char msg[256];
            snprintf(msg, sizeof(msg), "Failed to create DX12 MRT PSO (HRESULT: 0x%08X)", hr);
            throw std::runtime_error(msg);
        }

        return std::make_unique<DX12PipelineState>(pso.Get());
    }

    std::unique_ptr<RHISampler> DX12Device::CreateSampler()
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = {};
        return std::make_unique<DX12Sampler>(handle);
    }

    // ============================================================
    // DX12CommandContext
    // ============================================================

    DX12CommandContext::DX12CommandContext(ID3D12Device* device, ID3D12CommandQueue* cmdQueue,
                                           ID3D12RootSignature* rootSignature, ID3D12DescriptorHeap* srvHeap)
        : m_Device(device), m_CommandQueue(cmdQueue), m_RootSignature(rootSignature), m_SRVHeap(srvHeap)
    {
        HRESULT hr = m_Device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&m_CommandAllocator));
        if (FAILED(hr))
            throw std::runtime_error("Failed to create DX12 command allocator");

        hr = m_Device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            m_CommandAllocator.Get(), nullptr,
            IID_PPV_ARGS(&m_CommandList));
        if (FAILED(hr))
            throw std::runtime_error("Failed to create DX12 command list");

        // Close initially; will be reset at frame start
        m_CommandList->Close();
        m_IsOpen = false;

        // Create fence for this context
        hr = m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence));
        if (FAILED(hr))
            throw std::runtime_error("Failed to create DX12 context fence");
        m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    }

    DX12CommandContext::~DX12CommandContext()
    {
        if (m_FenceEvent)
        {
            CloseHandle(m_FenceEvent);
            m_FenceEvent = nullptr;
        }
    }

    void DX12CommandContext::BeginFrame(RHISwapChain* swapChain)
    {
        // Reset command list
        Reset();

        // Set root signature and descriptor heaps
        if (m_RootSignature)
            m_CommandList->SetGraphicsRootSignature(m_RootSignature);
        if (m_SRVHeap)
            m_CommandList->SetDescriptorHeaps(1, &m_SRVHeap);

        // Transition back buffer to render target
        if (swapChain)
        {
            auto backBuffer = swapChain->GetBackBuffer(swapChain->GetCurrentBackBufferIndex());
            ResourceBarrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        }
    }

    void DX12CommandContext::EndFrame(RHISwapChain* swapChain)
    {
        // Transition back buffer to present
        if (swapChain)
        {
            auto backBuffer = swapChain->GetBackBuffer(swapChain->GetCurrentBackBufferIndex());
            ResourceBarrier(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        }
    }

    void DX12CommandContext::BeginEvent(const char* name)
    {
        if (!name || !m_CommandList) return;
        // Convert to wide string for PIX
        int len = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
        std::vector<wchar_t> wname(len);
        MultiByteToWideChar(CP_UTF8, 0, name, -1, wname.data(), len);
        // PIX color: 0 = auto-assign color
        m_CommandList->BeginEvent(0, wname.data(), (UINT)(len * sizeof(wchar_t)));
    }

    void DX12CommandContext::EndEvent()
    {
        if (m_CommandList)
            m_CommandList->EndEvent();
    }

    void DX12CommandContext::SetMarker(const char* name)
    {
        if (!name || !m_CommandList) return;
        int len = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
        std::vector<wchar_t> wname(len);
        MultiByteToWideChar(CP_UTF8, 0, name, -1, wname.data(), len);
        m_CommandList->SetMarker(0, wname.data(), (UINT)(len * sizeof(wchar_t)));
    }

    void DX12CommandContext::Reset()
    {
        m_CommandAllocator->Reset();
        m_CommandList->Reset(m_CommandAllocator.Get(), nullptr);
        m_IsOpen = true;
    }

    void DX12CommandContext::Execute()
    {
        if (m_IsOpen)
        {
            m_CommandList->Close();
            m_IsOpen = false;
        }

        ID3D12CommandList* cmdLists[] = { m_CommandList.Get() };
        m_CommandQueue->ExecuteCommandLists(1, cmdLists);
    }

    void DX12CommandContext::ResourceBarrier(RHITexture* texture, int stateBefore, int stateAfter)
    {
        auto dxTexture = static_cast<DX12Texture*>(texture);
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = dxTexture->GetD3DResource();
        barrier.Transition.StateBefore = (D3D12_RESOURCE_STATES)stateBefore;
        barrier.Transition.StateAfter = (D3D12_RESOURCE_STATES)stateAfter;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        m_CommandList->ResourceBarrier(1, &barrier);
    }

    void DX12CommandContext::SetRenderTargets(RHITextureView** rtvs, uint32_t rtvCount, RHITextureView* dsv)
    {
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles(rtvCount);
        for (uint32_t i = 0; i < rtvCount; i++)
        {
            auto dxView = static_cast<DX12TextureView*>(rtvs[i]);
            rtvHandles[i] = dxView->GetCPUHandle();
        }

        D3D12_CPU_DESCRIPTOR_HANDLE* dsvHandle = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE dsvH = {};
        if (dsv)
        {
            auto dxDSV = static_cast<DX12TextureView*>(dsv);
            dsvH = dxDSV->GetCPUHandle();
            dsvHandle = &dsvH;
        }

        m_CommandList->OMSetRenderTargets(rtvCount, rtvHandles.data(), FALSE, dsvHandle);
    }

    void DX12CommandContext::ClearRenderTargetView(RHITextureView* rtv, const ClearColorValue& color)
    {
        auto dxView = static_cast<DX12TextureView*>(rtv);
        float clearColor[4] = { color.R, color.G, color.B, color.A };
        m_CommandList->ClearRenderTargetView(dxView->GetCPUHandle(), clearColor, 0, nullptr);
    }

    void DX12CommandContext::ClearDepthStencilView(
        RHITextureView* dsv, const ClearDepthStencilValue& value, uint8_t clearFlags)
    {
        auto dxView = static_cast<DX12TextureView*>(dsv);
        D3D12_CLEAR_FLAGS flags = (D3D12_CLEAR_FLAGS)0;
        if (clearFlags & 0x01) flags = (D3D12_CLEAR_FLAGS)(flags | D3D12_CLEAR_FLAG_DEPTH);
        if (clearFlags & 0x02) flags = (D3D12_CLEAR_FLAGS)(flags | D3D12_CLEAR_FLAG_STENCIL);

        m_CommandList->ClearDepthStencilView(dxView->GetCPUHandle(), flags, value.Depth, value.Stencil, 0, nullptr);
    }

    void DX12CommandContext::SetPipelineState(RHIPipelineState* pso)
    {
        auto dxPSO = static_cast<DX12PipelineState*>(pso);
        if (dxPSO && dxPSO->GetPSO())
            m_CommandList->SetPipelineState(dxPSO->GetPSO());
    }

    void DX12CommandContext::SetPrimitiveTopology(EPrimitiveTopology topology)
    {
        m_CommandList->IASetPrimitiveTopology(DX12ToDX12Topology(topology));
    }

    void DX12CommandContext::SetVertexBuffers(
        uint32_t startSlot, RHIBuffer* const* buffers, const VertexBufferView* views, uint32_t count)
    {
        std::vector<D3D12_VERTEX_BUFFER_VIEW> d3dViews(count);
        for (uint32_t i = 0; i < count; i++)
        {
            auto dxBuffer = static_cast<DX12Buffer*>(buffers[i]);
            d3dViews[i].BufferLocation = dxBuffer->GetGPUVirtualAddress();
            d3dViews[i].SizeInBytes = views[i].SizeInBytes;
            d3dViews[i].StrideInBytes = views[i].StrideInBytes;
        }
        m_CommandList->IASetVertexBuffers(startSlot, count, d3dViews.data());
    }

    void DX12CommandContext::SetIndexBuffer(RHIBuffer* buffer, const IndexBufferView* view)
    {
        if (!buffer || !view)
        {
            m_CommandList->IASetIndexBuffer(nullptr);
            return;
        }

        auto dxBuffer = static_cast<DX12Buffer*>(buffer);
        D3D12_INDEX_BUFFER_VIEW ibView = {};
        ibView.BufferLocation = dxBuffer->GetGPUVirtualAddress();
        ibView.SizeInBytes = view->SizeInBytes;
        ibView.Format = DX12ToDXGIFormat(view->Format);

        m_CommandList->IASetIndexBuffer(&ibView);
    }

    // DX12 shader binding is done via PSO, not individually
    void DX12CommandContext::SetVertexShader(RHIShader* shader) { /* handled by PSO */ }
    void DX12CommandContext::SetPixelShader(RHIShader* shader) { /* handled by PSO */ }
    void DX12CommandContext::SetGeometryShader(RHIShader* shader) { /* handled by PSO */ }
    void DX12CommandContext::SetInputLayout(RHIInputLayout* layout) { /* handled by PSO */ }

    void DX12CommandContext::SetConstantBuffer(uint32_t slot, RHIBuffer* buffer)
    {
        auto dxBuffer = static_cast<DX12Buffer*>(buffer);
        // Map HLSL constant buffer register to DX12 root parameter index:
        // b0 -> root param 0 (main CB)
        // b1 -> root param 2 (shadow CB)
        uint32_t rootParamIndex = slot;
        if (slot == 1)
            rootParamIndex = 2;  // b1 maps to root param 2 (root param 1 is SRV table)
        m_CommandList->SetGraphicsRootConstantBufferView(rootParamIndex, dxBuffer->GetGPUVirtualAddress());
    }

    void DX12CommandContext::SetShaderResourceView(uint32_t slot, RHITextureView* srv)
    {
        if (srv && m_SRVHeap)
        {
            // Use descriptor at index (slot + 1) in SRV heap (index 0 is reserved for ImGui)
            uint32_t descriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            // Copy the SRV descriptor to the shader-visible heap at the correct slot
            D3D12_CPU_DESCRIPTOR_HANDLE srcHandle;
            srcHandle.ptr = (SIZE_T)srv->GetNativeHandle();
            D3D12_CPU_DESCRIPTOR_HANDLE dstHandle = m_SRVHeap->GetCPUDescriptorHandleForHeapStart();
            dstHandle.ptr += (SIZE_T)(slot + 1) * descriptorSize;
            m_Device->CopyDescriptorsSimple(1, dstHandle, srcHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            // Always set descriptor table starting from index 1 (t0)
            // This way all SRVs (t0-t3) are in a contiguous range starting at heap index 1
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_SRVHeap->GetGPUDescriptorHandleForHeapStart();
            gpuHandle.ptr += (SIZE_T)1 * descriptorSize;
            m_CommandList->SetGraphicsRootDescriptorTable(1, gpuHandle);
        }
    }

    void DX12CommandContext::SetSampler(uint32_t slot, RHISampler* sampler) { /* TODO */ }

    void DX12CommandContext::SetViewports(const Viewport* viewports, uint32_t count)
    {
        std::vector<D3D12_VIEWPORT> d3dViewports(count);
        for (uint32_t i = 0; i < count; i++)
        {
            d3dViewports[i].TopLeftX = viewports[i].TopLeftX;
            d3dViewports[i].TopLeftY = viewports[i].TopLeftY;
            d3dViewports[i].Width = viewports[i].Width;
            d3dViewports[i].Height = viewports[i].Height;
            d3dViewports[i].MinDepth = viewports[i].MinDepth;
            d3dViewports[i].MaxDepth = viewports[i].MaxDepth;
        }
        m_CommandList->RSSetViewports(count, d3dViewports.data());
    }

    void DX12CommandContext::SetScissorRects(const ScissorRect* rects, uint32_t count)
    {
        std::vector<D3D12_RECT> d3dRects(count);
        for (uint32_t i = 0; i < count; i++)
        {
            d3dRects[i].left = rects[i].Left;
            d3dRects[i].top = rects[i].Top;
            d3dRects[i].right = rects[i].Right;
            d3dRects[i].bottom = rects[i].Bottom;
        }
        m_CommandList->RSSetScissorRects(count, d3dRects.data());
    }

    void DX12CommandContext::Draw(uint32_t vertexCount, uint32_t vertexStart)
    {
        m_CommandList->DrawInstanced(vertexCount, 1, vertexStart, 0);
    }

    void DX12CommandContext::DrawIndexed(uint32_t indexCount, uint32_t indexStart, int32_t vertexOffset)
    {
        m_CommandList->DrawIndexedInstanced(indexCount, 1, indexStart, vertexOffset, 0);
    }

    void DX12CommandContext::Flush()
    {
        Execute();

        // Wait for GPU
        m_FenceValue++;
        m_CommandQueue->Signal(m_Fence.Get(), m_FenceValue);
        if (m_Fence->GetCompletedValue() < m_FenceValue)
        {
            m_Fence->SetEventOnCompletion(m_FenceValue, m_FenceEvent);
            WaitForSingleObject(m_FenceEvent, INFINITE);
        }
    }

    void DX12CommandContext::SetRootSignature(ID3D12RootSignature* rootSig)
    {
        m_CommandList->SetGraphicsRootSignature(rootSig);
    }

    void DX12CommandContext::SetDescriptorHeaps(ID3D12DescriptorHeap* const* heaps, uint32_t count)
    {
        m_CommandList->SetDescriptorHeaps(count, heaps);
    }

    // ============================================================
    // DX12 ImGui Integration
    // ============================================================

    void DX12Device::InitImGui(void* windowHandle)
    {
        if (!m_ImGuiInitialized)
        {
            ImGui_ImplWin32_Init((HWND)windowHandle);
        }

        // Use new ImGui_ImplDX12_InitInfo API (legacy 6-param API is missing CommandQueue,
        // which causes nullptr crash in ImGui_ImplDX12_CreateFontsTexture)
        ImGui_ImplDX12_InitInfo init_info;
        init_info.Device = m_Device.Get();
        init_info.CommandQueue = m_CommandQueue.Get();
        init_info.NumFramesInFlight = 2;
        init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        init_info.SrvDescriptorHeap = m_SRVHeap.Get();
        init_info.LegacySingleSrvCpuDescriptor = m_SRVHeap->GetCPUDescriptorHandleForHeapStart();
        init_info.LegacySingleSrvGpuDescriptor = m_SRVHeap->GetGPUDescriptorHandleForHeapStart();
        ImGui_ImplDX12_Init(&init_info);
        m_ImGuiInitialized = true;
    }

    void DX12Device::ShutdownImGui()
    {
        if (m_ImGuiInitialized)
        {
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            m_ImGuiInitialized = false;
        }
    }

    void DX12Device::ImGuiNewFrame()
    {
        ImGui_ImplWin32_NewFrame();
        ImGui_ImplDX12_NewFrame();
    }

    void DX12Device::ImGuiRenderDrawData(RHICommandContext* ctx)
    {
        auto dx12Ctx = static_cast<DX12CommandContext*>(ctx);
        // Re-set descriptor heaps for ImGui (it needs SRV heap)
        if (m_SRVHeap)
        {
            ID3D12DescriptorHeap* heaps[] = { m_SRVHeap.Get() };
            dx12Ctx->GetCommandList()->SetDescriptorHeaps(1, heaps);
        }
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dx12Ctx->GetCommandList());
    }

    // ============================================================
    // DX12 RHI Factory
    // ============================================================

    void CreateDX12RHI(
        const RHIInitParams& params,
        std::unique_ptr<RHIDevice>& outDevice,
        std::unique_ptr<RHICommandContext>& outContext)
    {
        auto device = std::make_unique<DX12Device>(params.EnableDebug);
        auto context = std::make_unique<DX12CommandContext>(
            device->GetD3DDevice(), device->GetCommandQueue(),
            device->GetRootSignature(), device->GetSRVHeap());

        outDevice = std::move(device);
        outContext = std::move(context);
    }

} // namespace Kiwi
