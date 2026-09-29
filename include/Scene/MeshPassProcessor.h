#pragma once

#include "Scene/MeshBatch.h"
#include <vector>

namespace Kiwi { class InstanceCullingContext; }

namespace Kiwi
{

class ShaderLibrary;

// Turns MeshBatches into MeshDrawCommands for one pass, then sorts and merges them into instanced draws.
// Same role as UE5 FMeshPassProcessor plus the pass setup task that sorts and merges its commands.
class MeshPassProcessor
{
public:
    virtual ~MeshPassProcessor() = default;

    void Process(const std::vector<MeshBatch>& Batches, InstanceCullingContext& InstanceCulling);

    const std::vector<MeshDrawCommand>& GetCommands() const { return Commands; }

protected:
    virtual bool ShouldDraw(const MeshBatch& Batch) const = 0;
    virtual void AddMeshBatch(const MeshBatch& Batch) = 0;

    void AddCommand(const MeshDrawCommand& Command) { Commands.push_back(Command); }

    static bool HasGeometry(const MeshBatchElement& Element)
    {
        return Element.VertexBuffer && Element.IndexBuffer && Element.NumIndices > 0;
    }

    std::vector<MeshDrawCommand> Commands;
};

// Depth-only draw used by cascaded shadow maps.
class ShadowDepthPassProcessor : public MeshPassProcessor
{
public:
    ShadowDepthPassProcessor() = default;

protected:
    bool ShouldDraw(const MeshBatch& Batch) const override
    {
        return Batch.bCastShadow && Batch.bUseForDepthPass;
    }

    void AddMeshBatch(const MeshBatch& Batch) override;
};

// G-Buffer or forward base pass. Selects the surface shader per batch.
class BasePassProcessor : public MeshPassProcessor
{
public:
    struct Config
    {
        MeshPassShader Fallback{};
        EMaterialPass MaterialPass = EMaterialPass::GBuffer;
        bool bBindMaterials = true;
        // View-mode override. When set, the pass does not use the material shader map.
        const char* ForcedShader = nullptr;
        ShaderLibrary* Shaders = nullptr;
    };

    explicit BasePassProcessor(Config InConfig) : PassConfig(std::move(InConfig)) {}

protected:
    bool ShouldDraw(const MeshBatch& Batch) const override
    {
        return Batch.bUseForMaterial;
    }

    void AddMeshBatch(const MeshBatch& Batch) override;

private:
    MeshPassShader ResolveSingleShader(const MeshBatch& Batch) const;

    Config PassConfig;
};

} // namespace Kiwi
