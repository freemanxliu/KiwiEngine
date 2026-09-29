#include "Renderer/DeferredShadingRenderer.h"

#include "KiwiEngineApp.h"

namespace Kiwi
{

void DeferredShadingSceneRenderer::Render(RHICommandContext* Ctx, RHITextureView* SceneRTV, const Viewport& Vp, const ScissorRect& Sr)
{
    InitView();

    if (Path == ERenderPath::RayTracing)
    {
        RenderRayTracing(Ctx, SceneRTV, Vp, Sr);
        return;
    }

    // ================================================================
    // DEFERRED RENDERING PATH
    // ================================================================

    // ==== PASS 0: Each pass records its own draw commands from the view's mesh batches ====
    ShadowDepthPassProcessor ShadowPass;
    ShadowPass.Process(View.DynamicMeshElements, View.InstanceCulling);

    // ==== PASS 1: Shadow Pass (CSM) ====
    App.UpdateShadowData();
    App.RenderShadowPass(Ctx, ShadowPass.GetCommands(), View.InstanceCulling);

    // ==== PASS 2: G-Buffer Geometry Pass ====
    Ctx->BeginEvent("G-Buffer Pass");
    App.PassTimer.Begin("G-Buffer Pass");

    // Transition G-Buffer RTs to render target state
    for (int I = 0; I < KiwiEngineApp::GBUFFER_COUNT; I++)
    {
        Ctx->ResourceBarrier(App.GBufferRT[I].get(), RESOURCE_STATE_COMMON, RESOURCE_STATE_RENDER_TARGET);
    }

    // Set G-Buffer MRT + depth
    RHITextureView* GbufferRTVs[KiwiEngineApp::GBUFFER_COUNT] = {};
    for (int I = 0; I < KiwiEngineApp::GBUFFER_COUNT; I++)
        GbufferRTVs[I] = App.GBufferRTV[I].get();
    Ctx->SetRenderTargets(GbufferRTVs, KiwiEngineApp::GBUFFER_COUNT, App.GetDSV());
    Ctx->SetViewports(&Vp, 1);
    Ctx->SetScissorRects(&Sr, 1);

    // Clear G-Buffer and depth
    ClearColorValue ClearBlack = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int I = 0; I < KiwiEngineApp::GBUFFER_COUNT; I++)
    {
        Ctx->ClearRenderTargetView(GbufferRTVs[I], ClearBlack);
    }
    ClearDepthStencilValue DepthClear = { 1.0f, 0 };
    Ctx->ClearDepthStencilView(App.GetDSV(), DepthClear, 0x03);

