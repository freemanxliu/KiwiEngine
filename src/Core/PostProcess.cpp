#include "KiwiEngineApp.h"

#include "Scene/PostProcessShaders.h"

#include <cstring>
#include <iostream>
#include <utility>

void KiwiEngineApp::InitPostProcessResources(RHIDevice* device)
{
    auto api = device->GetApiType();
    // PostProcess shader library
    m_PostProcessLibrary.Initialize(m_PostProcessShaderDir, device);

    // Post-process constant buffer
    BufferDesc ppCbDesc;
    ppCbDesc.SizeInBytes = sizeof(PostProcessCBData);
    ppCbDesc.BindFlags = BUFFER_USAGE_CONSTANT;
    ppCbDesc.Usage = EResourceUsage::Dynamic;
    ppCbDesc.DebugName = "PostProcessCB";
    m_PostProcessCB = device->CreateBuffer(ppCbDesc);

    // DX11 sampler for post-process (linear clamp)
    // DX12 uses static sampler in root signature, so this is only for DX11
    m_PostProcessSampler = device->CreateSampler();

    // Compile passthrough shader (for final blit from offscreen to backbuffer)
    const char* ppVSSrc = g_PostProcessVS;
    const char* ppPSSrc = g_PostProcessPassthroughPS;
    if (api == RHI_API_TYPE::OPENGL || api == RHI_API_TYPE::VULKAN)
    {
        ppVSSrc = g_PostProcessVS_GLSL;
        ppPSSrc = g_PostProcessPassthroughPS_GLSL;
    }
    else if (api == RHI_API_TYPE::METAL)
    {
        ppVSSrc = g_PostProcessVS_MSL;
        ppPSSrc = g_PostProcessPassthroughPS_MSL;
    }
    m_PassthroughVS = device->CompileShader(
        EShaderType::Vertex, ppVSSrc, "VSMain", "vs_5_0");
    m_PassthroughPS = device->CompileShader(
        EShaderType::Pixel, ppPSSrc, "PSMain", "ps_5_0");
    if (m_PassthroughVS && m_PassthroughPS)
    {
        GraphicsPipelineStateInitializer passthroughInit;
        passthroughInit.VertexShader = m_PassthroughVS.get();
        passthroughInit.PixelShader = m_PassthroughPS.get();
        passthroughInit.DepthEnabled = false;
        passthroughInit.DepthWrite = false;
        passthroughInit.RasterizerState = RasterizerStateDesc(
            ERasterizerFillMode::Solid, ECullMode::None);
        m_PassthroughPSO = device->CreateGraphicsPipelineState(passthroughInit);
    }

    // Create offscreen render targets
    CreateOffscreenRenderTargets(
        device, GetWindow()->GetWidth(), GetWindow()->GetHeight());
}

void KiwiEngineApp::CreateOffscreenRenderTargets(RHIDevice* device, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) return;

    // Release existing
    for (int i = 0; i < 2; i++)
    {
        m_OffscreenSRV[i].reset();
        m_OffscreenRTV[i].reset();
        m_OffscreenRT[i].reset();
    }

    m_OffscreenWidth = width;
    m_OffscreenHeight = height;

    for (int i = 0; i < 2; i++)
    {
        TextureDesc rtDesc;
        rtDesc.Width = width;
        rtDesc.Height = height;
        rtDesc.Format = EFormat::R16G16B16A16_FLOAT;  // HDR scene color
        rtDesc.BindFlags = TEXTURE_BIND_RENDER_TARGET | TEXTURE_BIND_SHADER_RESOURCE;
        rtDesc.Usage = EResourceUsage::Default;
        rtDesc.MipLevels = 1;
        rtDesc.SampleCount = 1;
        rtDesc.DebugName = (i == 0) ? "OffscreenRT_0" : "OffscreenRT_1";

        m_OffscreenRT[i] = device->CreateTexture(rtDesc);
        m_OffscreenRTV[i] = device->CreateTextureView(
            m_OffscreenRT[i].get(), EDescriptorHeapType::RTV);
        m_OffscreenSRV[i] = device->CreateTextureView(
            m_OffscreenRT[i].get(), EDescriptorHeapType::CBV_SRV_UAV);
    }

    std::cout << "[Kiwi] Offscreen RT created: " << width << "x" << height << std::endl;
}

void KiwiEngineApp::ReleasePostProcessResources()
{
    for (int i = 0; i < 2; i++)
    {
        m_OffscreenSRV[i].reset();
        m_OffscreenRTV[i].reset();
        m_OffscreenRT[i].reset();
    }
    m_PostProcessCB.reset();
    m_PostProcessSampler.reset();
    m_PassthroughVS.reset();
    m_PassthroughPS.reset();
    m_PassthroughPSO.reset();
    m_PostProcessLibrary.ReleaseAll();
}

void KiwiEngineApp::CollectActivePostProcessEffects(std::vector<PostProcessMaterial*>& outEffects)
{
    outEffects.clear();
    for (auto& objPtr : m_Scene.GetObjects())
    {
        auto* ppComp = objPtr->GetComponent<PostProcessComponent>();
        if (!ppComp || !ppComp->Enabled) continue;

        for (auto& mat : ppComp->Materials)
        {
            if (mat.Enabled && m_PostProcessLibrary.HasShader(mat.ShaderName))
            {
                outEffects.push_back(&mat);
            }
        }
    }
}

