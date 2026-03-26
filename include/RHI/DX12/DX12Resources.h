#pragma once

#include "RHI/RHI.h"
#include "RHI/DX12/DX12Headers.h"
#include <vector>

namespace Kiwi
{

    // ============================================================
    // DX12 Buffer
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

    // ============================================================
    // DX12 Texture
    // ============================================================

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

    // ============================================================
    // DX12 Texture View
    // ============================================================

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

    // ============================================================
    // DX12 Shader
    // ============================================================

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

    // ============================================================
    // DX12 Input Layout
    // ============================================================

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

    // ============================================================
    // DX12 Pipeline State
    // ============================================================

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

    // ============================================================
    // DX12 Sampler
    // ============================================================

    class DX12Sampler : public RHISampler
    {
    public:
        DX12Sampler(D3D12_CPU_DESCRIPTOR_HANDLE handle)
            : m_Handle(handle) {}

        void* GetNativeHandle() const override { return (void*)m_Handle.ptr; }

    private:
        D3D12_CPU_DESCRIPTOR_HANDLE m_Handle;
    };

} // namespace Kiwi
