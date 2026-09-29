#include "KiwiEngineApp.h"

#include <cstring>

static void BindMeshGeometry(RHICommandContext* Ctx, const SharedMeshEntry& Mesh)
{
    VertexBufferView VbView;
    VbView.BufferLocation = 0;
    VbView.SizeInBytes = Mesh.VertexCount * sizeof(Vertex);
    VbView.StrideInBytes = sizeof(Vertex);
    RHIBuffer* VbPtr = Mesh.VertexBuffer;
    Ctx->SetVertexBuffers(0, &VbPtr, &VbView, 1);

    IndexBufferView IbView;
    IbView.BufferLocation = 0;
    IbView.SizeInBytes = Mesh.IndexCount * sizeof(uint32_t);
    IbView.Format = EFormat::R32_UINT;
    Ctx->SetIndexBuffer(Mesh.IndexBuffer, &IbView);
}

void KiwiEngineApp::SubmitMeshDrawCommands(RHICommandContext* Ctx, const std::vector<MeshDrawCommand>& Commands, InstanceCullingContext& InstanceCulling)
{
    InstanceCulling.Upload();
    RenderScene.GetGPUScene().Bind(Ctx);
    InstanceCulling.Bind(Ctx);

    RHIPipelineState* LastPSO = nullptr;
    RHIShader* LastVS = nullptr;
    RHIShader* LastPS = nullptr;
    bool HasLastShader = false;
    RHIBuffer* LastVB = nullptr;
    const PrimitiveSceneInfo* LastPrimitive = nullptr;

    for (const MeshDrawCommand& Command : Commands)
    {
        const bool ShaderChanged = !HasLastShader
            || Command.Shader.PSO != LastPSO
            || Command.Shader.VertexShader != LastVS
            || Command.Shader.PixelShader != LastPS;
        if (ShaderChanged)
        {
            if (Command.Shader.PSO)
                Ctx->SetPipelineState(Command.Shader.PSO);
            Ctx->SetVertexShader(Command.Shader.VertexShader);
            Ctx->SetPixelShader(Command.Shader.PixelShader);
            LastPSO = Command.Shader.PSO;
            LastVS = Command.Shader.VertexShader;
            LastPS = Command.Shader.PixelShader;
            HasLastShader = true;
        }

        Ctx->SetCullMode(Command.CullMode);

        if (Command.VertexBuffer != LastVB)
        {
            SharedMeshEntry Mesh;
            Mesh.VertexBuffer = Command.VertexBuffer;
            Mesh.IndexBuffer = Command.IndexBuffer;
            Mesh.VertexCount = Command.VertexCount;
            Mesh.IndexCount = Command.IndexCount;
            BindMeshGeometry(Ctx, Mesh);
            LastVB = Command.VertexBuffer;
        }

        if (Command.bBindMaterial && Command.Primitive && Command.Primitive != LastPrimitive)
        {
            BindMaterialTextures(Ctx, Command.Primitive->Proxy);
            LastPrimitive = Command.Primitive;
        }

        InstanceCulling.SetDrawInstanceOffset(Ctx, Command.DrawInstanceOffset);
        Ctx->DrawIndexedInstanced(Command.IndexCount, Command.NumInstances, Command.FirstIndex, Command.BaseVertexIndex, 0);
    }
}

