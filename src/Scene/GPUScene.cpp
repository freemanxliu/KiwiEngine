#include "Scene/GPUScene.h"
#include "Scene/Scene.h"
#include "Scene/Component.h"
#include "Scene/MeshComponent.h"
#include "Scene/Material.h"
#include <cstring>
#include <unordered_set>

namespace Kiwi
{

    static_assert(sizeof(PrimitiveSceneData) == 80, "PrimitiveSceneData stride must match the shader");
    static_assert(sizeof(InstanceSceneData) == 80, "InstanceSceneData stride must match the shader");
    static_assert(sizeof(DrawInstanceId) == 16, "DrawInstanceId stride must match the shader");

    MeshBatchKey MakeMeshBatchKey(const RenderItem& item, MaterialLibrary& materialLibrary)
    {
        MeshBatchKey key;
        key.SortOrder = item.SortOrder;
        key.MeshId = item.MeshID;
        key.CullMode = item.MeshComp ? item.MeshComp->CullMode : ECullMode::Back;
        key.Topology = EPrimitiveTopology::TriangleList;
        key.bCastShadow = true;
        key.bUseForMaterial = true;
        key.bUseForDepthPass = true;
        if (item.MeshComp)
        {
            key.Material = materialLibrary.GetMaterial(item.MeshComp->Material.Parent);
            key.BaseColorTex = item.MeshComp->Material.GetTexture(key.Material, "_BaseColorTex");
            key.NormalTex = item.MeshComp->Material.GetTexture(key.Material, "_NormalTex");
            key.MetallicRoughnessTex = item.MeshComp->Material.GetTexture(key.Material, "_MetallicRoughnessTex");
        }
        return key;
    }

    static std::unique_ptr<RHIBuffer> CreateStructured(RHIDevice* device, const char* name, uint32_t stride, uint32_t count)
    {
        BufferDesc desc;
        desc.BindFlags = BUFFER_USAGE_STRUCTURED;
        desc.Usage = EResourceUsage::Default;
        desc.DebugName = name;
        desc.StructByteStride = stride;
        desc.SizeInBytes = stride * count;
        return device->CreateBuffer(desc);
    }

    void GPUScene::Initialize(RHIDevice* device)
    {
        m_Device = device;
        m_Primitives.assign(MAX_GPU_SCENE_PRIMITIVES, {});
        m_Instances.assign(MAX_GPU_SCENE_PRIMITIVES, {});
        m_PrimitiveAlive.assign(MAX_GPU_SCENE_PRIMITIVES, 0);
        m_InstanceAlive.assign(MAX_GPU_SCENE_PRIMITIVES, 0);
        EnsureBuffers();

        BufferDesc offsetDesc;
        offsetDesc.BindFlags = BUFFER_USAGE_CONSTANT;
        offsetDesc.Usage = EResourceUsage::Dynamic;
        offsetDesc.DebugName = "GPUScene_DrawOffset";
        offsetDesc.SizeInBytes = 16;
        m_DrawOffsetCB = device->CreateBuffer(offsetDesc);
    }

    void GPUScene::EnsureBuffers()
    {
        if (!m_Device)
            return;
        if (!m_PrimitiveBuffer)
        {
            m_PrimitiveBuffer = CreateStructured(m_Device, "GPUScene_Primitives", sizeof(PrimitiveSceneData), MAX_GPU_SCENE_PRIMITIVES);
            if (m_PrimitiveBuffer)
                m_PrimitiveSRV = m_Device->CreateBufferSRV(m_PrimitiveBuffer.get(), MAX_GPU_SCENE_PRIMITIVES, sizeof(PrimitiveSceneData));
        }
        if (!m_InstanceBuffer)
        {
            m_InstanceBuffer = CreateStructured(m_Device, "GPUScene_Instances", sizeof(InstanceSceneData), MAX_GPU_SCENE_PRIMITIVES);
            if (m_InstanceBuffer)
                m_InstanceSRV = m_Device->CreateBufferSRV(m_InstanceBuffer.get(), MAX_GPU_SCENE_PRIMITIVES, sizeof(InstanceSceneData));
        }
        if (!m_DrawInstanceBuffer)
        {
            m_DrawInstanceBuffer = CreateStructured(m_Device, "GPUScene_DrawInstanceIds", sizeof(DrawInstanceId), MAX_GPU_SCENE_PRIMITIVES);
            if (m_DrawInstanceBuffer)
                m_DrawInstanceSRV = m_Device->CreateBufferSRV(m_DrawInstanceBuffer.get(), MAX_GPU_SCENE_PRIMITIVES, sizeof(DrawInstanceId));
        }
    }

