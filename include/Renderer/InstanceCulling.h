#pragma once

#include "RHI/RHI.h"
#include "RHI/UniformBuffer.h"
#include "Scene/Shaders.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Kiwi
{
    // Per-view list of instance ids for this frame's instanced draws (UE5 FInstanceCullingContext).
    // Pass processors append one run per merged draw; the draw reads its run through the offset in b4.
    class InstanceCullingContext
    {
    public:
        // Every mesh pass appends a full set of ids each frame, so the list is sized for several passes.
        static constexpr uint32_t MaxDrawInstanceIds = MAX_GPU_SCENE_PRIMITIVES * 4;

        void Initialize(RHIDevice* Device);

        // Start of frame. Offsets handed out before this are no longer valid.
        void Reset();

        // Returns the offset of the first id, or 0 with nothing appended when the list is full.
        uint32_t AppendDrawInstanceIds(const uint32_t* InstanceIds, uint32_t Count);

        // Uploads ids appended since the last upload. Earlier ids are unchanged, so draws already recorded stay valid.
        void Upload();

        // Draw instance id table (t10).
        void Bind(RHICommandContext* Ctx) const;
        void SetDrawInstanceOffset(RHICommandContext* Ctx, uint32_t Offset) const;

    private:
        std::unique_ptr<RHIBuffer> DrawInstanceBuffer;
        std::unique_ptr<RHITextureView> DrawInstanceSRV;
        TUniformBufferRef<DrawInstanceUniformBuffer> DrawOffsetCB;
        std::vector<DrawInstanceId> DrawInstanceIds;
        bool bDirty = false;
    };
}
