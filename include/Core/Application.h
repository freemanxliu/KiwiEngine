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
        Window*            GetWindow() const { return m_Window.get(); }

    protected:
        // 子类覆写
        virtual void OnInit() {}
        virtual void OnResize(uint32_t width, uint32_t height);
        virtual void OnUpdate(float deltaTime) {}
        virtual void OnRender() {}

    private:
        void Frame();

        std::unique_ptr<Window>            m_Window;
        std::unique_ptr<RHIDevice>          m_Device;
        std::unique_ptr<RHICommandContext>  m_Context;
        std::unique_ptr<RHISwapChain>       m_SwapChain;

        // 深度缓冲
        std::unique_ptr<RHITexture>         m_DepthStencil;
        std::unique_ptr<RHITextureView>     m_DSV;

        bool m_Initialized = false;

        // Timing
        using Clock = std::chrono::high_resolution_clock;
        Clock::time_point m_LastTime;
        float m_DeltaTime = 0.0f;
    };

} // namespace Kiwi
