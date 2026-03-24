#include "RHI/DX11/DX11Device.h"
#include <stdexcept>

namespace Kiwi
{

    // ============================================================
    // DX11SwapChain 实现
    // ============================================================

    DX11SwapChain::DX11SwapChain(IDXGISwapChain* swapChain,
                                 ID3D11Device* device,
                                 ID3D11DeviceContext* context,
                                 const SwapChainDesc& desc)
        : m_SwapChain(swapChain)
        , m_Device(device)
        , m_Context(context)
        , m_Desc(desc)
    {
        CreateRenderTargetViews();
    }

    DX11SwapChain::~DX11SwapChain()
    {
        m_BackBuffers.clear();
        m_RTVs.clear();
        // Release swap chain in full-screen mode
        if (m_SwapChain)
        {
            m_SwapChain->SetFullscreenState(FALSE, nullptr);
        }
    }

    void DX11SwapChain::CreateRenderTargetViews()
    {
        m_BackBuffers.clear();
        m_RTVs.clear();

        // DX11 DXGI_SWAP_EFFECT_DISCARD 模式下，只能通过 GetBuffer(0) 访问 back buffer
        // 不像 DX12 flip model 那样可以访问多个 buffer
        ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT hr = m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
        if (FAILED(hr))
        {
            char msg[256];
            snprintf(msg, sizeof(msg), "Failed to get swap chain back buffer (HRESULT: 0x%08X)", hr);
            throw std::runtime_error(msg);
        }

        // 创建 Texture 对象
        TextureDesc texDesc;
        texDesc.Width = m_Desc.Width;
        texDesc.Height = m_Desc.Height;
        texDesc.Format = m_Desc.Format;
        texDesc.BindFlags = BUFFER_USAGE_VERTEX; // render target
        texDesc.Usage = EResourceUsage::Default;
        m_BackBuffers.push_back(
            std::make_unique<DX11Texture>(backBuffer.Get(), texDesc));

        // 创建 RTV
        ComPtr<ID3D11RenderTargetView> rtv;
        hr = m_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, &rtv);
        if (FAILED(hr))
        {
            throw std::runtime_error("Failed to create render target view");
        }
        m_RTVs.push_back(
            std::make_unique<DX11TextureView>(rtv.Get()));
    }

    void DX11SwapChain::Present(uint32_t syncInterval)
    {
        m_SwapChain->Present(syncInterval, 0);
    }

    void DX11SwapChain::ResizeBuffers(uint32_t width, uint32_t height)
    {
        // 释放旧资源
        m_BackBuffers.clear();
        m_RTVs.clear();

        m_Context->OMSetRenderTargets(0, nullptr, nullptr);

        HRESULT hr = m_SwapChain->ResizeBuffers(
            m_Desc.BufferCount, width, height,
            ToDXGIFormat(m_Desc.Format), 0);
        if (FAILED(hr))
        {
            throw std::runtime_error("Failed to resize swap chain buffers");
        }

        m_Desc.Width = width;
        m_Desc.Height = height;

        CreateRenderTargetViews();
    }

    uint32_t DX11SwapChain::GetCurrentBackBufferIndex() const
    {
        // DX11 DXGI_SWAP_EFFECT_DISCARD 模式下始终只有一个可访问的 back buffer (index 0)
        return 0;
    }

    RHITexture* DX11SwapChain::GetBackBuffer(uint32_t index)
    {
        if (index >= m_BackBuffers.size()) return nullptr;
        return m_BackBuffers[index].get();
    }

    RHITextureView* DX11SwapChain::GetBackBufferRTV(uint32_t index)
    {
        if (index >= m_RTVs.size()) return nullptr;
        return m_RTVs[index].get();
    }

    // ============================================================
    // DX11Device 实现
    // ============================================================

    DX11Device::DX11Device(bool enableDebug)
        : m_EnableDebug(enableDebug)
    {
        UINT createDeviceFlags = 0;
        if (enableDebug)
        {
            createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
        }

        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        D3D_FEATURE_LEVEL featureLevel;
        HRESULT hr = D3D11CreateDevice(
            nullptr,                    // 默认适配器
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,                    // 无软件光栅器
            createDeviceFlags,
            featureLevels,
            _countof(featureLevels),
            D3D11_SDK_VERSION,
            &m_Device,
            &featureLevel,
            &m_Context);

        if (FAILED(hr))
        {
            // 如果 debug 层不可用，尝试不用 debug
            if (enableDebug)
            {
                hr = D3D11CreateDevice(
                    nullptr,
                    D3D_DRIVER_TYPE_HARDWARE,
                    nullptr,
                    0,
                    featureLevels,
                    _countof(featureLevels),
                    D3D11_SDK_VERSION,
                    &m_Device,
                    &featureLevel,
                    &m_Context);

                if (FAILED(hr))
                {
                    throw std::runtime_error("Failed to create D3D11 device");
                }
            }
            else
            {
                throw std::runtime_error("Failed to create D3D11 device");
            }
        }

        // 获取 DXGI 设备
        m_Device->QueryInterface(__uuidof(IDXGIDevice), (void**)&m_DXGIDevice);
        m_DXGIDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&m_Adapter);

        // 获取 debug 接口
        if (enableDebug)
        {
            m_Device->QueryInterface(__uuidof(ID3D11Debug), (void**)&m_Debug);
        }

        // 设置默认光栅化器状态
        D3D11_RASTERIZER_DESC rasterDesc = {};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_BACK;
        rasterDesc.FrontCounterClockwise = FALSE;
        rasterDesc.DepthClipEnable = TRUE;
        rasterDesc.ScissorEnable = FALSE;
        rasterDesc.MultisampleEnable = FALSE;
        rasterDesc.AntialiasedLineEnable = FALSE;

        ComPtr<ID3D11RasterizerState> rasterizerState;
        m_Device->CreateRasterizerState(&rasterDesc, &rasterizerState);
        m_Context->RSSetState(rasterizerState.Get());
    }

    DX11Device::~DX11Device()
    {
        // Report live objects if debug is enabled
        if (m_Debug)
        {
            m_Debug->ReportLiveDeviceObjects((D3D11_RLDO_FLAGS)1); // D3D11_RL_DETAIL = 1
        }
    }

    std::unique_ptr<RHISwapChain> DX11Device::CreateSwapChain(const SwapChainDesc& desc)
    {
        // 获取 DXGI Factory（需要通过 IDXGIAdapter 获取，不能直接从 IDXGIDevice 获取）
        ComPtr<IDXGIAdapter> adapter;
        HRESULT hr = m_DXGIDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&adapter);
        if (FAILED(hr) || !adapter)
        {
            throw std::runtime_error("Failed to get DXGI Adapter from DXGIDevice");
        }
        ComPtr<IDXGIFactory> factory;
        hr = adapter->GetParent(__uuidof(IDXGIFactory), (void**)&factory);
        if (FAILED(hr) || !factory)
        {
            throw std::runtime_error("Failed to get DXGI Factory from Adapter");
        }

        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = desc.BufferCount;
        sd.BufferDesc.Width = desc.Width;
        sd.BufferDesc.Height = desc.Height;
        sd.BufferDesc.Format = ToDXGIFormat(desc.Format);
        sd.BufferDesc.RefreshRate.Numerator = 0;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = (HWND)desc.WindowHandle;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.Windowed = desc.Windowed ? TRUE : FALSE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        ComPtr<IDXGISwapChain> swapChain;
        hr = factory->CreateSwapChain(
            m_Device.Get(), &sd, &swapChain);
        if (FAILED(hr))
        {
            char msg[256];
            snprintf(msg, sizeof(msg), "Failed to create DXGI SwapChain (HRESULT: 0x%08X)", hr);
            throw std::runtime_error(msg);
        }

        // 禁用 Alt+Enter 全屏切换（我们自己控制）
        factory->MakeWindowAssociation((HWND)desc.WindowHandle, DXGI_MWA_NO_ALT_ENTER);

        return std::make_unique<DX11SwapChain>(
            swapChain.Get(), m_Device.Get(), m_Context.Get(), desc);
    }

    std::unique_ptr<RHIBuffer> DX11Device::CreateBuffer(
        const BufferDesc& desc,
        const void* initialData)
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = desc.SizeInBytes;
        bd.Usage = ToDX11Usage(desc.Usage);
        bd.BindFlags = 0;
        bd.CPUAccessFlags = 0;
        bd.MiscFlags = 0;
        bd.StructureByteStride = desc.StructByteStride;

        if (desc.BindFlags & BUFFER_USAGE_VERTEX)      bd.BindFlags |= D3D11_BIND_VERTEX_BUFFER;
        if (desc.BindFlags & BUFFER_USAGE_INDEX)       bd.BindFlags |= D3D11_BIND_INDEX_BUFFER;
        if (desc.BindFlags & BUFFER_USAGE_CONSTANT)    bd.BindFlags |= D3D11_BIND_CONSTANT_BUFFER;
        if (desc.BindFlags & BUFFER_USAGE_UNORDERED)   bd.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

        if (desc.Usage == EResourceUsage::Dynamic)
        {
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        }
        else if (desc.Usage == EResourceUsage::Staging)
        {
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        }

        D3D11_SUBRESOURCE_DATA* initDataPtr = nullptr;
        D3D11_SUBRESOURCE_DATA initData = {};
        if (initialData)
        {
            initData.pSysMem = initialData;
            initData.SysMemPitch = 0;
            initData.SysMemSlicePitch = 0;
            initDataPtr = &initData;
        }

        ComPtr<ID3D11Buffer> buffer;
        HRESULT hr = m_Device->CreateBuffer(&bd, initDataPtr, &buffer);
        if (FAILED(hr))
        {
            throw std::runtime_error("Failed to create D3D11 buffer");
        }

        return std::make_unique<DX11Buffer>(buffer.Get(), m_Context.Get(), desc);
    }

    std::unique_ptr<RHITexture> DX11Device::CreateTexture(
        const TextureDesc& desc,
        const void* initialData)
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = desc.Width;
        td.Height = desc.Height;
        td.MipLevels = desc.MipLevels;
        td.ArraySize = desc.DepthOrArray;
        td.Format = ToDXGIFormat(desc.Format);
        td.SampleDesc.Count = desc.SampleCount;
        td.Usage = ToDX11Usage(desc.Usage);
        td.BindFlags = desc.BindFlags;
        td.CPUAccessFlags = 0;

        if (desc.Usage == EResourceUsage::Dynamic)
            td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        D3D11_SUBRESOURCE_DATA* initDataPtr = nullptr;
        D3D11_SUBRESOURCE_DATA initData = {};
        if (initialData)
        {
            initData.pSysMem = initialData;
            initDataPtr = &initData;
        }

        ComPtr<ID3D11Texture2D> texture;
        HRESULT hr = m_Device->CreateTexture2D(&td, initDataPtr, &texture);
        if (FAILED(hr))
        {
            throw std::runtime_error("Failed to create D3D11 texture");
        }

        return std::make_unique<DX11Texture>(texture.Get(), desc);
    }

    std::unique_ptr<RHITextureView> DX11Device::CreateTextureView(
        RHITexture* texture,
        EDescriptorHeapType heapType,
        EFormat format,
        int mipSlice,
        int arraySlice)
    {
        auto dxTexture = static_cast<DX11Texture*>(texture);
        ID3D11Resource* resource = dxTexture->GetD3DResource();
        DXGI_FORMAT dxgiFormat = (format == EFormat::Unknown)
            ? ToDXGIFormat(texture->GetDesc().Format)
            : ToDXGIFormat(format);

        switch (heapType)
        {
        case EDescriptorHeapType::RTV:
        {
            ComPtr<ID3D11RenderTargetView> rtv;
            if (FAILED(m_Device->CreateRenderTargetView(resource, nullptr, &rtv)))
                throw std::runtime_error("Failed to create RTV");
            return std::make_unique<DX11TextureView>(rtv.Get());
        }
        case EDescriptorHeapType::DSV:
        {
            ComPtr<ID3D11DepthStencilView> dsv;
            if (FAILED(m_Device->CreateDepthStencilView(resource, nullptr, &dsv)))
                throw std::runtime_error("Failed to create DSV");
            return std::make_unique<DX11TextureView>(dsv.Get());
        }
        case EDescriptorHeapType::CBV_SRV_UAV:
        {
            ComPtr<ID3D11ShaderResourceView> srv;
            if (FAILED(m_Device->CreateShaderResourceView(resource, nullptr, &srv)))
                throw std::runtime_error("Failed to create SRV");
            return std::make_unique<DX11TextureView>(srv.Get());
        }
        default:
            throw std::runtime_error("Unsupported descriptor heap type");
        }
    }

    std::unique_ptr<RHIShader> DX11Device::CreateShader(
        EShaderType type,
        const void* byteCode,
        size_t byteCodeSize)
    {
        // 创建一个真正的 ID3DBlob 并复制字节码进去
        // 不能把原始字节指针强转为 ID3DBlob*，那样会导致 AddRef 崩溃
        ComPtr<ID3DBlob> blob;
        HRESULT blobHr = D3DCreateBlob(byteCodeSize, &blob);
        if (FAILED(blobHr))
        {
            throw std::runtime_error("Failed to create shader blob");
        }
        memcpy(blob->GetBufferPointer(), byteCode, byteCodeSize);

        auto shader = std::make_unique<DX11Shader>(type, blob.Get());

        HRESULT hr = S_OK;
        switch (type)
        {
        case EShaderType::Vertex:
        {
            ComPtr<ID3D11VertexShader> vs;
            hr = m_Device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &vs);
            shader->SetD3D11Shader(vs.Get());
            break;
        }
        case EShaderType::Pixel:
        {
            ComPtr<ID3D11PixelShader> ps;
            hr = m_Device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &ps);
            shader->SetD3D11Shader(ps.Get());
            break;
        }
        case EShaderType::Geometry:
        {
            ComPtr<ID3D11GeometryShader> gs;
            hr = m_Device->CreateGeometryShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &gs);
            shader->SetD3D11Shader(gs.Get());
            break;
        }
        default:
            throw std::runtime_error("Unsupported shader type");
        }

        if (FAILED(hr))
        {
            throw std::runtime_error("Failed to create D3D11 shader");
        }

        return shader;
    }

    std::unique_ptr<RHIShader> DX11Device::CompileShader(
        EShaderType type,
        const char* hlslSource,
        const char* entryPoint,
        const char* shaderModel,
        const ShaderMacro* macros,
        uint32_t macroCount)
    {
        ComPtr<ID3DBlob> shaderBlob;
        ComPtr<ID3DBlob> errorBlob;

        D3D_SHADER_MACRO* dxMacros = nullptr;
        std::vector<D3D_SHADER_MACRO> dxMacroVec;
        if (macros && macroCount > 0)
        {
            dxMacroVec.reserve(macroCount + 1);
            for (uint32_t i = 0; i < macroCount; i++)
            {
                dxMacroVec.push_back({ macros[i].Name, macros[i].Definition });
            }
            dxMacroVec.push_back({ nullptr, nullptr });
            dxMacros = dxMacroVec.data();
        }

        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        HRESULT hr = D3DCompile(
            hlslSource,
            strlen(hlslSource),
            nullptr,
            dxMacros,
            nullptr,
            entryPoint,
            shaderModel,
            flags,
            0,
            &shaderBlob,
            &errorBlob);

        if (FAILED(hr))
        {
            std::string errorMsg = "Shader compilation failed: ";
            if (errorBlob)
            {
                errorMsg += (const char*)errorBlob->GetBufferPointer();
            }
            throw std::runtime_error(errorMsg);
        }

        return CreateShader(type, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());
    }

    std::unique_ptr<RHIInputLayout> DX11Device::CreateInputLayout(
        const InputElementDesc* elements,
        uint32_t elementCount,
        RHIShader* vertexShader)
    {
        std::vector<D3D11_INPUT_ELEMENT_DESC> dxElements(elementCount);
        for (uint32_t i = 0; i < elementCount; i++)
        {
            dxElements[i].SemanticName = elements[i].SemanticName;
            dxElements[i].SemanticIndex = elements[i].SemanticIndex;
            dxElements[i].Format = ToDXGIFormat(elements[i].Format);
            dxElements[i].AlignedByteOffset = elements[i].AlignedByteOffset;
            dxElements[i].InputSlot = elements[i].InputSlot;
            dxElements[i].InputSlotClass = (elements[i].InstanceDataStepRate > 0)
                ? D3D11_INPUT_PER_INSTANCE_DATA
                : D3D11_INPUT_PER_VERTEX_DATA;
            dxElements[i].InstanceDataStepRate = elements[i].InstanceDataStepRate;
        }

        auto dxVS = static_cast<DX11Shader*>(vertexShader);
        ID3DBlob* vsBlob = dxVS->GetBlob();

        ComPtr<ID3D11InputLayout> layout;
        HRESULT hr = m_Device->CreateInputLayout(
            dxElements.data(), elementCount,
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
            &layout);

        if (FAILED(hr))
        {
            throw std::runtime_error("Failed to create input layout");
        }

        return std::make_unique<DX11InputLayout>(layout.Get());
    }

    std::unique_ptr<RHIPipelineState> DX11Device::CreatePipelineState()
    {
        auto pso = std::make_unique<DX11PipelineState>();

        // 创建默认 blend state
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.AlphaToCoverageEnable = FALSE;
        blendDesc.IndependentBlendEnable = FALSE;
        blendDesc.RenderTarget[0].BlendEnable = FALSE;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        ComPtr<ID3D11BlendState> blendState;
        m_Device->CreateBlendState(&blendDesc, &blendState);
        pso->SetBlendState(blendState.Get());

        // 创建默认 rasterizer state
        D3D11_RASTERIZER_DESC rasterDesc = {};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_BACK;
        rasterDesc.FrontCounterClockwise = FALSE;
        rasterDesc.DepthBias = 0;
        rasterDesc.DepthBiasClamp = 0.0f;
        rasterDesc.SlopeScaledDepthBias = 0.0f;
        rasterDesc.DepthClipEnable = TRUE;
        rasterDesc.ScissorEnable = FALSE;
        rasterDesc.MultisampleEnable = FALSE;
        rasterDesc.AntialiasedLineEnable = FALSE;

        ComPtr<ID3D11RasterizerState> rasterState;
        m_Device->CreateRasterizerState(&rasterDesc, &rasterState);
        pso->SetRasterizerState(rasterState.Get());

        // 创建默认 depth stencil state
        D3D11_DEPTH_STENCIL_DESC dsDesc = {};
        dsDesc.DepthEnable = TRUE;
        dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
        dsDesc.StencilEnable = FALSE;

        ComPtr<ID3D11DepthStencilState> dsState;
        m_Device->CreateDepthStencilState(&dsDesc, &dsState);
        pso->SetDepthStencilState(dsState.Get());

        return pso;
    }

    std::unique_ptr<RHISampler> DX11Device::CreateSampler()
    {
        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.MipLODBias = 0.0f;
        sampDesc.MaxAnisotropy = 1;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        sampDesc.MinLOD = 0.0f;
        sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

        ComPtr<ID3D11SamplerState> sampler;
        HRESULT hr = m_Device->CreateSamplerState(&sampDesc, &sampler);
        if (FAILED(hr))
        {
            throw std::runtime_error("Failed to create sampler state");
        }

        return std::make_unique<DX11Sampler>(sampler.Get());
    }

    // ============================================================
    // DX11CommandContext 实现
    // ============================================================

    DX11CommandContext::DX11CommandContext(ID3D11DeviceContext* context)
        : m_Context(context)
    {
    }

    DX11CommandContext::~DX11CommandContext()
    {
    }

    void DX11CommandContext::SetRenderTargets(
        RHITextureView** rtvs, uint32_t rtvCount,
        RHITextureView* dsv)
    {
        std::vector<ID3D11RenderTargetView*> d3dRTVs(rtvCount);
        for (uint32_t i = 0; i < rtvCount; i++)
        {
            auto dxView = static_cast<DX11TextureView*>(rtvs[i]);
            d3dRTVs[i] = dxView->AsRTV();
        }

        ID3D11DepthStencilView* d3dDSV = nullptr;
        if (dsv)
        {
            auto dxDSV = static_cast<DX11TextureView*>(dsv);
            d3dDSV = dxDSV->AsDSV();
        }

        m_Context->OMSetRenderTargets(rtvCount, d3dRTVs.data(), d3dDSV);
    }

    void DX11CommandContext::ClearRenderTargetView(RHITextureView* rtv, const ClearColorValue& color)
    {
        auto dxView = static_cast<DX11TextureView*>(rtv);
        float clearColor[4] = { color.R, color.G, color.B, color.A };
        m_Context->ClearRenderTargetView(dxView->AsRTV(), clearColor);
    }

    void DX11CommandContext::ClearDepthStencilView(
        RHITextureView* dsv,
        const ClearDepthStencilValue& value,
        uint8_t clearFlags)
    {
        auto dxView = static_cast<DX11TextureView*>(dsv);
        UINT flags = 0;
        if (clearFlags & 0x01) flags |= D3D11_CLEAR_DEPTH;
        if (clearFlags & 0x02) flags |= D3D11_CLEAR_STENCIL;
        m_Context->ClearDepthStencilView(dxView->AsDSV(), flags, value.Depth, value.Stencil);
    }

    void DX11CommandContext::SetPipelineState(RHIPipelineState* pso)
    {
        auto dxPSO = static_cast<DX11PipelineState*>(pso);

        if (dxPSO->GetBlendState())
            m_Context->OMSetBlendState(dxPSO->GetBlendState(), nullptr, 0xFFFFFFFF);

        if (dxPSO->GetRasterizerState())
            m_Context->RSSetState(dxPSO->GetRasterizerState());

        if (dxPSO->GetDepthStencilState())
            m_Context->OMSetDepthStencilState(dxPSO->GetDepthStencilState(), 0);
    }

    void DX11CommandContext::SetPrimitiveTopology(EPrimitiveTopology topology)
    {
        m_Context->IASetPrimitiveTopology(ToDX11Topology(topology));
    }

    void DX11CommandContext::SetVertexBuffers(
        uint32_t startSlot,
        RHIBuffer* const* buffers,
        const VertexBufferView* views,
        uint32_t count)
    {
        std::vector<ID3D11Buffer*> d3dBuffers(count);
        std::vector<UINT> strides(count);
        std::vector<UINT> offsets(count);

        for (uint32_t i = 0; i < count; i++)
        {
            d3dBuffers[i] = static_cast<DX11Buffer*>(buffers[i])->GetD3DBuffer();
            strides[i] = views[i].StrideInBytes;
            offsets[i] = 0;
        }

        m_Context->IASetVertexBuffers(startSlot, count, d3dBuffers.data(), strides.data(), offsets.data());
    }

    void DX11CommandContext::SetIndexBuffer(RHIBuffer* buffer, const IndexBufferView* view)
    {
        if (!buffer || !view)
        {
            m_Context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
            return;
        }

        auto dxBuffer = static_cast<DX11Buffer*>(buffer);
        DXGI_FORMAT format = ToDXGIFormat(view->Format);

        m_Context->IASetIndexBuffer(dxBuffer->GetD3DBuffer(), format, 0);
    }

    void DX11CommandContext::SetVertexShader(RHIShader* shader)
    {
        if (shader)
        {
            auto dxShader = static_cast<DX11Shader*>(shader);
            m_Context->VSSetShader(dxShader->AsVertexShader(), nullptr, 0);
        }
        else
        {
            m_Context->VSSetShader(nullptr, nullptr, 0);
        }
    }

    void DX11CommandContext::SetPixelShader(RHIShader* shader)
    {
        if (shader)
        {
            auto dxShader = static_cast<DX11Shader*>(shader);
            m_Context->PSSetShader(dxShader->AsPixelShader(), nullptr, 0);
        }
        else
        {
            m_Context->PSSetShader(nullptr, nullptr, 0);
        }
    }

    void DX11CommandContext::SetGeometryShader(RHIShader* shader)
    {
        if (shader)
        {
            auto dxShader = static_cast<DX11Shader*>(shader);
            m_Context->GSSetShader(dxShader->AsGeometryShader(), nullptr, 0);
        }
        else
        {
            m_Context->GSSetShader(nullptr, nullptr, 0);
        }
    }

    void DX11CommandContext::SetInputLayout(RHIInputLayout* layout)
    {
        if (layout)
        {
            auto dxLayout = static_cast<DX11InputLayout*>(layout);
            m_Context->IASetInputLayout(dxLayout->GetD3DLayout());
        }
        else
        {
            m_Context->IASetInputLayout(nullptr);
        }
    }

    void DX11CommandContext::SetConstantBuffer(uint32_t slot, RHIBuffer* buffer)
    {
        auto dxBuffer = static_cast<DX11Buffer*>(buffer);
        ID3D11Buffer* d3dBuffer = dxBuffer->GetD3DBuffer();

        // 绑定到所有 shader stages
        m_Context->VSSetConstantBuffers(slot, 1, &d3dBuffer);
        m_Context->PSSetConstantBuffers(slot, 1, &d3dBuffer);
        m_Context->GSSetConstantBuffers(slot, 1, &d3dBuffer);
    }

    void DX11CommandContext::SetSampler(uint32_t slot, RHISampler* sampler)
    {
        auto dxSampler = static_cast<DX11Sampler*>(sampler);
        ID3D11SamplerState* state = dxSampler->GetD3DSampler();
        m_Context->PSSetSamplers(slot, 1, &state);
    }

    void DX11CommandContext::SetViewports(const Viewport* viewports, uint32_t count)
    {
        std::vector<D3D11_VIEWPORT> d3dViewports(count);
        for (uint32_t i = 0; i < count; i++)
        {
            d3dViewports[i].TopLeftX = viewports[i].TopLeftX;
            d3dViewports[i].TopLeftY = viewports[i].TopLeftY;
            d3dViewports[i].Width = viewports[i].Width;
            d3dViewports[i].Height = viewports[i].Height;
            d3dViewports[i].MinDepth = viewports[i].MinDepth;
            d3dViewports[i].MaxDepth = viewports[i].MaxDepth;
        }
        m_Context->RSSetViewports(count, d3dViewports.data());
    }

    void DX11CommandContext::SetScissorRects(const ScissorRect* rects, uint32_t count)
    {
        std::vector<D3D11_RECT> d3dRects(count);
        for (uint32_t i = 0; i < count; i++)
        {
            d3dRects[i].left = rects[i].Left;
            d3dRects[i].top = rects[i].Top;
            d3dRects[i].right = rects[i].Right;
            d3dRects[i].bottom = rects[i].Bottom;
        }
        m_Context->RSSetScissorRects(count, d3dRects.data());
    }

    void DX11CommandContext::Draw(uint32_t vertexCount, uint32_t vertexStart)
    {
        m_Context->Draw(vertexCount, vertexStart);
    }

    void DX11CommandContext::DrawIndexed(uint32_t indexCount, uint32_t indexStart, int32_t vertexOffset)
    {
        m_Context->DrawIndexed(indexCount, indexStart, vertexOffset);
    }

    void DX11CommandContext::Flush()
    {
        m_Context->Flush();
    }

    // ============================================================
    // RHI Factory 实现
    // ============================================================

    void CreateRHI(
        const RHIInitParams& params,
        std::unique_ptr<RHIDevice>& outDevice,
        std::unique_ptr<RHICommandContext>& outContext)
    {
        switch (params.ApiType)
        {
        case RHI_API_TYPE::DX11:
        {
            auto device = std::make_unique<DX11Device>(params.EnableDebug);
            auto context = std::make_unique<DX11CommandContext>(device->GetD3DContext());

            outDevice = std::move(device);
            outContext = std::move(context);
            break;
        }
        case RHI_API_TYPE::DX12:
            throw std::runtime_error("DX12 RHI backend is not yet implemented");
        case RHI_API_TYPE::VULKAN:
            throw std::runtime_error("Vulkan RHI backend is not yet implemented");
        default:
            throw std::runtime_error("Unknown RHI API type");
        }
    }

} // namespace Kiwi
