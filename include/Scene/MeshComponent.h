#pragma once

#include "Scene/Component.h"
#include "Scene/Mesh.h"
#include "Scene/PrimitiveType.h"
#include "Scene/Material.h"
#include "RHI/RHITypes.h"
#include <cstdint>
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

        // Instance of a material asset. Parent is the .mat in the library.
        MaterialInstance Material;

        int32_t     SortOrder  = 0;                             // Render sort priority (higher = rendered first)
        ECullMode   CullMode   = ECullMode::Back;

        void MarkRenderTransformDirty() override
        {
            if (RegisteredScene)
                RegisteredScene->UpdatePrimitiveTransform(this);
        }

        void MarkRenderStateDirty() override
        {
            if (RegisteredScene)
                RegisteredScene->UpdatePrimitiveMaterial(this);
        }
    };

} // namespace Kiwi
