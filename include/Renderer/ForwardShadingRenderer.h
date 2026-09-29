#pragma once

#include "Core/SceneRendering.h"

namespace Kiwi
{
    // Single forward base pass plus gizmos. Used on GL/Vulkan and when the G-Buffer is unavailable.
    class ForwardShadingSceneRenderer : public SceneRenderer
    {
    public:
        using SceneRenderer::SceneRenderer;

        ERenderPath GetRenderPath() const override { return ERenderPath::Forward; }
        void Render(RHICommandContext* Ctx, RHITextureView* SceneRTV, const Viewport& Vp, const ScissorRect& Sr) override;
    };
}
