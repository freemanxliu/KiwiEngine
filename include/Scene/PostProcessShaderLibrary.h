#pragma once

#include "RHI/RHI.h"
#include "Scene/PostProcessShaders.h"
#include "Scene/GLShaders.h"
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

    // Compiled post-process shader (fullscreen VS + custom PS, no input layout)
    struct CompiledPostProcessShader
    {
        std::string Name;
        std::unique_ptr<RHIShader> VertexShader;  // Shared fullscreen VS
        std::unique_ptr<RHIShader> PixelShader;   // Custom PS
        std::unique_ptr<RHIPipelineState> PSO;     // DX12 PSO / DX11 wrapper
    };

    // PostProcessShaderLibrary: scans PostProcessShaders/ folder,
    // compiles all .hlsl files as post-process pixel shaders
    // (using the built-in fullscreen VS).
    class PostProcessShaderLibrary
    {
    public:
        PostProcessShaderLibrary() = default;

        // Initialize: compile built-in fullscreen VS, scan folder for PS files.
        void Initialize(const std::string& shaderDir, RHIDevice* device)
        {
            m_Shaders.clear();
            m_ShaderNames.clear();

            // Compile the shared fullscreen vertex shader
            bool isGL = (device->GetApiType() == RHI_API_TYPE::OPENGL);
            const char* vsSrc = isGL ? g_PostProcessVS_GLSL : g_PostProcessVS;
            m_FullscreenVS = device->CompileShader(
                EShaderType::Vertex, vsSrc, "VSMain", "vs_5_0");

            if (!m_FullscreenVS)
            {
                std::cerr << "[Kiwi] PostProcessShaderLibrary: Failed to compile fullscreen VS!" << std::endl;
                return;
            }

            // Scan folder for .hlsl post-process shaders
            ScanAndCompile(shaderDir, device);

            std::cout << "[Kiwi] PostProcessShaderLibrary: " << m_Shaders.size()
                      << " shader(s) loaded." << std::endl;
            for (auto& name : m_ShaderNames)
                std::cout << "  - " << name << std::endl;
        }

        void ReleaseAll()
        {
            m_Shaders.clear();
            m_ShaderNames.clear();
            m_FullscreenVS.reset();
        }

        CompiledPostProcessShader* GetShader(const std::string& name)
        {
            auto it = m_Shaders.find(name);
            if (it != m_Shaders.end())
                return it->second.get();
            return nullptr;
        }

        const std::vector<std::string>& GetShaderNames() const
        {
            return m_ShaderNames;
        }

        bool HasShader(const std::string& name) const
        {
            return m_Shaders.find(name) != m_Shaders.end();
        }

        RHIShader* GetFullscreenVS() const { return m_FullscreenVS.get(); }

    private:
        void ScanAndCompile(const std::string& shaderDir, RHIDevice* device)
        {
            namespace fs = std::filesystem;

            std::error_code ec;
            if (!fs::exists(shaderDir, ec) || !fs::is_directory(shaderDir, ec))
            {
                std::cout << "[Kiwi] PostProcessShaderLibrary: Directory not found: "
                          << shaderDir << std::endl;
                return;
            }

            for (const auto& entry : fs::directory_iterator(shaderDir, ec))
            {
                if (!entry.is_regular_file()) continue;
                auto ext = entry.path().extension().string();
                bool isShader = (ext == ".hlsl" || ext == ".HLSL" || ext == ".glsl" || ext == ".GLSL");
                if (!isShader) continue;

                std::string name = entry.path().stem().string();

                // Read file content
                std::ifstream file(entry.path());
                if (!file.is_open())
                {
                    std::cerr << "[Kiwi] PostProcessShaderLibrary: Failed to read: "
                              << entry.path() << std::endl;
                    continue;
                }
                std::string hlslSource((std::istreambuf_iterator<char>(file)),
                                        std::istreambuf_iterator<char>());
                file.close();

                // Compile PS
                auto shader = std::make_unique<CompiledPostProcessShader>();
                shader->Name = name;

                bool ok = CompilePostProcessPS(shader.get(), hlslSource, device);
                if (ok)
                {
                    m_ShaderNames.push_back(name);
                    m_Shaders[name] = std::move(shader);
                    std::cout << "[Kiwi] PostProcessShaderLibrary: Compiled '"
                              << name << "'" << std::endl;
                }
                else
                {
                    std::cerr << "[Kiwi] PostProcessShaderLibrary: Failed to compile '"
                              << name << "'" << std::endl;
                }
            }
        }

        bool CompilePostProcessPS(CompiledPostProcessShader* shader,
                                   const std::string& hlslSource,
                                   RHIDevice* device)
        {
            try
            {
                // PS is custom, VS is shared fullscreen
                shader->PixelShader = device->CompileShader(
                    EShaderType::Pixel, hlslSource.c_str(), "PSMain", "ps_5_0");

                if (!shader->PixelShader) return false;

                // Clone VS reference (compile a new VS instance for PSO creation)
                bool isGL = (device->GetApiType() == RHI_API_TYPE::OPENGL);
                const char* vsSrc = isGL ? g_PostProcessVS_GLSL : g_PostProcessVS;
                shader->VertexShader = device->CompileShader(
                    EShaderType::Vertex, vsSrc, "VSMain", "vs_5_0");

                if (!shader->VertexShader) return false;

                // Create PSO without input layout (fullscreen triangle uses SV_VertexID)
                shader->PSO = device->CreateGraphicsPipelineState(
                    shader->VertexShader.get(), shader->PixelShader.get(), nullptr);

                return shader->PSO != nullptr;
            }
            catch (const std::exception& e)
            {
                std::cerr << "[Kiwi] PostProcessShaderLibrary: Error for '"
                          << shader->Name << "': " << e.what() << std::endl;
                return false;
            }
        }

        std::unique_ptr<RHIShader> m_FullscreenVS;
        std::unordered_map<std::string, std::unique_ptr<CompiledPostProcessShader>> m_Shaders;
        std::vector<std::string> m_ShaderNames;
    };

} // namespace Kiwi
