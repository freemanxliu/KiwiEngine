#include "RHI/RHI.h"

#include <iostream>
#include <stdexcept>

#if defined(_WIN32)
    #include "RHI/DX11/DX11Device.h"
#elif defined(__APPLE__)
    #include "RHI/Metal/MetalDevice.h"
#endif

namespace Kiwi
{

    void CreateRHI(
        const RHIInitParams& params,
        std::unique_ptr<RHIDevice>& outDevice,
        std::unique_ptr<RHICommandContext>& outContext)
    {
        switch (params.ApiType)
        {
        case RHI_API_TYPE::DX11:
#if defined(_WIN32)
        {
            auto device = std::make_unique<DX11Device>(params.EnableDebug);
            auto context = std::make_unique<DX11CommandContext>(device->GetD3DContext());
            outDevice = std::move(device);
            outContext = std::move(context);
            break;
        }
#else
            throw std::runtime_error("Direct3D 11 is only available on Windows");
#endif
        case RHI_API_TYPE::DX12:
#if defined(_WIN32)
        {
            extern void CreateDX12RHI(const RHIInitParams& params,
                std::unique_ptr<RHIDevice>& outDevice,
                std::unique_ptr<RHICommandContext>& outContext);
            CreateDX12RHI(params, outDevice, outContext);
            break;
        }
#else
            throw std::runtime_error("Direct3D 12 is only available on Windows");
#endif
        case RHI_API_TYPE::OPENGL:
#if defined(_WIN32)
        {
            extern void CreateGLRHI(const RHIInitParams& params,
                std::unique_ptr<RHIDevice>& outDevice,
                std::unique_ptr<RHICommandContext>& outContext);
            CreateGLRHI(params, outDevice, outContext);
            break;
        }
#else
            throw std::runtime_error("OpenGL is only available on Windows in this build");
#endif
        case RHI_API_TYPE::VULKAN:
#if defined(_WIN32)
        {
            extern void CreateVulkanRHI(const RHIInitParams& params,
                std::unique_ptr<RHIDevice>& outDevice,
                std::unique_ptr<RHICommandContext>& outContext);
            CreateVulkanRHI(params, outDevice, outContext);
            break;
        }
#else
            throw std::runtime_error("Vulkan is only available on Windows in this build");
#endif
        case RHI_API_TYPE::METAL:
#if defined(__APPLE__)
            CreateMetalRHI(params, outDevice, outContext);
            break;
#else
            throw std::runtime_error("Metal is only available on Apple platforms");
#endif
        default:
            throw std::runtime_error("Unknown RHI API type");
        }
    }

}
