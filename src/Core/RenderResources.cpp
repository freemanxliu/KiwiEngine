#include "KiwiEngineApp.h"

#include <cstddef>
#include <iostream>

namespace
{

// Deferred-path shader files (without extension), compiled by CompileDeferredShaders and watched by hot reload.
const char* const kDeferredShaderNames[] = {
    "GBufferPass",
    "DeferredLighting",
    "DeferredAmbient",
    "BufferVisualization",
    "ShadowPass",
};

// Fullscreen triangle into a single color target: no input layout, no depth.
GraphicsPipelineStateInitializer FullscreenPipelineDesc(EFormat colorFormat, RHIShader* vs, RHIShader* ps)
{
    GraphicsPipelineStateInitializer desc;
    desc.RenderTargetsEnabled = 1;
    desc.RenderTargetFormats[0] = colorFormat;
    desc.DepthEnabled = false;
    desc.DepthWrite = false;
    desc.RasterizerState = RasterizerStateDesc(ERasterizerFillMode::Solid, ECullMode::None);
    desc.VertexShader = vs;
    desc.PixelShader = ps;
    return desc;
}

} // namespace

void KiwiEngineApp::OnRHIShutdown()
{
    std::cout << "[Kiwi] Releasing GPU resources for RHI switch..." << std::endl;

    // Release all GPU resources
    m_GPUMeshes.clear();
    m_ViewUB.reset();
    m_ShadowViewUB.reset();
    m_GPUScene.Release();
    m_ObjectUB.reset();
    m_LightVolumeIB.reset();
    m_InputLayout.reset();
    m_PipelineState.reset();

    // Release all shaders via ShaderLibrary
    m_ShaderLibrary.ReleaseAll();
    m_MaterialShaders.ReleaseAll();
    m_TextureManager.ReleaseAll();

    ReleasePostProcessResources();
    ReleaseGBufferResources();
    ReleaseDeferredShaders();
    ReleaseShadowResources();
    m_Gizmo.ReleaseGPUResources();

    // Shutdown ImGui backend
    ShutdownImGui();
}

void KiwiEngineApp::OnRHIReady()
{
    std::cout << "[Kiwi] Rebuilding GPU resources after RHI switch..." << std::endl;

    InitRHIResources();
    RebuildAllGPUBuffers();
    m_Gizmo.CreateGPUResources(GetDevice());
    m_TextureManager.Initialize(GetDevice(), GetContext());
}

void KiwiEngineApp::ShutdownImGui()
{
    auto device = GetDevice();
    if (device)
        device->ShutdownImGui();
}

