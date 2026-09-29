#pragma once

#include "Core/Application.h"
#include "Core/EditorInput.h"
#include "Core/FrameRenderParams.h"
#include "Core/PassTimer.h"
#include "Core/RendererUtils.h"
#include "Core/SceneRendering.h"
#include "Editor/TransformGizmo.h"
#include "Math/Math.h"
#include "RHI/RHI.h"
#include "RHI/UniformBuffer.h"
#include "Renderer/RenderScene.h"
#include "Scene/CameraComponent.h"
#include "Scene/LightComponent.h"
#include "Scene/Material.h"
#include "Scene/MaterialShaderCache.h"
#include "Scene/MeshBatch.h"
#include "Scene/MeshComponent.h"
#include "Scene/MeshPassProcessor.h"
#include "Scene/PostProcessComponent.h"
#include "Scene/PostProcessShaderLibrary.h"
#include "Scene/Scene.h"
#include "Scene/SceneObject.h"
#include "Scene/ShaderLibrary.h"
#include "Scene/Shaders.h"
#include "Scene/TextureManager.h"
#include "Scene/ViewMode.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Kiwi;

// Scene editor application. Method definitions are split by subsystem:
//   Core/KiwiEngineApp.cpp   lifecycle, game frame (input, editor UI, frame snapshot), render frame, picking
//   Core/SceneRendering.cpp  scene renderer selection and view setup (SceneRenderer, ViewInfo)
//   Renderer/DeferredShadingRenderer.cpp / ForwardShadingRenderer.cpp  per-path Render() of the scene renderers
//   Core/Renderer.cpp        shared pass helpers (draw submission, shadow depth, gizmo)
//   Core/RayTracing.cpp      CPU ray tracing path (DeferredShadingSceneRenderer::RenderRayTracing)
//   Core/RenderResources.cpp RHI resources, deferred pipelines, shader hot reload
//   Core/PostProcess.cpp     offscreen targets and post-process chain
//   Core/Shadows.cpp         cascaded shadow map resources and cascade setup
//   Editor/*.cpp             ImGui editor (menu, toolbar, panels, content browser, material editor)
//
// Threads: the game thread owns Scene, the editor and everything under "Scene and assets" / "Editor state".
// The render thread owns RenderScene, the scene renderer and every GPU resource below "Shared render resources",
// and only sees the game through proxies and the FrameRenderParams of the frame it is rendering.
class KiwiEngineApp : public Application
{
public:
    KiwiEngineApp();
    ~KiwiEngineApp() override;

    static RHI_API_TYPE GetDefaultRHIType();

protected:
    void OnInit() override;
    void OnUpdate(float deltaTime) override;
    void OnRender() override;
    void OnRHIShutdown() override;
    void OnRHIReady() override;

private:
    // Scene renderers still call back into the pass functions and resources below.
    friend class Kiwi::SceneRenderer;
    friend class Kiwi::DeferredShadingSceneRenderer;
    friend class Kiwi::ForwardShadingSceneRenderer;

    // ============================================================
    // Lifecycle, scene and picking (Core/KiwiEngineApp.cpp)
    // ============================================================
    void ResolveAssetDirectories();
    void CreateDefaultScene(const std::string& savePath);
    void HandleViewportMouse();
    GizmoViewInfo MakeGizmoViewInfo() const;
    void UpdateWindowTitle();
    void UpdateCameraFromScene();
    void UpdateCameraProjection();
    void PickObject(int mouseX, int mouseY);
    void SanitizeRenderPath();
    void BuildFrameRenderParams(FrameRenderParams& Out);
    RenderStats GetRenderStats() const;

    // Render thread: renders, presents and submits one frame described by Params.
    void RenderFrame_RenderThread(FrameRenderParams& Params);
    void PublishRenderStats();
    RHITextureView* GetBackBufferRTV();

    // ============================================================
    // Scene renderer and its view (Core/SceneRendering.cpp)
    // ============================================================
    ERenderPath ResolveRenderPath() const;
    void PrepareSceneRenderer(ERenderPath Path);
    ViewInfo& GetViewInfo() { return SceneRenderer->GetView(); }