void KiwiEngineApp::RenderShadowPass(RHICommandContext* Ctx, const std::vector<MeshDrawCommand>& Commands, InstanceCullingContext& InstanceCulling)
{
    if (!ShadowPassPSO || !ShadowPassVS || ShadowUBData.NumCascades <= 0)
        return;

    Ctx->BeginEvent("Shadow Pass");
    PassTimer.Begin("Shadow Pass");

    int NumCascades = ShadowUBData.NumCascades;

    Ctx->SetPipelineState(ShadowPassPSO.get());
    Ctx->SetVertexShader(ShadowPassVS.get());
    Ctx->SetPixelShader(nullptr);
    Ctx->SetInputLayout(InputLayout.get());
    Ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);

    Ctx->ResourceBarrier(ShadowAtlasRT.get(),
        RESOURCE_STATE_COMMON, RESOURCE_STATE_DEPTH_WRITE);

    RHITextureView* NullRTV = nullptr;
    Ctx->SetRenderTargets(&NullRTV, 0, ShadowAtlasDSV.get());

    uint32_t AtlasSize = ShadowCascadeSize * 2;
    Viewport FullVP;
    FullVP.TopLeftX = 0; FullVP.TopLeftY = 0;
    FullVP.Width = (float)AtlasSize; FullVP.Height = (float)AtlasSize;
    FullVP.MinDepth = 0.0f; FullVP.MaxDepth = 1.0f;
    Ctx->SetViewports(&FullVP, 1);
    ScissorRect FullSR;
    FullSR.Left = 0; FullSR.Top = 0;
    FullSR.Right = (int32_t)AtlasSize; FullSR.Bottom = (int32_t)AtlasSize;
    Ctx->SetScissorRects(&FullSR, 1);

    ClearDepthStencilValue DepthClear = { 1.0f, 0 };
    Ctx->ClearDepthStencilView(ShadowAtlasDSV.get(), DepthClear, 0x01);

    // Atlas 2x2 layout: [0]=top-left, [1]=top-right, [2]=bottom-left, [3]=bottom-right
    static const int CascadeOffsetX[4] = { 0, 1, 0, 1 };
    static const int CascadeOffsetY[4] = { 0, 0, 1, 1 };

    for (int Cascade = 0; Cascade < NumCascades; Cascade++)
    {
        float Ox = (float)(CascadeOffsetX[Cascade] * ShadowCascadeSize);
        float Oy = (float)(CascadeOffsetY[Cascade] * ShadowCascadeSize);

        Viewport ShadowVP;
        ShadowVP.TopLeftX = Ox; ShadowVP.TopLeftY = Oy;
        ShadowVP.Width = (float)ShadowCascadeSize;
        ShadowVP.Height = (float)ShadowCascadeSize;
        ShadowVP.MinDepth = 0.0f; ShadowVP.MaxDepth = 1.0f;
        Ctx->SetViewports(&ShadowVP, 1);

        ScissorRect ShadowSR;
        ShadowSR.Left = (int32_t)Ox; ShadowSR.Top = (int32_t)Oy;
        ShadowSR.Right = (int32_t)(Ox + ShadowCascadeSize);
        ShadowSR.Bottom = (int32_t)(Oy + ShadowCascadeSize);
        Ctx->SetScissorRects(&ShadowSR, 1);

        ViewUniformBuffer LightViewUB = {};
        memcpy(LightViewUB.ViewMatrix, LightViewMatrices[Cascade].m, sizeof(float) * 16);
        memcpy(LightViewUB.ProjectionMatrix, LightProjMatrices[Cascade].m, sizeof(float) * 16);
        ShadowViewUB.UpdateUniformBufferImmediate(LightViewUB);
        Ctx->SetConstantBuffer(0, ShadowViewUB.GetReference());

        SubmitMeshDrawCommands(Ctx, Commands, InstanceCulling);
    }

    Ctx->ResourceBarrier(ShadowAtlasRT.get(),
        RESOURCE_STATE_DEPTH_WRITE, RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    PassTimer.End();
    Ctx->EndEvent();
}

