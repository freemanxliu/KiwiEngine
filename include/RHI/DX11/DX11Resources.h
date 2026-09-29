#pragma once

#include "RHI/RHI.h"
#include "RHI/DX11/DX11Headers.h"
#include "RHI/DX11/DX11Utils.h"

namespace Kiwi
{

    // ============================================================
    // DX11 Buffer
    // ============================================================

    class DX11Buffer : public RHIBuffer
    {
    public:
        DX11Buffer(ID3D11Buffer* buffer, ID3D11DeviceContext* context, const BufferDesc& desc)
            : Buffer(buffer), Context(context), Desc(desc) {}

        void* GetNativeHandle() const override { return Buffer.Get(); }
        const BufferDesc& GetDesc() const override { return Desc; }

        void* Map(uint32_t /*subresource*/ = 0) override
        {
            D3D11_MAPPED_SUBRESOURCE mapped;
            HRESULT hr = Context->Map(Buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (FAILED(hr)) return nullptr;
            return mapped.pData;
        }

        void Unmap(uint32_t /*subresource*/ = 0) override
        {
            Context->Unmap(Buffer.Get(), 0);
        }

        void UpdateData(const void* data, uint32_t size, uint32_t offset = 0) override
        {
            if (!data || size == 0)
                return;
            D3D11_BOX box = {};
            box.left = offset;
            box.right = offset + size;
            box.bottom = 1;
            box.back = 1;
            Context->UpdateSubresource(Buffer.Get(), 0, offset == 0 && size == Desc.SizeInBytes ? nullptr : &box, data, size, 0);
        }

        ID3D11Buffer* GetD3DBuffer() const { return Buffer.Get(); }

    private:
        ComPtr<ID3D11Buffer> Buffer;
        ComPtr<ID3D11DeviceContext> Context;
        BufferDesc Desc;
    };

    // ============================================================
    // DX11 Texture
    // ============================================================

    class DX11Texture : public RHITexture
    {
    public:
        DX11Texture(ID3D11Resource* resource, const TextureDesc& desc)
            : Texture(resource), Desc(desc) {}

        void* GetNativeHandle() const override { return Texture.Get(); }
        const TextureDesc& GetDesc() const override { return Desc; }

        ID3D11Resource* GetD3DResource() const { return Texture.Get(); }

    private:
        ComPtr<ID3D11Resource> Texture;
        TextureDesc Desc;
    };

    // ============================================================
    // DX11 Texture View
    // ============================================================

    class DX11TextureView : public RHITextureView
    {
    public:
        DX11TextureView(ID3D11RenderTargetView* rtv)
            : RTV(rtv), ViewType(Type::RTV) {}
        DX11TextureView(ID3D11DepthStencilView* dsv)
            : DSV(dsv), ViewType(Type::DSV) {}
        DX11TextureView(ID3D11ShaderResourceView* srv)
            : SRV(srv), ViewType(Type::SRV) {}

        void* GetNativeHandle() const override
        {
            switch (ViewType)
            {
            case Type::RTV: return RTV.Get();
            case Type::DSV: return DSV.Get();
            case Type::SRV: return SRV.Get();
            default: return nullptr;
            }
        }

        ID3D11RenderTargetView* AsRTV() const { return (ViewType == Type::RTV) ? RTV.Get() : nullptr; }
        ID3D11DepthStencilView* AsDSV() const { return (ViewType == Type::DSV) ? DSV.Get() : nullptr; }
        ID3D11ShaderResourceView* AsSRV() const { return (ViewType == Type::SRV) ? SRV.Get() : nullptr; }

    private:
        enum class Type { RTV, DSV, SRV };
        Type ViewType;

        ComPtr<ID3D11RenderTargetView>     RTV;
        ComPtr<ID3D11DepthStencilView>     DSV;
        ComPtr<ID3D11ShaderResourceView>   SRV;
    };

    // ============================================================
    // DX11 Shader
    // ============================================================

    class DX11Shader : public RHIShader
    {
    public:
        DX11Shader(EShaderType type, ID3DBlob* blob)
            : Type(type), Blob(blob) {}

        void* GetNativeHandle() const override { return Blob->GetBufferPointer(); }
        EShaderType GetType() const override { return Type; }

        ID3DBlob* GetBlob() const { return Blob.Get(); }

        // 获取具体的 DX11 shader 接口
        ID3D11VertexShader*   AsVertexShader()   const { return VertexShader.Get(); }
        ID3D11PixelShader*    AsPixelShader()    const { return PixelShader.Get(); }
        ID3D11GeometryShader* AsGeometryShader() const { return GeometryShader.Get(); }

        void SetD3D11Shader(ID3D11DeviceChild* shader)
        {
            if (Type == EShaderType::Vertex)
                VertexShader = static_cast<ID3D11VertexShader*>(shader);
            else if (Type == EShaderType::Pixel)
                PixelShader = static_cast<ID3D11PixelShader*>(shader);
            else if (Type == EShaderType::Geometry)
                GeometryShader = static_cast<ID3D11GeometryShader*>(shader);
        }

    private:
        EShaderType Type;
        ComPtr<ID3DBlob> Blob;

        ComPtr<ID3D11VertexShader>   VertexShader;
        ComPtr<ID3D11PixelShader>    PixelShader;
        ComPtr<ID3D11GeometryShader> GeometryShader;
    };

    // ============================================================
    // DX11 Input Layout
    // ============================================================

    class DX11InputLayout : public RHIInputLayout
    {
    public:
        DX11InputLayout(ID3D11InputLayout* layout)
            : Layout(layout) {}

        void* GetNativeHandle() const override { return Layout.Get(); }
        ID3D11InputLayout* GetD3DLayout() const { return Layout.Get(); }

    private:
        ComPtr<ID3D11InputLayout> Layout;
    };

    // ============================================================
    // DX11 Pipeline State (simplified for DX11)
    // ============================================================

    class DX11PipelineState : public RHIPipelineState
    {
    public:
        DX11PipelineState() = default;

        void SetBlendState(ID3D11BlendState* bs)    { BlendState = bs; }
        void SetRasterizerState(ID3D11RasterizerState* rs) { RasterizerState = rs; }
        void SetDepthStencilState(ID3D11DepthStencilState* ds) { DepthStencilState = ds; }

        ID3D11BlendState* GetBlendState() const { return BlendState.Get(); }
        ID3D11RasterizerState* GetRasterizerState() const { return RasterizerState.Get(); }
        ID3D11DepthStencilState* GetDepthStencilState() const { return DepthStencilState.Get(); }

        RasterizerStateDesc Rasterizer;

        void* GetNativeHandle() const override { return nullptr; } // DX11 没有 PSO 对象

    private:
        ComPtr<ID3D11BlendState>         BlendState;
        ComPtr<ID3D11RasterizerState>    RasterizerState;
        ComPtr<ID3D11DepthStencilState>  DepthStencilState;
    };

    // ============================================================
    // DX11 Sampler
    // ============================================================

    class DX11Sampler : public RHISampler
    {
    public:
        DX11Sampler(ID3D11SamplerState* sampler) : Sampler(sampler) {}
        void* GetNativeHandle() const override { return Sampler.Get(); }
        ID3D11SamplerState* GetD3DSampler() const { return Sampler.Get(); }

    private:
        ComPtr<ID3D11SamplerState> Sampler;
    };

} // namespace Kiwi
