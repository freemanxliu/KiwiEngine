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
    // GPUScene — primitive data for the frame.
    //
    //   1. Collect PrimitiveUniformBuffer data
    //   2. Upload the constant buffer and the instancing structured buffer
    //   3. Build MeshBatch list (pass-agnostic; passes turn it into draw commands)
    // ============================================================

    class GPUScene
    {
    public:
        GPUScene() = default;
        ~GPUScene() = default;

        void Initialize(RHIDevice* device);
        void Release();

        // Per-frame update: collect data from scene + classify into batches
        // Call ONCE per frame, before any rendering pass.
        void Update(Scene& scene, MaterialLibrary& materialLibrary,
                    const std::vector<struct RenderItem>& renderList);

        // Upload all data to GPU (CB + StructuredBuffer)
        void UploadToGPU();

        // ---- Single draw path (CB offset binding to b1) ----
        void BindPrimitive(RHICommandContext* ctx, uint32_t gpuSceneIndex) const;

        // ---- Instanced draw path (StructuredBuffer SRV t8 + BatchUB b4) ----
        void BindForInstancing(RHICommandContext* ctx) const;
        void SetBatchStartIndex(RHICommandContext* ctx, uint32_t startIndex) const;

        // ---- Accessors ----
        std::vector<MeshBatch>& GetMeshBatches() { return m_MeshBatches; }
        const std::vector<MeshBatch>& GetMeshBatches() const { return m_MeshBatches; }
        uint32_t GetNumPrimitives() const { return m_NumPrimitives; }

        // Get the GPU Scene index for a given RenderList index
        uint32_t GetGPUSceneIndex(uint32_t renderListIndex) const
        {
            if (renderListIndex < m_RenderListToGPUScene.size())
                return m_RenderListToGPUScene[renderListIndex];
            return 0;
        }

    private:
        void BuildBatches(const std::vector<struct RenderItem>& renderList,
                          const std::vector<uint32_t>& primitiveRenderIndices,
                          MaterialLibrary& materialLibrary);

        // GPU resources
        std::unique_ptr<RHIBuffer> m_ConstantBuffer;        // CB for single-draw offset binding (b1)
        std::unique_ptr<RHIBuffer> m_StructuredBuffer;      // StructuredBuffer for instanced reads (t8)
        std::unique_ptr<RHITextureView> m_StructuredSRV;    // SRV for StructuredBuffer
        std::unique_ptr<RHIBuffer> m_BatchUB;               // BatchUB (b4): g_BatchStartIndex
        RHIDevice* m_Device = nullptr;

        // CPU data
        std::vector<PrimitiveUniformBuffer> m_PrimitiveData;
        uint32_t m_NumPrimitives = 0;

        // RenderList index → GPU Scene index mapping
        std::vector<uint32_t> m_RenderListToGPUScene;

        std::vector<MeshBatch> m_MeshBatches;

        bool m_Dirty = true;
    };

} // namespace Kiwi
