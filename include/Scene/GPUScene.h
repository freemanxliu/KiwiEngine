#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <string>
#include "Scene/Shaders.h"
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
    };

    // ============================================================
    // InstanceBatch — a group of objects sharing the same mesh + material
    // that can be drawn in one DrawIndexedInstanced call.
    // ============================================================
    struct InstanceBatch
    {
        uint32_t MeshID;            // SharedMeshPool ID (EPrimitiveType)
        std::string MaterialName;   // Material name (for texture binding)
        uint32_t StartIndex;        // Start index in GPU Scene StructuredBuffer
        uint32_t InstanceCount;     // Number of instances in this batch
        // Indices into the original RenderList (to access MeshComponent for texture binding)
        std::vector<uint32_t> RenderListIndices;
    };

    // ============================================================
    // SingleDrawItem — an object that can't be batched (unique mesh or
    // unique mesh+material combo with count == 1).
    // Drawn with ordinary DrawIndexed + CB offset binding.
    // ============================================================
    struct SingleDrawItem
    {
        uint32_t RenderListIndex;   // Index in m_RenderList
        uint32_t GPUSceneIndex;     // Index in GPU Scene Buffer (for CB offset bind)
    };

    // ============================================================
    // GPUScene — Unified GPU Scene Manager
    //
    // Responsibilities:
    //   1. Collect all primitive data from scene (ObjectUniformBuffer)
    //   2. Upload to GPU: CB (for single draws) + StructuredBuffer (for instanced draws)
    //   3. Build InstanceBatch[] and SingleDrawItem[] lists
    //   4. Provide Draw APIs for both paths
    //
    // The caller (DrawSceneMeshesDeferred) does:
    //   1. For each InstanceBatch: bind VB/IB, bind SRV textures, DrawIndexedInstanced
    //   2. For each SingleDrawItem: bind VB/IB, bind CB offset, bind textures, DrawIndexed
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
        const std::vector<InstanceBatch>& GetInstanceBatches() const { return m_Batches; }
        const std::vector<SingleDrawItem>& GetSingleDrawItems() const { return m_SingleDraws; }
        uint32_t GetNumPrimitives() const { return m_NumPrimitives; }

        // Get the GPU Scene index for a given RenderList index
        uint32_t GetGPUSceneIndex(uint32_t renderListIndex) const
        {
            if (renderListIndex < m_RenderListToGPUScene.size())
                return m_RenderListToGPUScene[renderListIndex];
            return 0;
        }

    private:
        void BuildBatches(const std::vector<struct RenderItem>& renderList);

        // GPU resources
        std::unique_ptr<RHIBuffer> m_ConstantBuffer;        // CB for single-draw offset binding (b1)
        std::unique_ptr<RHIBuffer> m_StructuredBuffer;      // StructuredBuffer for instanced reads (t8)
        std::unique_ptr<RHITextureView> m_StructuredSRV;    // SRV for StructuredBuffer
        std::unique_ptr<RHIBuffer> m_BatchUB;               // BatchUB (b4): g_BatchStartIndex
        RHIDevice* m_Device = nullptr;

        // CPU data
        std::vector<ObjectUniformBuffer> m_PrimitiveData;
        uint32_t m_NumPrimitives = 0;

        // RenderList index → GPU Scene index mapping
        std::vector<uint32_t> m_RenderListToGPUScene;

        // Classified draw lists
        std::vector<InstanceBatch> m_Batches;       // Instanced batches (count >= 2)
        std::vector<SingleDrawItem> m_SingleDraws;  // Single draws (count == 1 or unique mesh)

        bool m_Dirty = true;
    };

} // namespace Kiwi
