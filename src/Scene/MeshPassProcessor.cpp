#include "Scene/MeshPassProcessor.h"
#include "Scene/GPUScene.h"
#include "Scene/ShaderLibrary.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace Kiwi
{

static bool SameDrawState(const MeshDrawCommand& a, const MeshDrawCommand& b)
{
    const char* matA = a.MaterialName ? a.MaterialName : "";
    const char* matB = b.MaterialName ? b.MaterialName : "";
    return a.Shader.PSO == b.Shader.PSO
        && a.Shader.VertexShader == b.Shader.VertexShader
        && a.Shader.PixelShader == b.Shader.PixelShader
        && a.VertexBuffer == b.VertexBuffer
        && a.IndexBuffer == b.IndexBuffer
        && a.IndexCount == b.IndexCount
        && a.CullMode == b.CullMode
        && strcmp(matA, matB) == 0;
}

static void MergeDrawCommands(std::vector<MeshDrawCommand>& commands, GPUScene& gpuScene)
{
    if (commands.empty())
        return;

    std::stable_sort(commands.begin(), commands.end(), [](const MeshDrawCommand& a, const MeshDrawCommand& b)
    {
        if (a.Shader.PSO != b.Shader.PSO)
            return a.Shader.PSO < b.Shader.PSO;
        if (a.Shader.VertexShader != b.Shader.VertexShader)
            return a.Shader.VertexShader < b.Shader.VertexShader;
        if (a.Shader.PixelShader != b.Shader.PixelShader)
            return a.Shader.PixelShader < b.Shader.PixelShader;
        if (a.VertexBuffer != b.VertexBuffer)
            return a.VertexBuffer < b.VertexBuffer;
        if (a.IndexBuffer != b.IndexBuffer)
            return a.IndexBuffer < b.IndexBuffer;
        if (a.IndexCount != b.IndexCount)
            return a.IndexCount < b.IndexCount;
        if (a.CullMode != b.CullMode)
            return (int)a.CullMode < (int)b.CullMode;
        const char* matA = a.MaterialName ? a.MaterialName : "";
        const char* matB = b.MaterialName ? b.MaterialName : "";
        return strcmp(matA, matB) < 0;
    });

    std::vector<MeshDrawCommand> merged;
    merged.reserve(commands.size());
    size_t run = 0;
    while (run < commands.size())
    {
        size_t end = run + 1;
        while (end < commands.size() && SameDrawState(commands[run], commands[end]))
            ++end;

        std::vector<uint32_t> ids;
        ids.reserve(end - run);
        for (size_t i = run; i < end; ++i)
            ids.push_back(commands[i].InstanceId);

        MeshDrawCommand command = commands[run];
        command.NumInstances = (uint32_t)ids.size();
        command.bInstanced = ids.size() > 1;
        command.DrawInstanceOffset = gpuScene.AppendDrawInstanceIds(ids.data(), command.NumInstances);
        merged.push_back(command);
        run = end;
    }
    commands.swap(merged);
}

void MeshPassProcessor::Process(const std::vector<MeshBatch>& batches, GPUScene& gpuScene)
{
    m_Commands.clear();
    for (const MeshBatch& batch : batches)
    {
        if (ShouldDraw(batch))
            AddMeshBatch(batch);
    }
    MergeDrawCommands(m_Commands, gpuScene);
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
    command.NumInstances = 1;
    command.bInstanced = false;
    command.InstanceId = element.InstanceId;
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

    const MeshPassShader shader = ShaderForPass(batch, EMaterialPass::Depth, false);
    for (const MeshBatchElement& element : batch.Elements)
    {
        if (!HasGeometry(element) || !shader.VertexShader)
            continue;
        AddCommand(MakeCommand(batch, element, shader, false, false));
    }
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
