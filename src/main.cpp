#include "KiwiEngineApp.h"

// ============================================================
// Main
// ============================================================

int main()
{
    try
    {
        std::cout << "========================================" << std::endl;
        std::cout << "  Kiwi Engine - Scene Editor" << std::endl;
        std::cout << "  RHI: DX11 / DX12 / OpenGL / Vulkan / Metal" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << std::endl;

        // Load engine configuration
        auto& config = Kiwi::EngineConfig::Get();
        config.LoadDefaultConfig();

        // Initialize RenderDoc BEFORE creating any graphics device
        // Note: RenderDoc's Vulkan hook conflicts with NVIDIA drivers (nvoglv64.dll)
        // when both OpenGL and Vulkan are used in the same process.
        // Skip RenderDoc if default RHI is Vulkan.
        std::string defaultRHI = config.GetString("Rendering", "DefaultRHI", "DX11");
        bool isVulkanDefault = (defaultRHI == "VULKAN" || defaultRHI == "Vulkan" || defaultRHI == "vulkan");

        auto& rdoc = Kiwi::RenderDocIntegration::Get();
        if (!isVulkanDefault)
        {
            bool rdocAvailable = rdoc.Initialize();
            if (rdocAvailable)
            {
                std::cout << "[Kiwi] RenderDoc attached - frame capture available." << std::endl;
                std::cout << "[Kiwi] Note: Vulkan backend disabled when RenderDoc is active." << std::endl;
            }
        }
        else
        {
            std::cout << "[Kiwi] Vulkan default RHI - RenderDoc skipped (incompatible)." << std::endl;
        }
        std::cout << std::endl;

        KiwiEngineApp app;
        app.Run();

        // Shutdown RenderDoc
        rdoc.Shutdown();

        std::cout << std::endl;
        std::cout << "[Kiwi] Engine shutdown complete." << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Kiwi] Fatal Error: " << e.what() << std::endl;
        ShowErrorDialog("Kiwi Engine Error", e.what());
        return 1;
    }
}
