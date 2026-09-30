#pragma once

#include "RHI/RHI.h"
#include "RHI/RHITypes.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Kiwi
{

struct PrimitiveSceneInfo;

// One draw inside a mesh batch. Same role as UE5 FMeshBatchElement.
struct MeshBatchElement
{
    uint32_t FirstIndex = 0;
    uint32_t NumIndices = 0;
    uint32_t NumInstances = 1;
    int32_t BaseVertexIndex = 0;

    // Stable GPU Scene ids for this element.
    uint32_t PrimitiveId = 0;
    uint32_t InstanceId = 0;
    const PrimitiveSceneInfo* Primitive = nullptr;

    // Squared distance from the view origin. Pass processors use it to order draws front to back.
    float ViewDistanceSq = 0.0f;

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
        return Shaders[passIndex][bInstanced ? 1 : 0];
    }

    void Set(EMaterialPass pass, bool bInstanced, MeshPassShader shader)
    {
        Shaders[(int)pass][bInstanced ? 1 : 0] = shader;
    }

private:
    MeshPassShader Shaders[(int)EMaterialPass::Count][2]{};
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
    int32_t SortPriority = 0; // PrimitiveComponent::SortOrder. Higher draws first in every pass.
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

    // Sort inputs, filled by the pass processor. Draws never merge across priorities.
    int32_t SortPriority = 0;
    float ViewDistanceSq = 0.0f;

    RHIBuffer* VertexBuffer = nullptr;
    RHIBuffer* IndexBuffer = nullptr;
    uint32_t VertexCount = 0;
    uint32_t IndexCount = 0;
    uint32_t NumInstances = 1;
    int32_t BaseVertexIndex = 0;
    uint32_t FirstIndex = 0;

    bool bInstanced = false;
    uint32_t InstanceId = 0;
    uint32_t DrawInstanceOffset = 0;

    bool bBindMaterial = false;
    const PrimitiveSceneInfo* Primitive = nullptr;
    const char* MaterialName = nullptr;
};

} // namespace Kiwi
