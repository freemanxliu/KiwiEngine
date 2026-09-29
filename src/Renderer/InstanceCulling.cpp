#include "Renderer/InstanceCulling.h"
#include "RHI/RHICommandList.h"

namespace Kiwi
{

static_assert(sizeof(DrawInstanceId) == 16, "DrawInstanceId stride must match the shader");

void InstanceCullingContext::Initialize(RHIDevice* Device)
{
    BufferDesc Desc;
    Desc.BindFlags = BUFFER_USAGE_STRUCTURED;
    Desc.Usage = EResourceUsage::Default;
    Desc.DebugName = "DrawInstanceIds";
    Desc.StructByteStride = sizeof(DrawInstanceId);
    Desc.SizeInBytes = sizeof(DrawInstanceId) * MaxDrawInstanceIds;
    DrawInstanceBuffer = Device->CreateBuffer(Desc);
    if (DrawInstanceBuffer)
        DrawInstanceSRV = Device->CreateBufferSRV(DrawInstanceBuffer.get(), MaxDrawInstanceIds, sizeof(DrawInstanceId));

    DrawOffsetCB = TUniformBufferRef<DrawInstanceUniformBuffer>::CreateEmptyUniformBufferImmediate(Device, EUniformBufferUsage::SingleDraw, "DrawInstanceOffset");
    DrawInstanceIds.reserve(MaxDrawInstanceIds);
}

void InstanceCullingContext::Reset()
{
    DrawInstanceIds.clear();
    bDirty = false;
}

uint32_t InstanceCullingContext::AppendDrawInstanceIds(const uint32_t* InstanceIds, uint32_t Count)
{
    const uint32_t Offset = (uint32_t)DrawInstanceIds.size();
    if (Offset + Count > MaxDrawInstanceIds)
        return 0;
    for (uint32_t I = 0; I < Count; ++I)
    {
        DrawInstanceId Entry = {};
        Entry.Id = InstanceIds[I];
        DrawInstanceIds.push_back(Entry);
    }
    bDirty = true;
    return Offset;
}

void InstanceCullingContext::Upload()
{
    if (!bDirty || !DrawInstanceBuffer || DrawInstanceIds.empty())
        return;
    RHIUpdateBuffer(DrawInstanceBuffer.get(), DrawInstanceIds.data(), (uint32_t)(DrawInstanceIds.size() * sizeof(DrawInstanceId)), 0);
    bDirty = false;
}

void InstanceCullingContext::Bind(RHICommandContext* Ctx) const
{
    if (DrawInstanceSRV)
        Ctx->SetShaderResourceView(10, DrawInstanceSRV.get());
}

void InstanceCullingContext::SetDrawInstanceOffset(RHICommandContext* Ctx, uint32_t Offset) const
{
    if (!DrawOffsetCB)
        return;
    DrawOffsetCB.UpdateUniformBufferImmediate({ Offset, { 0, 0, 0 } });
    Ctx->SetConstantBuffer(4, DrawOffsetCB.GetReference());
}

} // namespace Kiwi
