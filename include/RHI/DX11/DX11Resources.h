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
            : m_Buffer(buffer), m_Context(context), m_Desc(desc) {}

        void* GetNativeHandle() const override { return m_Buffer.Get(); }
        const BufferDesc& GetDesc() const override { return m_Desc; }

        void* Map(uint32_t /*subresource*/ = 0) override
        {
            D3D11_MAPPED_SUBRESOURCE mapped;
            HRESULT hr = m_Context->Map(m_Buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (FAILED(hr)) return nullptr;
            return mapped.pData;
        }

        void Unmap(uint32_t /*subresource*/ = 0) override
        {
            m_Context->Unmap(m_Buffer.Get(), 0);
        }

        void UpdateData(const void* data, uint32_t size, uint32_t offset = 0) override
        {
            m_Context->UpdateSubresource(m_Buffer.Get(), 0, nullptr, data, size, 0);
        }

        ID3D11Buffer* GetD3DBuffer() const { return m_Buffer.Get(); }

    private:
        ComPtr<ID3D11Buffer> m_Buffer;
        ComPtr<ID3D11DeviceContext> m_Context;
        BufferDesc m_Desc;
    };

    // ============================================================
    // DX11 Texture
    // ============================================================

    class DX11Texture : public RHITexture
    {
    public:
        DX11Texture(ID3D11Resource* resource, const TextureDesc& desc)
            : m_Texture(resource), m_Desc(desc) {}

        void* GetNativeHandle() const override { return m_Texture.Get(); }
        const TextureDesc& GetDesc() const override { return m_Desc; }

        ID3D11Resource* GetD3DResource() const { return m_Texture.Get(); }

    private:
        ComPtr<ID3D11Resource> m_Texture;
        TextureDesc m_Desc;
    };

    // ============================================================
    // DX11 Texture View
    // ============================================================

    class DX11TextureView : public RHITextureView
    {
    public:
        DX11TextureView(ID3D11RenderTargetView* rtv)
            : m_RTV(rtv), m_Type(Type::RTV) {}
        DX11TextureView(ID3D11DepthStencilView* dsv)
            : m_DSV(dsv), m_Type(Type::DSV) {}
        DX11TextureView(ID3D11ShaderResourceView* srv)
            : m_SRV(srv), m_Type(Type::SRV) {}

        void* GetNativeHandle() const override
        {
            switch (m_Type)
            {
            case Type::RTV: return m_RTV.Get();
            case Type::DSV: return m_DSV.Get();
            case Type::SRV: return m_SRV.Get();
            default: return nullptr;
            }
        }

        ID3D11RenderTargetView* AsRTV() const { return (m_Type == Type::RTV) ? m_RTV.Get() : nullptr; }
        ID3D11DepthStencilView* AsDSV() const { return (m_Type == Type::DSV) ? m_DSV.Get() : nullptr; }
        ID3D11ShaderResourceView* AsSRV() const { return (m_Type == Type::SRV) ? m_SRV.Get() : nullptr; }

    private:
        enum class Type { RTV, DSV, SRV };
        Type m_Type;

        ComPtr<ID3D11RenderTargetView>     m_RTV;
        ComPtr<ID3D11DepthStencilView>     m_DSV;
        ComPtr<ID3D11ShaderResourceView>   m_SRV;
    };

    // ============================================================
    // DX11 Shader
    // ============================================================

    class DX11Shader : public RHIShader
    {
    public:
        DX11Shader(EShaderType type, ID3DBlob* blob)
            : m_Type(type), m_Blob(blob) {}

        void* GetNativeHandle() const override { return m_Blob->GetBufferPointer(); }
        EShaderType GetType() const override { return m_Type; }

        ID3DBlob* GetBlob() const { return m_Blob.Get(); }

        // 获取具体的 DX11 shader 接口
        ID3D11VertexShader*   AsVertexShader()   const { return m_VertexShader.Get(); }
        ID3D11PixelShader*    AsPixelShader()    const { return m_PixelShader.Get(); }
        ID3D11GeometryShader* AsGeometryShader() const { return m_GeometryShader.Get(); }

        void SetD3D11Shader(ID3D11DeviceChild* shader)
        {
            if (m_Type == EShaderType::Vertex)
                m_VertexShader = static_cast<ID3D11VertexShader*>(shader);
            else if (m_Type == EShaderType::Pixel)
                m_PixelShader = static_cast<ID3D11PixelShader*>(shader);
            else if (m_Type == EShaderType::Geometry)
                m_GeometryShader = static_cast<ID3D11GeometryShader*>(shader);
        }

    private:
        EShaderType m_Type;
        ComPtr<ID3DBlob> m_Blob;

        ComPtr<ID3D11VertexShader>   m_VertexShader;
        ComPtr<ID3D11PixelShader>    m_PixelShader;
        ComPtr<ID3D11GeometryShader> m_GeometryShader;
    };

    // ============================================================
    // DX11 Input Layout
    // ============================================================

    class DX11InputLayout : public RHIInputLayout
    {
    public:
        DX11InputLayout(ID3D11InputLayout* layout)
            : m_Layout(layout) {}

        void* GetNativeHandle() const override { return m_Layout.Get(); }
        ID3D11InputLayout* GetD3DLayout() const { return m_Layout.Get(); }

    private:
        ComPtr<ID3D11InputLayout> m_Layout;
    };

    // ============================================================
    // DX11 Pipeline State (simplified for DX11)
    // ============================================================

    class DX11PipelineState : public RHIPipelineState
    {
    public:
        DX11PipelineState() = default;

        void SetBlendState(ID3D11BlendState* bs)    { m_BlendState = bs; }
        void SetRasterizerState(ID3D11RasterizerState* rs) { m_RasterizerState = rs; }
        void SetDepthStencilState(ID3D11DepthStencilState* ds) { m_DepthStencilState = ds; }

        ID3D11BlendState* GetBlendState() const { return m_BlendState.Get(); }
        ID3D11RasterizerState* GetRasterizerState() const { return m_RasterizerState.Get(); }
        ID3D11DepthStencilState* GetDepthStencilState() const { return m_DepthStencilState.Get(); }

        void* GetNativeHandle() const override { return nullptr; } // DX11 没有 PSO 对象

    private:
        ComPtr<ID3D11BlendState>         m_BlendState;
        ComPtr<ID3D11RasterizerState>    m_RasterizerState;
        ComPtr<ID3D11DepthStencilState>  m_DepthStencilState;
    };

    // ============================================================
    // DX11 Sampler
    // ============================================================

    class DX11Sampler : public RHISampler
    {
    public:
        DX11Sampler(ID3D11SamplerState* sampler) : m_Sampler(sampler) {}
        void* GetNativeHandle() const override { return m_Sampler.Get(); }
        ID3D11SamplerState* GetD3DSampler() const { return m_Sampler.Get(); }

    private:
        ComPtr<ID3D11SamplerState> m_Sampler;
    };

} // namespace Kiwi