void KiwiEngineApp::InitRHIResources()
{
    auto device = GetDevice();

    // We need a temporary VS to create the shared input layout
    auto api = device->GetApiType();
    const char* defaultVSSrc = BuiltinVertexShader(api);
    auto tempVS = device->CompileShader(
        EShaderType::Vertex, defaultVSSrc, "main", "vs_5_0");

    // Create input layout (shared across all shaders — same vertex format)
    InputElementDesc inputElements[] = {
        { "POSITION", 0, EFormat::R32G32B32_FLOAT,    (uint32_t)offsetof(Vertex, Position), 0, 0 },
        { "NORMAL",   0, EFormat::R32G32B32_FLOAT,    (uint32_t)offsetof(Vertex, Normal),   0, 0 },
        { "TANGENT",  0, EFormat::R32G32B32_FLOAT,    (uint32_t)offsetof(Vertex, Tangent),  0, 0 },
        { "COLOR",    0, EFormat::R32G32B32A32_FLOAT, (uint32_t)offsetof(Vertex, Color),    0, 0 },
        { "TEXCOORD", 0, EFormat::R32G32_FLOAT,       (uint32_t)offsetof(Vertex, TexCoord), 0, 0 },
    };
    m_InputLayout = device->CreateInputLayout(inputElements, 5, tempVS.get());

    // Constant buffers: View (b0) + Object (b1)
    BufferDesc cbDesc;
    cbDesc.BindFlags = BUFFER_USAGE_CONSTANT;
    cbDesc.Usage = EResourceUsage::Dynamic;
    cbDesc.DebugName = "ViewUniformBuffer";
    cbDesc.SizeInBytes = sizeof(ViewUniformBuffer);
    m_ViewUB = device->CreateBuffer(cbDesc);

    cbDesc.DebugName = "ShadowViewUniformBuffer";
    m_ShadowViewUB = device->CreateBuffer(cbDesc);

    // GPU Scene Buffer: managed by GPUScene class (UE5 FPrimitiveSceneData pattern)
    // Supports dirty-flag incremental updates, one Map/Unmap per frame.
    m_GPUScene.Initialize(device);

    // Small per-draw ObjectUB for fullscreen passes and gizmos (not part of scene)
    cbDesc.DebugName = "ObjectUB_Aux";
    cbDesc.SizeInBytes = sizeof(PrimitiveUniformBuffer);
    m_ObjectUB = device->CreateBuffer(cbDesc);

    // Deferred lighting geometry. Vertex positions live in DeferredLighting's vertex shader;
    // SV_VertexID is the index value. [0, 60) is the point light icosahedron, [60, 63) the fullscreen triangle.
    static const uint32_t lightVolumeIndices[LIGHT_VOLUME_INDEX_COUNT + 3] = {
        0, 5, 1,  0, 1, 7,  0, 11, 5,  0, 7, 10,  0, 10, 11,
        1, 5, 9,  1, 8, 7,  1, 9, 8,   2, 3, 4,   2, 6, 3,
        2, 4, 11, 2, 10, 6, 2, 11, 10, 3, 9, 4,   3, 6, 8,
        3, 8, 9,  4, 9, 5,  4, 5, 11,  6, 7, 8,   6, 10, 7,
        0, 1, 2,
    };
    BufferDesc lightVolumeIBDesc;
    lightVolumeIBDesc.SizeInBytes = sizeof(lightVolumeIndices);
    lightVolumeIBDesc.BindFlags = BUFFER_USAGE_INDEX;
    lightVolumeIBDesc.Usage = EResourceUsage::Immutable;
    lightVolumeIBDesc.DebugName = "LightVolumeIB";
    m_LightVolumeIB = device->CreateBuffer(lightVolumeIBDesc, lightVolumeIndices);

    // Pipeline state (DX11: empty wrapper, DX12: managed per-shader)
    m_PipelineState = device->CreatePipelineState();

    std::string shaderDir = ForwardShaderDirectory(api, m_ShaderDir);
    m_ShaderLibrary.Initialize(shaderDir, device, m_InputLayout.get());
    InitMaterialShaders(device);

    InitPostProcessResources(device);

    if (IsDeferredRHI(api))
    {
        CompileDeferredShaders(device);
        CreateGBufferResources(device, GetWindow()->GetWidth(), GetWindow()->GetHeight());
        InitShadowResources(device);
        RecordDeferredShaderTimestamps(api);
    }

    // Mark first load complete — future reloads will be incremental
    m_FirstShaderLoad = false;

    // Init ImGui backend
    device->InitImGui(GetWindow()->GetHWND());
}

void KiwiEngineApp::InitMaterialShaders(RHIDevice* device)
{
    namespace fs = std::filesystem;
    std::string surfaceDir = m_ShaderDir + "/../SurfaceShaders";
    if (!fs::exists(surfaceDir))
        surfaceDir = m_ShaderDir + "/../../../SurfaceShaders";
    auto api = device->GetApiType();
    std::string templateDir = (api == RHI_API_TYPE::METAL)
        ? ForwardShaderDirectory(api, m_ShaderDir) + "/MaterialTemplates"
        : m_ShaderDir + "/MaterialTemplates";
    m_MaterialShaders.Initialize(device, m_InputLayout.get(), surfaceDir, templateDir);
}

// ============================================================
// G-Buffer render targets
// ============================================================

