#include "KiwiEngineApp.h"

#include <cstring>

static void BindMeshGeometry(RHICommandContext* ctx, const SharedMeshEntry& mesh)
{
    VertexBufferView vbView;
    vbView.BufferLocation = 0;
    vbView.SizeInBytes = mesh.VertexCount * sizeof(Vertex);
    vbView.StrideInBytes = sizeof(Vertex);
    RHIBuffer* vbPtr = mesh.VertexBuffer;
    ctx->SetVertexBuffers(0, &vbPtr, &vbView, 1);

    IndexBufferView ibView;
    ibView.BufferLocation = 0;
    ibView.SizeInBytes = mesh.IndexCount * sizeof(uint32_t);
    ibView.Format = EFormat::R32_UINT;
    ctx->SetIndexBuffer(mesh.IndexBuffer, &ibView);
}

void KiwiEngineApp::PrepareMeshBatches()
{
    m_MaterialShaders.SetSharedShader(EMaterialPass::Depth, false, { m_ShadowPassPSO.get(), m_ShadowPassVS.get(), nullptr });
    m_MaterialShaders.SetSharedShader(EMaterialPass::Depth, true, { m_ShadowPassPSO_Instanced.get(), m_ShadowPassVS_Instanced.get(), nullptr });
    m_MaterialShaders.SetSharedShader(EMaterialPass::GBuffer, true, { m_GBufferPSO_Instanced.get(), m_GBufferVS_Instanced.get(), m_GBufferPS.get() });
    
    m_MaterialShaders.SetFallback(EMaterialPass::GBuffer, { m_GBufferPSO.get(), m_GBufferVS.get(), m_GBufferPS.get() });

    m_GPUScene.Update(m_Scene, m_MaterialLibrary, m_RenderList);
    m_GPUScene.UploadToGPU();

    for (MeshBatch& batch : m_GPUScene.GetMeshBatches())
    {
        batch.ShaderMap = m_MaterialShaders.GetShaderMap(batch.SurfaceShader);
        for (MeshBatchElement& element : batch.Elements)
        {
            SharedMeshEntry mesh = GetSharedMesh(element.ObjectIndex);
            element.VertexBuffer = mesh.VertexBuffer;
            element.IndexBuffer = mesh.IndexBuffer;
            element.VertexCount = mesh.VertexCount;
            element.NumIndices = mesh.IndexCount;
        }
    }
}

void KiwiEngineApp::SubmitMeshDrawCommands(RHICommandContext* ctx, const std::vector<MeshDrawCommand>& commands)
{
    bool instancingBound = false;
    RHIPipelineState* lastPSO = nullptr;
    RHIShader* lastVS = nullptr;
    RHIShader* lastPS = nullptr;
    bool hasLastShader = false;
    RHIBuffer* lastVB = nullptr;
    const MeshComponent* lastMesh = nullptr;

    for (const MeshDrawCommand& command : commands)
    {
        if (command.bInstanced)
        {
            if (!instancingBound)
            {
                m_GPUScene.BindForInstancing(ctx);
                instancingBound = true;
            }
        }

        const bool shaderChanged = !hasLastShader
            || command.Shader.PSO != lastPSO
            || command.Shader.VertexShader != lastVS
            || command.Shader.PixelShader != lastPS;
        if (shaderChanged)
        {
            if (command.Shader.PSO)
                ctx->SetPipelineState(command.Shader.PSO);
            ctx->SetVertexShader(command.Shader.VertexShader);
            ctx->SetPixelShader(command.Shader.PixelShader);
            lastPSO = command.Shader.PSO;
            lastVS = command.Shader.VertexShader;
            lastPS = command.Shader.PixelShader;
            hasLastShader = true;
        }

        ctx->SetCullMode(command.CullMode);

        if (command.VertexBuffer != lastVB)
        {
            SharedMeshEntry mesh;
            mesh.VertexBuffer = command.VertexBuffer;
            mesh.IndexBuffer = command.IndexBuffer;
            mesh.VertexCount = command.VertexCount;
            mesh.IndexCount = command.IndexCount;
            BindMeshGeometry(ctx, mesh);
            lastVB = command.VertexBuffer;
        }

        if (command.bBindMaterial && command.Mesh && command.Mesh != lastMesh)
        {
            BindMaterialTextures(ctx, command.Mesh);
            lastMesh = command.Mesh;
        }

        if (command.bInstanced)
        {
            m_GPUScene.SetBatchStartIndex(ctx, command.InstanceOffset);
            ctx->DrawIndexedInstanced(command.IndexCount, command.NumInstances, command.FirstIndex, command.BaseVertexIndex, 0);
        }
        else
        {
            m_GPUScene.BindPrimitive(ctx, command.PrimitiveId);
            ctx->DrawIndexed(command.IndexCount, command.FirstIndex, command.BaseVertexIndex);
        }
    }
}

