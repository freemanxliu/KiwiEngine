#pragma once

#include "Math/Math.h"
#include "RHI/RHI.h"
#include "RHI/UniformBuffer.h"
#include "Renderer/InstanceCulling.h"
#include "Scene/MeshBatch.h"
#include "Scene/Shaders.h"
#include "Scene/ViewMode.h"

#include <cstdint>
#include <memory>
#include <vector>

class KiwiEngineApp;

namespace Kiwi
{
    class RenderScene;
    class MaterialLibrary;
    class MeshComponent;

    struct PrimitiveSceneInfo;

    // A primitive that survived frustum culling.
    struct RenderItem
    {
        const PrimitiveSceneInfo* Primitive;
        int32_t SortOrder;           // Higher = rendered first
        float DistToCamera;          // Squared distance from object center to camera
        uint32_t MeshID;             // Shared mesh pool ID (EPrimitiveType)
        const char* MaterialName;    // Parent material name
    };

    // Camera transforms of a view (UE5 FViewMatrices). The combined matrices are derived in Init().
    class ViewMatrices
    {
    public:
        void Init(const Mat4& InViewMatrix, const Mat4& InProjectionMatrix, const Vec3& InViewOrigin, float InNearPlane, float InFarPlane);

        const Mat4& GetViewMatrix() const { return ViewMatrix; }
        const Mat4& GetProjectionMatrix() const { return ProjectionMatrix; }
        const Mat4& GetViewProjectionMatrix() const { return ViewProjectionMatrix; }
        const Mat4& GetInvViewProjectionMatrix() const { return InvViewProjectionMatrix; }
        const Vec3& GetViewOrigin() const { return ViewOrigin; }
        float GetNearPlane() const { return NearPlane; }
        float GetFarPlane() const { return FarPlane; }

    private:
        Mat4 ViewMatrix = Mat4::Identity();
        Mat4 ProjectionMatrix = Mat4::Identity();
        Mat4 ViewProjectionMatrix = Mat4::Identity();
        Mat4 InvViewProjectionMatrix = Mat4::Identity();
        Vec3 ViewOrigin = { 0.0f, 0.0f, 0.0f };
        float NearPlane = 0.1f;
        float FarPlane = 1000.0f;
    };

    // Per-view render state for one frame (UE5 FViewInfo).
    class ViewInfo
    {
    public:
        ViewMatrices Matrices;
        EViewMode ViewMode = EViewMode::Lit;
        uint32_t ViewWidth = 0;
        uint32_t ViewHeight = 0;

        // Visible mesh components in scene order. Filled by SceneRenderer::InitView().
        std::vector<RenderItem> VisibleItems;

        // One mesh batch per visible item. Each mesh pass processor sorts and merges its own draw commands.
        std::vector<MeshBatch> DynamicMeshElements;

        // Draw instance ids of this frame's mesh passes. Reset by SceneRenderer::InitView().
        InstanceCullingContext InstanceCulling;

        TUniformBufferRef<ViewUniformBuffer> ViewUniformBufferRef;

        // Uploads the view matrices, screen size and lights into ViewUniformBufferRef (b0).
        void UpdateViewUniformBuffer(const GPULightData* Lights, int32_t NumLights, int32_t NumDirectionalLights);
    };

    // Renders one frame of the scene for its view (UE5 FSceneRenderer).
    class SceneRenderer
    {
    public:
        SceneRenderer(KiwiEngineApp& InApp, RHIDevice* Device);
        virtual ~SceneRenderer() = default;

        virtual ERenderPath GetRenderPath() const = 0;
        virtual void Render(RHICommandContext* Ctx, RHITextureView* SceneRTV, const Viewport& Vp, const ScissorRect& Sr) = 0;

        ViewInfo& GetView() { return View; }
        const ViewInfo& GetView() const { return View; }

        static std::unique_ptr<SceneRenderer> Create(ERenderPath Path, KiwiEngineApp& App, RHIDevice* Device);

    protected:
        // Visibility then mesh batch gathering for View. Called at the start of Render(), after the GPU scene update.
        void InitView();

        KiwiEngineApp& App;
        ViewInfo View;

    private:
        // Frustum culls the scene and fills View.VisibleItems.
        void ComputeViewVisibility(const RenderScene& InScene);

        // Builds View.DynamicMeshElements from View.VisibleItems.
        void GatherDynamicMeshElements();
    };

    // Declared in Renderer/DeferredShadingRenderer.h and Renderer/ForwardShadingRenderer.h.
    class DeferredShadingSceneRenderer;
    class ForwardShadingSceneRenderer;
}