void KiwiEngineApp::CreateGBufferResources(RHIDevice* device, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) return;

    ReleaseGBufferResources();

    m_GBufferWidth = width;
    m_GBufferHeight = height;

    // World position is reconstructed from hardware depth + inverse ViewProj matrix.
    EFormat gbufferFormats[GBUFFER_COUNT] = {
        EFormat::R8G8B8A8_UNORM, // A: normal
        EFormat::R8G8B8A8_UNORM, // B: metallic, specular, roughness, shading model
        EFormat::R8G8B8A8_UNORM, // C: base color + AO
        EFormat::R8G8B8A8_UNORM, // D: emissive
    };

    const char* gbufferNames[GBUFFER_COUNT] = {
        "GBufferA_Normal",
        "GBufferB_Material",
        "GBufferC_BaseColor",
        "GBufferD_Emissive",
    };

    for (int i = 0; i < GBUFFER_COUNT; i++)
    {
        TextureDesc desc;
        desc.Width = width;
        desc.Height = height;
        desc.Format = gbufferFormats[i];
        desc.BindFlags = TEXTURE_BIND_RENDER_TARGET | TEXTURE_BIND_SHADER_RESOURCE;
        desc.Usage = EResourceUsage::Default;
        desc.MipLevels = 1;
        desc.SampleCount = 1;
        desc.DebugName = gbufferNames[i];

        m_GBufferRT[i] = device->CreateTexture(desc);
        m_GBufferRTV[i] = device->CreateTextureView(
            m_GBufferRT[i].get(), EDescriptorHeapType::RTV);
        m_GBufferSRV[i] = device->CreateTextureView(
            m_GBufferRT[i].get(), EDescriptorHeapType::CBV_SRV_UAV);
    }

    std::cout << "[Kiwi] G-Buffer created: " << width << "x" << height << std::endl;
}

void KiwiEngineApp::ReleaseGBufferResources()
{
    // Only release RT/RTV/SRV — shader/PSO are managed separately
    for (int i = 0; i < GBUFFER_COUNT; i++)
    {
        m_GBufferSRV[i].reset();
        m_GBufferRTV[i].reset();
        m_GBufferRT[i].reset();
    }
}

// ============================================================
// Deferred pipelines
// ============================================================

void KiwiEngineApp::ReleaseDeferredShaders()
{
    m_GBufferVS.reset();
    m_GBufferPS.reset();
    m_GBufferPSO.reset();
    m_GBufferVS_Instanced.reset();
    m_GBufferPSO_Instanced.reset();
    m_DeferredLightingVS.reset();
    m_DeferredPointLightVS.reset();
    m_DeferredLightingPS.reset();
    m_BufferVisVS.reset();
    m_BufferVisPS.reset();
    m_BufferVisPSO.reset();
    m_DeferredAmbientVS.reset();
    m_DeferredAmbientPS.reset();
    m_DeferredAmbientPSO.reset();
    m_DeferredLightingAdditivePSO.reset();
    m_DeferredPointLightPSO.reset();
}

std::string KiwiEngineApp::DeferredShaderPath(RHI_API_TYPE api, const char* name) const
{
    if (api == RHI_API_TYPE::METAL)
        return ForwardShaderDirectory(api, m_ShaderDir) + "/" + name + ".metal";
    return m_ShaderDir + "/" + std::string(name) + ".hlsl";
}

bool KiwiEngineApp::CreateGBufferPipelines(RHIDevice* device, const std::string& src)
{
    m_GBufferVS.reset();
    m_GBufferPS.reset();
    m_GBufferPSO.reset();
    m_GBufferVS_Instanced.reset();
    m_GBufferPSO_Instanced.reset();

    m_GBufferVS = device->CompileShader(EShaderType::Vertex, src.c_str(), "VSMain", "vs_5_0");
    m_GBufferPS = device->CompileShader(EShaderType::Pixel, src.c_str(), "PSMain", "ps_5_0");
    if (!m_GBufferVS || !m_GBufferPS)
        return false;

    // MRT PSO, one target per G-Buffer slice
    GraphicsPipelineStateInitializer gbufferPSODesc;
    gbufferPSODesc.RenderTargetsEnabled = GBUFFER_COUNT;
    for (int i = 0; i < GBUFFER_COUNT; i++)
        gbufferPSODesc.RenderTargetFormats[i] = EFormat::R8G8B8A8_UNORM;
    gbufferPSODesc.DepthStencilTargetFormat = EFormat::D32_FLOAT;
    gbufferPSODesc.DepthEnabled = true;
    gbufferPSODesc.DepthWrite = true;
    gbufferPSODesc.RasterizerState = RasterizerStateDesc(ERasterizerFillMode::Solid, ECullMode::Back);
    gbufferPSODesc.VertexShader = m_GBufferVS.get();
    gbufferPSODesc.PixelShader = m_GBufferPS.get();
    gbufferPSODesc.VertexDeclaration = m_InputLayout.get();
    m_GBufferPSO = device->CreateGraphicsPipelineState(gbufferPSODesc);

    ShaderMacro instMacro = { "USE_GPU_SCENE_INSTANCING", "1" };
    m_GBufferVS_Instanced = device->CompileShader(
        EShaderType::Vertex, src.c_str(), "VSMain", "vs_5_0", &instMacro, 1);
    if (m_GBufferVS_Instanced)
    {
        GraphicsPipelineStateInitializer gbufferInstanced = gbufferPSODesc;
        gbufferInstanced.VertexShader = m_GBufferVS_Instanced.get();
        m_GBufferPSO_Instanced = device->CreateGraphicsPipelineState(gbufferInstanced);
    }

    return m_GBufferPSO != nullptr;
}