// Helper: Bind material textures for the current object
void KiwiEngineApp::BindMaterialTextures(RHICommandContext* Ctx, const PrimitiveSceneProxy& Proxy)
{
    const std::string& BaseColorTex = Proxy.BaseColorTexture;
    const std::string& NormalTex    = Proxy.NormalTexture;
    const std::string& MrTex        = Proxy.MetallicRoughnessTexture;

    // t4 = BaseColor texture
    if (!BaseColorTex.empty())
    {
        GPUTexture* Tex = TextureManager.GetTexture(BaseColorTex);
        if (!Tex) Tex = TextureManager.LoadTexture(BaseColorTex);
        if (Tex && Tex->SRV)
            Ctx->SetShaderResourceView(4, Tex->SRV.get());
        else
            Ctx->SetShaderResourceView(4, TextureManager.GetWhiteTexture()->SRV.get());
    }
    else
    {
        if (TextureManager.GetWhiteTexture())
            Ctx->SetShaderResourceView(4, TextureManager.GetWhiteTexture()->SRV.get());
    }

    // t5 = Normal map texture
    if (!NormalTex.empty())
    {
        GPUTexture* Tex = TextureManager.GetTexture(NormalTex);
        if (!Tex) Tex = TextureManager.LoadTexture(NormalTex);
        if (Tex && Tex->SRV)
            Ctx->SetShaderResourceView(5, Tex->SRV.get());
        else
            Ctx->SetShaderResourceView(5, TextureManager.GetDefaultNormalTexture()->SRV.get());
    }
    else
    {
        if (TextureManager.GetDefaultNormalTexture())
            Ctx->SetShaderResourceView(5, TextureManager.GetDefaultNormalTexture()->SRV.get());
    }

    // t6 = MetallicRoughness texture (optional; not yet sampled in shader but bound for future use)
    if (!MrTex.empty())
    {
        GPUTexture* Tex = TextureManager.GetTexture(MrTex);
        if (!Tex) Tex = TextureManager.LoadTexture(MrTex);
        if (Tex && Tex->SRV)
            Ctx->SetShaderResourceView(6, Tex->SRV.get());
    }
}

// Update CBs for deferred lighting fullscreen pass
void KiwiEngineApp::UpdateDeferredLightingCB()
{
    auto Ctx = GetContext();

    // Upload PrimitiveUniformBuffer (identity world + no selection)
    PrimitiveUniformBuffer Oub = {};
    Mat4 Identity = Mat4::Identity();
    memcpy(Oub.WorldMatrix, Identity.m, sizeof(Identity.m));
    Oub.Selected = 0.0f;
    Oub.ObjectPadding[0] = Oub.ObjectPadding[1] = 0.0f;

    ObjectUB.UpdateUniformBufferImmediate(Oub);
    Ctx->SetConstantBuffer(1, ObjectUB.GetReference());
}

// Update CBs for buffer visualization fullscreen pass
void KiwiEngineApp::UpdateBufferVisualizationCB()
{
    auto Ctx = GetContext();

    // Upload PrimitiveUniformBuffer with visualize mode
    PrimitiveUniformBuffer Oub = {};
    Mat4 Identity = Mat4::Identity();
    memcpy(Oub.WorldMatrix, Identity.m, sizeof(Identity.m));
    Oub.Selected = 0.0f;

    // Use g_ShadingModelID field to pass the visualization mode (repurposed for this pass)
    switch (RenderParams.ViewMode)
    {
    case EViewMode::BaseColor: Oub.ShadingModelID = 0.0f; break;
    case EViewMode::Roughness: Oub.ShadingModelID = 1.0f; break;
    case EViewMode::Metallic:  Oub.ShadingModelID = 2.0f; break;
    default:                   Oub.ShadingModelID = 0.0f; break;
    }
    Oub.ObjectPadding[0] = Oub.ObjectPadding[1] = Oub.ObjectPadding[2] = 0.0f;

    ObjectUB.UpdateUniformBufferImmediate(Oub);
    Ctx->SetConstantBuffer(1, ObjectUB.GetReference());
}

void KiwiEngineApp::DrawGizmo(RHICommandContext* Ctx)
{
    if (!RenderParams.Gizmo.bVisible)
        return;

    // Gizmo always uses the Default shader
    CompiledShader* DefaultShader = ShaderLibrary.GetDefault();
    if (DefaultShader)
    {
        if (DefaultShader->PSO)
            Ctx->SetPipelineState(DefaultShader->PSO.get());
        Ctx->SetVertexShader(DefaultShader->VertexShader.get());
        Ctx->SetPixelShader(DefaultShader->PixelShader.get());
    }

    Gizmo.Draw(Ctx, RenderParams.Gizmo, RenderParams.CameraPosition, ObjectUB);
}
