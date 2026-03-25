#pragma once

#include "Scene/Component.h"
#include "Scene/Mesh.h"
#include <string>

namespace Kiwi
{

    // ============================================================
    // MeshComponent — renders a mesh at the component's transform
    // ============================================================
    class MeshComponent : public Component
    {
    public:
        MeshComponent() = default;
        ~MeshComponent() override = default;

        MeshComponent(MeshComponent&&) = default;
        MeshComponent& operator=(MeshComponent&&) = default;

        EComponentType GetType() const override { return EComponentType::Mesh; }
        const char* GetTypeName() const override { return "MeshComponent"; }

        // Mesh data
        Mesh MeshData;

        // Rendering properties
        Vec4        Color      = { 0.8f, 0.8f, 0.8f, 1.0f }; // Object color
        std::string ShaderName = "Default";                     // Shader to use
        int32_t     SortOrder  = 0;                             // Render sort priority (higher = rendered first)

        // Material properties (PBR-lite)
        float       Roughness  = 0.5f;                          // Surface roughness [0, 1]
        float       Metallic   = 0.0f;                          // Metallic factor [0, 1]
    };

} // namespace Kiwi
