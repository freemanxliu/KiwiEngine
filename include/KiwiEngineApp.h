#pragma once

#include "Core/Application.h"
#include "Core/EditorInput.h"
#include "Core/PassTimer.h"
#include "Core/RendererUtils.h"
#include "Editor/TransformGizmo.h"
#include "Math/Math.h"
#include "RHI/RHI.h"
#include "Scene/CameraComponent.h"
#include "Scene/GPUScene.h"
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
#include <string>
#include <unordered_map>
#include <vector>

using namespace Kiwi;

// GPU Mesh Data — holds buffers for a single mesh
struct GPUMeshData
{
    std::unique_ptr<RHIBuffer> VertexBuffer;
    std::unique_ptr<RHIBuffer> IndexBuffer;
    uint32_t VertexCount = 0;
    uint32_t IndexCount = 0;
};

// Shared Mesh Pool — same EPrimitiveType shares one VB/IB pair
struct SharedMeshEntry
{
    RHIBuffer* VertexBuffer = nullptr;   // Non-owning pointer into shared pool
    RHIBuffer* IndexBuffer  = nullptr;
    uint32_t   VertexCount  = 0;
    uint32_t   IndexCount   = 0;
    uint32_t   MeshID       = 0;        // Unique ID for sorting/batching
};

// Scene editor application. Method definitions are split by subsystem:
//   Core/KiwiEngineApp.cpp   lifecycle, per-frame update, scene/camera/lights, picking, mesh buffers
//   Core/Renderer.cpp        frame passes (shadow, G-Buffer, lighting, forward, gizmo)
//   Core/RayTracing.cpp      CPU ray tracing path
//   Core/RenderResources.cpp RHI resources, deferred pipelines, shader hot reload
//   Core/PostProcess.cpp     offscreen targets and post-process chain
//   Core/Shadows.cpp         cascaded shadow map resources and cascade setup
//   Editor/*.cpp             ImGui editor (menu, toolbar, panels, content browser, material editor)
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
    void CollectLightsFromScene();
    void InitView();
    void PickObject(int mouseX, int mouseY);
    void RebuildAllGPUBuffers();
    SharedMeshEntry GetSharedMesh(size_t objectIndex) const;

    // ============================================================
    // Frame passes (Core/Renderer.cpp, Core/RayTracing.cpp)
    // ============================================================
    void PrepareMeshBatches();
    void SubmitMeshDrawCommands(RHICommandContext* ctx, const std::vector<MeshDrawCommand>& commands);
    void RenderShadowPass(RHICommandContext* ctx, const std::vector<MeshDrawCommand>& commands);
    void RenderDeferred(RHICommandContext* ctx, RHITextureView* sceneRTV, const Viewport& vp, const ScissorRect& sr);
    void RenderForward(RHICommandContext* ctx, RHITextureView* sceneRTV, const Viewport& vp, const ScissorRect& sr);
    void RenderRayTracing(RHICommandContext* ctx, RHITextureView* sceneRTV, const Viewport& vp, const ScissorRect& sr);
    void UploadViewUB();
    void BindMaterialTextures(RHICommandContext* ctx, MeshComponent* meshComp);
    void UpdateDeferredLightingCB();
    void UpdateBufferVisualizationCB();
    void DrawGizmo(RHICommandContext* ctx);

    // ============================================================
    // RHI resources, deferred pipelines, shader hot reload (Core/RenderResources.cpp)
    // ============================================================
    void InitRHIResources();
    void ShutdownImGui();
    void InitMaterialShaders(RHIDevice* device);
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
    void CollectActivePostProcessEffects(std::vector<PostProcessMaterial*>& outEffects);
    void ExecutePostProcessPasses(RHICommandContext* ctx, RHIDevice* device,
                                  const std::vector<PostProcessMaterial*>& effects,
                                  RHISwapChain* swapChain);

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
    Scene m_Scene;
    EditorInput m_EditorInput;
    std::vector<GPUMeshData> m_GPUMeshes;
    std::vector<SharedMeshEntry> m_SharedMeshPool;  // Shared VB/IB per primitive type
    std::vector<RenderItem> m_RenderList; // Sorted visible objects from InitView()

    ShaderLibrary m_ShaderLibrary;
    MaterialShaderCache m_MaterialShaders;
    TextureManager m_TextureManager;
    MaterialLibrary m_MaterialLibrary;
    std::string m_ShaderDir; // Path to Shaders/ folder
    std::string m_ScenesDir; // Path to Scenes/ folder
    std::string m_TexturesDir; // Path to Textures/ folder
    std::string m_MaterialsDir; // Path to Materials/ folder
    std::string m_PostProcessShaderDir;
    std::string m_GLShaderDir;

    float m_TotalTime = 0.0f;

    // ============================================================
    // Editor state
    // ============================================================
    TransformGizmo m_Gizmo;
    PassTimer m_PassTimer;
    bool m_ShowStats = false;

    // Content Browser state
    bool m_ShowContentBrowser = false;
    std::string m_ContentBrowserSelectedDir; // Currently selected folder in tree

    // Material Editor state
    bool m_ShowMaterialEditor = false;
    std::string m_MaterialEditorTarget;  // Name of material being edited

    // Texture picker popup state (used by material editor and inspector)
    bool m_ShowTexturePicker = false;
    std::string m_TexturePickerPropKey;   // Which material property to set (e.g. "_BaseColorTex")
    std::string m_TexturePickerMatTarget; // Parent material asset, used by the material editor
    MeshComponent* m_TexturePickerMesh = nullptr; // Primitive instance to write, when set

    // Save Scene dialog state
    bool m_ShowSaveDialog = false;
    char m_SaveSceneName[128] = {};
    std::string m_LastWindowTitle; // Track to avoid redundant SetWindowText

    // RenderDoc state
    bool m_CaptureTriggered = false;
    bool m_AutoOpenRenderDoc = false;
    uint32_t m_LastCaptureCount = 0;

    // Shader reload state
    bool m_PendingShaderReload = false;
    bool m_FirstShaderLoad = true;  // First load does full recompile to record timestamps
    std::unordered_map<std::string, std::filesystem::file_time_type> m_DeferredShaderTimestamps;

    // ============================================================
    // Shared render resources
    // ============================================================
    std::unique_ptr<RHIInputLayout>   m_InputLayout;
    std::unique_ptr<RHIBuffer>        m_ViewUB;      // b0: camera ViewUniformBuffer, uploaded once per frame
    std::unique_ptr<RHIBuffer>        m_ShadowViewUB; // b0 during the shadow pass only
    GPUScene                          m_GPUScene;       // b1: GPU Scene Buffer (all primitives, per-frame upload)
    std::unique_ptr<RHIBuffer>        m_ObjectUB;    // b1: PrimitiveUniformBuffer (aux: fullscreen/gizmo)
    static constexpr uint32_t LIGHT_VOLUME_INDEX_COUNT = 60;
    std::unique_ptr<RHIBuffer>        m_LightVolumeIB; // deferred light icosahedron + fullscreen triangle indices
    std::unique_ptr<RHIPipelineState> m_PipelineState;  // DX11

    // Camera (cached from scene CameraComponent each frame)
    Mat4 m_ViewMatrix;
    Mat4 m_ProjectionMatrix;
    Vec3 m_CameraPosition;

    // Lights (cached from scene LightComponents each frame)
    GPULightData m_LightDataCache[MAX_LIGHTS] = {};
    int m_NumActiveLights = 0;
    int m_NumDirectionalLights = 0; // m_LightDataCache holds directional lights first

    // ---- View Mode ----
    ERenderPath m_RenderPath = ERenderPath::Deferred;
    EViewMode m_ViewMode = EViewMode::Lit;
    std::vector<MeshBatch> m_VisibleMeshBatches;

    // ============================================================
    // Post-process resources
    // ============================================================
    PostProcessShaderLibrary m_PostProcessLibrary;

    // Offscreen render targets (ping-pong buffers)
    std::unique_ptr<RHITexture>     m_OffscreenRT[2];
    std::unique_ptr<RHITextureView> m_OffscreenRTV[2];
    std::unique_ptr<RHITextureView> m_OffscreenSRV[2];
    uint32_t m_OffscreenWidth = 0;
    uint32_t m_OffscreenHeight = 0;

    std::unique_ptr<RHIBuffer> m_PostProcessCB;

    // DX11 sampler for post-process (DX12 uses static sampler in root signature)
    std::unique_ptr<RHISampler> m_PostProcessSampler;

    // Passthrough shader (compiled from built-in code)
    std::unique_ptr<RHIShader> m_PassthroughVS;
    std::unique_ptr<RHIShader> m_PassthroughPS;
    std::unique_ptr<RHIPipelineState> m_PassthroughPSO;

    // ============================================================
    // Deferred rendering resources
    // ============================================================
    static constexpr int GBUFFER_COUNT = 4; // A normal, B material, C base color, D emissive
    std::unique_ptr<RHITexture>     m_GBufferRT[GBUFFER_COUNT];
    std::unique_ptr<RHITextureView> m_GBufferRTV[GBUFFER_COUNT];
    std::unique_ptr<RHITextureView> m_GBufferSRV[GBUFFER_COUNT];
    uint32_t m_GBufferWidth = 0;
    uint32_t m_GBufferHeight = 0;

    // G-Buffer shaders (compiled separately from ShaderLibrary)
    std::unique_ptr<RHIShader> m_GBufferVS;
    std::unique_ptr<RHIShader> m_GBufferPS;
    std::unique_ptr<RHIPipelineState> m_GBufferPSO; // MRT PSO

    // G-Buffer instanced variants (USE_GPU_SCENE_INSTANCING)
    std::unique_ptr<RHIShader> m_GBufferVS_Instanced;
    std::unique_ptr<RHIPipelineState> m_GBufferPSO_Instanced;

    // Deferred Lighting shaders: fullscreen VS for directional lights, light volume VS for point lights
    std::unique_ptr<RHIShader> m_DeferredLightingVS;
    std::unique_ptr<RHIShader> m_DeferredPointLightVS;
    std::unique_ptr<RHIShader> m_DeferredLightingPS;

    // Deferred Ambient shader (opaque first-write pass)
    std::unique_ptr<RHIShader> m_DeferredAmbientVS;
    std::unique_ptr<RHIShader> m_DeferredAmbientPS;
    std::unique_ptr<RHIPipelineState> m_DeferredAmbientPSO;

    // Additive blend PSOs for the instanced light draws
    std::unique_ptr<RHIPipelineState> m_DeferredLightingAdditivePSO;
    std::unique_ptr<RHIPipelineState> m_DeferredPointLightPSO;

    // Buffer Visualization shader (fullscreen pass for debug ViewModes)
    std::unique_ptr<RHIShader> m_BufferVisVS;
    std::unique_ptr<RHIShader> m_BufferVisPS;
    std::unique_ptr<RHIPipelineState> m_BufferVisPSO;

    // ============================================================
    // Ray tracing resources
    // ============================================================
    int m_RayTracingSamplesPerPixel = 1;
    float m_RayTracingResolutionPercent = 50.0f;
    uint32_t m_RayTraceWidth = 0;
    uint32_t m_RayTraceHeight = 0;
    std::unique_ptr<RHIShader> m_RayTraceBlitVS;
    std::unique_ptr<RHIShader> m_RayTraceBlitPS;
    std::unique_ptr<RHIPipelineState> m_RayTraceBlitPSO;
    std::unique_ptr<RHITexture> m_RayTraceColor;
    std::unique_ptr<RHITextureView> m_RayTraceColorSRV;

    // ============================================================
    // Cascaded Shadow Map (CSM) resources — single atlas
    // ============================================================
    static constexpr int MAX_SHADOW_CASCADES = 4;
    std::unique_ptr<RHITexture>     m_ShadowAtlasRT;       // Single atlas texture (2*size x 2*size)
    std::unique_ptr<RHITextureView> m_ShadowAtlasDSV;      // DSV for the whole atlas
    std::unique_ptr<RHITextureView> m_ShadowAtlasSRV;      // SRV for sampling in lighting pass
    uint32_t m_ShadowCascadeSize = 0;                      // Per-cascade resolution (e.g. 2048)

    // Shadow pass shader and PSO
    std::unique_ptr<RHIShader> m_ShadowPassVS;
    std::unique_ptr<RHIPipelineState> m_ShadowPassPSO;

    // Shadow pass instanced variants (USE_GPU_SCENE_INSTANCING)
    std::unique_ptr<RHIShader> m_ShadowPassVS_Instanced;
    std::unique_ptr<RHIPipelineState> m_ShadowPassPSO_Instanced;

    // Shadow uniform buffer (b2)
    std::unique_ptr<RHIBuffer> m_ShadowCB;

    // Comparison sampler for DX11 shadow sampling
    std::unique_ptr<RHISampler> m_ShadowSampler;

    // Cached CSM data (computed each frame)
    ShadowUniformBuffer m_ShadowUBData = {};
    Mat4 m_LightViewProjMatrices[MAX_SHADOW_CASCADES];
    Mat4 m_LightViewMatrices[MAX_SHADOW_CASCADES];
    Mat4 m_LightProjMatrices[MAX_SHADOW_CASCADES];
};