    void GPUScene::Release()
    {
        m_PrimitiveBuffer.reset();
        m_PrimitiveSRV.reset();
        m_InstanceBuffer.reset();
        m_InstanceSRV.reset();
        m_DrawInstanceBuffer.reset();
        m_DrawInstanceSRV.reset();
        m_DrawOffsetCB.reset();
        m_Primitives.clear();
        m_Instances.clear();
        m_PrimitiveAlive.clear();
        m_InstanceAlive.clear();
        m_FreePrimitives.clear();
        m_FreeInstances.clear();
        m_DrawInstanceIds.clear();
        m_PrimitiveCount = 0;
        m_InstanceCount = 0;
        m_Device = nullptr;
    }

    uint32_t GPUScene::AllocatePrimitive()
    {
        uint32_t id;
        if (!m_FreePrimitives.empty())
        {
            id = m_FreePrimitives.back();
            m_FreePrimitives.pop_back();
        }
        else
        {
            if (m_PrimitiveCount >= MAX_GPU_SCENE_PRIMITIVES)
                return MeshComponent::kInvalidGPUSceneId;
            id = m_PrimitiveCount++;
        }
        m_PrimitiveAlive[id] = 1;
        return id;
    }

    uint32_t GPUScene::AllocateInstance()
    {
        uint32_t id;
        if (!m_FreeInstances.empty())
        {
            id = m_FreeInstances.back();
            m_FreeInstances.pop_back();
        }
        else
        {
            if (m_InstanceCount >= MAX_GPU_SCENE_PRIMITIVES)
                return MeshComponent::kInvalidGPUSceneId;
            id = m_InstanceCount++;
        }
        m_InstanceAlive[id] = 1;
        return id;
    }

    void GPUScene::FreePrimitive(uint32_t id)
    {
        if (id >= m_PrimitiveAlive.size() || !m_PrimitiveAlive[id])
            return;
        m_PrimitiveAlive[id] = 0;
        m_Primitives[id] = {};
        m_FreePrimitives.push_back(id);
    }

    void GPUScene::FreeInstance(uint32_t id)
    {
        if (id >= m_InstanceAlive.size() || !m_InstanceAlive[id])
            return;
        m_InstanceAlive[id] = 0;
        m_Instances[id] = {};
        m_FreeInstances.push_back(id);
    }

    void GPUScene::UploadSlot(RHIBuffer* buffer, const void* data, uint32_t stride, uint32_t index)
    {
        if (buffer)
            buffer->UpdateData(data, stride, index * stride);
    }

