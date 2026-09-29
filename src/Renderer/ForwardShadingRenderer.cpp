#include "Renderer/ForwardShadingRenderer.h"

#include "KiwiEngineApp.h"

namespace Kiwi
{

void ForwardShadingSceneRenderer::Render(RHICommandContext* Ctx, RHITextureView* SceneRTV, const Viewport& Vp, const ScissorRect& Sr)
{
    InitView();

    // ================================================================
    // FORWARD RENDERING PATH (Unlit ViewMode)
    // ================================================================

    // ---- Set render targets ----
    Ctx->SetRenderTargets(&SceneRTV, 1, App.GetDSV());
    Ctx->SetViewports(&Vp, 1);
    Ctx->SetScissorRects(&Sr, 1);

    // ---- Clear ----
    ClearColorValue ClearColor = { 0.12f, 0.12f, 0.18f, 1.0f };
    Ctx->ClearRenderTargetView(SceneRTV, ClearColor);
    ClearDepthStencilValue DepthClear = { 1.0f, 0 };
    Ctx->ClearDepthStencilView(App.GetDSV(), DepthClear, 0x03);

    // ---- Setup pipeline ----
    Ctx->SetPipelineState(App.PipelineState.get());
    Ctx->SetInputLayout(App.InputLayout.get());
    Ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);

    const char* ForwardShader = nullptr;
    if (View.ViewMode == EViewMode::Unlit)
        ForwardShader = "Unlit";
    else if (View.ViewMode != EViewMode::Lit)
        ForwardShader = "DefaultLit";

    Ctx->BeginEvent("Forward Pass");
    App.PassTimer.Begin("Forward Pass");
    Ctx->SetConstantBuffer(0, View.ViewUniformBufferRef.GetReference());

    BasePassProcessor::Config ForwardConfig;
    ForwardConfig.MaterialPass = EMaterialPass::Forward;
    ForwardConfig.bBindMaterials = true;
    ForwardConfig.ForcedShader = ForwardShader;
    ForwardConfig.Shaders = &App.ShaderLibrary;
    BasePassProcessor ForwardPass(ForwardConfig);
    ForwardPass.Process(View.DynamicMeshElements, View.InstanceCulling);
    App.SubmitMeshDrawCommands(Ctx, ForwardPass.GetCommands(), View.InstanceCulling);
    App.PassTimer.End();
    Ctx->EndEvent();

    // ---- Draw Gizmo ----
    Ctx->BeginEvent("Gizmo Pass");
    App.PassTimer.Begin("Gizmo Pass");
    Ctx->SetConstantBuffer(0, View.ViewUniformBufferRef.GetReference());
    Ctx->SetCullMode(ECullMode::Back);
    App.DrawGizmo(Ctx);
    Ctx->ClearCullModeOverride();
    App.PassTimer.End();
    Ctx->EndEvent();
}

} // namespace Kiwi
