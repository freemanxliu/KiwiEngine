#pragma once

#include "Scene/MeshBatch.h"
#include "RHI/RHI.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Kiwi
{

    struct MaterialPassShader
    {
        std::unique_ptr<RHIShader> VertexShader;
        std::unique_ptr<RHIShader> PixelShader;
        std::unique_ptr<RHIPipelineState> PSO;
    };

    // Compiles a surface function (EvaluateMaterial) into a pass shader.
    // Key is (surface name, pass). The surface only fills MaterialAttributes.
    class MaterialShaderCache
    {
    public:
        void Initialize(RHIDevice* device, RHIInputLayout* layout,
            const std::string& surfaceDir, const std::string& templateDir);
        void ReleaseAll();

        MaterialPassShader* Get(const std::string& surfaceName, EMaterialPass pass);
        const std::vector<std::string>& GetSurfaceNames() const { return m_SurfaceNames; }

        // Shared shaders used by every material (depth, and DefaultSurface instancing).
        void SetSharedShader(EMaterialPass pass, bool bInstanced, MeshPassShader shader);
        void SetFallback(EMaterialPass pass, MeshPassShader shader);

        // Shader map for one surface. Pointer stays valid until ReleaseAll.
        const MaterialShaderMap* GetShaderMap(const std::string& surfaceName);

    private:
        MaterialPassShader* Compile(const std::string& surfaceName, EMaterialPass pass);
        MeshPassShader ShaderOrFallback(const std::string& surfaceName, EMaterialPass pass);

        RHIDevice* m_Device = nullptr;
        RHIInputLayout* m_Layout = nullptr;
        std::string m_SurfaceDir;
        std::string m_TemplateDir;
        std::vector<std::string> m_SurfaceNames;
        std::unordered_map<std::string, std::unique_ptr<MaterialPassShader>> m_Shaders;
        std::unordered_map<std::string, std::unique_ptr<MaterialShaderMap>> m_ShaderMaps;
        MeshPassShader m_Shared[(int)EMaterialPass::Count][2]{};
        MeshPassShader m_Fallback[(int)EMaterialPass::Count]{};
    };

} // namespace Kiwi