    void GPUScene::Update(Scene& scene, MaterialLibrary& materialLibrary)
    {
        EnsureBuffers();
        m_DrawInstanceIds.clear();
        m_DrawIdsDirty = false;

        auto* selected = scene.GetSelectedObject();
        std::unordered_set<MeshComponent*> alive;
        alive.reserve(scene.GetObjects().size());

        for (const auto& objPtr : scene.GetObjects())
        {
            MeshComponent* mesh = objPtr->GetComponent<MeshComponent>();
            if (!mesh)
                continue;
            alive.insert(mesh);

            auto slotDead = [](uint32_t id, const std::vector<uint8_t>& alive)
            {
                return id == MeshComponent::kInvalidGPUSceneId || id >= alive.size() || !alive[id];
            };
            if (slotDead(mesh->PrimitiveId, m_PrimitiveAlive) || slotDead(mesh->InstanceId, m_InstanceAlive))
            {
                if (!slotDead(mesh->PrimitiveId, m_PrimitiveAlive))
                    FreePrimitive(mesh->PrimitiveId);
                if (!slotDead(mesh->InstanceId, m_InstanceAlive))
                    FreeInstance(mesh->InstanceId);
                mesh->PrimitiveId = AllocatePrimitive();
                mesh->InstanceId = AllocateInstance();
            }
            if (mesh->PrimitiveId == MeshComponent::kInvalidGPUSceneId ||
                mesh->InstanceId == MeshComponent::kInvalidGPUSceneId)
                continue;

            Material* parent = materialLibrary.GetMaterial(mesh->Material.Parent);
            Vec4 color = mesh->Material.GetColor(parent, "_Color", { 0.8f, 0.8f, 0.8f, 1.0f });
            Vec4 emissive = mesh->Material.GetColor(parent, "_Emissive", { 0, 0, 0, 1 });

            PrimitiveSceneData primitive = {};
            primitive.ObjectColor[0] = color.x;
            primitive.ObjectColor[1] = color.y;
            primitive.ObjectColor[2] = color.z;
            primitive.ObjectColor[3] = color.w;
            primitive.Material0[0] = (objPtr.get() == selected) ? 1.0f : 0.0f;
            primitive.Material0[1] = mesh->Material.GetFloat(parent, "_Roughness", 0.5f);
            primitive.Material0[2] = mesh->Material.GetFloat(parent, "_Metallic", 0.0f);
            primitive.Material0[3] = mesh->Material.GetTexture(parent, "_BaseColorTex").empty() ? 0.0f : 1.0f;
            primitive.Material1[0] = mesh->Material.GetTexture(parent, "_NormalTex").empty() ? 0.0f : 1.0f;
            primitive.Material1[1] = parent ? (float)(uint8_t)parent->ShadingModel : 1.0f;
            primitive.Material1[2] = emissive.x;
            primitive.Material1[3] = emissive.y;
            primitive.Material2[0] = emissive.z;
            primitive.InstanceSceneDataOffset = mesh->InstanceId;
            primitive.NumInstances = 1;

            InstanceSceneData instance = {};
            Mat4 world = mesh->GetWorldMatrix();
            memcpy(instance.WorldMatrix, world.m, sizeof(world.m));
            instance.PrimitiveId = mesh->PrimitiveId;

            if (memcmp(&m_Primitives[mesh->PrimitiveId], &primitive, sizeof(primitive)) != 0)
            {
                m_Primitives[mesh->PrimitiveId] = primitive;
                UploadSlot(m_PrimitiveBuffer.get(), &primitive, sizeof(primitive), mesh->PrimitiveId);
            }
            if (memcmp(&m_Instances[mesh->InstanceId], &instance, sizeof(instance)) != 0)
            {
                m_Instances[mesh->InstanceId] = instance;
                UploadSlot(m_InstanceBuffer.get(), &instance, sizeof(instance), mesh->InstanceId);
            }
        }

        for (uint32_t id = 0; id < m_PrimitiveCount; ++id)
        {
            if (!m_PrimitiveAlive[id])
                continue;
            bool stillAlive = false;
            for (MeshComponent* mesh : alive)
            {
                if (mesh->PrimitiveId == id)
                {
                    stillAlive = true;
                    break;
                }
            }
            if (!stillAlive)
                FreePrimitive(id);
        }
        for (uint32_t id = 0; id < m_InstanceCount; ++id)
        {
            if (!m_InstanceAlive[id])
                continue;
            bool stillAlive = false;
            for (MeshComponent* mesh : alive)
            {
                if (mesh->InstanceId == id)
                {
                    stillAlive = true;
                    break;
                }
            }
            if (!stillAlive)
                FreeInstance(id);
        }
    }

    uint32_t GPUScene::AppendDrawInstanceIds(const uint32_t* instanceIds, uint32_t count)
    {
        uint32_t offset = (uint32_t)m_DrawInstanceIds.size();
        m_DrawInstanceIds.reserve(offset + count);
        for (uint32_t i = 0; i < count; ++i)
        {
            DrawInstanceId entry = {};
            entry.Id = instanceIds[i];
            m_DrawInstanceIds.push_back(entry);
        }
        m_DrawIdsDirty = true;
        return offset;
    }

    void GPUScene::UploadDrawInstanceIds()
    {
        if (!m_DrawIdsDirty || !m_DrawInstanceBuffer || m_DrawInstanceIds.empty())
            return;
        m_DrawInstanceBuffer->UpdateData(m_DrawInstanceIds.data(),
            (uint32_t)(m_DrawInstanceIds.size() * sizeof(DrawInstanceId)), 0);
        m_DrawIdsDirty = false;
    }

    void GPUScene::Bind(RHICommandContext* ctx) const
    {
        if (m_InstanceSRV)
            ctx->SetShaderResourceView(8, m_InstanceSRV.get());
        if (m_PrimitiveSRV)
            ctx->SetShaderResourceView(9, m_PrimitiveSRV.get());
        if (m_DrawInstanceSRV)
            ctx->SetShaderResourceView(10, m_DrawInstanceSRV.get());
    }

    void GPUScene::SetDrawInstanceOffset(RHICommandContext* ctx, uint32_t offset) const
    {
        if (!m_DrawOffsetCB)
            return;
        uint32_t data[4] = { offset, 0, 0, 0 };
        void* mapped = m_DrawOffsetCB->Map();
        if (mapped)
        {
            memcpy(mapped, data, sizeof(data));
            m_DrawOffsetCB->Unmap();
        }
        ctx->SetConstantBuffer(4, m_DrawOffsetCB.get());
    }

} // namespace Kiwi