    // ============================================================
    // Pass helpers used by the scene renderers (Core/Renderer.cpp)
    // ============================================================
    void SubmitMeshDrawCommands(RHICommandContext* Ctx, const std::vector<MeshDrawCommand>& Commands, InstanceCullingContext& InstanceCulling);
    void RenderShadowPass(RHICommandContext* Ctx, const std::vector<MeshDrawCommand>& Commands, InstanceCullingContext& InstanceCulling);
    void BindMaterialTextures(RHICommandContext* Ctx, const PrimitiveSceneProxy& Proxy);
    void UpdateDeferredLightingCB();
    void UpdateBufferVisualizationCB();
    void DrawGizmo(RHICommandContext* Ctx);

    // ============================================================
    // RHI resources, deferred pipelines, shader hot reload (Core/RenderResources.cpp)
    // ============================================================
    void InitRHIResources();
    void ShutdownImGui();
    void InitMaterialShaders(RHIDevice* device);
    void RegisterSharedMaterialShaders();
    void CreateGBufferResources(RHIDevice* device, uint32_t width, uint32_t height);
    void ReleaseGBufferResources();
    void ReleaseDeferredShaders();
    std::string DeferredShaderPath(RHI_API_TYPE api, const char* name) const;
    // Each Create*Pipelines resets its shaders/PSOs, then compiles `src`. Returns false on compile failure.
    bool CreateGBufferPipelines(RHIDevice* device, const std::string& src);
    bool CreateDeferredLightingPipelines(RHIDevice* device, const std::string& src);
    bool CreateDeferredAmbientPipeline(RHIDevice* device, const std::string& src);
    bool CreateBufferVisualizationPipeline(RHIDevice* device, const std::string& src);
    bool CreateDeferredPipelines(RHIDevice* device, const std::string& shaderName, const std::string& src);
    bool CompileDeferredShader(RHIDevice* device, const char* shaderName);
    void CompileDeferredShaders(RHIDevice* device);
    void RecordDeferredShaderTimestamps(RHI_API_TYPE api);
    void ReloadAllShaders();
    void ReloadModifiedShaders();
    int ReloadDeferredShaderIfModified(RHIDevice* device, const char* shaderName);

    // ============================================================
    // Post-process (Core/PostProcess.cpp)
    // ============================================================
    void InitPostProcessResources(RHIDevice* device);
    void CreateOffscreenRenderTargets(RHIDevice* device, uint32_t width, uint32_t height);
    void ReleasePostProcessResources();
    void CollectActivePostProcessEffects(std::vector<PostProcessMaterial>& outEffects);
    void ExecutePostProcessPasses(RHICommandContext* ctx, const std::vector<PostProcessMaterial>& effects);

    // ============================================================
    // Cascaded shadow maps (Core/Shadows.cpp)
    // ============================================================
    void InitShadowResources(RHIDevice* device);
    bool CreateShadowPipelines(RHIDevice* device, const std::string& src);
    void CreateShadowMaps(RHIDevice* device, uint32_t cascadeSize, int numCascades);
    void ReleaseShadowMaps();
    void ReleaseShadowShaders();
    void ReleaseShadowResources();
    void UpdateShadowData();
    void UploadShadowUB();

    // ============================================================
    // Editor UI (Editor/*.cpp)
    // ============================================================
    void DrawEditorUI();
    // EditorUI.cpp
    void DrawMenuBar();
    void DrawScenePanel();
    void DrawPlacerTab();
    void DrawRenderingTab();
    // EditorToolbar.cpp
    float OverlayWidth() const;
    void DrawRenderDocOverlay();
    void DrawStatsOverlay();
    void DrawStatsPanel();
    void DrawViewModeButton();
    void DrawCameraButton();
    void DrawShaderReloadButton();
    void DrawGizmoModeBar();
    // DetailsPanel.cpp
    void DrawDetailTab();
    // ContentBrowser.cpp
    void DrawContentBrowser();
    void OnContentDoubleClick(const std::string& fullPath, const std::string& ext);
    // MaterialEditor.cpp
    void DrawTexturePicker();
    void DrawTexturePickerModal();
    // mesh: when set, the slot edits that primitive's material instance.
    void DrawTextureSlotRow(const std::string& slotLabel, const std::string& propKey,
                            const std::string& uniqueId, Material* mat, MeshComponent* mesh = nullptr);
    void DrawMaterialEditor();

    // ============================================================
    // Scene and assets
    // ============================================================
    Scene Scene;
    EditorInput EditorInput;
    std::unique_ptr<SceneRenderer> SceneRenderer; // Render thread. Rebuilt when the render path or RHI changes

