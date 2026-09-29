#include "KiwiEngineApp.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace
{

// PSSM practical split scheme: lambda blends logarithmic (1) and uniform (0) splits.
void CalculateCascadeSplits(float nearZ, float farZ, float shadowDistance, int numCascades, float lambda, float* outSplits)
{
    float maxDist = std::min(farZ, shadowDistance);
    float range = maxDist - nearZ;

    for (int i = 0; i < numCascades; i++)
    {
        float p = (float)(i + 1) / (float)numCascades;

        // Logarithmic split
        float logSplit = nearZ * std::pow(maxDist / nearZ, p);
        // Uniform split
        float uniformSplit = nearZ + range * p;
        // PSSM blend
        outSplits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
    }
}

// Orthographic light view-projection that bounds one cascade slice of the camera frustum.
Mat4 ComputeLightViewProjForCascade(
    const Vec3& lightDir,
    const Mat4& cameraView, const Mat4& cameraProj,
    float cascadeNear, float cascadeFar,
    float cameraNear, float cameraFar, float fovY, float aspect,
    Mat4* outLightView, Mat4* outLightProj)
{
    // 1. Compute the frustum corners for this cascade slice in world space
    float tanHalfFov = tanf(fovY * 0.5f);

    // Near and far plane dimensions
    float nearH = 2.0f * tanHalfFov * cascadeNear;
    float nearW = nearH * aspect;
    float farH = 2.0f * tanHalfFov * cascadeFar;
    float farW = farH * aspect;

    // Camera basis vectors (from view matrix — row-major, v*M convention)
    Vec3 camRight = { cameraView.m[0][0], cameraView.m[1][0], cameraView.m[2][0] };
    Vec3 camUp    = { cameraView.m[0][1], cameraView.m[1][1], cameraView.m[2][1] };
    Vec3 camFwd   = { cameraView.m[0][2], cameraView.m[1][2], cameraView.m[2][2] };

    // Camera position (inverse of translation in view matrix)
    Vec3 camPos;
    camPos.x = -(cameraView.m[3][0] * camRight.x + cameraView.m[3][1] * camUp.x + cameraView.m[3][2] * camFwd.x);
    camPos.y = -(cameraView.m[3][0] * camRight.y + cameraView.m[3][1] * camUp.y + cameraView.m[3][2] * camFwd.y);
    camPos.z = -(cameraView.m[3][0] * camRight.z + cameraView.m[3][1] * camUp.z + cameraView.m[3][2] * camFwd.z);

    // Near center and far center
    Vec3 nearCenter = camPos + camFwd * cascadeNear;
    Vec3 farCenter  = camPos + camFwd * cascadeFar;

    // 8 frustum corners
    Vec3 corners[8];
    // Near face
    corners[0] = nearCenter + camUp * (nearH * 0.5f) - camRight * (nearW * 0.5f); // top-left
    corners[1] = nearCenter + camUp * (nearH * 0.5f) + camRight * (nearW * 0.5f); // top-right
    corners[2] = nearCenter - camUp * (nearH * 0.5f) - camRight * (nearW * 0.5f); // bottom-left
    corners[3] = nearCenter - camUp * (nearH * 0.5f) + camRight * (nearW * 0.5f); // bottom-right
    // Far face
    corners[4] = farCenter + camUp * (farH * 0.5f) - camRight * (farW * 0.5f);
    corners[5] = farCenter + camUp * (farH * 0.5f) + camRight * (farW * 0.5f);
    corners[6] = farCenter - camUp * (farH * 0.5f) - camRight * (farW * 0.5f);
    corners[7] = farCenter - camUp * (farH * 0.5f) + camRight * (farW * 0.5f);

    // 2. Compute frustum center
    Vec3 center = { 0, 0, 0 };
    for (int i = 0; i < 8; i++)
    {
        center.x += corners[i].x;
        center.y += corners[i].y;
        center.z += corners[i].z;
    }
    center = center * (1.0f / 8.0f);

    // 3. Build light view matrix (looking along -lightDir at the center)
    Vec3 lightDirN = lightDir.Normalize();
    float radius = 0.0f;
    for (int i = 0; i < 8; i++)
    {
        float dist = (corners[i] - center).Length();
        radius = std::max(radius, dist);
    }

    // Snap to texel grid to reduce shimmer
    radius = std::ceil(radius * 16.0f) / 16.0f;

    Vec3 lightEye = center - lightDirN * radius;
    Vec3 lightTarget = center;
    Vec3 lightUp = { 0.0f, 1.0f, 0.0f };
    // If light is nearly vertical, use a different up vector
    if (std::abs(lightDirN.y) > 0.99f)
        lightUp = { 0.0f, 0.0f, 1.0f };

    Mat4 lightView = Mat4::LookAt(lightEye, lightTarget, lightUp);

    // 4. Build orthographic projection that encompasses the frustum
    Mat4 lightProj = Mat4::Orthographic(radius * 2.0f, radius * 2.0f, 0.0f, radius * 2.0f);

    if (outLightView) *outLightView = lightView;
    if (outLightProj) *outLightProj = lightProj;
    return lightView * lightProj;
}

} // namespace