void KiwiEngineApp::RenderShadowPass(RHICommandContext* ctx, const std::vector<MeshDrawCommand>& commands)
{
    if (!m_ShadowPassPSO || !m_ShadowPassVS || m_ShadowUBData.NumCascades <= 0)
        return;

    ctx->BeginEvent("Shadow Pass");
    m_PassTimer.Begin("Shadow Pass");

    int numCascades = m_ShadowUBData.NumCascades;

    ctx->SetPipelineState(m_ShadowPassPSO.get());
    ctx->SetVertexShader(m_ShadowPassVS.get());
    ctx->SetPixelShader(nullptr);
    ctx->SetInputLayout(m_InputLayout.get());
    ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);

    ctx->ResourceBarrier(m_ShadowAtlasRT.get(),
        RESOURCE_STATE_COMMON, RESOURCE_STATE_DEPTH_WRITE);

    RHITextureView* nullRTV = nullptr;
    ctx->SetRenderTargets(&nullRTV, 0, m_ShadowAtlasDSV.get());

    uint32_t atlasSize = m_ShadowCascadeSize * 2;
    Viewport fullVP;
    fullVP.TopLeftX = 0; fullVP.TopLeftY = 0;
    fullVP.Width = (float)atlasSize; fullVP.Height = (float)atlasSize;
    fullVP.MinDepth = 0.0f; fullVP.MaxDepth = 1.0f;
    ctx->SetViewports(&fullVP, 1);
    ScissorRect fullSR;
    fullSR.Left = 0; fullSR.Top = 0;
    fullSR.Right = (int32_t)atlasSize; fullSR.Bottom = (int32_t)atlasSize;
    ctx->SetScissorRects(&fullSR, 1);

    ClearDepthStencilValue depthClear = { 1.0f, 0 };
    ctx->ClearDepthStencilView(m_ShadowAtlasDSV.get(), depthClear, 0x01);

    // Atlas 2x2 layout: [0]=top-left, [1]=top-right, [2]=bottom-left, [3]=bottom-right
    static const int cascadeOffsetX[4] = { 0, 1, 0, 1 };
    static const int cascadeOffsetY[4] = { 0, 0, 1, 1 };

    for (int cascade = 0; cascade < numCascades; cascade++)
    {
        float ox = (float)(cascadeOffsetX[cascade] * m_ShadowCascadeSize);
        float oy = (float)(cascadeOffsetY[cascade] * m_ShadowCascadeSize);

        Viewport shadowVP;
        shadowVP.TopLeftX = ox; shadowVP.TopLeftY = oy;
        shadowVP.Width = (float)m_ShadowCascadeSize;
        shadowVP.Height = (float)m_ShadowCascadeSize;
        shadowVP.MinDepth = 0.0f; shadowVP.MaxDepth = 1.0f;
        ctx->SetViewports(&shadowVP, 1);

        ScissorRect shadowSR;
        shadowSR.Left = (int32_t)ox; shadowSR.Top = (int32_t)oy;
        shadowSR.Right = (int32_t)(ox + m_ShadowCascadeSize);
        shadowSR.Bottom = (int32_t)(oy + m_ShadowCascadeSize);
        ctx->SetScissorRects(&shadowSR, 1);

        ViewUniformBuffer lightViewUB = {};
        memcpy(lightViewUB.ViewMatrix, m_LightViewMatrices[cascade].m, sizeof(float) * 16);
        memcpy(lightViewUB.ProjectionMatrix, m_LightProjMatrices[cascade].m, sizeof(float) * 16);
        if (m_ShadowViewUB)
        {
            void* mapped = m_ShadowViewUB->Map();
            if (mapped)
            {
                memcpy(mapped, &lightViewUB, sizeof(lightViewUB));
                m_ShadowViewUB->Unmap();
            }
            ctx->SetConstantBuffer(0, m_ShadowViewUB.get());
        }

        SubmitMeshDrawCommands(ctx, commands);
    }

    ctx->ResourceBarrier(m_ShadowAtlasRT.get(),
        RESOURCE_STATE_DEPTH_WRITE, RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    m_PassTimer.End();
    ctx->EndEvent();
}