bool KiwiEngineApp::CreateDeferredLightingPipelines(RHIDevice* device, const std::string& src)
{
    m_DeferredLightingVS.reset();
    m_DeferredPointLightVS.reset();
    m_DeferredLightingPS.reset();
    m_DeferredLightingAdditivePSO.reset();
    m_DeferredPointLightPSO.reset();

    m_DeferredLightingVS = device->CompileShader(EShaderType::Vertex, src.c_str(), "VSMain", "vs_5_0");
    m_DeferredPointLightVS = device->CompileShader(EShaderType::Vertex, src.c_str(), "VSPointLight", "vs_5_0");
    m_DeferredLightingPS = device->CompileShader(EShaderType::Pixel, src.c_str(), "PSMain", "ps_5_0");
    if (!m_DeferredLightingVS || !m_DeferredPointLightVS || !m_DeferredLightingPS)
        return false;

    // Additive into the HDR scene color.
    GraphicsPipelineStateInitializer directionalDesc = FullscreenPipelineDesc(
        EFormat::R16G16B16A16_FLOAT, m_DeferredLightingVS.get(), m_DeferredLightingPS.get());
    directionalDesc.AdditiveBlend = true;
    m_DeferredLightingAdditivePSO = device->CreateGraphicsPipelineState(directionalDesc);

    // Back faces only: each covered pixel is shaded once, including when the camera is inside the volume.
    GraphicsPipelineStateInitializer pointDesc = directionalDesc;
    pointDesc.RasterizerState = RasterizerStateDesc(ERasterizerFillMode::Solid, ECullMode::Front);
    pointDesc.VertexShader = m_DeferredPointLightVS.get();
    m_DeferredPointLightPSO = device->CreateGraphicsPipelineState(pointDesc);

    return m_DeferredLightingAdditivePSO && m_DeferredPointLightPSO;
}

bool KiwiEngineApp::CreateDeferredAmbientPipeline(RHIDevice* device, const std::string& src)
{
    m_DeferredAmbientVS.reset();
    m_DeferredAmbientPS.reset();
    m_DeferredAmbientPSO.reset();

    m_DeferredAmbientVS = device->CompileShader(EShaderType::Vertex, src.c_str(), "VSMain", "vs_5_0");
    m_DeferredAmbientPS = device->CompileShader(EShaderType::Pixel, src.c_str(), "PSMain", "ps_5_0");
    if (!m_DeferredAmbientVS || !m_DeferredAmbientPS)
        return false;

    m_DeferredAmbientPSO = device->CreateGraphicsPipelineState(FullscreenPipelineDesc(
        EFormat::R16G16B16A16_FLOAT, m_DeferredAmbientVS.get(), m_DeferredAmbientPS.get()));
    return m_DeferredAmbientPSO != nullptr;
}

bool KiwiEngineApp::CreateBufferVisualizationPipeline(RHIDevice* device, const std::string& src)
{
    m_BufferVisVS.reset();
    m_BufferVisPS.reset();
    m_BufferVisPSO.reset();

    m_BufferVisVS = device->CompileShader(EShaderType::Vertex, src.c_str(), "VSMain", "vs_5_0");
    m_BufferVisPS = device->CompileShader(EShaderType::Pixel, src.c_str(), "PSMain", "ps_5_0");
    if (!m_BufferVisVS || !m_BufferVisPS)
        return false;

    m_BufferVisPSO = device->CreateGraphicsPipelineState(FullscreenPipelineDesc(
        EFormat::R8G8B8A8_UNORM, m_BufferVisVS.get(), m_BufferVisPS.get()));
    return m_BufferVisPSO != nullptr;
}

