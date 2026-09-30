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
    // PrimitiveComponent — renders a mesh at the component's transform
    // ============================================================
    class PrimitiveComponent : public Component
    {
    public:
        PrimitiveComponent() = default;
        ~PrimitiveComponent() override = default;

        PrimitiveComponent(PrimitiveComponent&&) = default;
        PrimitiveComponent& operator=(PrimitiveComponent&&) = default;

        EComponentType GetType() const override { return EComponentType::Primitive; }
        const char* GetTypeName() const override { return "PrimitiveComponent"; }

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
