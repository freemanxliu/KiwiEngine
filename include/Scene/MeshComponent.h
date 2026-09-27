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

        static constexpr uint32_t kInvalidGPUSceneId = 0xffffffffu;

        // Instance of a material asset. Parent is the .mat in the library.
        MaterialInstance Material;

        // Stable GPU Scene slots. Assigned when the mesh enters the scene.
        uint32_t PrimitiveId = kInvalidGPUSceneId;
        uint32_t InstanceId = kInvalidGPUSceneId;
        int32_t     SortOrder  = 0;                             // Render sort priority (higher = rendered first)
        ECullMode   CullMode   = ECullMode::Back;
    };

} // namespace Kiwi
