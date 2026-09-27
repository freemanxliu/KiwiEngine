#include "KiwiEngineApp.h"

void KiwiEngineApp::RenderDeferred(
    RHICommandContext* ctx,
    RHITextureView* sceneRTV,
    const Viewport& vp,
    const ScissorRect& sr)
{
        // ================================================================
        // DEFERRED RENDERING PATH
        // ================================================================

        // ==== PASS 0: Upload GPU Scene Buffer (once per frame, before any pass) ====
        m_GPUScene.Update(m_Scene, m_MaterialLibrary, m_RenderList);
        m_GPUScene.UploadToGPU();

        // ==== PASS 1: Shadow Pass (CSM) ====
        UpdateShadowData();
        RenderShadowPass(ctx);

        // ==== PASS 2: G-Buffer Geometry Pass ====
        ctx->BeginEvent("G-Buffer Pass");
        m_PassTimer.Begin("G-Buffer Pass");

        // Transition G-Buffer RTs to render target state
        for (int i = 0; i < GBUFFER_COUNT; i++)
        {
            ctx->ResourceBarrier(m_GBufferRT[i].get(),
                RESOURCE_STATE_COMMON, RESOURCE_STATE_RENDER_TARGET);
        }

        // Set G-Buffer MRT + depth
        RHITextureView* gbufferRTVs[GBUFFER_COUNT] = {
            m_GBufferRTV[0].get(),
            m_GBufferRTV[1].get(),
            m_GBufferRTV[2].get(),
        };
        ctx->SetRenderTargets(gbufferRTVs, GBUFFER_COUNT, GetDSV());
        ctx->SetViewports(&vp, 1);
        ctx->SetScissorRects(&sr, 1);

        // Clear G-Buffer and depth
        ClearColorValue clearBlack = { 0.0f, 0.0f, 0.0f, 0.0f };
        for (int i = 0; i < GBUFFER_COUNT; i++)
        {
            ctx->ClearRenderTargetView(gbufferRTVs[i], clearBlack);
        }
        ClearDepthStencilValue depthClear = { 1.0f, 0 };
        ctx->ClearDepthStencilView(GetDSV(), depthClear, 0x03);

        // Set G-Buffer pipeline state
        ctx->SetPipelineState(m_GBufferPSO.get());
        ctx->SetVertexShader(m_GBufferVS.get());
        ctx->SetPixelShader(m_GBufferPS.get());
        ctx->SetInputLayout(m_InputLayout.get());
        ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);

        // Draw all mesh components with G-Buffer shader
        DrawSceneMeshesDeferred(ctx);

        m_PassTimer.End();
        ctx->EndEvent();

        // Transition G-Buffer RTs to shader resource
        for (int i = 0; i < GBUFFER_COUNT; i++)
        {
            ctx->ResourceBarrier(m_GBufferRT[i].get(),
                RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        // Transition depth buffer to shader resource for deferred lighting
        ctx->ResourceBarrier(GetDepthTexture(),
            RESOURCE_STATE_DEPTH_WRITE, RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        // ==== PASS 2: Deferred Lighting / Buffer Visualization ====
        if (m_ViewMode == EViewMode::Lit)
        {
            // ---- UE5 Multi-Pass Deferred Lighting ----
            ctx->BeginEvent("Deferred Lighting");
            m_PassTimer.Begin("Deferred Lighting");

            ctx->SetRenderTargets(&sceneRTV, 1, nullptr);
            ctx->SetViewports(&vp, 1);
            ctx->SetScissorRects(&sr, 1);

            // Bind G-Buffer + depth (shared across all sub-passes)
            ctx->SetShaderResourceView(0, m_GBufferSRV[0].get());
            ctx->SetShaderResourceView(1, m_GBufferSRV[1].get());
            ctx->SetShaderResourceView(2, m_GBufferSRV[2].get());
            ctx->SetShaderResourceView(7, GetDepthSRV());
            ctx->SetSampler(0, m_PostProcessSampler.get());

            // Upload ViewUB (b0)
            UpdateDeferredLightingCB();

            // ---- Step 1: Ambient Pass (opaque first write) ----
            ctx->BeginEvent("Ambient Pass");
            if (m_DeferredAmbientPSO)
            {
                ctx->SetPipelineState(m_DeferredAmbientPSO.get());
                ctx->SetVertexShader(m_DeferredAmbientVS.get());
                ctx->SetPixelShader(m_DeferredAmbientPS.get());
                ctx->SetInputLayout(nullptr);
                ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);
                ctx->Draw(3, 0);
            }
            else
            {
                ClearColorValue clearColor = { 0.05f, 0.05f, 0.08f, 1.0f };
                ctx->ClearRenderTargetView(sceneRTV, clearColor);
            }
            ctx->EndEvent();

            // ---- Step 2: Per-Light Passes (additive blend) ----
            if (m_DeferredLightingAdditivePSO && m_NumActiveLights > 0)
            {
                ctx->SetPipelineState(m_DeferredLightingAdditivePSO.get());
                ctx->SetVertexShader(m_DeferredLightingVS.get());
                ctx->SetPixelShader(m_DeferredLightingPS.get());
                ctx->SetInputLayout(nullptr);
                ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);

                // Shadow atlas + comparison sampler
                ctx->SetShaderResourceView(3, m_ShadowAtlasSRV.get());
                ctx->SetSampler(2, m_ShadowSampler.get());
                UploadShadowUB();
                ctx->SetConstantBuffer(2, m_ShadowCB.get());

                for (int li = 0; li < m_NumActiveLights; li++)
                {
                    ctx->BeginEvent("Light Pass");

                    // Upload LightUB (b3)
                    LightUniformBuffer lub = {};
                    memcpy(lub.ColorIntensity, m_LightDataCache[li].ColorIntensity, sizeof(float) * 3);
                    lub.LightType = m_LightDataCache[li].Type;
                    memcpy(lub.DirectionOrPos, m_LightDataCache[li].DirectionOrPos, sizeof(float) * 3);
                    lub.Radius = m_LightDataCache[li].Radius;

                    void* mapped = m_LightCB->Map();
                    if (mapped) { memcpy(mapped, &lub, sizeof(lub)); m_LightCB->Unmap(); }
                    ctx->SetConstantBuffer(3, m_LightCB.get());

                    ctx->Draw(3, 0);
                    ctx->EndEvent();
                }
            }

            // Unbind SRVs
            ctx->SetShaderResourceView(0, nullptr);
            ctx->SetShaderResourceView(1, nullptr);
            ctx->SetShaderResourceView(2, nullptr);
            ctx->SetShaderResourceView(3, nullptr);
            ctx->SetShaderResourceView(7, nullptr);

            m_PassTimer.End();
            ctx->EndEvent();
        }
        else
        {
            // Buffer visualization pass (BaseColor, Roughness, Metallic)
            ctx->BeginEvent("Buffer Visualization Pass");
            m_PassTimer.Begin("Buffer Visualization Pass");

            ctx->SetRenderTargets(&sceneRTV, 1, nullptr);
            ctx->SetViewports(&vp, 1);
            ctx->SetScissorRects(&sr, 1);

            ClearColorValue clearColor = { 0.12f, 0.12f, 0.18f, 1.0f };
            ctx->ClearRenderTargetView(sceneRTV, clearColor);

            // Set buffer visualization pipeline
            ctx->SetPipelineState(m_BufferVisPSO.get());
            ctx->SetVertexShader(m_BufferVisVS.get());
            ctx->SetPixelShader(m_BufferVisPS.get());
            ctx->SetInputLayout(nullptr);
            ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);

            // Bind G-Buffer SRVs
            ctx->SetShaderResourceView(0, m_GBufferSRV[0].get());
            ctx->SetShaderResourceView(1, m_GBufferSRV[1].get());
            ctx->SetShaderResourceView(2, m_GBufferSRV[2].get());

            ctx->SetSampler(0, m_PostProcessSampler.get());

            // Update CB with visualization mode
            UpdateBufferVisualizationCB();

            ctx->Draw(3, 0);

            ctx->SetShaderResourceView(0, nullptr);
            ctx->SetShaderResourceView(1, nullptr);
            ctx->SetShaderResourceView(2, nullptr);

            m_PassTimer.End();
            ctx->EndEvent();
        }

        // Transition depth buffer back to depth write for gizmo pass
        ctx->ResourceBarrier(GetDepthTexture(),
            RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RESOURCE_STATE_DEPTH_WRITE);

        // ==== PASS 3: Forward Gizmo Pass (on top of deferred result) ====
        ctx->BeginEvent("Gizmo Pass");
        m_PassTimer.Begin("Gizmo Pass");

        UploadViewUB(ctx);

        // Re-set render targets for forward gizmo drawing (with depth for correct occlusion)
        ctx->SetRenderTargets(&sceneRTV, 1, GetDSV());
        ctx->SetViewports(&vp, 1);
        ctx->SetScissorRects(&sr, 1);

        ctx->SetInputLayout(m_InputLayout.get());
        ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);
        DrawGizmo(ctx);

        m_PassTimer.End();
        ctx->EndEvent();
}

