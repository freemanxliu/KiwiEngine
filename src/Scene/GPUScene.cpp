#include "Scene/GPUScene.h"
#include "Scene/Scene.h"
#include "Scene/Component.h"
#include "Scene/MeshComponent.h"
#include "Scene/Material.h"
#include <cstring>
#include <iostream>
#include <algorithm>

// Forward declare RenderItem (defined in main.cpp but we only need MeshID/MaterialName)
// The struct is passed by const reference from the caller.

namespace Kiwi
{
    // Need RenderItem definition — it's declared in main.cpp as a local struct.
    // We only access it via the interface, so we use a forward-compatible approach:
    // The Update() function receives RenderItem by const ref from main.cpp.
    // To avoid circular dependency, we define a minimal compatible struct here for
    // accessing the fields we need. The actual struct must match main.cpp's layout.

    void GPUScene::Initialize(RHIDevice* device)
    {
        m_Device = device;

        // Constant Buffer: for single-draw CB offset binding (b1)
        BufferDesc cbDesc;
        cbDesc.BindFlags = BUFFER_USAGE_CONSTANT;
        cbDesc.Usage = EResourceUsage::Dynamic;
        cbDesc.DebugName = "GPUScene_CB";
        cbDesc.SizeInBytes = MAX_GPU_SCENE_PRIMITIVES * OBJECT_UB_STRIDE;
        m_ConstantBuffer = device->CreateBuffer(cbDesc);

        // BatchUB: small CB for g_BatchStartIndex (b4)
        BufferDesc batchDesc;
        batchDesc.BindFlags = BUFFER_USAGE_CONSTANT;
        batchDesc.Usage = EResourceUsage::Dynamic;
        batchDesc.DebugName = "GPUScene_BatchUB";
        batchDesc.SizeInBytes = 16; // uint + uint3 padding
        m_BatchUB = device->CreateBuffer(batchDesc);

        m_PrimitiveData.resize(MAX_GPU_SCENE_PRIMITIVES);
        m_NumPrimitives = 0;
        m_Dirty = true;
    }

    void GPUScene::Release()
    {
        m_ConstantBuffer.reset();
        m_StructuredBuffer.reset();
        m_StructuredSRV.reset();
        m_BatchUB.reset();
        m_PrimitiveData.clear();
        m_Batches.clear();
        m_SingleDraws.clear();
        m_RenderListToGPUScene.clear();
        m_NumPrimitives = 0;
        m_Device = nullptr;
    }

    void GPUScene::Update(Scene& scene, MaterialLibrary& materialLibrary,
                          const std::vector<RenderItem>& renderList)
    {
        auto* selectedObj = scene.GetSelectedObject();
        auto& objects = scene.GetObjects();

        m_NumPrimitives = 0;
        m_RenderListToGPUScene.resize(renderList.size());

        // Fill primitive data in RenderList order (matches sort order for batching)
        for (uint32_t ri = 0; ri < (uint32_t)renderList.size() && m_NumPrimitives < MAX_GPU_SCENE_PRIMITIVES; ++ri)
        {
            const auto& item = renderList[ri];
            auto* meshComp = item.MeshComp;
            if (!meshComp) continue;

            uint32_t gpuIdx = m_NumPrimitives;
            m_RenderListToGPUScene[ri] = gpuIdx;

            ObjectUniformBuffer& oub = m_PrimitiveData[gpuIdx];
            memset(&oub, 0, sizeof(oub));

            // World transform
            Mat4 worldMatrix = meshComp->GetWorldMatrix();
            memcpy(oub.WorldMatrix, worldMatrix.m, sizeof(worldMatrix.m));

            // Material properties
            Material* mat = materialLibrary.GetMaterial(meshComp->MaterialName);
            Vec4  color     = mat ? mat->GetColor("_Color",     { 0.8f, 0.8f, 0.8f, 1.0f }) : Vec4{ 0.8f, 0.8f, 0.8f, 1.0f };
            float roughness = mat ? mat->GetFloat("_Roughness", 0.5f) : 0.5f;
            float metallic  = mat ? mat->GetFloat("_Metallic",  0.0f) : 0.0f;
            std::string baseColorTex = mat ? mat->GetTexture("_BaseColorTex") : "";
            std::string normalTex    = mat ? mat->GetTexture("_NormalTex")    : "";

            oub.ObjectColor[0] = color.x;
            oub.ObjectColor[1] = color.y;
            oub.ObjectColor[2] = color.z;
            oub.ObjectColor[3] = color.w;

            // Check selection
            if (item.ObjectIndex < objects.size())
                oub.Selected = (objects[item.ObjectIndex].get() == selectedObj) ? 1.0f : 0.0f;

            oub.Roughness = roughness;
            oub.Metallic  = metallic;
            oub.HasBaseColorTex = baseColorTex.empty() ? 0.0f : 1.0f;
            oub.HasNormalTex    = normalTex.empty()    ? 0.0f : 1.0f;
            oub.VisualizeMode = 0.0f;

            ++m_NumPrimitives;
        }

        // Build batches from the sorted RenderList
        BuildBatches(renderList);

        m_Dirty = true;
    }

