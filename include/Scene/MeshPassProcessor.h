#pragma once

#include "Scene/MeshBatch.h"
#include <vector>

namespace Kiwi { class GPUScene; }

namespace Kiwi
{

class ShaderLibrary;

// Turns MeshBatches into MeshDrawCommands for one pass.
// Same role as UE5 FMeshPassProcessor.
class MeshPassProcessor
{
public:
    virtual ~MeshPassProcessor() = default;

    void Process(const std::vector<MeshBatch>& batches, GPUScene& gpuScene);

    const std::vector<MeshDrawCommand>& GetCommands() const { return m_Commands; }

protected:
    virtual bool ShouldDraw(const MeshBatch& batch) const = 0;
    virtual void AddMeshBatch(const MeshBatch& batch) = 0;

    void AddCommand(const MeshDrawCommand& command) { m_Commands.push_back(command); }

    static bool HasGeometry(const MeshBatchElement& element)
    {
        return element.VertexBuffer && element.IndexBuffer && element.NumIndices > 0;
    }

    std::vector<MeshDrawCommand> m_Commands;
};

// Depth-only draw used by cascaded shadow maps.
class ShadowDepthPassProcessor : public MeshPassProcessor
{
public:
    ShadowDepthPassProcessor() = default;

protected:
    bool ShouldDraw(const MeshBatch& batch) const override
    {
        return batch.bCastShadow && batch.bUseForDepthPass;
    }

    void AddMeshBatch(const MeshBatch& batch) override;
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

    explicit BasePassProcessor(Config config) : m_Config(std::move(config)) {}

protected:
    bool ShouldDraw(const MeshBatch& batch) const override
    {
        return batch.bUseForMaterial;
    }

    void AddMeshBatch(const MeshBatch& batch) override;

private:
    MeshPassShader ResolveSingleShader(const MeshBatch& batch) const;

    Config m_Config;
};

} // namespace Kiwi
