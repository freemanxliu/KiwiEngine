#include "Core/Application.h"
#include <iostream>

namespace Kiwi
{

    Application::Application(const WindowDesc& windowDesc, const RHIInitParams& rhiParams)
        : m_RHIParams(rhiParams)
        , m_CurrentRHIType(rhiParams.ApiType)
    {
        // 创建窗口
        m_Window = std::make_unique<Window>(windowDesc);

        // 创建 RHI 设备
        CreateRHI(rhiParams, m_Device, m_Context);

        // 创建 SwapChain
        SwapChainDesc scDesc;
        scDesc.WindowHandle = m_Window->GetHWND();
        scDesc.Width = windowDesc.Width;
        scDesc.Height = windowDesc.Height;
        scDesc.BufferCount = 2;
        scDesc.Format = EFormat::R8G8B8A8_UNORM;
        scDesc.Windowed = true;

        m_SwapChain = m_Device->CreateSwapChain(scDesc);

        // 创建深度缓冲
        RecreateDepthStencil(windowDesc.Width, windowDesc.Height);
    }

    Application::~Application()
    {
        // 释放顺序：SwapChain -> Context -> Device（RAII 自动管理）
    }

    void Application::Run()
    {
        m_Window->Show();

        if (!m_Initialized)
        {
            OnInit();
            m_Initialized = true;
        }

        m_LastTime = Clock::now();

        while (!m_Window->ShouldClose())
        {
            // 检查是否有 pending RHI 切换
            if (m_PendingRHISwitch)
            {
                m_PendingRHISwitch = false;
                SwitchRHI(m_PendingRHIType);
            }

            Frame();
        }
    }

    void Application::Frame()
    {
        m_Window->PumpMessages();

        if (m_Window->ShouldClose())
            return;

        // 计算 deltaTime
        auto now = Clock::now();
        m_DeltaTime = std::chrono::duration<float>(now - m_LastTime).count();
        m_LastTime = now;

        // 更新
        PreUpdate(m_DeltaTime);

        OnUpdate(m_DeltaTime);

        PostUpdate(m_DeltaTime);
        
        // 渲染
        PreRender();

        OnRender();

        PostRender();

        // 呈现
        m_SwapChain->Present(1); // VSync ON
    }

    void Application::RecreateDepthStencil(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0) return;

        m_DepthStencil.reset();
        m_DSV.reset();
        m_DepthSRV.reset();

        TextureDesc depthDesc;
        depthDesc.Width = width;
        depthDesc.Height = height;
        depthDesc.Format = EFormat::R32_TYPELESS; // Typeless for DSV(D32_FLOAT) + SRV(R32_FLOAT) dual use
        depthDesc.Usage = EResourceUsage::Default;
        depthDesc.SampleCount = 1;
        depthDesc.BindFlags = TEXTURE_HINT_DEPTH_STENCIL | TEXTURE_BIND_SHADER_RESOURCE;
        depthDesc.DebugName = "MainDepthBuffer";

        m_DepthStencil = m_Device->CreateTexture(depthDesc);
        m_DSV = m_Device->CreateTextureView(m_DepthStencil.get(), EDescriptorHeapType::DSV, EFormat::D32_FLOAT);
        m_DepthSRV = m_Device->CreateTextureView(m_DepthStencil.get(), EDescriptorHeapType::CBV_SRV_UAV, EFormat::R32_FLOAT);
    }

    void Application::OnResize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0) return;

        // 释放旧的深度缓冲
        m_DepthStencil.reset();
        m_DSV.reset();
        m_DepthSRV.reset();

        // Resize SwapChain
        if (m_SwapChain)
        {
            m_SwapChain->ResizeBuffers(width, height);
        }

        // 重建深度缓冲
        RecreateDepthStencil(width, height);

        // 设置视口
        Viewport vp;
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = (float)width;
        vp.Height = (float)height;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;

        m_Context->SetViewports(&vp, 1);

        ScissorRect sr;
        sr.Left = 0;
        sr.Top = 0;
        sr.Right = (int32_t)width;
        sr.Bottom = (int32_t)height;

        m_Context->SetScissorRects(&sr, 1);
    }

    void Application::SwitchRHI(RHI_API_TYPE newType)
    {
        if (newType == m_CurrentRHIType)
            return;

        auto rhiName = [](RHI_API_TYPE t) -> const char* {
            switch (t) {
            case RHI_API_TYPE::DX11:   return "Direct3D 11";
            case RHI_API_TYPE::DX12:   return "Direct3D 12";
            case RHI_API_TYPE::OPENGL: return "OpenGL";
            case RHI_API_TYPE::VULKAN: return "Vulkan";
            default:                   return "Unknown";
            }
        };

        std::cout << "[Kiwi] Switching RHI from "
                  << rhiName(m_CurrentRHIType) << " to "
                  << rhiName(newType) << "..." << std::endl;

        // 1. 通知子类释放 GPU 资源
        OnRHIShutdown();

        // 2. 释放深度缓冲
        m_DepthStencil.reset();
        m_DSV.reset();
        m_DepthSRV.reset();

        // 3. 释放 SwapChain
        m_SwapChain.reset();

        // 4. 释放 Context 和 Device
        m_Context.reset();
        m_Device.reset();

        // 5. 创建新的 RHI
        m_CurrentRHIType = newType;
        m_RHIParams.ApiType = newType;

        CreateRHI(m_RHIParams, m_Device, m_Context);

        // 6. 创建新的 SwapChain
        SwapChainDesc scDesc;
        scDesc.WindowHandle = m_Window->GetHWND();
        scDesc.Width = m_Window->GetWidth();
        scDesc.Height = m_Window->GetHeight();
        scDesc.BufferCount = 2;
        scDesc.Format = EFormat::R8G8B8A8_UNORM;
        scDesc.Windowed = true;

        m_SwapChain = m_Device->CreateSwapChain(scDesc);

        // 7. 重建深度缓冲
        RecreateDepthStencil(m_Window->GetWidth(), m_Window->GetHeight());

        // 8. 设置视口
        Viewport vp;
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = (float)m_Window->GetWidth();
        vp.Height = (float)m_Window->GetHeight();
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        m_Context->SetViewports(&vp, 1);

        ScissorRect sr;
        sr.Left = 0;
        sr.Top = 0;
        sr.Right = (int32_t)m_Window->GetWidth();
        sr.Bottom = (int32_t)m_Window->GetHeight();
        m_Context->SetScissorRects(&sr, 1);

        // 9. 通知子类重建 GPU 资源
        OnRHIReady();

        std::cout << "[Kiwi] RHI switch complete! Now using "
                  << rhiName(newType) << std::endl;
    }

} // namespace Kiwi
