#include "Scene/MaterialShaderCache.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace Kiwi
{

    static std::string ReadText(const std::string& path)
    {
        std::ifstream file(path);
        if (!file.is_open())
            return {};
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    static std::string ExpandCommon(const std::string& source, const std::string& templateDir)
    {
        const std::string include = "#include \"Common.hlsli\"";
        auto pos = source.find(include);
        if (pos == std::string::npos)
            return source;
        std::string common = ReadText(templateDir + "/../Common.hlsli");
        if (common.empty())
            return source;
        std::string out = source;
        out.replace(pos, include.size(), common);
        return out;
    }

    void MaterialShaderCache::Initialize(RHIDevice* device, RHIInputLayout* layout,
        const std::string& surfaceDir, const std::string& templateDir)
    {
        ReleaseAll();
        m_Device = device;
        m_Layout = layout;
        m_SurfaceDir = surfaceDir;
        m_TemplateDir = templateDir;

        namespace fs = std::filesystem;
        std::error_code ec;
        if (!device || !fs::is_directory(surfaceDir, ec))
            return;

        const bool metal = device->GetApiType() == RHI_API_TYPE::METAL;
        const char* ext = metal ? ".metal" : ".hlsl";
        for (const auto& entry : fs::directory_iterator(surfaceDir, ec))
        {
            if (!entry.is_regular_file())
                continue;
            if (entry.path().extension() == ext)
                m_SurfaceNames.push_back(entry.path().stem().string());
        }
        std::cout << "[Kiwi] MaterialShaderCache: " << m_SurfaceNames.size()
                  << " surface shader(s) in " << surfaceDir << std::endl;
    }

    static const char* PassName(EMaterialPass pass)
    {
        switch (pass)
        {
        case EMaterialPass::Depth: return "Depth";
        case EMaterialPass::GBuffer: return "GBuffer";
        case EMaterialPass::Forward: return "Forward";
        default: return "Unknown";
        }
    }

    void MaterialShaderCache::ReleaseAll()
    {
        m_Shaders.clear();
        m_ShaderMaps.clear();
        m_SurfaceNames.clear();
        m_Device = nullptr;
        m_Layout = nullptr;
        for (int pass = 0; pass < (int)EMaterialPass::Count; ++pass)
        {
            m_Shared[pass][0] = {};
            m_Shared[pass][1] = {};
            m_Fallback[pass] = {};
        }
    }

    MaterialPassShader* MaterialShaderCache::Get(const std::string& surfaceName, EMaterialPass pass)
    {
        if (!m_Device || surfaceName.empty() || pass == EMaterialPass::Depth)
            return nullptr;
        const std::string key = surfaceName + "|" + PassName(pass);
        auto it = m_Shaders.find(key);
        if (it != m_Shaders.end())
            return it->second.get();
        return Compile(surfaceName, pass);
    }

    void MaterialShaderCache::SetSharedShader(EMaterialPass pass, bool bInstanced, MeshPassShader shader)
    {
        m_Shared[(int)pass][bInstanced ? 1 : 0] = shader;
    }

    void MaterialShaderCache::SetFallback(EMaterialPass pass, MeshPassShader shader)
    {
        m_Fallback[(int)pass] = shader;
    }

    MeshPassShader MaterialShaderCache::ShaderOrFallback(const std::string& surfaceName, EMaterialPass pass)
    {
        if (MaterialPassShader* shader = Get(surfaceName, pass))
            return { shader->PSO.get(), shader->VertexShader.get(), shader->PixelShader.get() };
        return m_Fallback[(int)pass];
    }

    const MaterialShaderMap* MaterialShaderCache::GetShaderMap(const std::string& surfaceName)
    {
        const std::string surface = surfaceName.empty() ? std::string("DefaultSurface") : surfaceName;
        auto it = m_ShaderMaps.find(surface);
        if (it == m_ShaderMaps.end())
            it = m_ShaderMaps.emplace(surface, std::make_unique<MaterialShaderMap>()).first;

        MaterialShaderMap& map = *it->second;
        map.SurfaceShader = surface;
        map.Set(EMaterialPass::Depth, false, m_Shared[(int)EMaterialPass::Depth][0]);
        map.Set(EMaterialPass::Depth, true, m_Shared[(int)EMaterialPass::Depth][1]);
        map.Set(EMaterialPass::GBuffer, false, ShaderOrFallback(surface, EMaterialPass::GBuffer));
        // The instanced G-Buffer shader is the shared default-surface permutation.
        if (surface == "DefaultSurface")
            map.Set(EMaterialPass::GBuffer, true, m_Shared[(int)EMaterialPass::GBuffer][1]);
        else
            map.Set(EMaterialPass::GBuffer, true, {});
        map.Set(EMaterialPass::Forward, false, ShaderOrFallback(surface, EMaterialPass::Forward));
        map.Set(EMaterialPass::Forward, true, m_Shared[(int)EMaterialPass::Forward][1]);
        return &map;
    }

    MaterialPassShader* MaterialShaderCache::Compile(const std::string& surfaceName, EMaterialPass pass)
    {
        const bool metal = m_Device->GetApiType() == RHI_API_TYPE::METAL;
        const char* ext = metal ? ".metal" : ".hlsl";
        const char* passName = PassName(pass);

        std::string surface = ReadText(m_SurfaceDir + "/" + surfaceName + ext);
        std::string templ = ReadText(m_TemplateDir + "/" + passName + ext);
        if (surface.empty() || templ.empty())
        {
            std::cerr << "[Kiwi] Material shader missing: " << surfaceName << " " << passName << std::endl;
            return nullptr;
        }

        const std::string marker = "/*%SURFACE%*/";
        auto pos = templ.find(marker);
        if (pos == std::string::npos)
            return nullptr;
        templ.replace(pos, marker.size(), surface);
        if (!metal)
            templ = ExpandCommon(templ, m_TemplateDir);

        auto vs = m_Device->CompileShader(EShaderType::Vertex, templ.c_str(), "VSMain", "vs_5_0");
        auto ps = m_Device->CompileShader(EShaderType::Pixel, templ.c_str(), "PSMain", "ps_5_0");
        if (!vs || !ps)
            return nullptr;

        auto shader = std::make_unique<MaterialPassShader>();
        shader->VertexShader = std::move(vs);
        shader->PixelShader = std::move(ps);

        GraphicsPipelineStateInitializer init;
        init.VertexShader = shader->VertexShader.get();
        init.PixelShader = shader->PixelShader.get();
        init.VertexDeclaration = m_Layout;
        init.DepthEnabled = true;
        init.DepthWrite = true;
        init.RasterizerState = RasterizerStateDesc(ERasterizerFillMode::Solid, ECullMode::Back);
        init.DepthStencilTargetFormat = EFormat::D32_FLOAT;
        if (pass == EMaterialPass::GBuffer)
        {
            init.RenderTargetsEnabled = 4;
            for (uint32_t i = 0; i < 4; ++i)
                init.RenderTargetFormats[i] = EFormat::R8G8B8A8_UNORM;
        }
        else
        {
            init.RenderTargetsEnabled = 1;
            init.RenderTargetFormats[0] = EFormat::R8G8B8A8_UNORM;
        }
        shader->PSO = m_Device->CreateGraphicsPipelineState(init);
        if (!shader->PSO)
            return nullptr;

        const std::string key = surfaceName + "|" + PassName(pass);
        auto* raw = shader.get();
        m_Shaders[key] = std::move(shader);
        std::cout << "[Kiwi] Compiled material surface " << surfaceName << " (" << passName << ")" << std::endl;
        return raw;
    }

} // namespace Kiwi