    void GPUScene::BuildBatches(const std::vector<RenderItem>& renderList)
    {
        m_Batches.clear();
        m_SingleDraws.clear();

        if (renderList.empty()) return;

        // RenderList is already sorted by MeshID → MaterialName.
        // Walk through and group consecutive items with same MeshID + MaterialName.
        struct TempGroup
        {
            uint32_t MeshID;
            std::string MaterialName;
            std::vector<uint32_t> Indices; // RenderList indices
        };

        std::vector<TempGroup> groups;
        TempGroup current;
        current.MeshID = renderList[0].MeshID;
        current.MaterialName = renderList[0].MaterialName ? renderList[0].MaterialName : "";
        current.Indices.push_back(0);

        for (uint32_t i = 1; i < (uint32_t)renderList.size(); ++i)
        {
            uint32_t meshID = renderList[i].MeshID;
            const char* matName = renderList[i].MaterialName ? renderList[i].MaterialName : "";

            if (meshID == current.MeshID && current.MaterialName == matName)
            {
                current.Indices.push_back(i);
            }
            else
            {
                groups.push_back(std::move(current));
                current.MeshID = meshID;
                current.MaterialName = matName;
                current.Indices.clear();
                current.Indices.push_back(i);
            }
        }
        groups.push_back(std::move(current));

        // Classify: count >= 2 → InstanceBatch, count == 1 → SingleDrawItem
        for (auto& group : groups)
        {
            if (group.Indices.size() >= 2)
            {
                InstanceBatch batch;
                batch.MeshID = group.MeshID;
                batch.MaterialName = group.MaterialName;
                // GPU Scene indices for this batch are contiguous (because renderList is sorted
                // and we fill GPU Scene in renderList order)
                batch.StartIndex = m_RenderListToGPUScene[group.Indices[0]];
                batch.InstanceCount = (uint32_t)group.Indices.size();
                batch.RenderListIndices = std::move(group.Indices);
                m_Batches.push_back(std::move(batch));
            }
            else
            {
                SingleDrawItem item;
                item.RenderListIndex = group.Indices[0];
                item.GPUSceneIndex = m_RenderListToGPUScene[group.Indices[0]];
                m_SingleDraws.push_back(item);
            }
        }

        std::cout << "[Kiwi] GPUScene: " << m_NumPrimitives << " primitives → "
                  << m_Batches.size() << " instanced batches + "
                  << m_SingleDraws.size() << " single draws" << std::endl;
    }

    void GPUScene::UploadToGPU()
    {
        if (!m_ConstantBuffer || m_NumPrimitives == 0) return;

        // Upload to Constant Buffer (for single-draw CB offset path)
        {
            void* mapped = m_ConstantBuffer->Map();
            if (mapped)
            {
                uint8_t* dst = (uint8_t*)mapped;
                for (uint32_t i = 0; i < m_NumPrimitives; ++i)
                {
                    memcpy(dst + i * OBJECT_UB_STRIDE, &m_PrimitiveData[i], sizeof(ObjectUniformBuffer));
                }
                m_ConstantBuffer->Unmap();
            }
        }

        // Create/recreate StructuredBuffer for instanced draw path
        if (!m_Batches.empty() && m_Device)
        {
            uint32_t requiredSize = m_NumPrimitives * sizeof(ObjectUniformBuffer);

            // Recreate if size changed or first time
            if (!m_StructuredBuffer || m_StructuredBuffer->GetDesc().SizeInBytes < requiredSize)
            {
                m_StructuredSRV.reset();
                m_StructuredBuffer.reset();

                BufferDesc sbDesc;
                sbDesc.BindFlags = BUFFER_USAGE_STRUCTURED;
                sbDesc.Usage = EResourceUsage::Dynamic;
                sbDesc.DebugName = "GPUScene_StructuredBuffer";
                sbDesc.SizeInBytes = MAX_GPU_SCENE_PRIMITIVES * sizeof(ObjectUniformBuffer);
                sbDesc.StructByteStride = sizeof(ObjectUniformBuffer);
                m_StructuredBuffer = m_Device->CreateBuffer(sbDesc);

                if (m_StructuredBuffer)
                {
                    m_StructuredSRV = m_Device->CreateBufferSRV(
                        m_StructuredBuffer.get(),
                        MAX_GPU_SCENE_PRIMITIVES,
                        sizeof(ObjectUniformBuffer));
                }
            }

            // Upload primitive data to StructuredBuffer
            if (m_StructuredBuffer)
            {
                void* mapped = m_StructuredBuffer->Map();
                if (mapped)
                {
                    memcpy(mapped, m_PrimitiveData.data(),
                           m_NumPrimitives * sizeof(ObjectUniformBuffer));
                    m_StructuredBuffer->Unmap();
                }
            }
        }

        m_Dirty = false;
    }

    void GPUScene::BindPrimitive(RHICommandContext* ctx, uint32_t gpuSceneIndex) const
    {
        if (!m_ConstantBuffer) return;
        ctx->SetConstantBufferOffset(1, m_ConstantBuffer.get(),
            gpuSceneIndex * (OBJECT_UB_STRIDE / 16), OBJECT_UB_STRIDE / 16);
    }

    void GPUScene::BindForInstancing(RHICommandContext* ctx) const
    {
        // Bind StructuredBuffer as SRV t8
        if (m_StructuredSRV)
            ctx->SetShaderResourceView(8, m_StructuredSRV.get());
    }

    void GPUScene::SetBatchStartIndex(RHICommandContext* ctx, uint32_t startIndex) const
    {
        if (!m_BatchUB) return;
        uint32_t data[4] = { startIndex, 0, 0, 0 };
        void* mapped = m_BatchUB->Map();
        if (mapped)
        {
            memcpy(mapped, data, sizeof(data));
            m_BatchUB->Unmap();
        }
        ctx->SetConstantBuffer(4, m_BatchUB.get());
    }

} // namespace Kiwi