    ShaderLibrary ShaderLibrary;
    MaterialShaderCache MaterialShaders;
    TextureManager TextureManager;
    MaterialLibrary MaterialLibrary;
    std::string ShaderDir; // Path to Shaders/ folder
    std::string ScenesDir; // Path to Scenes/ folder
    std::string TexturesDir; // Path to Textures/ folder
    std::string MaterialsDir; // Path to Materials/ folder
    std::string PostProcessShaderDir;
    std::string GLShaderDir;

    float TotalTime = 0.0f;

    // ============================================================
    // Editor state
    // ============================================================
    TransformGizmo Gizmo; // drag state is game thread; the GPU meshes are read by the render thread
    bool ShowStats = false;

    // Content Browser state
    bool ShowContentBrowser = false;
    std::string ContentBrowserSelectedDir; // Currently selected folder in tree

    // Material Editor state
    bool ShowMaterialEditor = false;
    std::string MaterialEditorTarget;  // Name of material being edited

    // Texture picker popup state (used by material editor and inspector)
    bool ShowTexturePicker = false;
    std::string TexturePickerPropKey;   // Which material property to set (e.g. "_BaseColorTex")
    std::string TexturePickerMatTarget; // Parent material asset, used by the material editor
    MeshComponent* TexturePickerMesh = nullptr; // Primitive instance to write, when set

    // Save Scene dialog state
    bool ShowSaveDialog = false;
    char SaveSceneName[128] = {};
    std::string LastWindowTitle; // Track to avoid redundant SetWindowText

    // RenderDoc state
    bool CaptureTriggered = false;
    bool AutoOpenRenderDoc = false;
    uint32_t LastCaptureCount = 0;

    // Shader reload state
    bool PendingShaderReload = false;
    bool FirstShaderLoad = true;  // First load does full recompile to record timestamps
    std::unordered_map<std::string, std::filesystem::file_time_type> DeferredShaderTimestamps;

    // ============================================================
    // Shared render resources
    // ============================================================
    std::unique_ptr<RHIInputLayout>   InputLayout;
    TUniformBufferRef<ViewUniformBuffer>      ShadowViewUB; // b0 during the shadow pass only
    Kiwi::RenderScene                         RenderScene;  // Renderer-side scene: primitive records + GPUScene (t8/t9)
    TUniformBufferRef<PrimitiveUniformBuffer> ObjectUB;     // b1: aux fullscreen/gizmo draws
    static constexpr uint32_t LIGHT_VOLUME_INDEX_COUNT = 60;
    std::unique_ptr<RHIBuffer>        LightVolumeIB; // deferred light icosahedron + fullscreen triangle indices
    std::unique_ptr<RHIPipelineState> PipelineState;  // DX11

    // Camera (cached from scene CameraComponent each frame). Game thread.
    Mat4 ViewMatrix;
    Mat4 ProjectionMatrix;
    Vec3 CameraPosition;

    // ---- View Mode (game thread; the render thread reads RenderParams) ----
    ERenderPath RenderPath = ERenderPath::Deferred;
    EViewMode ViewMode = EViewMode::Lit;

    // ---- Render thread frame state ----
    FrameRenderParams RenderParams; // the frame being rendered
    PassTimer PassTimer;
    mutable std::mutex RenderStatsMutex;
    RenderStats PublishedRenderStats; // written by the render thread, read by the editor

    // ============================================================
    // Post-process resources
    // ============================================================
    PostProcessShaderLibrary PostProcessLibrary;

    // Offscreen render targets (ping-pong buffers)
    std::unique_ptr<RHITexture>     OffscreenRT[2];
    std::unique_ptr<RHITextureView> OffscreenRTV[2];
    std::unique_ptr<RHITextureView> OffscreenSRV[2];
    uint32_t OffscreenWidth = 0;
    uint32_t OffscreenHeight = 0;

    TUniformBufferRef<PostProcessCBData> PostProcessCB;

    // DX11 sampler for post-process (DX12 uses static sampler in root signature)
    std::unique_ptr<RHISampler> PostProcessSampler;

    // Passthrough shader (compiled from built-in code)
    std::unique_ptr<RHIShader> PassthroughVS;
    std::unique_ptr<RHIShader> PassthroughPS;
    std::unique_ptr<RHIPipelineState> PassthroughPSO;