void KiwiEngineApp::ExecutePostProcessPasses(
    RHICommandContext* ctx, RHIDevice* device,
    const std::vector<PostProcessMaterial*>& effects,
    RHISwapChain* swapChain)
{
    uint32_t winW = GetWindow()->GetWidth();
    uint32_t winH = GetWindow()->GetHeight();

    // Viewport and scissor for fullscreen passes
    Viewport vp;
    vp.TopLeftX = 0; vp.TopLeftY = 0;
    vp.Width = (float)winW;
    vp.Height = (float)winH;
    vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;

    ScissorRect sr;
    sr.Left = 0; sr.Top = 0;
    sr.Right = (int32_t)winW;
    sr.Bottom = (int32_t)winH;

    // Scene was rendered to RT[0].
    // Post-process reads from srcIdx, writes to dstIdx, then swap.
    int srcIdx = 0;
    int dstIdx = 1;

    for (size_t passIdx = 0; passIdx < effects.size(); passIdx++)
    {
        auto* mat = effects[passIdx];
        bool isLastPass = (passIdx == effects.size() - 1);

        CompiledPostProcessShader* ppShader =
            m_PostProcessLibrary.GetShader(mat->ShaderName);
        if (!ppShader) continue;

        // Determine output target
        RHITextureView* outputRTV = nullptr;
        if (isLastPass)
        {
            // Last pass writes directly to backbuffer
            outputRTV = swapChain->GetBackBufferRTV(swapChain->GetCurrentBackBufferIndex());
        }
        else
        {
            // Intermediate pass writes to ping-pong buffer
            ctx->ResourceBarrier(m_OffscreenRT[dstIdx].get(),
                RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RESOURCE_STATE_RENDER_TARGET);
            outputRTV = m_OffscreenRTV[dstIdx].get();
        }

        // Transition source to SRV
        ctx->ResourceBarrier(m_OffscreenRT[srcIdx].get(),
            RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        // Set render target (no depth for post-process)
        ctx->SetRenderTargets(&outputRTV, 1, nullptr);
        ctx->SetViewports(&vp, 1);
        ctx->SetScissorRects(&sr, 1);

        // Clear intermediate targets (not backbuffer for last pass — the fullscreen draw covers all pixels)
        if (!isLastPass)
        {
            ClearColorValue black = { 0.0f, 0.0f, 0.0f, 1.0f };
            ctx->ClearRenderTargetView(outputRTV, black);
        }

        // Set post-process pipeline state
        if (ppShader->PSO)
            ctx->SetPipelineState(ppShader->PSO.get());
        ctx->SetVertexShader(ppShader->VertexShader.get());
        ctx->SetPixelShader(ppShader->PixelShader.get());
        ctx->SetInputLayout(nullptr); // No vertex input for fullscreen triangle
        ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);

        // Bind source texture as SRV
        ctx->SetShaderResourceView(0, m_OffscreenSRV[srcIdx].get());

        // Bind sampler (DX11 only; DX12 uses static sampler)
        ctx->SetSampler(0, m_PostProcessSampler.get());

        // Update post-process constant buffer
        PostProcessCBData ppCB;
        ppCB.ScreenWidth = (float)winW;
        ppCB.ScreenHeight = (float)winH;
        ppCB.Intensity = mat->Intensity;
        ppCB.Time = m_TotalTime;

        void* mapped = m_PostProcessCB->Map();
        if (mapped)
        {
            memcpy(mapped, &ppCB, sizeof(ppCB));
            m_PostProcessCB->Unmap();
        }
        ctx->SetConstantBuffer(0, m_PostProcessCB.get());

        // Draw fullscreen triangle (3 vertices, no vertex buffer)
        ctx->Draw(3, 0);

        // Unbind SRV to avoid resource hazard
        ctx->SetShaderResourceView(0, nullptr);

        // Swap ping-pong indices for next pass
        if (!isLastPass)
        {
            std::swap(srcIdx, dstIdx);
        }
    }

    // ---- Built-in Tonemap Pass (always last) ----
    // Reads from current srcIdx (HDR), writes to backbuffer (LDR)
    {
        auto* tonemapShader = m_PostProcessLibrary.GetShader("Tonemap");
        if (tonemapShader && tonemapShader->PixelShader)
        {
            // Source: current ping-pong buffer (HDR)
            ctx->ResourceBarrier(m_OffscreenRT[srcIdx].get(),
                RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

            // Destination: backbuffer
            RHITextureView* bbRTV = swapChain->GetBackBufferRTV(swapChain->GetCurrentBackBufferIndex());
            ctx->SetRenderTargets(&bbRTV, 1, nullptr);
            ctx->SetViewports(&vp, 1);
            ctx->SetScissorRects(&sr, 1);

            ctx->SetPipelineState(tonemapShader->PSO.get());
            ctx->SetVertexShader(tonemapShader->VertexShader.get());
            ctx->SetPixelShader(tonemapShader->PixelShader.get());
            ctx->SetInputLayout(nullptr);
            ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);

            ctx->SetShaderResourceView(0, m_OffscreenSRV[srcIdx].get());
            ctx->SetSampler(0, m_PostProcessSampler.get());

            // Upload exposure via PostProcessCB
            float ppData[4] = { 1.0f, 0.0f, 0.0f, 0.0f }; // exposure = 1.0
            void* mapped = m_PostProcessCB->Map();
            if (mapped) { memcpy(mapped, ppData, sizeof(ppData)); m_PostProcessCB->Unmap(); }
            ctx->SetConstantBuffer(0, m_PostProcessCB.get());

            ctx->Draw(3, 0);
            ctx->SetShaderResourceView(0, nullptr);

            return; // Tonemap wrote directly to backbuffer — done
        }
    }

    // Fallback: if Tonemap shader not available, blit RT[0] to backbuffer (no tonemap)
}
