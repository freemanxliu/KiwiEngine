#include "Scene/MeshPassProcessor.h"
#include "Scene/ShaderLibrary.h"

#include <algorithm>
#include <cstring>

namespace Kiwi
{

void MeshPassProcessor::Process(const std::vector<MeshBatch>& batches)
{
    m_Commands.clear();
    for (const MeshBatch& batch : batches)
    {
        if (ShouldDraw(batch))
            AddMeshBatch(batch);
    }

    std::stable_sort(m_Commands.begin(), m_Commands.end(), [](const MeshDrawCommand& a, const MeshDrawCommand& b)
    {
        if (a.bInstanced != b.bInstanced)
            return a.bInstanced > b.bInstanced;
        if (a.Shader.PSO != b.Shader.PSO)
            return a.Shader.PSO < b.Shader.PSO;
        if (a.Shader.VertexShader != b.Shader.VertexShader)
            return a.Shader.VertexShader < b.Shader.VertexShader;
        if (a.Shader.PixelShader != b.Shader.PixelShader)
            return a.Shader.PixelShader < b.Shader.PixelShader;
        if (a.VertexBuffer != b.VertexBuffer)
            return a.VertexBuffer < b.VertexBuffer;
        const char* matA = a.MaterialName ? a.MaterialName : "";
        const char* matB = b.MaterialName ? b.MaterialName : "";
        return strcmp(matA, matB) < 0;
    });
}

static MeshDrawCommand MakeCommand(const MeshBatch& batch, const MeshBatchElement& element,
    MeshPassShader shader, bool instanced, bool bindMaterial)
{
    MeshDrawCommand command;
    command.Shader = shader;
    command.CullMode = batch.CullMode;
    command.VertexBuffer = element.VertexBuffer;
    command.IndexBuffer = element.IndexBuffer;
    command.VertexCount = element.VertexCount;
    command.IndexCount = element.NumIndices;
    command.FirstIndex = element.FirstIndex;
    command.BaseVertexIndex = element.BaseVertexIndex;
    command.NumInstances = instanced ? (uint32_t)batch.Elements.size() : 1;
    command.bInstanced = instanced;
    command.PrimitiveId = element.PrimitiveId;
    command.InstanceOffset = batch.InstanceOffset;
    command.bBindMaterial = bindMaterial;
    command.Mesh = element.Mesh;
    command.MaterialName = batch.MaterialName.c_str();
    return command;
}

static MeshPassShader ShaderForPass(const MeshBatch& batch, EMaterialPass pass, bool bInstanced)
{
    if (!batch.ShaderMap)
        return {};
    return batch.ShaderMap->Get(pass, bInstanced);
}

void ShadowDepthPassProcessor::AddMeshBatch(const MeshBatch& batch)
{
    if (batch.Elements.empty())
        return;

    const MeshPassShader instanced = ShaderForPass(batch, EMaterialPass::Depth, true);
    const bool useInstanced = batch.bCanBeInstanced
        && instanced.PSO
        && instanced.VertexShader
        && HasGeometry(batch.Elements[0]);
    if (useInstanced)
    {
        AddCommand(MakeCommand(batch, batch.Elements[0], instanced, true, false));
        return;
    }

    const MeshPassShader single = ShaderForPass(batch, EMaterialPass::Depth, false);
    for (const MeshBatchElement& element : batch.Elements)
    {
        if (!HasGeometry(element) || !single.VertexShader)
            continue;
        AddCommand(MakeCommand(batch, element, single, false, false));
    }
}

bool BasePassProcessor::CanInstance(const MeshBatch& batch) const
{
    if (!batch.bCanBeInstanced || m_Config.ForcedShader)
        return false;
    const MeshPassShader instanced = ShaderForPass(batch, m_Config.MaterialPass, true);
    return instanced.PSO && instanced.VertexShader;
}

MeshPassShader BasePassProcessor::ResolveSingleShader(const MeshBatch& batch) const
{
    if (m_Config.ForcedShader && m_Config.Shaders)
    {
        CompiledShader* shader = m_Config.Shaders->GetShader(m_Config.ForcedShader);
        if (!shader)
            shader = m_Config.Shaders->GetDefault();
        if (shader)
            return { shader->PSO.get(), shader->VertexShader.get(), shader->PixelShader.get() };
    }

    MeshPassShader shader = ShaderForPass(batch, m_Config.MaterialPass, false);
    if (shader.VertexShader)
        return shader;
    return m_Config.Fallback;
}

void BasePassProcessor::AddMeshBatch(const MeshBatch& batch)
{
    if (batch.Elements.empty())
        return;

    if (CanInstance(batch) && HasGeometry(batch.Elements[0]))
    {
        MeshPassShader instanced = ShaderForPass(batch, m_Config.MaterialPass, true);
        AddCommand(MakeCommand(batch, batch.Elements[0], instanced, true, m_Config.bBindMaterials));
        return;
    }

    MeshPassShader shader = ResolveSingleShader(batch);
    if (!shader.VertexShader)
        return;

    for (const MeshBatchElement& element : batch.Elements)
    {
        if (!HasGeometry(element))
            continue;
        AddCommand(MakeCommand(batch, element, shader, false, m_Config.bBindMaterials));
    }
}

} // namespace Kiwi
