#include "Core/SceneRendering.h"

#include "Core/RendererUtils.h"
#include "KiwiEngineApp.h"
#include "Renderer/DeferredShadingRenderer.h"
#include "Renderer/ForwardShadingRenderer.h"

#include <algorithm>
#include <cstring>

namespace Kiwi
{

void ViewMatrices::Init(const Mat4& InViewMatrix, const Mat4& InProjectionMatrix, const Vec3& InViewOrigin, float InNearPlane, float InFarPlane)
{
    ViewMatrix = InViewMatrix;
    ProjectionMatrix = InProjectionMatrix;
    ViewProjectionMatrix = InViewMatrix * InProjectionMatrix;
    InvViewProjectionMatrix = ViewProjectionMatrix.Inverse();
    ViewOrigin = InViewOrigin;
    NearPlane = InNearPlane;
    FarPlane = InFarPlane;
}

void ViewInfo::UpdateViewUniformBuffer(const GPULightData* Lights, int32_t NumLights, int32_t NumDirectionalLights)
{
    if (!ViewUniformBufferRef) return;

    const ViewMatrices& Vm = Matrices;
    const Vec3& Origin = Vm.GetViewOrigin();

    ViewUniformBuffer Vub = {};
    memcpy(Vub.ViewMatrix, Vm.GetViewMatrix().m, sizeof(Vub.ViewMatrix));
    memcpy(Vub.ProjectionMatrix, Vm.GetProjectionMatrix().m, sizeof(Vub.ProjectionMatrix));
    memcpy(Vub.ViewProjectionMatrix, Vm.GetViewProjectionMatrix().m, sizeof(Vub.ViewProjectionMatrix));
    memcpy(Vub.InvViewProjectionMatrix, Vm.GetInvViewProjectionMatrix().m, sizeof(Vub.InvViewProjectionMatrix));
    Vub.CameraPos[0] = Origin.x;
    Vub.CameraPos[1] = Origin.y;
    Vub.CameraPos[2] = Origin.z;
    Vub.ViewPadding1 = (float)(int)ViewMode;
    Vub.ScreenWidth = (float)ViewWidth;
    Vub.ScreenHeight = (float)ViewHeight;
    Vub.NearPlane = Vm.GetNearPlane();
    Vub.FarPlane = Vm.GetFarPlane();
    Vub.NumLights = NumLights;
    Vub.NumDirectionalLights = NumDirectionalLights;
    Vub.ViewPadding2[0] = Vub.ViewPadding2[1] = 0.0f;
    if (Lights)
        memcpy(Vub.Lights, Lights, sizeof(GPULightData) * std::min<int32_t>(std::max(NumLights, 0), MAX_LIGHTS));

    ViewUniformBufferRef.UpdateUniformBufferImmediate(Vub);
}

SceneRenderer::SceneRenderer(KiwiEngineApp& InApp, RHIDevice* Device)
    : App(InApp)
{
    View.ViewUniformBufferRef = TUniformBufferRef<ViewUniformBuffer>::CreateEmptyUniformBufferImmediate(Device, EUniformBufferUsage::SingleFrame, "ViewUniformBuffer");
    View.InstanceCulling.Initialize(Device);
}

std::unique_ptr<SceneRenderer> SceneRenderer::Create(ERenderPath Path, KiwiEngineApp& App, RHIDevice* Device)
{
    if (Path == ERenderPath::Forward)
        return std::make_unique<ForwardShadingSceneRenderer>(App, Device);
    return std::make_unique<DeferredShadingSceneRenderer>(App, Device, Path);
}

void SceneRenderer::InitView()
{
    View.InstanceCulling.Reset();
    ComputeViewVisibility(App.RenderScene);
    GatherDynamicMeshElements();
}

void SceneRenderer::ComputeViewVisibility(const RenderScene& InScene)
{
    std::vector<RenderItem>& VisibleItems = View.VisibleItems;
    VisibleItems.clear();

    Frustum ViewFrustum;
    ViewFrustum.ExtractFromViewProjection(View.Matrices.GetViewProjectionMatrix());

    // Proxies carry world bounds from the game thread, so culling never touches the game scene.
    for (const auto& [Key, Primitive] : InScene.GetPrimitives())
    {
        const PrimitiveSceneProxy& Proxy = Primitive.Proxy;
        if (!Proxy.bVisible || !Primitive.HasGPUSlots())
            continue;
        if (!ViewFrustum.TestAABB(Proxy.WorldBounds))
            continue;

        Vec3 Diff = Proxy.WorldBounds.GetCenter() - View.Matrices.GetViewOrigin();
        float DistSq = Diff.Dot(Diff);

        RenderItem Item;
        Item.Primitive = &Primitive;
        Item.SortOrder = Proxy.SortOrder;
        Item.DistToCamera = DistSq;
        Item.MeshID = (uint32_t)Proxy.PrimitiveType;
        Item.MaterialName = Proxy.MaterialName.c_str();
        VisibleItems.push_back(Item);
    }
}

void SceneRenderer::GatherDynamicMeshElements()
{
    std::vector<MeshBatch>& MeshBatches = View.DynamicMeshElements;
    MeshBatches.clear();
    MeshBatches.reserve(View.VisibleItems.size());

    for (const RenderItem& Item : View.VisibleItems)
    {
        const PrimitiveSceneInfo* Primitive = Item.Primitive;
        const PrimitiveSceneProxy& Proxy = Primitive->Proxy;

        SharedMeshEntry Geometry = App.RenderScene.GetSharedMesh(Proxy.PrimitiveType);
        if (!Geometry.VertexBuffer || Geometry.IndexCount == 0)
            continue;

        MeshBatch Batch;
        Batch.ShaderMap = App.MaterialShaders.GetShaderMap(Proxy.SurfaceShader);
        Batch.MeshId = Item.MeshID;
        Batch.MaterialName = Proxy.MaterialName;
        Batch.SurfaceShader = Proxy.SurfaceShader;
        Batch.SortPriority = Item.SortOrder;
        Batch.CullMode = Proxy.CullMode;
        Batch.bCastShadow = true;
        Batch.bUseForMaterial = true;
        Batch.bUseForDepthPass = true;

        MeshBatchElement Element;
        Element.InstanceId = Primitive->InstanceId;
        Element.PrimitiveId = Primitive->PrimitiveId;
        Element.Primitive = Primitive;
        Element.ViewDistanceSq = Item.DistToCamera;
        Element.VertexBuffer = Geometry.VertexBuffer;
        Element.IndexBuffer = Geometry.IndexBuffer;
        Element.VertexCount = Geometry.VertexCount;
        Element.NumIndices = Geometry.IndexCount;
        Batch.Elements.push_back(Element);
        MeshBatches.push_back(std::move(Batch));
    }
}

} // namespace Kiwi