void KiwiEngineApp::InitShadowResources(RHIDevice* device)
{
    ShadowCB = TUniformBufferRef<ShadowUniformBuffer>::CreateEmptyUniformBufferImmediate(device, EUniformBufferUsage::SingleFrame, "ShadowCB");

    // DX11 comparison sampler (DX12 uses static sampler s2 in root signature, returns nullptr)
    ShadowSampler = device->CreateComparisonSampler();

    // Resized in UpdateShadowData to match the shadow-casting light's settings
    CreateShadowMaps(device, 2048, 4);
}

bool KiwiEngineApp::CreateShadowPipelines(RHIDevice* device, const std::string& src)
{
    ReleaseShadowShaders();

    ShadowPassVS = device->CompileShader(EShaderType::Vertex, src.c_str(), "VSMain", "vs_5_0");
    if (!ShadowPassVS)
        return false;

    // Depth-only, no color output
    GraphicsPipelineStateInitializer shadowPSODesc;
    shadowPSODesc.RenderTargetsEnabled = 0;
    shadowPSODesc.RenderTargetFormats[0] = EFormat::Unknown;
    shadowPSODesc.DepthStencilTargetFormat = EFormat::D32_FLOAT;
    shadowPSODesc.DepthEnabled = true;
    shadowPSODesc.DepthWrite = true;
    shadowPSODesc.RasterizerState = RasterizerStateDesc(ERasterizerFillMode::Solid, ECullMode::Back, 100.0f, 1.5f);
    shadowPSODesc.VertexShader = ShadowPassVS.get();
    shadowPSODesc.VertexDeclaration = InputLayout.get();
    std::unique_ptr<RHIShader> shadowPS;
    if (device->GetApiType() == RHI_API_TYPE::METAL)
    {
        shadowPS = device->CompileShader(EShaderType::Pixel, src.c_str(), "PSMain", "ps_5_0");
        shadowPSODesc.PixelShader = shadowPS.get();
    }
    ShadowPassPSO = device->CreateGraphicsPipelineState(shadowPSODesc);

    ShaderMacro instMacro = { "USE_GPU_SCENE_INSTANCING", "1" };
    ShadowPassVS_Instanced = device->CompileShader(
        EShaderType::Vertex, src.c_str(), "VSMain", "vs_5_0", &instMacro, 1);
    if (ShadowPassVS_Instanced)
    {
        GraphicsPipelineStateInitializer shadowInstanced = shadowPSODesc;
        shadowInstanced.VertexShader = ShadowPassVS_Instanced.get();
        ShadowPassPSO_Instanced = device->CreateGraphicsPipelineState(shadowInstanced);
    }

    return ShadowPassPSO != nullptr;
}

void KiwiEngineApp::CreateShadowMaps(RHIDevice* device, uint32_t cascadeSize, int numCascades)
{
    ReleaseShadowMaps();

    ShadowCascadeSize = cascadeSize;
    uint32_t atlasSize = cascadeSize * 2; // 2x2 atlas layout

    TextureDesc desc;
    desc.Width = atlasSize;
    desc.Height = atlasSize;
    desc.Format = EFormat::R32_TYPELESS; // Typeless for DSV(D32_FLOAT) + SRV(R32_FLOAT)
    desc.BindFlags = TEXTURE_HINT_DEPTH_STENCIL | TEXTURE_BIND_SHADER_RESOURCE;
    desc.Usage = EResourceUsage::Default;
    desc.MipLevels = 1;
    desc.SampleCount = 1;
    desc.DebugName = "ShadowAtlas_CSM";

    ShadowAtlasRT = device->CreateTexture(desc);

    // DSV view: D32_FLOAT format (covers entire atlas)
    ShadowAtlasDSV = device->CreateTextureView(
        ShadowAtlasRT.get(), EDescriptorHeapType::DSV, EFormat::D32_FLOAT);

    // SRV view: R32_FLOAT format
    ShadowAtlasSRV = device->CreateTextureView(
        ShadowAtlasRT.get(), EDescriptorHeapType::CBV_SRV_UAV, EFormat::R32_FLOAT);

    std::cout << "[Kiwi] Shadow atlas created: " << numCascades << " cascades @ "
              << cascadeSize << "x" << cascadeSize << " (atlas " << atlasSize << "x" << atlasSize << ")" << std::endl;
}

