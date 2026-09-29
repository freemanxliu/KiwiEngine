#pragma once

#include "RHI/RHI.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
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
    // Thread-safe: the render thread loads material textures while the editor loads previews.
    // Returned pointers stay valid until ReleaseAll().
    // ============================================================
    class TextureManager
    {
    public:
        TextureManager() = default;

        void Initialize(RHIDevice* device)
        {
            std::lock_guard<std::recursive_mutex> Lock(Mutex);
            Device = device;
            CreateDefaultTextures();
        }

        // Release all GPU resources (call before RHI switch)
        void ReleaseAll()
        {
            std::lock_guard<std::recursive_mutex> Lock(Mutex);
            Textures.clear();
            WhiteTexture = nullptr;
            BlackTexture = nullptr;
            NormalTexture = nullptr;
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
            std::lock_guard<std::recursive_mutex> Lock(Mutex);
            auto it = Textures.find(filePath);
            return (it != Textures.end()) ? it->second.get() : nullptr;
        }

        // Get default textures (always available)
        GPUTexture* GetWhiteTexture() const { return WhiteTexture; }
        GPUTexture* GetBlackTexture() const { return BlackTexture; }
        GPUTexture* GetDefaultNormalTexture() const { return NormalTexture; }

        // Get all loaded texture paths (for UI dropdown)
        std::vector<std::string> GetLoadedPaths() const
        {
            std::lock_guard<std::recursive_mutex> Lock(Mutex);
            std::vector<std::string> paths;
            for (const auto& pair : Textures)
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

        mutable std::recursive_mutex Mutex;
        RHIDevice* Device = nullptr;

        std::unordered_map<std::string, std::unique_ptr<GPUTexture>> Textures;

        // Default textures
        GPUTexture* WhiteTexture = nullptr;   // 1x1 white (255,255,255,255)
        GPUTexture* BlackTexture = nullptr;   // 1x1 black (0,0,0,255)
        GPUTexture* NormalTexture = nullptr;  // 1x1 flat normal (128,128,255,255)
    };

} // namespace Kiwi