// Game thread: keep the user's choices valid for the current RHI.
void KiwiEngineApp::SanitizeRenderPath()
{
    // GL/Vulkan have no G-Buffer. Buffer visualization needs the deferred path.
    if (!IsDeferredRHI(GetCurrentRHIType()))
        RenderPath = ERenderPath::Forward;
    if (RenderPath != ERenderPath::Deferred && IsBufferVisualization(ViewMode))
        ViewMode = EViewMode::Lit;
}

// Render thread. Falls back to forward for this frame only when the G-Buffer is missing.
ERenderPath KiwiEngineApp::ResolveRenderPath() const
{
    if (RenderParams.RenderPath == ERenderPath::Deferred && (!GBufferPSO || !GBufferRT[0]))
        return ERenderPath::Forward;
    return RenderParams.RenderPath;
}

void KiwiEngineApp::PrepareSceneRenderer(ERenderPath Path)
{
    if (!SceneRenderer || SceneRenderer->GetRenderPath() != Path)
    {
        // The old renderer's view and draw instance buffers may still be referenced by submitted frames.
        GetRenderingThread().WaitForRHIThread();
        SceneRenderer = SceneRenderer::Create(Path, *this, GetDevice());
    }

    const FrameRenderParams& Params = RenderParams;
    ViewInfo& View = SceneRenderer->GetView();
    View.Matrices.Init(Params.ViewMatrix, Params.ProjectionMatrix, Params.CameraPosition, Params.NearPlane, Params.FarPlane);
    View.ViewMode = Params.ViewMode;
    View.ViewWidth = Params.ViewWidth;
    View.ViewHeight = Params.ViewHeight;
}