    // ============================================================
    // Deferred rendering resources
    // ============================================================
    static constexpr int GBUFFER_COUNT = 4; // A normal, B material, C base color, D emissive
    std::unique_ptr<RHITexture>     GBufferRT[GBUFFER_COUNT];
    std::unique_ptr<RHITextureView> GBufferRTV[GBUFFER_COUNT];
    std::unique_ptr<RHITextureView> GBufferSRV[GBUFFER_COUNT];
    uint32_t GBufferWidth = 0;
    uint32_t GBufferHeight = 0;

    // G-Buffer shaders (compiled separately from ShaderLibrary)
    std::unique_ptr<RHIShader> GBufferVS;
    std::unique_ptr<RHIShader> GBufferPS;
    std::unique_ptr<RHIPipelineState> GBufferPSO; // MRT PSO

    // G-Buffer instanced variants (USE_GPU_SCENE_INSTANCING)
    std::unique_ptr<RHIShader> GBufferVS_Instanced;
    std::unique_ptr<RHIPipelineState> GBufferPSO_Instanced;

    // Deferred Lighting shaders: fullscreen VS for directional lights, light volume VS for point lights
    std::unique_ptr<RHIShader> DeferredLightingVS;
    std::unique_ptr<RHIShader> DeferredPointLightVS;
    std::unique_ptr<RHIShader> DeferredLightingPS;

    // Deferred Ambient shader (opaque first-write pass)
    std::unique_ptr<RHIShader> DeferredAmbientVS;
    std::unique_ptr<RHIShader> DeferredAmbientPS;
    std::unique_ptr<RHIPipelineState> DeferredAmbientPSO;

    // Additive blend PSOs for the instanced light draws
    std::unique_ptr<RHIPipelineState> DeferredLightingAdditivePSO;
    std::unique_ptr<RHIPipelineState> DeferredPointLightPSO;

    // Buffer Visualization shader (fullscreen pass for debug ViewModes)
    std::unique_ptr<RHIShader> BufferVisVS;
    std::unique_ptr<RHIShader> BufferVisPS;
    std::unique_ptr<RHIPipelineState> BufferVisPSO;

    // ============================================================
    // Ray tracing resources
    // ============================================================
    int RayTracingSamplesPerPixel = 1;        // game thread setting
    float RayTracingResolutionPercent = 50.0f; // game thread setting
    uint32_t RayTraceWidth = 0;
    uint32_t RayTraceHeight = 0;
    std::unique_ptr<RHIShader> RayTraceBlitVS;
    std::unique_ptr<RHIShader> RayTraceBlitPS;
    std::unique_ptr<RHIPipelineState> RayTraceBlitPSO;
    std::unique_ptr<RHITexture> RayTraceColor;
    std::unique_ptr<RHITextureView> RayTraceColorSRV;

    // ============================================================
    // Cascaded Shadow Map (CSM) resources — single atlas
    // ============================================================
    static constexpr int MAX_SHADOW_CASCADES = 4;
    std::unique_ptr<RHITexture>     ShadowAtlasRT;       // Single atlas texture (2*size x 2*size)
    std::unique_ptr<RHITextureView> ShadowAtlasDSV;      // DSV for the whole atlas
    std::unique_ptr<RHITextureView> ShadowAtlasSRV;      // SRV for sampling in lighting pass
    uint32_t ShadowCascadeSize = 0;                      // Per-cascade resolution (e.g. 2048)

    // Shadow pass shader and PSO
    std::unique_ptr<RHIShader> ShadowPassVS;
    std::unique_ptr<RHIPipelineState> ShadowPassPSO;

    // Shadow pass instanced variants (USE_GPU_SCENE_INSTANCING)
    std::unique_ptr<RHIShader> ShadowPassVS_Instanced;
    std::unique_ptr<RHIPipelineState> ShadowPassPSO_Instanced;

    // Shadow uniform buffer (b2)
    TUniformBufferRef<ShadowUniformBuffer> ShadowCB;

    // Comparison sampler for DX11 shadow sampling
    std::unique_ptr<RHISampler> ShadowSampler;

    // Cached CSM data (computed each frame)
    ShadowUniformBuffer ShadowUBData = {};
    Mat4 LightViewProjMatrices[MAX_SHADOW_CASCADES];
    Mat4 LightViewMatrices[MAX_SHADOW_CASCADES];
    Mat4 LightProjMatrices[MAX_SHADOW_CASCADES];
};
