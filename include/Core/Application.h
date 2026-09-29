#pragma once

#include "RHI/RHI.h"
#include "RHI/LockedRHIDevice.h"
#include "Core/RenderingThread.h"
#include "Core/Window.h"
#include <memory>
#include <chrono>

namespace Kiwi
{

    class Application
    {
    public:
        Application(const WindowDesc& windowDesc, const RHIInitParams& rhiParams);
        virtual ~Application();

        // 启动主循环
        void Run();

        // 获取子系统
        // The device every thread except the RHI thread uses; calls take the RHI lock.
        RHIDevice*         GetDevice() const { return LockedDevice.get(); }
        // The render thread's command list. Records for the RHI thread, or forwards to the backend context.
        RHICommandList*    GetContext() { return &RenderingThread.GetCommandList(); }
        RHISwapChain*      GetSwapChain() const { return SwapChain.get(); }
        RHITextureView*    GetDSV() const { return DSV.get(); }
        RHITextureView*    GetDepthSRV() const { return DepthSRV.get(); }
        RHITexture*        GetDepthTexture() const { return DepthStencil.get(); }
        Window*            GetWindow() const { return Window.get(); }
        // The window may already report a new size mid-frame; render-side sizing must follow the swap chain, which only resizes after a flush.
        uint32_t           GetSwapChainWidth() const { return SwapChainWidth; }
        uint32_t           GetSwapChainHeight() const { return SwapChainHeight; }
        Kiwi::RenderingThread& GetRenderingThread() { return RenderingThread; }

        // 获取当前 RHI 类型
        RHI_API_TYPE GetCurrentRHIType() const { return CurrentRHIType; }

        // 运行时切换 RHI
        void SwitchRHI(RHI_API_TYPE newType);

    protected:
        // 子类覆写
        virtual void OnInit() {}
        // Game thread, after the rendering threads have been flushed.
        virtual void OnResize(uint32_t width, uint32_t height);
        virtual void OnUpdate(float deltaTime) {}
        // Game thread: snapshot the frame and enqueue its render command. Presenting is part of that command.
        virtual void OnRender() {}

        // RHI 切换前后的回调。Both run on the game thread with the rendering threads stopped.
        virtual void OnRHIShutdown() {}  // 在销毁旧 RHI 之前调用（释放 GPU 资源）
        virtual void OnRHIReady() {}     // 在创建新 RHI 之后调用（重建 GPU 资源）

        // 标记需要切换 RHI（在下一帧开头安全时刻执行）
        bool PendingRHISwitch = false;
        RHI_API_TYPE PendingRHIType = RHI_API_TYPE::DX11;

    private:
        void Frame();
        void CreateDeviceAndSwapChain();
        void RecreateDepthStencil(uint32_t width, uint32_t height);
        void StartRenderingThreads();
        void StopRenderingThreads();
        EThreadingMode ResolveThreadingMode() const;

        std::unique_ptr<Window>            Window;
        std::unique_ptr<RHIDevice>          Device;
        std::unique_ptr<LockedRHIDevice>    LockedDevice;
        std::unique_ptr<RHICommandContext>  Context;
        std::unique_ptr<RHISwapChain>       SwapChain;
        Kiwi::RenderingThread               RenderingThread;

        // 深度缓冲
        std::unique_ptr<RHITexture>         DepthStencil;
        std::unique_ptr<RHITextureView>     DSV;
        std::unique_ptr<RHITextureView>     DepthSRV;

        bool Initialized = false;
        RHI_API_TYPE CurrentRHIType = RHI_API_TYPE::DX11;
        RHIInitParams RHIParams;
        uint32_t SwapChainWidth = 0;
        uint32_t SwapChainHeight = 0;

        // Timing
        using Clock = std::chrono::high_resolution_clock;
        Clock::time_point LastTime;
        float DeltaTime = 0.0f;
    };

} // namespace Kiwi
