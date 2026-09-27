#pragma once

#include "RHI/RHI.h"
#include "RHI/RHITypes.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Kiwi
{

class MeshComponent;
class Material;

// Everything that must match for primitives to share one MeshBatch.
// Add a field here and one comparison in Compare(); grouping follows.
struct MeshBatchKey
{
    int32_t SortOrder = 0;
    uint32_t MeshId = 0;
    const Material* Material = nullptr;
    std::string BaseColorTex;
    std::string NormalTex;
    std::string MetallicRoughnessTex;
    ECullMode CullMode = ECullMode::Back;
    EPrimitiveTopology Topology = EPrimitiveTopology::TriangleList;
    bool bCastShadow = true;
    bool bUseForMaterial = true;
    bool bUseForDepthPass = true;

    // <0 if this key is ordered first, 0 if the two primitives can share a batch.
    int Compare(const MeshBatchKey& other) const
    {
        if (SortOrder != other.SortOrder)
            return SortOrder > other.SortOrder ? -1 : 1;
        if (MeshId != other.MeshId)
            return MeshId < other.MeshId ? -1 : 1;
        if (Material != other.Material)
            return Material < other.Material ? -1 : 1;
        if (int cmp = BaseColorTex.compare(other.BaseColorTex))
            return cmp < 0 ? -1 : 1;
        if (int cmp = NormalTex.compare(other.NormalTex))
            return cmp < 0 ? -1 : 1;
        if (int cmp = MetallicRoughnessTex.compare(other.MetallicRoughnessTex))
            return cmp < 0 ? -1 : 1;
        if (CullMode != other.CullMode)
            return (int)CullMode < (int)other.CullMode ? -1 : 1;
        if (Topology != other.Topology)
            return (int)Topology < (int)other.Topology ? -1 : 1;
        if (bCastShadow != other.bCastShadow)
            return bCastShadow ? 1 : -1;
        if (bUseForMaterial != other.bUseForMaterial)
            return bUseForMaterial ? 1 : -1;
        if (bUseForDepthPass != other.bUseForDepthPass)
            return bUseForDepthPass ? 1 : -1;
        return 0;
    }
};

// One draw inside a mesh batch. Same role as UE5 FMeshBatchElement.
struct MeshBatchElement
{
    uint32_t FirstIndex = 0;
    uint32_t NumIndices = 0;
    uint32_t NumInstances = 1;
    int32_t BaseVertexIndex = 0;

    // Index of this primitive in the GPU scene buffer.
    uint32_t PrimitiveId = 0;
    size_t ObjectIndex = 0;
    MeshComponent* Mesh = nullptr;

    RHIBuffer* VertexBuffer = nullptr;
    RHIBuffer* IndexBuffer = nullptr;
    uint32_t VertexCount = 0;
};

struct MeshPassShader
{
    RHIPipelineState* PSO = nullptr;
    RHIShader* VertexShader = nullptr;
    RHIShader* PixelShader = nullptr;
};

// One slot per mesh pass. A material shader map holds every pass the material can draw.
enum class EMaterialPass : uint8_t
{
    Depth = 0,
    GBuffer,
    Forward,
    Count
};

// Shaders for every pass of one material. Same role as a UE5 material shader map.
struct MaterialShaderMap
{
    std::string SurfaceShader = "DefaultSurface";

    MeshPassShader Get(EMaterialPass pass, bool bInstanced) const
    {
        const int passIndex = (int)pass;
        if (passIndex < 0 || passIndex >= (int)EMaterialPass::Count)
            return {};
        return m_Shaders[passIndex][bInstanced ? 1 : 0];
    }

    void Set(EMaterialPass pass, bool bInstanced, MeshPassShader shader)
    {
        m_Shaders[(int)pass][bInstanced ? 1 : 0] = shader;
    }

private:
    MeshPassShader m_Shaders[(int)EMaterialPass::Count][2]{};
};

// Pass-agnostic visible mesh. Shadow and the base pass both consume this.
// Same role as UE5 FMeshBatch: shared material and geometry, plus elements.
struct MeshBatch
{
    std::vector<MeshBatchElement> Elements;

    // Material layer. Passes ask this map for the shader they need.
    const MaterialShaderMap* ShaderMap = nullptr;

    uint32_t MeshId = 0;
    std::string MaterialName;
    std::string SurfaceShader = "DefaultSurface";
    ECullMode CullMode = ECullMode::Back;
    EPrimitiveTopology Type = EPrimitiveTopology::TriangleList;

    bool bUseForMaterial = true;
    bool bUseForDepthPass = true;
    bool bCastShadow = true;

    // Elements are one contiguous GPU-scene run and may be drawn with DrawIndexedInstanced.
    bool bCanBeInstanced = false;
    uint32_t InstanceOffset = 0;
};

// What a pass processor records. Same role as UE5 FMeshDrawCommand.
struct MeshDrawCommand
{
    MeshPassShader Shader;
    ECullMode CullMode = ECullMode::Back;

    RHIBuffer* VertexBuffer = nullptr;
    RHIBuffer* IndexBuffer = nullptr;
    uint32_t VertexCount = 0;
    uint32_t IndexCount = 0;
    uint32_t NumInstances = 1;
    int32_t BaseVertexIndex = 0;
    uint32_t FirstIndex = 0;

    bool bInstanced = false;
    uint32_t PrimitiveId = 0;
    uint32_t InstanceOffset = 0;

    bool bBindMaterial = false;
    MeshComponent* Mesh = nullptr;
    const char* MaterialName = nullptr;
};

} // namespace Kiwi
