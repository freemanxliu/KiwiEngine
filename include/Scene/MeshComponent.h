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

        // Material reference
        std::string MaterialName = "Default-Material";          // Material asset name (references a .mat file)
        int32_t     SortOrder  = 0;                             // Render sort priority (higher = rendered first)
    };

} // namespace Kiwi
