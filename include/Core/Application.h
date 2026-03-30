#pragma once

#include "RHI/RHI.h"
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
        RHIDevice*         GetDevice() const { return m_Device.get(); }
        RHICommandContext* GetContext() const { return m_Context.get(); }
        RHISwapChain*      GetSwapChain() const { return m_SwapChain.get(); }
        RHITextureView*    GetDSV() const { return m_DSV.get(); }
        RHITextureView*    GetDepthSRV() const { return m_DepthSRV.get(); }
        RHITexture*        GetDepthTexture() const { return m_DepthStencil.get(); }
        Window*            GetWindow() const { return m_Window.get(); }

        // 获取当前 RHI 类型
        RHI_API_TYPE GetCurrentRHIType() const { return m_CurrentRHIType; }

        // 运行时切换 RHI
        void SwitchRHI(RHI_API_TYPE newType);

    protected:
        // 子类覆写
        virtual void OnInit() {}
        virtual void OnResize(uint32_t width, uint32_t height);
        virtual void OnUpdate(float deltaTime) {}
        virtual void OnRender() {}

        // RHI 切换前后的回调
        virtual void OnRHIShutdown() {}  // 在销毁旧 RHI 之前调用（释放 GPU 资源）
        virtual void OnRHIReady() {}     // 在创建新 RHI 之后调用（重建 GPU 资源）

        // 标记需要切换 RHI（在下一帧开头安全时刻执行）
        bool m_PendingRHISwitch = false;
        RHI_API_TYPE m_PendingRHIType = RHI_API_TYPE::DX11;

    private:
        void Frame();
        void RecreateDepthStencil(uint32_t width, uint32_t height);

        std::unique_ptr<Window>            m_Window;
        std::unique_ptr<RHIDevice>          m_Device;
        std::unique_ptr<RHICommandContext>  m_Context;
        std::unique_ptr<RHISwapChain>       m_SwapChain;

        // 深度缓冲
        std::unique_ptr<RHITexture>         m_DepthStencil;
        std::unique_ptr<RHITextureView>     m_DSV;
        std::unique_ptr<RHITextureView>     m_DepthSRV;

        bool m_Initialized = false;
        RHI_API_TYPE m_CurrentRHIType = RHI_API_TYPE::DX11;
        RHIInitParams m_RHIParams;

        // Timing
        using Clock = std::chrono::high_resolution_clock;
        Clock::time_point m_LastTime;
        float m_DeltaTime = 0.0f;
    };

} // namespace Kiwi