void KiwiEngineApp::RenderDeferred(
    RHICommandContext* ctx,
    RHITextureView* sceneRTV,
    const Viewport& vp,
    const ScissorRect& sr)
{
    // ================================================================
    // DEFERRED RENDERING PATH
    // ================================================================

    // ==== PASS 0: Build mesh batches once, then each pass records its own draw commands ====
    PrepareMeshBatches();
    
    ShadowDepthPassProcessor shadowPass;
    shadowPass.Process(m_GPUScene.GetMeshBatches());

    // ==== PASS 1: Shadow Pass (CSM) ====
    UpdateShadowData();
    RenderShadowPass(ctx, shadowPass.GetCommands());

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
    RHITextureView* gbufferRTVs[GBUFFER_COUNT] = {};
    for (int i = 0; i < GBUFFER_COUNT; i++)
        gbufferRTVs[i] = m_GBufferRTV[i].get();
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

    // Draw visible meshes into the G-Buffer.
    ctx->SetConstantBuffer(0, m_ViewUB.get());
    BasePassProcessor::Config gbufferConfig;
    gbufferConfig.Fallback = { m_GBufferPSO.get(), m_GBufferVS.get(), m_GBufferPS.get() };
    gbufferConfig.MaterialPass = EMaterialPass::GBuffer;
    gbufferConfig.bBindMaterials = true;
    BasePassProcessor gbufferPass(gbufferConfig);
    gbufferPass.Process(m_GPUScene.GetMeshBatches());
    SubmitMeshDrawCommands(ctx, gbufferPass.GetCommands());

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

    // Fullscreen lighting does not use the mesh cull override.
    ctx->ClearCullModeOverride();

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
        ctx->SetShaderResourceView(9, m_GBufferSRV[3].get());
        ctx->SetShaderResourceView(7, GetDepthSRV());
        ctx->SetSampler(0, m_PostProcessSampler.get());

        ctx->SetConstantBuffer(0, m_ViewUB.get());
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

        ctx->SetConstantBuffer(0, m_ViewUB.get());
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

    ctx->SetConstantBuffer(0, m_ViewUB.get());

    // Re-set render targets for forward gizmo drawing (with depth for correct occlusion)
    ctx->SetRenderTargets(&sceneRTV, 1, GetDSV());
    ctx->SetViewports(&vp, 1);
    ctx->SetScissorRects(&sr, 1);

    ctx->SetCullMode(ECullMode::Back);
    ctx->SetInputLayout(m_InputLayout.get());
    ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);
    DrawGizmo(ctx);
    ctx->ClearCullModeOverride();

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
    PrepareMeshBatches();
    ctx->SetConstantBuffer(0, m_ViewUB.get());

    BasePassProcessor::Config forwardConfig;
    forwardConfig.MaterialPass = EMaterialPass::Forward;
    forwardConfig.bBindMaterials = true;
    forwardConfig.ForcedShader = forwardShader;
    forwardConfig.Shaders = &m_ShaderLibrary;
    BasePassProcessor forwardPass(forwardConfig);
    forwardPass.Process(m_GPUScene.GetMeshBatches());
    SubmitMeshDrawCommands(ctx, forwardPass.GetCommands());
    m_PassTimer.End();
    ctx->EndEvent();

    // ---- Draw Gizmo ----
    ctx->BeginEvent("Gizmo Pass");
    m_PassTimer.Begin("Gizmo Pass");
    ctx->SetConstantBuffer(0, m_ViewUB.get());
    ctx->SetCullMode(ECullMode::Back);
    DrawGizmo(ctx);
    ctx->ClearCullModeOverride();
    m_PassTimer.End();
    ctx->EndEvent();
}
