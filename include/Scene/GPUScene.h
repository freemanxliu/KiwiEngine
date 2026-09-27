#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <string>
#include "Scene/Shaders.h"
#include "Scene/MeshBatch.h"
#include "RHI/RHI.h"
#include "RHI/RHITypes.h"

namespace Kiwi
{
    class MeshComponent;
    class Scene;
    class MaterialLibrary;

    // ============================================================
    // RenderItem — references a visible MeshComponent after frustum culling
    // Used by both the rendering loop and GPUScene batch classification.
    // ============================================================
    struct RenderItem
    {
        size_t         ObjectIndex;   // Index into m_Scene.GetObjects()
        MeshComponent* MeshComp;      // The mesh component to render
        int32_t        SortOrder;     // Higher = rendered first
        float          DistToCamera;  // Squared distance from object center to camera

        // Batching sort keys (set during InitView)
        uint32_t       MeshID;        // Shared mesh pool ID (EPrimitiveType)
        const char*    MaterialName;  // Material name (for SRV batching)
        MeshBatchKey   BatchKey;
    };

    MeshBatchKey MakeMeshBatchKey(const RenderItem& item, MaterialLibrary& materialLibrary);

    // ============================================================
    // GPUScene — persistent primitive and instance tables.
    //
    // Ids stay valid after a mesh enters the scene. Only dirty slots are
    // uploaded. Draw commands look up InstanceId, then PrimitiveId.
    // ============================================================

    class GPUScene
    {
    public:
        GPUScene() = default;
        ~GPUScene() = default;

        void Initialize(RHIDevice* device);
        void Release();

        // Allocate stable ids and upload dirty primitive/instance slots.
        void Update(Scene& scene, MaterialLibrary& materialLibrary);

        // This frame's draw-instance-id list. Cleared by Update.
        uint32_t AppendDrawInstanceIds(const uint32_t* instanceIds, uint32_t count);
        void UploadDrawInstanceIds();

        void Bind(RHICommandContext* ctx) const;
        void SetDrawInstanceOffset(RHICommandContext* ctx, uint32_t offset) const;

        uint32_t GetNumPrimitives() const { return m_PrimitiveCount; }
        uint32_t GetNumInstances() const { return m_InstanceCount; }

    private:
        uint32_t AllocatePrimitive();
        uint32_t AllocateInstance();
        void FreePrimitive(uint32_t id);
        void FreeInstance(uint32_t id);
        void EnsureBuffers();
        void UploadSlot(RHIBuffer* buffer, const void* data, uint32_t stride, uint32_t index);

        RHIDevice* m_Device = nullptr;
        std::unique_ptr<RHIBuffer> m_PrimitiveBuffer;
        std::unique_ptr<RHITextureView> m_PrimitiveSRV;
        std::unique_ptr<RHIBuffer> m_InstanceBuffer;
        std::unique_ptr<RHITextureView> m_InstanceSRV;
        std::unique_ptr<RHIBuffer> m_DrawInstanceBuffer;
        std::unique_ptr<RHITextureView> m_DrawInstanceSRV;
        std::unique_ptr<RHIBuffer> m_DrawOffsetCB;

        std::vector<PrimitiveSceneData> m_Primitives;
        std::vector<InstanceSceneData> m_Instances;
        std::vector<uint8_t> m_PrimitiveAlive;
        std::vector<uint8_t> m_InstanceAlive;
        std::vector<uint32_t> m_FreePrimitives;
        std::vector<uint32_t> m_FreeInstances;
        uint32_t m_PrimitiveCount = 0;
        uint32_t m_InstanceCount = 0;

        std::vector<DrawInstanceId> m_DrawInstanceIds;
        bool m_DrawIdsDirty = false;
    };

} // namespace Kiwi
