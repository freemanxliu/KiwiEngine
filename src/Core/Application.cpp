#include "Core/Application.h"
#include "Core/EngineConfig.h"
#include <iostream>

namespace Kiwi
{

    Application::Application(const WindowDesc& windowDesc, const RHIInitParams& rhiParams)
        : RHIParams(rhiParams)
        , CurrentRHIType(rhiParams.ApiType)
    {
        // 创建窗口
        Window = std::make_unique<Kiwi::Window>(windowDesc);

        CreateDeviceAndSwapChain();
    }

    Application::~Application()
    {
        StopRenderingThreads();
        // 释放顺序：SwapChain -> Context -> Device（RAII 自动管理）
    }

    void Application::CreateDeviceAndSwapChain()
    {
        // 创建 RHI 设备
        CreateRHI(RHIParams, Device, Context);
        LockedDevice = std::make_unique<LockedRHIDevice>(Device.get());

        // 创建 SwapChain
        SwapChainDesc scDesc;
        scDesc.WindowHandle = Window->GetNativeHandle();
        scDesc.Width = Window->GetWidth();
        scDesc.Height = Window->GetHeight();
        scDesc.BufferCount = 2;
        scDesc.Format = EFormat::R8G8B8A8_UNORM;
        scDesc.Windowed = true;

        SwapChain = Device->CreateSwapChain(scDesc);
        SwapChainWidth = Window->GetWidth();
        SwapChainHeight = Window->GetHeight();

        // 创建深度缓冲。用窗口的像素尺寸，Retina 下它大于 WindowDesc 里的点尺寸。
        RecreateDepthStencil(Window->GetWidth(), Window->GetHeight());
    }

    EThreadingMode Application::ResolveThreadingMode() const
    {
        // The GL context is current on the main thread only.
        if (CurrentRHIType == RHI_API_TYPE::OPENGL)
            return EThreadingMode::SingleThreaded;

        std::string Mode = EngineConfig::Get().GetString("Rendering", "ThreadingMode", "RenderAndRHIThread");
        if (Mode == "SingleThreaded")
            return EThreadingMode::SingleThreaded;
        if (Mode == "RenderThread")
            return EThreadingMode::RenderThread;
        return EThreadingMode::RenderAndRHIThread;
    }

    void Application::StartRenderingThreads()
    {
        EThreadingMode Mode = ResolveThreadingMode();
        RenderingThread.Start(Mode, Context.get(), Device.get());
        std::cout << "[Kiwi] Threading mode: " << GetThreadingModeName(Mode) << std::endl;
    }

    void Application::StopRenderingThreads()
    {
        RenderingThread.Stop();
    }

    void Application::Run()
    {
        Window->Show();

        if (!Initialized)
        {
            OnInit();
            Initialized = true;
        }
        StartRenderingThreads();

        LastTime = Clock::now();

        while (!Window->ShouldClose())
        {
            // 检查是否有 pending RHI 切换
            if (PendingRHISwitch)
            {
                PendingRHISwitch = false;
                SwitchRHI(PendingRHIType);
            }

            Frame();
        }

        StopRenderingThreads();
    }

    void Application::Frame()
    {
        Window->PumpMessages();

        if (Window->ShouldClose())
            return;

        // The swap chain and depth buffer are only touched while the renderer is idle.
        if (Window->GetWidth() != SwapChainWidth || Window->GetHeight() != SwapChainHeight)
        {
            RenderingThread.FlushRenderingCommands();
            OnResize(Window->GetWidth(), Window->GetHeight());
        }

        // 计算 deltaTime
        auto now = Clock::now();
        DeltaTime = std::chrono::duration<float>(now - LastTime).count();
        LastTime = now;

        // 更新
        OnUpdate(DeltaTime);

        // 渲染：enqueue this frame, then wait until the render thread is at most one frame behind.
        OnRender();
        RenderingThread.FrameEndSync();
    }

    void Application::RecreateDepthStencil(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0) return;

        DepthStencil.reset();
        DSV.reset();
        DepthSRV.reset();

        TextureDesc depthDesc;
        depthDesc.Width = width;
        depthDesc.Height = height;
        depthDesc.Format = EFormat::R32_TYPELESS; // Typeless for DSV(D32_FLOAT) + SRV(R32_FLOAT) dual use
        depthDesc.Usage = EResourceUsage::Default;
        depthDesc.SampleCount = 1;
        depthDesc.BindFlags = TEXTURE_HINT_DEPTH_STENCIL | TEXTURE_BIND_SHADER_RESOURCE;
        depthDesc.DebugName = "MainDepthBuffer";

        DepthStencil = Device->CreateTexture(depthDesc);
        DSV = Device->CreateTextureView(DepthStencil.get(), EDescriptorHeapType::DSV, EFormat::D32_FLOAT);
        DepthSRV = Device->CreateTextureView(DepthStencil.get(), EDescriptorHeapType::CBV_SRV_UAV, EFormat::R32_FLOAT);
    }

    void Application::OnResize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0) return;

        // 释放旧的深度缓冲
        DepthStencil.reset();
        DSV.reset();
        DepthSRV.reset();

        // Resize SwapChain
        if (SwapChain)
        {
            SwapChain->ResizeBuffers(width, height);
        }
        SwapChainWidth = width;
        SwapChainHeight = height;

        // 重建深度缓冲。Viewports are set by the renderer every frame.
        RecreateDepthStencil(width, height);
    }

    void Application::SwitchRHI(RHI_API_TYPE newType)
    {
        if (newType == CurrentRHIType)
            return;

        auto rhiName = [](RHI_API_TYPE t) -> const char* {
            switch (t) {
            case RHI_API_TYPE::DX11:   return "Direct3D 11";
            case RHI_API_TYPE::DX12:   return "Direct3D 12";
            case RHI_API_TYPE::OPENGL: return "OpenGL";
            case RHI_API_TYPE::VULKAN: return "Vulkan";
            case RHI_API_TYPE::METAL:  return "Metal";
            default:                   return "Unknown";
            }
        };

        std::cout << "[Kiwi] Switching RHI from "
                  << rhiName(CurrentRHIType) << " to "
                  << rhiName(newType) << "..." << std::endl;

        // 1. 停止渲染线程，then let the subclass release its GPU resources
        StopRenderingThreads();
        OnRHIShutdown();

        // 2. 释放深度缓冲
        DepthStencil.reset();
        DSV.reset();
        DepthSRV.reset();

        // 3. 释放 SwapChain
        SwapChain.reset();

        // 4. 释放 Context 和 Device
        Context.reset();
        LockedDevice.reset();
        Device.reset();

        // 5. 创建新的 RHI、SwapChain 和深度缓冲
        CurrentRHIType = newType;
        RHIParams.ApiType = newType;
        CreateDeviceAndSwapChain();

        // 6. 通知子类重建 GPU 资源，then restart the rendering threads for the new backend
        OnRHIReady();
        StartRenderingThreads();

        std::cout << "[Kiwi] RHI switch complete! Now using "
                  << rhiName(newType) << std::endl;
    }

} // namespace Kiwi
