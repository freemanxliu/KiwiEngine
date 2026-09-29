#include "Scene/MeshPassProcessor.h"
#include "Renderer/InstanceCulling.h"
#include "Scene/ShaderLibrary.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace Kiwi
{

// Orders by sort priority, then draw state. 0 means the two commands can share one instanced draw.
static int CompareDrawState(const MeshDrawCommand& A, const MeshDrawCommand& B)
{
    if (A.SortPriority != B.SortPriority)
        return A.SortPriority > B.SortPriority ? -1 : 1;
    if (A.Shader.PSO != B.Shader.PSO)
        return A.Shader.PSO < B.Shader.PSO ? -1 : 1;
    if (A.Shader.VertexShader != B.Shader.VertexShader)
        return A.Shader.VertexShader < B.Shader.VertexShader ? -1 : 1;
    if (A.Shader.PixelShader != B.Shader.PixelShader)
        return A.Shader.PixelShader < B.Shader.PixelShader ? -1 : 1;
    if (A.VertexBuffer != B.VertexBuffer)
        return A.VertexBuffer < B.VertexBuffer ? -1 : 1;
    if (A.IndexBuffer != B.IndexBuffer)
        return A.IndexBuffer < B.IndexBuffer ? -1 : 1;
    if (A.IndexCount != B.IndexCount)
        return A.IndexCount < B.IndexCount ? -1 : 1;
    if (A.CullMode != B.CullMode)
        return (int)A.CullMode < (int)B.CullMode ? -1 : 1;
    const char* MatA = A.MaterialName ? A.MaterialName : "";
    const char* MatB = B.MaterialName ? B.MaterialName : "";
    const int Cmp = strcmp(MatA, MatB);
    return Cmp < 0 ? -1 : (Cmp > 0 ? 1 : 0);
}

// Same role as UE5 SortAndMergeDynamicPassMeshDrawCommands: sort, then fold equal-state runs into instanced draws.
static void SortAndMergeDrawCommands(std::vector<MeshDrawCommand>& Commands, InstanceCullingContext& InstanceCulling)
{
    if (Commands.empty())
        return;

    // Front to back inside one state run, so instances inside a merged draw also get early-Z benefit.
    std::sort(Commands.begin(), Commands.end(), [](const MeshDrawCommand& A, const MeshDrawCommand& B)
    {
        if (int Cmp = CompareDrawState(A, B))
            return Cmp < 0;
        return A.ViewDistanceSq < B.ViewDistanceSq;
    });

    std::vector<MeshDrawCommand> Merged;
    Merged.reserve(Commands.size());
    size_t Run = 0;
    while (Run < Commands.size())
    {
        size_t End = Run + 1;
        while (End < Commands.size() && CompareDrawState(Commands[Run], Commands[End]) == 0)
            ++End;

        std::vector<uint32_t> Ids;
        Ids.reserve(End - Run);
        for (size_t I = Run; I < End; ++I)
            Ids.push_back(Commands[I].InstanceId);

        MeshDrawCommand Command = Commands[Run];
        Command.NumInstances = (uint32_t)Ids.size();
        Command.bInstanced = Ids.size() > 1;
        Command.DrawInstanceOffset = InstanceCulling.AppendDrawInstanceIds(Ids.data(), Command.NumInstances);
        Merged.push_back(Command);
        Run = End;
    }
    Commands.swap(Merged);
}

void MeshPassProcessor::Process(const std::vector<MeshBatch>& Batches, InstanceCullingContext& InstanceCulling)
{
    Commands.clear();
    for (const MeshBatch& Batch : Batches)
    {
        if (ShouldDraw(Batch))
            AddMeshBatch(Batch);
    }
    SortAndMergeDrawCommands(Commands, InstanceCulling);
}

static MeshDrawCommand MakeCommand(const MeshBatch& Batch, const MeshBatchElement& Element,
    MeshPassShader Shader, bool Instanced, bool BindMaterial)
{
    MeshDrawCommand Command;
    Command.Shader = Shader;
    Command.CullMode = Batch.CullMode;
    Command.SortPriority = Batch.SortPriority;
    Command.ViewDistanceSq = Element.ViewDistanceSq;
    Command.VertexBuffer = Element.VertexBuffer;
    Command.IndexBuffer = Element.IndexBuffer;
    Command.VertexCount = Element.VertexCount;
    Command.IndexCount = Element.NumIndices;
    Command.FirstIndex = Element.FirstIndex;
    Command.BaseVertexIndex = Element.BaseVertexIndex;
    Command.NumInstances = 1;
    Command.bInstanced = false;
    Command.InstanceId = Element.InstanceId;
    Command.bBindMaterial = BindMaterial;
    Command.Primitive = Element.Primitive;
    Command.MaterialName = Batch.MaterialName.c_str();
    return Command;
}

static MeshPassShader ShaderForPass(const MeshBatch& Batch, EMaterialPass Pass, bool BInstanced)
{
    if (!Batch.ShaderMap)
        return {};
    return Batch.ShaderMap->Get(Pass, BInstanced);
}

void ShadowDepthPassProcessor::AddMeshBatch(const MeshBatch& Batch)
{
    if (Batch.Elements.empty())
        return;

    const MeshPassShader Shader = ShaderForPass(Batch, EMaterialPass::Depth, false);
    for (const MeshBatchElement& Element : Batch.Elements)
    {
        if (!HasGeometry(Element) || !Shader.VertexShader)
            continue;
        AddCommand(MakeCommand(Batch, Element, Shader, false, false));
    }
}

MeshPassShader BasePassProcessor::ResolveSingleShader(const MeshBatch& Batch) const
{
    if (PassConfig.ForcedShader && PassConfig.Shaders)
    {
        CompiledShader* Shader = PassConfig.Shaders->GetShader(PassConfig.ForcedShader);
        if (!Shader)
            Shader = PassConfig.Shaders->GetDefault();
        if (Shader)
            return { Shader->PSO.get(), Shader->VertexShader.get(), Shader->PixelShader.get() };
    }

    MeshPassShader Shader = ShaderForPass(Batch, PassConfig.MaterialPass, false);
    if (Shader.VertexShader)
        return Shader;
    return PassConfig.Fallback;
}

void BasePassProcessor::AddMeshBatch(const MeshBatch& Batch)
{
    if (Batch.Elements.empty())
        return;

    MeshPassShader Shader = ResolveSingleShader(Batch);
    if (!Shader.VertexShader)
        return;

    for (const MeshBatchElement& Element : Batch.Elements)
    {
        if (!HasGeometry(Element))
            continue;
        AddCommand(MakeCommand(Batch, Element, Shader, false, PassConfig.bBindMaterials));
    }
}

} // namespace Kiwi
