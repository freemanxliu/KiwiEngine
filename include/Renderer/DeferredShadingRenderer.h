#pragma once

#include "Core/SceneRendering.h"

namespace Kiwi
{
    // Shadow depth, G-Buffer, deferred lighting and gizmo passes (UE5 FDeferredShadingSceneRenderer).
    // Also hosts the CPU ray tracing path, as UE5 does for path tracing.
    class DeferredShadingSceneRenderer : public SceneRenderer
    {
    public:
        DeferredShadingSceneRenderer(KiwiEngineApp& App, RHIDevice* Device, ERenderPath InPath)
            : SceneRenderer(App, Device)
            , Path(InPath)
        {
        }

        ERenderPath GetRenderPath() const override { return Path; }
        void Render(RHICommandContext* Ctx, RHITextureView* SceneRTV, const Viewport& Vp, const ScissorRect& Sr) override;

    private:
        // Core/RayTracing.cpp
        void RenderRayTracing(RHICommandContext* Ctx, RHITextureView* SceneRTV, const Viewport& Vp, const ScissorRect& Sr);

        ERenderPath Path;
    };
}
