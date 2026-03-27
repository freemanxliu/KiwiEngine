#pragma once

#include "RHI/RHI.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <iostream>

namespace Kiwi
{

    // ============================================================
    // GPU Texture — holds texture + SRV for binding
    // ============================================================
    struct GPUTexture
    {
        std::unique_ptr<RHITexture> Texture;
        std::unique_ptr<RHITextureView> SRV;
        uint32_t Width = 0;
        uint32_t Height = 0;
        std::string Path;   // source file path
    };

    // ============================================================
    // TextureManager — loads images from disk, caches GPU textures
    // ============================================================
    class TextureManager
    {
    public:
        TextureManager() = default;

        // Initialize with RHI device and command context
        void Initialize(RHIDevice* device, RHICommandContext* ctx)
        {
            m_Device = device;
            m_Context = ctx;
            CreateDefaultTextures();
        }

        // Release all GPU resources (call before RHI switch)
        void ReleaseAll()
        {
            m_Textures.clear();
            m_WhiteTexture = nullptr;
            m_BlackTexture = nullptr;
            m_NormalTexture = nullptr;
        }

        // Load a texture from file (PNG, JPG, BMP, TGA, etc.)
        // Returns cached version if already loaded.
        GPUTexture* LoadTexture(const std::string& filePath);

        // Load an HDR texture from file (.hdr equirectangular)
        // Stores as R16G16B16A16_FLOAT for PBR IBL.
        GPUTexture* LoadHDRTexture(const std::string& filePath);

        // Get a loaded texture by path. Returns nullptr if not loaded.
        GPUTexture* GetTexture(const std::string& filePath) const
        {
            auto it = m_Textures.find(filePath);
            return (it != m_Textures.end()) ? it->second.get() : nullptr;
        }

        // Get default textures (always available)
        GPUTexture* GetWhiteTexture() const { return m_WhiteTexture; }
        GPUTexture* GetBlackTexture() const { return m_BlackTexture; }
        GPUTexture* GetDefaultNormalTexture() const { return m_NormalTexture; }

        // Get all loaded texture paths (for UI dropdown)
        std::vector<std::string> GetLoadedPaths() const
        {
            std::vector<std::string> paths;
            for (const auto& pair : m_Textures)
                paths.push_back(pair.first);
            return paths;
        }

    private:
        // Create 1x1 default textures (white, black, flat normal)
        void CreateDefaultTextures();

        // Create a GPU texture from raw RGBA8 pixel data
        GPUTexture* CreateFromRGBA(const std::string& name, const uint8_t* data,
                                    uint32_t width, uint32_t height);

        // Create a GPU texture from raw RGBA16F (half-float) pixel data
        GPUTexture* CreateFromFloat16(const std::string& name, const uint16_t* data,
                                       uint32_t width, uint32_t height);

        RHIDevice* m_Device = nullptr;
        RHICommandContext* m_Context = nullptr;

        std::unordered_map<std::string, std::unique_ptr<GPUTexture>> m_Textures;

        // Default textures
        GPUTexture* m_WhiteTexture = nullptr;   // 1x1 white (255,255,255,255)
        GPUTexture* m_BlackTexture = nullptr;   // 1x1 black (0,0,0,255)
        GPUTexture* m_NormalTexture = nullptr;  // 1x1 flat normal (128,128,255,255)
    };

} // namespace Kiwi
