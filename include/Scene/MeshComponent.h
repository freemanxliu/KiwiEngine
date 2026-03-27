#pragma once

#include "Scene/Component.h"
#include "Scene/Mesh.h"
#include "Scene/PrimitiveType.h"
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

        // Primitive type used to generate this mesh (for serialization)
        EPrimitiveType PrimitiveType = EPrimitiveType::Cube;

        // Rendering properties
        Vec4        Color      = { 0.8f, 0.8f, 0.8f, 1.0f }; // Object color (fallback if no material)
        std::string ShaderName = "DefaultLit";                  // Shader to use (legacy, overridden by Material)
        std::string MaterialName = "Default-Material";          // Material asset reference
        int32_t     SortOrder  = 0;                             // Render sort priority (higher = rendered first)

        // Material properties (PBR-lite)
        float       Roughness  = 0.5f;                          // Surface roughness [0, 1]
        float       Metallic   = 0.0f;                          // Metallic factor [0, 1]

        // Texture paths (empty = use default solid color)
        std::string BaseColorTexture;                             // Albedo/diffuse texture path
        std::string NormalTexture;                                // Normal map texture path
        std::string MetallicRoughnessTexture;                     // Combined metallic(B) + roughness(G) map
    };

} // namespace Kiwi
