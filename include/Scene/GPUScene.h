#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>
#include "Scene/Shaders.h"
#include "RHI/RHI.h"
#include "RHI/RHITypes.h"

namespace Kiwi
{
    // ============================================================
    // GPUScene — GPU mirror of the render scene's primitive and instance tables (UE5 FGPUScene).
    //
    // RenderScene owns primitive lifetime and decides the ids. This class only
    // hands out slots, keeps a CPU copy and uploads the changed range once per
    // frame. Draw commands look up InstanceId, then PrimitiveId.
    // ============================================================

    class GPUScene
    {
    public:
        static constexpr uint32_t InvalidId = 0xffffffffu;

        void Initialize(RHIDevice* InDevice);
        void Release();

        // Returns InvalidId when the table is full.
        uint32_t AllocatePrimitive();
        uint32_t AllocateInstance();
        void FreePrimitive(uint32_t Id);
        void FreeInstance(uint32_t Id);

        // Writes the CPU copy and marks the slot dirty if the data changed. Nothing reaches the GPU until Upload().
        void UpdatePrimitive(uint32_t Id, const PrimitiveSceneData& Data);
        void UpdateInstance(uint32_t Id, const InstanceSceneData& Data);

        // One UpdateData per table, covering the lowest to highest dirty slot.
        void Upload();

        // Instance table (t8) and primitive table (t9).
        void Bind(RHICommandContext* Ctx) const;

        uint32_t GetNumPrimitives() const { return PrimitiveCount; }
        uint32_t GetNumInstances() const { return InstanceCount; }

    private:
        // Half-open range of slots changed since the last upload. Empty when Begin >= End.
        struct DirtyRange
        {
            uint32_t Begin = UINT32_MAX;
            uint32_t End = 0;

            void Add(uint32_t Id) { Begin = std::min(Begin, Id); End = std::max(End, Id + 1); }
            bool IsEmpty() const { return Begin >= End; }
            void Reset() { Begin = UINT32_MAX; End = 0; }
        };

        RHIDevice* Device = nullptr;
        std::unique_ptr<RHIBuffer> PrimitiveBuffer;
        std::unique_ptr<RHITextureView> PrimitiveSRV;
        std::unique_ptr<RHIBuffer> InstanceBuffer;
        std::unique_ptr<RHITextureView> InstanceSRV;

        std::vector<PrimitiveSceneData> Primitives;
        std::vector<InstanceSceneData> Instances;
        std::vector<uint8_t> PrimitiveAlive;
        std::vector<uint8_t> InstanceAlive;
        std::vector<uint32_t> FreePrimitives;
        std::vector<uint32_t> FreeInstances;
        uint32_t PrimitiveCount = 0;
        uint32_t InstanceCount = 0;
        DirtyRange DirtyPrimitives;
        DirtyRange DirtyInstances;
    };

} // namespace Kiwi
