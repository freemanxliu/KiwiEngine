#pragma once

#include "Scene/Component.h"
#include <string>
#include <vector>

namespace Kiwi
{

    // ============================================================
    // PostProcessMaterial — a single full-screen shader pass
    // ============================================================
    struct PostProcessMaterial
    {
        std::string ShaderName;   // Name of the post-process shader (from PostProcessShaders/ folder)
        bool        Enabled = true;
        float       Intensity = 1.0f;  // Generic intensity parameter (shader-specific)
    };

    // ============================================================
    // PostProcessComponent — applies full-screen shader effects
    // Each material is a full-screen pass executed in order.
    // ============================================================
    class PostProcessComponent : public Component
    {
    public:
        PostProcessComponent() = default;
        ~PostProcessComponent() override = default;

        PostProcessComponent(PostProcessComponent&&) = default;
        PostProcessComponent& operator=(PostProcessComponent&&) = default;

        EComponentType GetType() const override { return EComponentType::PostProcess; }
        const char* GetTypeName() const override { return "PostProcessComponent"; }

        // Material list — executed in order (first to last)
        std::vector<PostProcessMaterial> Materials;

        // Add a material with the given shader name
        void AddMaterial(const std::string& shaderName, float intensity = 1.0f)
        {
            PostProcessMaterial mat;
            mat.ShaderName = shaderName;
            mat.Intensity = intensity;
            Materials.push_back(std::move(mat));
        }

        // Remove material at index
        void RemoveMaterial(size_t index)
        {
            if (index < Materials.size())
                Materials.erase(Materials.begin() + index);
        }
    };

} // namespace Kiwi
