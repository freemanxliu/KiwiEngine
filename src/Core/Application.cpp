#include "Core/Application.h"
#include <iostream>

namespace Kiwi
{

    Application::Application(const WindowDesc& windowDesc, const RHIInitParams& rhiParams)
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
        OnResize(windowDesc.Width, windowDesc.Height);
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
        OnUpdate(m_DeltaTime);

        // 渲染
        OnRender();

        // 呈现
        m_SwapChain->Present(1); // VSync ON
    }

    void Application::OnResize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0) return;

        // 释放旧的深度缓冲
        m_DepthStencil.reset();
        m_DSV.reset();

        // Resize SwapChain
        if (m_SwapChain)
        {
            m_SwapChain->ResizeBuffers(width, height);
        }

        // 重建深度缓冲
        TextureDesc depthDesc;
        depthDesc.Width = width;
        depthDesc.Height = height;
        depthDesc.Format = EFormat::D24_UNORM_S8_UINT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        depthDesc.Usage = EResourceUsage::Default;
        depthDesc.SampleCount = 1;

        m_DepthStencil = m_Device->CreateTexture(depthDesc);

        m_DSV = m_Device->CreateTextureView(
            m_DepthStencil.get(),
            EDescriptorHeapType::DSV);

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

} // namespace Kiwi