void KiwiEngineApp::RenderForward(
    RHICommandContext* ctx,
    RHITextureView* sceneRTV,
    const Viewport& vp,
    const ScissorRect& sr)
{
        // ================================================================
        // FORWARD RENDERING PATH (Unlit ViewMode)
        // ================================================================

        // ---- Set render targets ----
        ctx->SetRenderTargets(&sceneRTV, 1, GetDSV());
        ctx->SetViewports(&vp, 1);
        ctx->SetScissorRects(&sr, 1);

        // ---- Clear ----
        ClearColorValue clearColor = { 0.12f, 0.12f, 0.18f, 1.0f };
        ctx->ClearRenderTargetView(sceneRTV, clearColor);
        ClearDepthStencilValue depthClear = { 1.0f, 0 };
        ctx->ClearDepthStencilView(GetDSV(), depthClear, 0x03);

        // ---- Setup pipeline ----
        ctx->SetPipelineState(m_PipelineState.get());
        ctx->SetInputLayout(m_InputLayout.get());
        ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);

        const char* forwardShader = nullptr;
        if (m_ViewMode == EViewMode::Unlit)
            forwardShader = "Unlit";
        else if (m_ViewMode != EViewMode::Lit)
            forwardShader = "DefaultLit";

        ctx->BeginEvent("Forward Pass");
        m_PassTimer.Begin("Forward Pass");
        m_GPUScene.Update(m_Scene, m_MaterialLibrary, m_RenderList);
        m_GPUScene.UploadToGPU();
        DrawSceneMeshesForward(ctx, forwardShader);
        m_PassTimer.End();
        ctx->EndEvent();

        // ---- Draw Gizmo ----
        ctx->BeginEvent("Gizmo Pass");
        m_PassTimer.Begin("Gizmo Pass");
        UploadViewUB(ctx);
        DrawGizmo(ctx);
        m_PassTimer.End();
        ctx->EndEvent();
}
