#pragma once

// RenderDoc In-App API integration for KiwiEngine
// Provides automatic attachment and frame capture functionality

#include <cstdint>

namespace Kiwi
{

    class RenderDocIntegration
    {
    public:
        // Singleton access
        static RenderDocIntegration& Get();

        // Initialize RenderDoc - call before creating any graphics device
        // Returns true if RenderDoc was found and loaded successfully
        bool Initialize();

        // Shutdown and release
        void Shutdown();

        // Check if RenderDoc is available and loaded
        bool IsAvailable() const { return m_RenderDocAPI != nullptr; }

        // Capture the next frame
        // Call this before the frame starts rendering
        void TriggerCapture();

        // Start/End frame capture manually
        void StartFrameCapture(void* device = nullptr, void* window = nullptr);
        void EndFrameCapture(void* device = nullptr, void* window = nullptr);

        // Check if currently capturing
        bool IsFrameCapturing() const;

        // Get number of captures taken
        uint32_t GetNumCaptures() const;

        // Launch RenderDoc replay UI for the last capture
        void LaunchReplayUI();

        // Set capture file path template
        void SetCaptureFilePathTemplate(const char* pathTemplate);

        // Get capture file path for a given index
        bool GetCapturePath(uint32_t index, char* pathBuffer, uint32_t pathBufferSize) const;

    private:
        RenderDocIntegration() = default;
        ~RenderDocIntegration() = default;
        RenderDocIntegration(const RenderDocIntegration&) = delete;
        RenderDocIntegration& operator=(const RenderDocIntegration&) = delete;

        void* m_RenderDocModule = nullptr;  // HMODULE
        void* m_RenderDocAPI = nullptr;     // RENDERDOC_API_1_6_0*
    };

} // namespace Kiwi
