#pragma once

#include "RHI/RHI.h"
#include "RHI/DX11/DX11Device.h"
#include "RHI/DX12/DX12Device.h"
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

    // Compiled shader pair (VS + PS) for a single named shader
    struct CompiledShader
    {
        std::string Name;
        std::unique_ptr<RHIShader> VertexShader;
        std::unique_ptr<RHIShader> PixelShader;
        // DX12 needs separate PSO per shader
        std::unique_ptr<DX12PipelineState> DX12PSO;
    };

    // ShaderLibrary: scans a Shaders/ folder, compiles all .hlsl files,
    // and provides runtime lookup by name.
    class ShaderLibrary
    {
    public:
        ShaderLibrary() = default;

        // Initialize: scan folder, compile all shaders.
        // Call after RHI device is ready.
        // shaderDir: path to the Shaders/ folder (e.g. "Shaders" relative to exe, or absolute)
        // device: the active RHI device
        // apiType: current RHI backend
        // inputLayout: the shared input layout (needed for DX12 PSO creation)
        void Initialize(const std::string& shaderDir, RHIDevice* device, RHI_API_TYPE apiType,
                        RHIInputLayout* inputLayout)
        {
            m_Shaders.clear();
            m_ShaderNames.clear();
            m_ApiType = apiType;

            // Always register the built-in default shader first (from Shaders.h)
            CompileBuiltinDefault(device, apiType, inputLayout);

            // Scan folder for .hlsl files
            ScanAndCompile(shaderDir, device, apiType, inputLayout);

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
        void CompileBuiltinDefault(RHIDevice* device, RHI_API_TYPE apiType, RHIInputLayout* inputLayout)
        {
            auto shader = std::make_unique<CompiledShader>();
            shader->Name = "Default";

            if (apiType == RHI_API_TYPE::DX12)
            {
                auto dx12Device = static_cast<DX12Device*>(device);
                shader->VertexShader = dx12Device->CompileShader(
                    EShaderType::Vertex, g_VertexShaderHLSL, "main", "vs_5_0");
                shader->PixelShader = dx12Device->CompileShader(
                    EShaderType::Pixel, g_PixelShaderHLSL, "main", "ps_5_0");
                shader->DX12PSO = CreateDX12PSOForShader(dx12Device, shader.get(), inputLayout);
            }
            else
            {
                auto dx11Device = static_cast<DX11Device*>(device);
                shader->VertexShader = dx11Device->CompileShader(
                    EShaderType::Vertex, g_VertexShaderHLSL, "main", "vs_5_0");
                shader->PixelShader = dx11Device->CompileShader(
                    EShaderType::Pixel, g_PixelShaderHLSL, "main", "ps_5_0");
            }

            m_ShaderNames.push_back("Default");
            m_Shaders["Default"] = std::move(shader);
        }

        // Scan a directory for .hlsl files and compile each one
        void ScanAndCompile(const std::string& shaderDir, RHIDevice* device, RHI_API_TYPE apiType,
                            RHIInputLayout* inputLayout)
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
                // Case-insensitive .hlsl check
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

                bool ok = CompileShaderFromSource(shader.get(), hlslSource, device, apiType, inputLayout);
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
                                     RHIDevice* device, RHI_API_TYPE apiType, RHIInputLayout* inputLayout)
        {
            try
            {
                if (apiType == RHI_API_TYPE::DX12)
                {
                    auto dx12Device = static_cast<DX12Device*>(device);
                    shader->VertexShader = dx12Device->CompileShader(
                        EShaderType::Vertex, hlslSource.c_str(), "VSMain", "vs_5_0");
                    shader->PixelShader = dx12Device->CompileShader(
                        EShaderType::Pixel, hlslSource.c_str(), "PSMain", "ps_5_0");

                    if (!shader->VertexShader || !shader->PixelShader) return false;

                    shader->DX12PSO = CreateDX12PSOForShader(dx12Device, shader, inputLayout);
                    return shader->DX12PSO != nullptr;
                }
                else
                {
                    auto dx11Device = static_cast<DX11Device*>(device);
                    shader->VertexShader = dx11Device->CompileShader(
                        EShaderType::Vertex, hlslSource.c_str(), "VSMain", "vs_5_0");
                    shader->PixelShader = dx11Device->CompileShader(
                        EShaderType::Pixel, hlslSource.c_str(), "PSMain", "ps_5_0");

                    return shader->VertexShader && shader->PixelShader;
                }
            }
            catch (const std::exception& e)
            {
                std::cerr << "[Kiwi] ShaderLibrary: Compile error for '" << shader->Name
                          << "': " << e.what() << std::endl;
                return false;
            }
        }

        // Create a DX12 PSO for a given compiled shader
        std::unique_ptr<DX12PipelineState> CreateDX12PSOForShader(
            DX12Device* dx12Device, CompiledShader* shader, RHIInputLayout* inputLayout)
        {
            auto dx12InputLayout = static_cast<DX12InputLayout*>(inputLayout);
            auto vsShader = static_cast<DX12Shader*>(shader->VertexShader.get());
            auto psShader = static_cast<DX12Shader*>(shader->PixelShader.get());

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.pRootSignature = dx12Device->GetRootSignature();

            psoDesc.VS.pShaderBytecode = vsShader->GetBlob()->GetBufferPointer();
            psoDesc.VS.BytecodeLength = vsShader->GetBlob()->GetBufferSize();
            psoDesc.PS.pShaderBytecode = psShader->GetBlob()->GetBufferPointer();
            psoDesc.PS.BytecodeLength = psShader->GetBlob()->GetBufferSize();

            const auto& elements = dx12InputLayout->GetElements();
            psoDesc.InputLayout.pInputElementDescs = elements.data();
            psoDesc.InputLayout.NumElements = (UINT)elements.size();

            psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
            psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
            psoDesc.RasterizerState.DepthClipEnable = TRUE;

            psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
            psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

            psoDesc.DepthStencilState.DepthEnable = TRUE;
            psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
            psoDesc.DepthStencilState.StencilEnable = FALSE;

            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
            psoDesc.SampleDesc.Count = 1;

            ComPtr<ID3D12PipelineState> pso;
            HRESULT hr = dx12Device->GetD3DDevice()->CreateGraphicsPipelineState(
                &psoDesc, IID_PPV_ARGS(&pso));
            if (FAILED(hr))
            {
                std::cerr << "[Kiwi] Failed to create DX12 PSO for shader '"
                          << shader->Name << "' (HRESULT: 0x" << std::hex << hr << ")" << std::endl;
                return nullptr;
            }

            return std::make_unique<DX12PipelineState>(pso.Get());
        }

        std::unordered_map<std::string, std::unique_ptr<CompiledShader>> m_Shaders;
        std::vector<std::string> m_ShaderNames;
        RHI_API_TYPE m_ApiType = RHI_API_TYPE::DX11;
    };

} // namespace Kiwi
