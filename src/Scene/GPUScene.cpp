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
        m_MeshBatches.clear();
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
        std::vector<uint32_t> primitiveRenderIndices;
        primitiveRenderIndices.reserve(renderList.size());

        // Fill primitive data in RenderList order. BuildBatches reorders it to match MeshBatchKey.
        for (uint32_t ri = 0; ri < (uint32_t)renderList.size() && m_NumPrimitives < MAX_GPU_SCENE_PRIMITIVES; ++ri)
        {
            const auto& item = renderList[ri];
            auto* meshComp = item.MeshComp;
            if (!meshComp) continue;

            uint32_t gpuIdx = m_NumPrimitives;
            m_RenderListToGPUScene[ri] = gpuIdx;

            PrimitiveUniformBuffer& oub = m_PrimitiveData[gpuIdx];
            memset(&oub, 0, sizeof(oub));

            // World transform
            Mat4 worldMatrix = meshComp->GetWorldMatrix();
            memcpy(oub.WorldMatrix, worldMatrix.m, sizeof(worldMatrix.m));

            // Material instance parameters, falling back to the parent asset.
            Material* mat = materialLibrary.GetMaterial(meshComp->Material.Parent);
            const MaterialInstance& instance = meshComp->Material;
            Vec4 color = instance.GetColor(mat, "_Color", { 0.8f, 0.8f, 0.8f, 1.0f });
            float roughness = instance.GetFloat(mat, "_Roughness", 0.5f);
            float metallic  = instance.GetFloat(mat, "_Metallic",  0.0f);
            std::string baseColorTex = instance.GetTexture(mat, "_BaseColorTex");
            std::string normalTex    = instance.GetTexture(mat, "_NormalTex");

            oub.ObjectColor[0] = color.x;
            oub.ObjectColor[1] = color.y;
            oub.ObjectColor[2] = color.z;
            oub.ObjectColor[3] = color.w;

            // Check selection
            oub.Selected = (objects[item.ObjectIndex].get() == selectedObj) ? 1.0f : 0.0f;

            oub.Roughness = roughness;
            oub.Metallic  = metallic;
            oub.HasBaseColorTex = baseColorTex.empty() ? 0.0f : 1.0f;
            oub.HasNormalTex    = normalTex.empty()    ? 0.0f : 1.0f;
            oub.ShadingModelID  = mat ? (float)(uint8_t)mat->ShadingModel : 1.0f;
            Vec4 emissive = instance.GetColor(mat, "_Emissive", { 0, 0, 0, 1 });
            oub.ObjectPadding[0] = emissive.x;
            oub.ObjectPadding[1] = emissive.y;
            oub.ObjectPadding[2] = emissive.z;

            primitiveRenderIndices.push_back(ri);
            ++m_NumPrimitives;
        }

        BuildBatches(renderList, primitiveRenderIndices, materialLibrary);

        m_Dirty = true;
    }

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

    void GPUScene::BuildBatches(const std::vector<RenderItem>& renderList,
                                const std::vector<uint32_t>& primitiveRenderIndices,
                                MaterialLibrary& materialLibrary)
    {
        m_MeshBatches.clear();
        if (primitiveRenderIndices.empty())
            return;

        struct Entry
        {
            MeshBatchKey Key;
            uint32_t RenderListIndex = 0;
        };

        std::vector<Entry> entries;
        entries.reserve(primitiveRenderIndices.size());
        for (uint32_t renderListIndex : primitiveRenderIndices)
        {
            Entry entry;
            entry.Key = MakeMeshBatchKey(renderList[renderListIndex], materialLibrary);
            entry.RenderListIndex = renderListIndex;
            entries.push_back(entry);
        }

        std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b)
        {
            return a.Key.Compare(b.Key) < 0;
        });

        // Instancing reads a contiguous range, so pack primitive data into key order.
        std::vector<PrimitiveUniformBuffer> packed(entries.size());
        for (uint32_t newIndex = 0; newIndex < (uint32_t)entries.size(); ++newIndex)
        {
            uint32_t renderListIndex = entries[newIndex].RenderListIndex;
            uint32_t oldIndex = m_RenderListToGPUScene[renderListIndex];
            packed[newIndex] = m_PrimitiveData[oldIndex];
            m_RenderListToGPUScene[renderListIndex] = newIndex;
        }
        std::copy(packed.begin(), packed.end(), m_PrimitiveData.begin());
        m_NumPrimitives = (uint32_t)entries.size();

        uint32_t instancedCount = 0;
        uint32_t runStart = 0;
        while (runStart < entries.size())
        {
            uint32_t runEnd = runStart + 1;
            while (runEnd < entries.size() && entries[runEnd].Key.Compare(entries[runStart].Key) == 0)
                ++runEnd;

            const Entry& first = entries[runStart];
            const RenderItem& firstItem = renderList[first.RenderListIndex];
            MeshBatch batch;
            batch.MeshId = first.Key.MeshId;
            batch.MaterialName = firstItem.MeshComp ? firstItem.MeshComp->Material.Parent : "";
            batch.SurfaceShader = (first.Key.Material && !first.Key.Material->SurfaceShader.empty())
                ? first.Key.Material->SurfaceShader
                : "DefaultSurface";
            batch.CullMode = first.Key.CullMode;
            batch.Type = first.Key.Topology;
            batch.bCastShadow = first.Key.bCastShadow;
            batch.bUseForMaterial = first.Key.bUseForMaterial;
            batch.bUseForDepthPass = first.Key.bUseForDepthPass;
            batch.bCanBeInstanced = (runEnd - runStart) >= 2;
            batch.InstanceOffset = runStart;
            if (batch.bCanBeInstanced)
                ++instancedCount;

            batch.Elements.reserve(runEnd - runStart);
            for (uint32_t index = runStart; index < runEnd; ++index)
            {
                const RenderItem& item = renderList[entries[index].RenderListIndex];
                MeshBatchElement element;
                element.PrimitiveId = index;
                element.ObjectIndex = item.ObjectIndex;
                element.Mesh = item.MeshComp;
                element.NumInstances = 1;
                batch.Elements.push_back(element);
            }
            m_MeshBatches.push_back(std::move(batch));
            runStart = runEnd;
        }

        std::cout << "[Kiwi] GPUScene: " << m_NumPrimitives << " primitives → "
                  << m_MeshBatches.size() << " mesh batches ("
                  << instancedCount << " instanced)" << std::endl;
    }

    void GPUScene::UploadToGPU()
    {
        if (!m_ConstantBuffer || m_NumPrimitives == 0)
        {
            return;
        }
        
        // Upload to Constant Buffer (for single-draw CB offset path)
        {
            void* mapped = m_ConstantBuffer->Map();
            if (mapped)
            {
                uint8_t* dst = (uint8_t*)mapped;
                for (uint32_t i = 0; i < m_NumPrimitives; ++i)
                {
                    memcpy(dst + i * OBJECT_UB_STRIDE, &m_PrimitiveData[i], sizeof(PrimitiveUniformBuffer));
                }
                m_ConstantBuffer->Unmap();
            }
        }

        // Create/recreate StructuredBuffer for instanced draw path
        bool hasInstancedBatch = false;
        for (const MeshBatch& batch : m_MeshBatches)
        {
            if (batch.bCanBeInstanced)
            {
                hasInstancedBatch = true;
                break;
            }
        }

        if (hasInstancedBatch && m_Device)
        {
            uint32_t requiredSize = m_NumPrimitives * sizeof(PrimitiveUniformBuffer);

            // Recreate if size changed or first time
            if (!m_StructuredBuffer || m_StructuredBuffer->GetDesc().SizeInBytes < requiredSize)
            {
                m_StructuredSRV.reset();
                m_StructuredBuffer.reset();

                BufferDesc sbDesc;
                sbDesc.BindFlags = BUFFER_USAGE_STRUCTURED;
                sbDesc.Usage = EResourceUsage::Dynamic;
                sbDesc.DebugName = "GPUScene_StructuredBuffer";
                sbDesc.SizeInBytes = MAX_GPU_SCENE_PRIMITIVES * sizeof(PrimitiveUniformBuffer);
                sbDesc.StructByteStride = sizeof(PrimitiveUniformBuffer);
                m_StructuredBuffer = m_Device->CreateBuffer(sbDesc);

                if (m_StructuredBuffer)
                {
                    m_StructuredSRV = m_Device->CreateBufferSRV(
                        m_StructuredBuffer.get(),
                        MAX_GPU_SCENE_PRIMITIVES,
                        sizeof(PrimitiveUniformBuffer));
                }
            }

            // Upload primitive data to StructuredBuffer
            if (m_StructuredBuffer)
            {
                void* mapped = m_StructuredBuffer->Map();
                if (mapped)
                {
                    memcpy(mapped, m_PrimitiveData.data(),
                           m_NumPrimitives * sizeof(PrimitiveUniformBuffer));
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