bool KiwiEngineApp::CreateDeferredPipelines(RHIDevice* device, const std::string& shaderName, const std::string& src)
{
    if (shaderName == "GBufferPass")         return CreateGBufferPipelines(device, src);
    if (shaderName == "DeferredLighting")    return CreateDeferredLightingPipelines(device, src);
    if (shaderName == "DeferredAmbient")     return CreateDeferredAmbientPipeline(device, src);
    if (shaderName == "BufferVisualization") return CreateBufferVisualizationPipeline(device, src);
    if (shaderName == "ShadowPass")          return CreateShadowPipelines(device, src);
    return false;
}

bool KiwiEngineApp::CompileDeferredShader(RHIDevice* device, const char* shaderName)
{
    std::string src = ReadShaderFileWithIncludes(DeferredShaderPath(device->GetApiType(), shaderName));
    if (src.empty())
        return false;

    bool ok = CreateDeferredPipelines(device, shaderName, src);
    if (ok)
        std::cout << "[Kiwi] " << shaderName << " shader compiled successfully" << std::endl;
    else
        std::cerr << "[Kiwi] Failed to compile " << shaderName << " shader" << std::endl;
    return ok;
}

void KiwiEngineApp::CompileDeferredShaders(RHIDevice* device)
{
    for (const char* name : kDeferredShaderNames)
        CompileDeferredShader(device, name);
}

// ============================================================
// Shader hot reload
// ============================================================

void KiwiEngineApp::RecordDeferredShaderTimestamps(RHI_API_TYPE api)
{
    for (const char* name : kDeferredShaderNames)
        m_DeferredShaderTimestamps[name] = std::filesystem::last_write_time(DeferredShaderPath(api, name));
}

// Release and recompile every shader without an RHI switch
void KiwiEngineApp::ReloadAllShaders()
{
    auto device = GetDevice();
    if (!device) return;

    std::cout << "[Kiwi] Reloading all shaders..." << std::endl;

    // 1. Release all existing shader resources
    m_ShaderLibrary.ReleaseAll();
    m_MaterialShaders.ReleaseAll();
    ReleaseDeferredShaders();
    ReleasePostProcessResources();
    ReleaseShadowShaders(); // Keep shadow CB/sampler/atlas

    // 2. Recompile ShaderLibrary
    auto api = device->GetApiType();
    std::string shaderDir = ForwardShaderDirectory(api, m_ShaderDir);
    m_ShaderLibrary.Initialize(shaderDir, device, m_InputLayout.get());
    InitMaterialShaders(device);

    // 3. Recompile post-process shaders
    InitPostProcessResources(device);

    // 4. Recompile deferred + shadow shaders
    if (IsDeferredRHI(api))
    {
        CompileDeferredShaders(device);
        RecordDeferredShaderTimestamps(api);
    }

    m_FirstShaderLoad = false;

    std::cout << "[Kiwi] All shaders reloaded." << std::endl;
}

// Incremental reload: only recompile shaders whose source files have been modified
void KiwiEngineApp::ReloadModifiedShaders()
{
    auto device = GetDevice();
    if (!device) return;

    // On first load, do a full reload (timestamps aren't recorded yet)
    if (m_FirstShaderLoad)
    {
        ReloadAllShaders();
        return;
    }

    std::cout << "[Kiwi] Checking for modified shaders..." << std::endl;
    int total = 0;

    total += m_ShaderLibrary.ReloadModifiedShaders();
    total += m_PostProcessLibrary.ReloadModifiedShaders();

    if (IsDeferredRHI(device->GetApiType()))
    {
        for (const char* name : kDeferredShaderNames)
            total += ReloadDeferredShaderIfModified(device, name);
    }

    if (total > 0)
        std::cout << "[Kiwi] " << total << " shader(s) hot-reloaded." << std::endl;
    else
        std::cout << "[Kiwi] No modified shaders found." << std::endl;
}

int KiwiEngineApp::ReloadDeferredShaderIfModified(RHIDevice* device, const char* shaderName)
{
    const std::string filePath = DeferredShaderPath(device->GetApiType(), shaderName);
    auto lastWrite = std::filesystem::last_write_time(filePath);
    auto it = m_DeferredShaderTimestamps.find(shaderName);
    if (it != m_DeferredShaderTimestamps.end() && it->second == lastWrite)
        return 0;

    std::string src = ReadShaderFileWithIncludes(filePath);
    if (src.empty()) return 0;

    std::cout << "[Kiwi] Recompiling deferred shader: " << shaderName << std::endl;
    CreateDeferredPipelines(device, shaderName, src);
    m_DeferredShaderTimestamps[shaderName] = lastWrite;
    return 1;
}
