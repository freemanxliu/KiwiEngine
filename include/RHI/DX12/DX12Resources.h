#pragma once

#include "RHI/RHI.h"
#include "RHI/DX12/DX12Headers.h"
#include "RHI/ConstantBufferVersioning.h"
#include <algorithm>
#include <memory>
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
            : Resource(resource), Desc(desc), MappedPtr(mappedPtr) {}

        DX12Buffer(ConstantUploadAllocator* allocator, const BufferDesc& desc, const void* initialData)
            : Desc(desc)
            , Constant(std::make_unique<VersionedConstantBuffer>(allocator, (std::max)(desc.SizeInBytes, 16u), initialData)) {}

        ~DX12Buffer() override
        {
            if (MappedPtr && Resource)
            {
                Resource->Unmap(0, nullptr);
                MappedPtr = nullptr;
            }
        }

        void* GetNativeHandle() const override { return Resource.Get(); }
        const BufferDesc& GetDesc() const override { return Desc; }

        void* Map(uint32_t subresource = 0) override
        {
            if (Constant) return Constant->Map();
            if (MappedPtr) return MappedPtr;
            D3D12_RANGE readRange = { 0, 0 };
            HRESULT hr = Resource->Map(0, &readRange, &MappedPtr);
            if (FAILED(hr)) return nullptr;
            return MappedPtr;
        }

        void Unmap(uint32_t subresource = 0) override
        {
            if (Constant)
            {
                Constant->Unmap();
                return;
            }
            if (MappedPtr)
            {
                Resource->Unmap(0, nullptr);
                MappedPtr = nullptr;
            }
        }

        void UpdateData(const void* data, uint32_t size, uint32_t offset = 0) override
        {
            if (Constant)
            {
                Constant->UpdateData(data, size, offset);
                return;
            }
            void* mapped = Map();
            if (mapped)
            {
                memcpy((uint8_t*)mapped + offset, data, size);
                Unmap();
            }
        }

        ID3D12Resource* GetD3DResource() const { return Resource.Get(); }
        D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const
        {
            if (Constant)
                return Constant->GetCurrent().GpuAddress;
            return Resource->GetGPUVirtualAddress();
        }

    private:
        ComPtr<ID3D12Resource> Resource;
        BufferDesc Desc;
        void* MappedPtr = nullptr;
        std::unique_ptr<VersionedConstantBuffer> Constant;
    };

    // ============================================================
    // DX12 Texture
    // ============================================================

    class DX12Texture : public RHITexture
    {
    public:
        DX12Texture(ID3D12Resource* resource, const TextureDesc& desc)
            : Resource(resource), Desc(desc) {}

        void* GetNativeHandle() const override { return Resource.Get(); }
        const TextureDesc& GetDesc() const override { return Desc; }
        ID3D12Resource* GetD3DResource() const { return Resource.Get(); }

    private:
        ComPtr<ID3D12Resource> Resource;
        TextureDesc Desc;
    };

    // ============================================================
    // DX12 Texture View
    // ============================================================

    class DX12TextureView : public RHITextureView
    {
    public:
        DX12TextureView(D3D12_CPU_DESCRIPTOR_HANDLE handle)
            : Handle(handle) {}

        void* GetNativeHandle() const override { return (void*)Handle.ptr; }
        D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle() const { return Handle; }

        // For SRV views: hold the CPU-only descriptor heap so it's not destroyed
        void SetSRVHeap(ComPtr<ID3D12DescriptorHeap> heap) { SRVHeap = std::move(heap); }

    private:
        D3D12_CPU_DESCRIPTOR_HANDLE Handle;
        ComPtr<ID3D12DescriptorHeap> SRVHeap; // Optional: owns the CPU descriptor heap for SRV
    };

    // ============================================================
    // DX12 Shader
    // ============================================================

    class DX12Shader : public RHIShader
    {
    public:
        DX12Shader(EShaderType type, ID3DBlob* blob)
            : Type(type), Blob(blob) {}

        void* GetNativeHandle() const override { return Blob->GetBufferPointer(); }
        EShaderType GetType() const override { return Type; }
        ID3DBlob* GetBlob() const { return Blob.Get(); }

    private:
        EShaderType Type;
        ComPtr<ID3DBlob> Blob;
    };

    // ============================================================
    // DX12 Input Layout
    // ============================================================

    class DX12InputLayout : public RHIInputLayout
    {
    public:
        DX12InputLayout(const std::vector<D3D12_INPUT_ELEMENT_DESC>& elements)
            : Elements(elements) {}

        void* GetNativeHandle() const override { return (void*)Elements.data(); }
        const std::vector<D3D12_INPUT_ELEMENT_DESC>& GetElements() const { return Elements; }

    private:
        std::vector<D3D12_INPUT_ELEMENT_DESC> Elements;
    };

    // ============================================================
    // DX12 Pipeline State
    // ============================================================

    class DX12PipelineState : public RHIPipelineState
    {
    public:
        DX12PipelineState(ID3D12PipelineState* pso)
            : PSO(pso) {}

        void* GetNativeHandle() const override { return PSO.Get(); }
        ID3D12PipelineState* GetPSO() const { return PSO.Get(); }

    private:
        ComPtr<ID3D12PipelineState> PSO;
    };

    // ============================================================
    // DX12 Sampler
    // ============================================================

    class DX12Sampler : public RHISampler
    {
    public:
        DX12Sampler(D3D12_CPU_DESCRIPTOR_HANDLE handle)
            : Handle(handle) {}

        void* GetNativeHandle() const override { return (void*)Handle.ptr; }

    private:
        D3D12_CPU_DESCRIPTOR_HANDLE Handle;
    };

} // namespace Kiwi
