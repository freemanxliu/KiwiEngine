#include "Scene/TextureManager.h"
#include "stb/stb_image.h"
#include <filesystem>
#include <cmath>
#include <cstdint>

// Float32 to Float16 conversion helper
static uint16_t FloatToHalf(float f)
{
    uint32_t x = *(uint32_t*)&f;
    uint32_t sign = (x >> 16) & 0x8000;
    int32_t exponent = ((x >> 23) & 0xFF) - 127 + 15;
    uint32_t mantissa = x & 0x7FFFFF;

    if (exponent <= 0) return (uint16_t)sign; // Underflow to 0
    if (exponent >= 31) return (uint16_t)(sign | 0x7C00); // Overflow to Inf
    return (uint16_t)(sign | (exponent << 10) | (mantissa >> 13));
}

namespace Kiwi
{

    GPUTexture* TextureManager::LoadTexture(const std::string& filePath)
    {
        // Check cache
        auto it = m_Textures.find(filePath);
        if (it != m_Textures.end())
            return it->second.get();

        // Load from disk
        int width, height, channels;
        stbi_set_flip_vertically_on_load(0); // Don't flip — UV convention matches DirectX
        unsigned char* pixels = stbi_load(filePath.c_str(), &width, &height, &channels, 4); // Force RGBA
        if (!pixels)
        {
            std::cerr << "[Kiwi] TextureManager: Failed to load: " << filePath
                      << " (" << stbi_failure_reason() << ")" << std::endl;
            return nullptr;
        }

        std::string name = std::filesystem::path(filePath).stem().string();
        GPUTexture* result = CreateFromRGBA(filePath, pixels, (uint32_t)width, (uint32_t)height);
        stbi_image_free(pixels);

        if (result)
        {
            result->Path = filePath;
            std::cout << "[Kiwi] TextureManager: Loaded '" << name
                      << "' (" << width << "x" << height << ")" << std::endl;
        }

        return result;
    }

    void TextureManager::CreateDefaultTextures()
    {
        // 1x1 White
        uint8_t white[] = { 255, 255, 255, 255 };
        m_WhiteTexture = CreateFromRGBA("__white", white, 1, 1);

        // 1x1 Black
        uint8_t black[] = { 0, 0, 0, 255 };
        m_BlackTexture = CreateFromRGBA("__black", black, 1, 1);

        // 1x1 Flat Normal (tangent space: 0,0,1 encoded as 128,128,255)
        uint8_t normal[] = { 128, 128, 255, 255 };
        m_NormalTexture = CreateFromRGBA("__normal", normal, 1, 1);
    }

    GPUTexture* TextureManager::CreateFromRGBA(const std::string& name, const uint8_t* data,
                                                uint32_t width, uint32_t height)
    {
        if (!m_Device) return nullptr;

        TextureDesc desc;
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.Format = EFormat::R8G8B8A8_UNORM;
        desc.BindFlags = TEXTURE_BIND_SHADER_RESOURCE;
        desc.Usage = EResourceUsage::Immutable;
        desc.DebugName = name.c_str();

        auto texture = m_Device->CreateTexture(desc, data);
        if (!texture)
        {
            std::cerr << "[Kiwi] TextureManager: Failed to create GPU texture for '" << name << "'" << std::endl;
            return nullptr;
        }

        auto srv = m_Device->CreateTextureView(texture.get(), EDescriptorHeapType::CBV_SRV_UAV);
        if (!srv)
        {
            std::cerr << "[Kiwi] TextureManager: Failed to create SRV for '" << name << "'" << std::endl;
            return nullptr;
        }

        auto gpuTex = std::make_unique<GPUTexture>();
        gpuTex->Texture = std::move(texture);
        gpuTex->SRV = std::move(srv);
        gpuTex->Width = width;
        gpuTex->Height = height;
        gpuTex->Path = name;

        GPUTexture* ptr = gpuTex.get();
        m_Textures[name] = std::move(gpuTex);
        return ptr;
    }

    GPUTexture* TextureManager::LoadHDRTexture(const std::string& filePath)
    {
        auto it = m_Textures.find(filePath);
        if (it != m_Textures.end())
            return it->second.get();

        int width, height, channels;
        stbi_set_flip_vertically_on_load(0);
        float* hdrPixels = stbi_loadf(filePath.c_str(), &width, &height, &channels, 4);
        if (!hdrPixels)
        {
            std::cerr << "[Kiwi] TextureManager: Failed to load HDR: " << filePath
                      << " (" << stbi_failure_reason() << ")" << std::endl;
            return nullptr;
        }

        // Convert float32 RGBA to float16 RGBA
        size_t pixelCount = (size_t)width * height;
        std::vector<uint16_t> halfData(pixelCount * 4);
        for (size_t i = 0; i < pixelCount * 4; i++)
        {
            halfData[i] = FloatToHalf(hdrPixels[i]);
        }
        stbi_image_free(hdrPixels);

        GPUTexture* result = CreateFromFloat16(filePath, halfData.data(), (uint32_t)width, (uint32_t)height);
        if (result)
        {
            result->Path = filePath;
            std::string name = std::filesystem::path(filePath).stem().string();
            std::cout << "[Kiwi] TextureManager: Loaded HDR '" << name
                      << "' (" << width << "x" << height << ")" << std::endl;
        }
        return result;
    }

    GPUTexture* TextureManager::CreateFromFloat16(const std::string& name, const uint16_t* data,
                                                   uint32_t width, uint32_t height)
    {
        if (!m_Device) return nullptr;

        TextureDesc desc;
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.Format = EFormat::R16G16B16A16_FLOAT;
        desc.BindFlags = TEXTURE_BIND_SHADER_RESOURCE;
        desc.Usage = EResourceUsage::Immutable;
        desc.DebugName = name.c_str();

        auto texture = m_Device->CreateTexture(desc, data);
        if (!texture)
        {
            std::cerr << "[Kiwi] TextureManager: Failed to create HDR GPU texture for '" << name << "'" << std::endl;
            return nullptr;
        }

        auto srv = m_Device->CreateTextureView(texture.get(), EDescriptorHeapType::CBV_SRV_UAV);
        if (!srv)
        {
            std::cerr << "[Kiwi] TextureManager: Failed to create SRV for HDR '" << name << "'" << std::endl;
            return nullptr;
        }

        auto gpuTex = std::make_unique<GPUTexture>();
        gpuTex->Texture = std::move(texture);
        gpuTex->SRV = std::move(srv);
        gpuTex->Width = width;
        gpuTex->Height = height;
        gpuTex->Path = name;

        GPUTexture* ptr = gpuTex.get();
        m_Textures[name] = std::move(gpuTex);
        return ptr;
    }

} // namespace Kiwi
