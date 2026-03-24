#pragma once

#include "RHI/RHI.h"
#include "Scene/Shaders.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>

namespace Kiwi
{

    // Compiled shader pair (VS + PS + PSO) for a single named shader
    struct CompiledShader
    {
        std::string Name;
        std::unique_ptr<RHIShader> VertexShader;
        std::unique_ptr<RHIShader> PixelShader;
        // Unified PSO — DX12 creates a full PSO, DX11 returns lightweight wrapper
        std::unique_ptr<RHIPipelineState> PSO;
    };

    // ShaderLibrary: scans a Shaders/ folder, compiles all .hlsl files,
    // and provides runtime lookup by name.
    class ShaderLibrary
    {
    public:
        ShaderLibrary() = default;

        // Initialize: scan folder, compile all shaders.
        // Call after RHI device is ready.
        void Initialize(const std::string& shaderDir, RHIDevice* device, RHIInputLayout* inputLayout)
        {
            m_Shaders.clear();
            m_ShaderNames.clear();

            // Always register the built-in default shader first (from Shaders.h)
            CompileBuiltinDefault(device, inputLayout);

            // Scan folder for .hlsl files
            ScanAndCompile(shaderDir, device, inputLayout);

            std::cout << "[Kiwi] ShaderLibrary: " << m_Shaders.size() << " shader(s) loaded." << std::endl;
            for (auto& name : m_ShaderNames)
                std::cout << "  - " << name << std::endl;
        }

        // Release all GPU resources (call before RHI switch)
        void ReleaseAll()
        {
            m_Shaders.clear();
            m_ShaderNames.clear();
        }

        // Get a compiled shader by name. Returns nullptr if not found.
        CompiledShader* GetShader(const std::string& name)
        {
            auto it = m_Shaders.find(name);
            if (it != m_Shaders.end())
                return it->second.get();
            return nullptr;
        }

        // Get the default shader
        CompiledShader* GetDefault()
        {
            return GetShader("Default");
        }

        // Get all shader names (for UI dropdown)
        const std::vector<std::string>& GetShaderNames() const
        {
            return m_ShaderNames;
        }

        // Check if a shader name exists
        bool HasShader(const std::string& name) const
        {
            return m_Shaders.find(name) != m_Shaders.end();
        }

    private:
        // Compile the built-in default shader from Shaders.h (always available even without files)
        void CompileBuiltinDefault(RHIDevice* device, RHIInputLayout* inputLayout)
        {
            auto shader = std::make_unique<CompiledShader>();
            shader->Name = "Default";

            // Use unified CompileShader — no need to know which backend
            shader->VertexShader = device->CompileShader(
                EShaderType::Vertex, g_VertexShaderHLSL, "main", "vs_5_0");
            shader->PixelShader = device->CompileShader(
                EShaderType::Pixel, g_PixelShaderHLSL, "main", "ps_5_0");

            // Create PSO through unified interface
            shader->PSO = device->CreateGraphicsPipelineState(
                shader->VertexShader.get(), shader->PixelShader.get(), inputLayout);

            m_ShaderNames.push_back("Default");
            m_Shaders["Default"] = std::move(shader);
        }

        // Scan a directory for .hlsl files and compile each one
        void ScanAndCompile(const std::string& shaderDir, RHIDevice* device, RHIInputLayout* inputLayout)
        {
            namespace fs = std::filesystem;

            std::error_code ec;
            if (!fs::exists(shaderDir, ec) || !fs::is_directory(shaderDir, ec))
            {
                std::cout << "[Kiwi] ShaderLibrary: Shader directory not found: " << shaderDir << std::endl;
                return;
            }

            for (const auto& entry : fs::directory_iterator(shaderDir, ec))
            {
                if (!entry.is_regular_file()) continue;
                auto ext = entry.path().extension().string();
                if (ext != ".hlsl" && ext != ".HLSL") continue;

                std::string name = entry.path().stem().string();

                // Skip "Default" from file — we already have the built-in
                if (name == "Default") continue;

                // Read file content
                std::ifstream file(entry.path());
                if (!file.is_open())
                {
                    std::cerr << "[Kiwi] ShaderLibrary: Failed to read: " << entry.path() << std::endl;
                    continue;
                }
                std::string hlslSource((std::istreambuf_iterator<char>(file)),
                                        std::istreambuf_iterator<char>());
                file.close();

                // Compile
                auto shader = std::make_unique<CompiledShader>();
                shader->Name = name;

                bool ok = CompileShaderFromSource(shader.get(), hlslSource, device, inputLayout);
                if (ok)
                {
                    m_ShaderNames.push_back(name);
                    m_Shaders[name] = std::move(shader);
                    std::cout << "[Kiwi] ShaderLibrary: Compiled '" << name << "'" << std::endl;
                }
                else
                {
                    std::cerr << "[Kiwi] ShaderLibrary: Failed to compile '" << name << "'" << std::endl;
                }
            }
        }

        bool CompileShaderFromSource(CompiledShader* shader, const std::string& hlslSource,
                                     RHIDevice* device, RHIInputLayout* inputLayout)
        {
            try
            {
                // Use unified CompileShader — works for any backend
                shader->VertexShader = device->CompileShader(
                    EShaderType::Vertex, hlslSource.c_str(), "VSMain", "vs_5_0");
                shader->PixelShader = device->CompileShader(
                    EShaderType::Pixel, hlslSource.c_str(), "PSMain", "ps_5_0");

                if (!shader->VertexShader || !shader->PixelShader) return false;

                // Create PSO through unified interface
                shader->PSO = device->CreateGraphicsPipelineState(
                    shader->VertexShader.get(), shader->PixelShader.get(), inputLayout);

                return shader->PSO != nullptr;
            }
            catch (const std::exception& e)
            {
                std::cerr << "[Kiwi] ShaderLibrary: Compile error for '" << shader->Name
                          << "': " << e.what() << std::endl;
                return false;
            }
        }

        std::unordered_map<std::string, std::unique_ptr<CompiledShader>> m_Shaders;
        std::vector<std::string> m_ShaderNames;
    };

} // namespace Kiwi