    // Set G-Buffer pipeline state
    Ctx->SetPipelineState(App.GBufferPSO.get());
    Ctx->SetVertexShader(App.GBufferVS.get());
    Ctx->SetPixelShader(App.GBufferPS.get());
    Ctx->SetInputLayout(App.InputLayout.get());
    Ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);

    // Draw visible meshes into the G-Buffer.
    Ctx->SetConstantBuffer(0, View.ViewUniformBufferRef.GetReference());
    BasePassProcessor::Config GbufferConfig;
    GbufferConfig.Fallback = { App.GBufferPSO.get(), App.GBufferVS.get(), App.GBufferPS.get() };
    GbufferConfig.MaterialPass = EMaterialPass::GBuffer;
    GbufferConfig.bBindMaterials = true;
    BasePassProcessor GbufferPass(GbufferConfig);
    GbufferPass.Process(View.DynamicMeshElements, View.InstanceCulling);
    App.SubmitMeshDrawCommands(Ctx, GbufferPass.GetCommands(), View.InstanceCulling);

    App.PassTimer.End();
    Ctx->EndEvent();

    // Transition G-Buffer RTs to shader resource
    for (int I = 0; I < KiwiEngineApp::GBUFFER_COUNT; I++)
    {
        Ctx->ResourceBarrier(App.GBufferRT[I].get(), RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    // Transition depth buffer to shader resource for deferred lighting
    Ctx->ResourceBarrier(App.GetDepthTexture(), RESOURCE_STATE_DEPTH_WRITE, RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // Fullscreen lighting does not use the mesh cull override.
    Ctx->ClearCullModeOverride();

    // ==== PASS 2: Deferred Lighting / Buffer Visualization ====
    if (View.ViewMode == EViewMode::Lit)
    {
        // ---- UE5 Multi-Pass Deferred Lighting ----
        Ctx->BeginEvent("Deferred Lighting");
        App.PassTimer.Begin("Deferred Lighting");

        Ctx->SetRenderTargets(&SceneRTV, 1, nullptr);
        Ctx->SetViewports(&Vp, 1);
        Ctx->SetScissorRects(&Sr, 1);

        // Bind G-Buffer + depth (shared across all sub-passes)
        Ctx->SetShaderResourceView(0, App.GBufferSRV[0].get());
        Ctx->SetShaderResourceView(1, App.GBufferSRV[1].get());
        Ctx->SetShaderResourceView(2, App.GBufferSRV[2].get());
        Ctx->SetShaderResourceView(9, App.GBufferSRV[3].get());
        Ctx->SetShaderResourceView(7, App.GetDepthSRV());
        Ctx->SetSampler(0, App.PostProcessSampler.get());

        Ctx->SetConstantBuffer(0, View.ViewUniformBufferRef.GetReference());
        App.UpdateDeferredLightingCB();

        // ---- Step 1: Ambient Pass (opaque first write) ----
        Ctx->BeginEvent("Ambient Pass");
        if (App.DeferredAmbientPSO)
        {
            Ctx->SetPipelineState(App.DeferredAmbientPSO.get());
            Ctx->SetVertexShader(App.DeferredAmbientVS.get());
            Ctx->SetPixelShader(App.DeferredAmbientPS.get());
            Ctx->SetInputLayout(nullptr);
            Ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);
            Ctx->Draw(3, 0);
        }
        else
        {
            ClearColorValue ClearColor = { 0.05f, 0.05f, 0.08f, 1.0f };
            Ctx->ClearRenderTargetView(SceneRTV, ClearColor);
        }
        Ctx->EndEvent();

        // ---- Step 2: Instanced light draws (additive blend) ----
        // Lights come from g_Lights in ViewUB: directional first, then point lights.
        const uint32_t NumDirectional = (uint32_t)App.RenderScene.GetNumDirectionalLights();
        const uint32_t NumPoint = (uint32_t)(App.RenderScene.GetNumLights() - App.RenderScene.GetNumDirectionalLights());
        if (App.LightVolumeIB && App.RenderScene.GetNumLights() > 0)
        {
            Ctx->SetInputLayout(nullptr);
            Ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);
            IndexBufferView IbView;
            IbView.BufferLocation = 0;
            IbView.SizeInBytes = (KiwiEngineApp::LIGHT_VOLUME_INDEX_COUNT + 3) * sizeof(uint32_t);
            IbView.Format = EFormat::R32_UINT;
            Ctx->SetIndexBuffer(App.LightVolumeIB.get(), &IbView);

            // Shadow atlas + comparison sampler (the shared pixel shader declares them for both draws)
            Ctx->SetShaderResourceView(3, App.ShadowAtlasSRV.get());
            Ctx->SetSampler(2, App.ShadowSampler.get());
            App.UploadShadowUB();
            Ctx->SetConstantBuffer(2, App.ShadowCB.GetReference());

            if (App.DeferredLightingAdditivePSO && NumDirectional > 0)
            {
                Ctx->BeginEvent("Directional Lights");
                Ctx->SetPipelineState(App.DeferredLightingAdditivePSO.get());
                Ctx->SetVertexShader(App.DeferredLightingVS.get());
                Ctx->SetPixelShader(App.DeferredLightingPS.get());
                Ctx->DrawIndexedInstanced(3, NumDirectional, KiwiEngineApp::LIGHT_VOLUME_INDEX_COUNT, 0, 0);
                Ctx->EndEvent();
            }

            if (App.DeferredPointLightPSO && NumPoint > 0)
            {
                Ctx->BeginEvent("Point Lights");
                Ctx->SetPipelineState(App.DeferredPointLightPSO.get());
                Ctx->SetVertexShader(App.DeferredPointLightVS.get());
                Ctx->SetPixelShader(App.DeferredLightingPS.get());
                Ctx->DrawIndexedInstanced(KiwiEngineApp::LIGHT_VOLUME_INDEX_COUNT, NumPoint, 0, 0, 0);
                Ctx->EndEvent();
            }
        }

        // Unbind SRVs
        Ctx->SetShaderResourceView(0, nullptr);
        Ctx->SetShaderResourceView(1, nullptr);
        Ctx->SetShaderResourceView(2, nullptr);
        Ctx->SetShaderResourceView(3, nullptr);
        Ctx->SetShaderResourceView(7, nullptr);

        App.PassTimer.End();
        Ctx->EndEvent();
    }
    else
    {
        // Buffer visualization pass (BaseColor, Roughness, Metallic)
        Ctx->BeginEvent("Buffer Visualization Pass");
        App.PassTimer.Begin("Buffer Visualization Pass");

        Ctx->SetRenderTargets(&SceneRTV, 1, nullptr);
        Ctx->SetViewports(&Vp, 1);
        Ctx->SetScissorRects(&Sr, 1);

        ClearColorValue ClearColor = { 0.12f, 0.12f, 0.18f, 1.0f };
        Ctx->ClearRenderTargetView(SceneRTV, ClearColor);

        // Set buffer visualization pipeline
        Ctx->SetPipelineState(App.BufferVisPSO.get());
        Ctx->SetVertexShader(App.BufferVisVS.get());
        Ctx->SetPixelShader(App.BufferVisPS.get());
        Ctx->SetInputLayout(nullptr);
        Ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);

        // Bind G-Buffer SRVs
        Ctx->SetShaderResourceView(0, App.GBufferSRV[0].get());
        Ctx->SetShaderResourceView(1, App.GBufferSRV[1].get());
        Ctx->SetShaderResourceView(2, App.GBufferSRV[2].get());

        Ctx->SetSampler(0, App.PostProcessSampler.get());

        Ctx->SetConstantBuffer(0, View.ViewUniformBufferRef.GetReference());
        App.UpdateBufferVisualizationCB();

        Ctx->Draw(3, 0);

        Ctx->SetShaderResourceView(0, nullptr);
        Ctx->SetShaderResourceView(1, nullptr);
        Ctx->SetShaderResourceView(2, nullptr);

        App.PassTimer.End();
        Ctx->EndEvent();
    }

    // Transition depth buffer back to depth write for gizmo pass
    Ctx->ResourceBarrier(App.GetDepthTexture(), RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RESOURCE_STATE_DEPTH_WRITE);

    // ==== PASS 3: Forward Gizmo Pass (on top of deferred result) ====
    Ctx->BeginEvent("Gizmo Pass");
    App.PassTimer.Begin("Gizmo Pass");

    Ctx->SetConstantBuffer(0, View.ViewUniformBufferRef.GetReference());

    // Re-set render targets for forward gizmo drawing (with depth for correct occlusion)
    Ctx->SetRenderTargets(&SceneRTV, 1, App.GetDSV());
    Ctx->SetViewports(&Vp, 1);
    Ctx->SetScissorRects(&Sr, 1);

    Ctx->SetCullMode(ECullMode::Back);
    Ctx->SetInputLayout(App.InputLayout.get());
    Ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);
    App.DrawGizmo(Ctx);
    Ctx->ClearCullModeOverride();

    App.PassTimer.End();
    Ctx->EndEvent();
}

} // namespace Kiwi
