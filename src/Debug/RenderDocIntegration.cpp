#include "Debug/RenderDocIntegration.h"

// Include the official RenderDoc In-App API header
#include "renderdoc_app.h"

#include <Windows.h>
#include <iostream>
#include <string>

namespace Kiwi
{

    RenderDocIntegration& RenderDocIntegration::Get()
    {
        static RenderDocIntegration instance;
        return instance;
    }

    bool RenderDocIntegration::Initialize()
    {
        if (m_RenderDocAPI)
        {
            std::cout << "[RenderDoc] Already initialized." << std::endl;
            return true;
        }

        // Try to find renderdoc.dll
        // 1. First check if it's already loaded (e.g., launched from RenderDoc)
        HMODULE rdocModule = GetModuleHandleA("renderdoc.dll");

        if (!rdocModule)
        {
            // 2. Try common install paths
            const char* searchPaths[] = {
                "C:\\Program Files\\RenderDoc\\renderdoc.dll",
                "C:\\Program Files (x86)\\RenderDoc\\renderdoc.dll",
                "renderdoc.dll",  // current directory or PATH
                nullptr
            };

            for (int i = 0; searchPaths[i] != nullptr; i++)
            {
                rdocModule = LoadLibraryA(searchPaths[i]);
                if (rdocModule)
                {
                    std::cout << "[RenderDoc] Loaded from: " << searchPaths[i] << std::endl;
                    break;
                }
            }
        }
        else
        {
            std::cout << "[RenderDoc] Already loaded in process (launched from RenderDoc UI)." << std::endl;
        }

        if (!rdocModule)
        {
            std::cout << "[RenderDoc] renderdoc.dll not found. RenderDoc integration disabled." << std::endl;
            std::cout << "[RenderDoc] Install RenderDoc from https://renderdoc.org to enable frame capture." << std::endl;
            return false;
        }

        m_RenderDocModule = rdocModule;

        // Get the API entry point
        pRENDERDOC_GetAPI RENDERDOC_GetAPI =
            (pRENDERDOC_GetAPI)GetProcAddress(rdocModule, "RENDERDOC_GetAPI");

        if (!RENDERDOC_GetAPI)
        {
            std::cerr << "[RenderDoc] Failed to get RENDERDOC_GetAPI entry point." << std::endl;
            return false;
        }

        // Request API version 1.6.0
        RENDERDOC_API_1_6_0* rdocAPI = nullptr;
        int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, (void**)&rdocAPI);
        if (ret != 1 || !rdocAPI)
        {
            std::cerr << "[RenderDoc] Failed to initialize RenderDoc API (version 1.6.0)." << std::endl;
            return false;
        }

        m_RenderDocAPI = rdocAPI;

        // Configure RenderDoc options
        rdocAPI->SetCaptureOptionU32(eRENDERDOC_Option_AllowVSync, 1);
        rdocAPI->SetCaptureOptionU32(eRENDERDOC_Option_AllowFullscreen, 1);
        rdocAPI->SetCaptureOptionU32(eRENDERDOC_Option_APIValidation, 1);
        rdocAPI->SetCaptureOptionU32(eRENDERDOC_Option_CaptureCallstacks, 0);
        rdocAPI->SetCaptureOptionU32(eRENDERDOC_Option_RefAllResources, 1);
        rdocAPI->SetCaptureOptionU32(eRENDERDOC_Option_SaveAllInitials, 1);

        // Disable RenderDoc's own overlay (we have our own ImGui button)
        rdocAPI->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);

        // Set default capture path
        rdocAPI->SetCaptureFilePathTemplate("captures/kiwi_frame");

        // Remove default capture key (F12) - we use our own button
        rdocAPI->SetCaptureKeys(nullptr, 0);

        std::cout << "[RenderDoc] Integration initialized successfully!" << std::endl;
        std::cout << "[RenderDoc] Captures will be saved to: captures/" << std::endl;

        return true;
    }

    void RenderDocIntegration::Shutdown()
    {
        m_RenderDocAPI = nullptr;
        // Don't FreeLibrary - RenderDoc manages its own lifecycle
        m_RenderDocModule = nullptr;
        std::cout << "[RenderDoc] Shutdown." << std::endl;
    }

    void RenderDocIntegration::TriggerCapture()
    {
        if (!m_RenderDocAPI) return;

        auto api = static_cast<RENDERDOC_API_1_6_0*>(m_RenderDocAPI);
        api->TriggerCapture();
        std::cout << "[RenderDoc] Frame capture triggered! (will capture next frame)" << std::endl;
    }

    void RenderDocIntegration::StartFrameCapture(void* device, void* window)
    {
        if (!m_RenderDocAPI) return;

        auto api = static_cast<RENDERDOC_API_1_6_0*>(m_RenderDocAPI);
        api->StartFrameCapture(device, window);
    }

    void RenderDocIntegration::EndFrameCapture(void* device, void* window)
    {
        if (!m_RenderDocAPI) return;

        auto api = static_cast<RENDERDOC_API_1_6_0*>(m_RenderDocAPI);
        api->EndFrameCapture(device, window);
    }

    bool RenderDocIntegration::IsFrameCapturing() const
    {
        if (!m_RenderDocAPI) return false;

        auto api = static_cast<RENDERDOC_API_1_6_0*>(m_RenderDocAPI);
        return api->IsFrameCapturing() != 0;
    }

    uint32_t RenderDocIntegration::GetNumCaptures() const
    {
        if (!m_RenderDocAPI) return 0;

        auto api = static_cast<RENDERDOC_API_1_6_0*>(m_RenderDocAPI);
        return api->GetNumCaptures();
    }

    void RenderDocIntegration::LaunchReplayUI()
    {
        if (!m_RenderDocAPI) return;

        auto api = static_cast<RENDERDOC_API_1_6_0*>(m_RenderDocAPI);

        uint32_t numCaptures = api->GetNumCaptures();
        if (numCaptures == 0)
        {
            std::cout << "[RenderDoc] No captures available to replay." << std::endl;
            return;
        }

        // Get the last capture path
        char path[512] = {};
        uint32_t pathLen = sizeof(path);
        uint64_t timestamp = 0;
        api->GetCapture(numCaptures - 1, path, &pathLen, &timestamp);

        std::cout << "[RenderDoc] Launching Replay UI for: " << path << std::endl;

        // Launch the replay UI
        uint32_t pid = api->LaunchReplayUI(1, nullptr);
        if (pid == 0)
        {
            std::cerr << "[RenderDoc] Failed to launch Replay UI." << std::endl;
        }
    }

    void RenderDocIntegration::SetCaptureFilePathTemplate(const char* pathTemplate)
    {
        if (!m_RenderDocAPI) return;

        auto api = static_cast<RENDERDOC_API_1_6_0*>(m_RenderDocAPI);
        api->SetCaptureFilePathTemplate(pathTemplate);
    }

    bool RenderDocIntegration::GetCapturePath(uint32_t index, char* pathBuffer, uint32_t pathBufferSize) const
    {
        if (!m_RenderDocAPI) return false;

        auto api = static_cast<RENDERDOC_API_1_6_0*>(m_RenderDocAPI);
        uint64_t timestamp = 0;
        uint32_t ret = api->GetCapture(index, pathBuffer, &pathBufferSize, &timestamp);
        return ret == 1;
    }

} // namespace Kiwi
