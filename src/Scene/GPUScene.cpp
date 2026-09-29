#include "Scene/GPUScene.h"
#include "RHI/RHICommandList.h"
#include <cstring>

namespace Kiwi
{

    static_assert(sizeof(PrimitiveSceneData) == 80, "PrimitiveSceneData stride must match the shader");
    static_assert(sizeof(InstanceSceneData) == 80, "InstanceSceneData stride must match the shader");

    static std::unique_ptr<RHIBuffer> CreateStructured(RHIDevice* Device, const char* Name, uint32_t Stride, uint32_t Count)
    {
        BufferDesc Desc;
        Desc.BindFlags = BUFFER_USAGE_STRUCTURED;
        Desc.Usage = EResourceUsage::Default;
        Desc.DebugName = Name;
        Desc.StructByteStride = Stride;
        Desc.SizeInBytes = Stride * Count;
        return Device->CreateBuffer(Desc);
    }

    void GPUScene::Initialize(RHIDevice* InDevice)
    {
        Device = InDevice;
        Primitives.assign(MAX_GPU_SCENE_PRIMITIVES, {});
        Instances.assign(MAX_GPU_SCENE_PRIMITIVES, {});
        PrimitiveAlive.assign(MAX_GPU_SCENE_PRIMITIVES, 0);
        InstanceAlive.assign(MAX_GPU_SCENE_PRIMITIVES, 0);

        PrimitiveBuffer = CreateStructured(Device, "GPUScene_Primitives", sizeof(PrimitiveSceneData), MAX_GPU_SCENE_PRIMITIVES);
        if (PrimitiveBuffer)
            PrimitiveSRV = Device->CreateBufferSRV(PrimitiveBuffer.get(), MAX_GPU_SCENE_PRIMITIVES, sizeof(PrimitiveSceneData));
        InstanceBuffer = CreateStructured(Device, "GPUScene_Instances", sizeof(InstanceSceneData), MAX_GPU_SCENE_PRIMITIVES);
        if (InstanceBuffer)
            InstanceSRV = Device->CreateBufferSRV(InstanceBuffer.get(), MAX_GPU_SCENE_PRIMITIVES, sizeof(InstanceSceneData));
    }

    void GPUScene::Release()
    {
        PrimitiveBuffer.reset();
        PrimitiveSRV.reset();
        InstanceBuffer.reset();
        InstanceSRV.reset();
        Primitives.clear();
        Instances.clear();
        PrimitiveAlive.clear();
        InstanceAlive.clear();
        FreePrimitives.clear();
        FreeInstances.clear();
        PrimitiveCount = 0;
        InstanceCount = 0;
        DirtyPrimitives.Reset();
        DirtyInstances.Reset();
        Device = nullptr;
    }

    static uint32_t AllocateSlot(std::vector<uint32_t>& FreeList, std::vector<uint8_t>& Alive, uint32_t& Count)
    {
        uint32_t Id;
        if (!FreeList.empty())
        {
            Id = FreeList.back();
            FreeList.pop_back();
        }
        else
        {
            if (Count >= Alive.size())
                return GPUScene::InvalidId;
            Id = Count++;
        }
        Alive[Id] = 1;
        return Id;
    }

    uint32_t GPUScene::AllocatePrimitive()
    {
        return AllocateSlot(FreePrimitives, PrimitiveAlive, PrimitiveCount);
    }

    uint32_t GPUScene::AllocateInstance()
    {
        return AllocateSlot(FreeInstances, InstanceAlive, InstanceCount);
    }

    void GPUScene::FreePrimitive(uint32_t Id)
    {
        if (Id >= PrimitiveAlive.size() || !PrimitiveAlive[Id])
            return;
        PrimitiveAlive[Id] = 0;
        Primitives[Id] = {};
        FreePrimitives.push_back(Id);
    }

    void GPUScene::FreeInstance(uint32_t Id)
    {
        if (Id >= InstanceAlive.size() || !InstanceAlive[Id])
            return;
        InstanceAlive[Id] = 0;
        Instances[Id] = {};
        FreeInstances.push_back(Id);
    }

    void GPUScene::UpdatePrimitive(uint32_t Id, const PrimitiveSceneData& Data)
    {
        if (Id >= Primitives.size() || memcmp(&Primitives[Id], &Data, sizeof(Data)) == 0)
            return;
        Primitives[Id] = Data;
        DirtyPrimitives.Add(Id);
    }

    void GPUScene::UpdateInstance(uint32_t Id, const InstanceSceneData& Data)
    {
        if (Id >= Instances.size() || memcmp(&Instances[Id], &Data, sizeof(Data)) == 0)
            return;
        Instances[Id] = Data;
        DirtyInstances.Add(Id);
    }

    template <typename T>
    static void UploadRange(RHIBuffer* Buffer, const std::vector<T>& Data, uint32_t Begin, uint32_t End)
    {
        if (Buffer)
            RHIUpdateBuffer(Buffer, &Data[Begin], (End - Begin) * sizeof(T), Begin * sizeof(T));
    }

    void GPUScene::Upload()
    {
        if (!DirtyPrimitives.IsEmpty())
            UploadRange(PrimitiveBuffer.get(), Primitives, DirtyPrimitives.Begin, DirtyPrimitives.End);
        if (!DirtyInstances.IsEmpty())
            UploadRange(InstanceBuffer.get(), Instances, DirtyInstances.Begin, DirtyInstances.End);
        DirtyPrimitives.Reset();
        DirtyInstances.Reset();
    }

    void GPUScene::Bind(RHICommandContext* Ctx) const
    {
        if (InstanceSRV)
            Ctx->SetShaderResourceView(8, InstanceSRV.get());
        if (PrimitiveSRV)
            Ctx->SetShaderResourceView(9, PrimitiveSRV.get());
    }

} // namespace Kiwi