void KiwiEngineApp::ReleaseShadowMaps()
{
    ShadowAtlasSRV.reset();
    ShadowAtlasDSV.reset();
    ShadowAtlasRT.reset();
}

void KiwiEngineApp::ReleaseShadowShaders()
{
    ShadowPassVS.reset();
    ShadowPassPSO.reset();
    ShadowPassVS_Instanced.reset();
    ShadowPassPSO_Instanced.reset();
}

void KiwiEngineApp::ReleaseShadowResources()
{
    ReleaseShadowMaps();
    ReleaseShadowShaders();
    ShadowCB.SafeRelease();
    ShadowSampler.reset();
}

void KiwiEngineApp::UpdateShadowData()
{
    memset(&ShadowUBData, 0, sizeof(ShadowUBData));

    const DirectionalShadowSettings* shadowLight = RenderScene.GetShadowCastingLight();

    if (!shadowLight)
    {
        ShadowUBData.NumCascades = 0;
        return;
    }

    int numCascades = std::min(shadowLight->NumCascades, MAX_SHADOW_CASCADES);
    ShadowUBData.NumCascades = numCascades;
    ShadowUBData.ShadowBias = shadowLight->ShadowBias;
    ShadowUBData.NormalBias = shadowLight->NormalBias;
    ShadowUBData.ShadowStrength = shadowLight->ShadowStrength;
    ShadowUBData.ShadowMapSize = (float)(ShadowCascadeSize * 2); // Atlas total size

    // Recreate shadow maps if resolution changed
    auto device = GetDevice();
    if (ShadowCascadeSize != (uint32_t)shadowLight->ShadowMapResolution ||
        !ShadowAtlasRT)
    {
        // Submitted frames may still sample the old atlas. Nothing recorded this frame uses it yet.
        GetRenderingThread().WaitForRHIThread();
        CreateShadowMaps(device, (uint32_t)shadowLight->ShadowMapResolution, numCascades);
    }

    // Get camera parameters for frustum calculation
    if (!RenderParams.bHasCamera) return;

    float fovY = DegToRad(RenderParams.FieldOfView);
    float aspect = (float)RenderParams.ViewWidth / (float)std::max(RenderParams.ViewHeight, 1u);
    float nearZ = RenderParams.NearPlane;
    float farZ = RenderParams.FarPlane;

    // Calculate cascade splits
    float splits[MAX_SHADOW_CASCADES];
    CalculateCascadeSplits(nearZ, farZ, shadowLight->ShadowDistance, numCascades,
        shadowLight->CascadeSplitLambda, splits);

    for (int i = 0; i < numCascades; i++)
    {
        ShadowUBData.CascadeSplits[i] = splits[i];
    }

    // Compute light VP matrices for each cascade
    Vec3 lightDir = shadowLight->Direction; // Direction the light shines toward

    float cascadeNear = nearZ;
    for (int i = 0; i < numCascades; i++)
    {
        float cascadeFar = splits[i];

        LightViewProjMatrices[i] = ComputeLightViewProjForCascade(
            lightDir, RenderParams.ViewMatrix, RenderParams.ProjectionMatrix,
            cascadeNear, cascadeFar, nearZ, farZ, fovY, aspect,
            &LightViewMatrices[i], &LightProjMatrices[i]);

        memcpy(ShadowUBData.LightViewProj[i],
            LightViewProjMatrices[i].m, sizeof(float) * 16);

        cascadeNear = cascadeFar;
    }
}

void KiwiEngineApp::UploadShadowUB()
{
    ShadowCB.UpdateUniformBufferImmediate(ShadowUBData);
}
