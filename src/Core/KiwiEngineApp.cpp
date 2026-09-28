#include "KiwiEngineApp.h"

#include "Core/EngineConfig.h"
#include "Core/Platform.h"
#include "Math/RayMath.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <iostream>

namespace
{

void ComputeWorldAABB(const MeshComponent& mesh, Vec3& outMin, Vec3& outMax)
{
    Mat4 world = mesh.GetWorldMatrix();
    const auto& verts = mesh.MeshData.GetVertices();

    if (verts.empty())
    {
        outMin = outMax = mesh.Position;
        return;
    }

    outMin = { 1e30f, 1e30f, 1e30f };
    outMax = { -1e30f, -1e30f, -1e30f };

    for (const auto& v : verts)
    {
        float wx = v.Position.x * world.m[0][0] + v.Position.y * world.m[1][0] + v.Position.z * world.m[2][0] + world.m[3][0];
        float wy = v.Position.x * world.m[0][1] + v.Position.y * world.m[1][1] + v.Position.z * world.m[2][1] + world.m[3][1];
        float wz = v.Position.x * world.m[0][2] + v.Position.y * world.m[1][2] + v.Position.z * world.m[2][2] + world.m[3][2];

        outMin.x = std::min(outMin.x, wx); outMin.y = std::min(outMin.y, wy); outMin.z = std::min(outMin.z, wz);
        outMax.x = std::max(outMax.x, wx); outMax.y = std::max(outMax.y, wy); outMax.z = std::max(outMax.z, wz);
    }
}

} // namespace

KiwiEngineApp::KiwiEngineApp()
    : Application(
        WindowDesc{ "Kiwi Engine - Scene Editor", 1280, 720 },
        RHIInitParams{ GetDefaultRHIType(), true })
{
}

KiwiEngineApp::~KiwiEngineApp()
{
    ShutdownImGui();
    ImGui::DestroyContext();
}

RHI_API_TYPE KiwiEngineApp::GetDefaultRHIType()
{
    auto& config = Kiwi::EngineConfig::Get();
    std::string rhi = config.GetString("Rendering", "DefaultRHI", "DX11");
    std::cout << "[Kiwi] DefaultRHI from config: '" << rhi << "'" << std::endl;
#if defined(__APPLE__)
    if (rhi != "Metal" && rhi != "metal" && rhi != "METAL")
        std::cout << "[Kiwi] This build renders with Metal." << std::endl;
    return RHI_API_TYPE::METAL;
#else
    if (rhi == "DX12" || rhi == "dx12")   return RHI_API_TYPE::DX12;
    if (rhi == "OpenGL" || rhi == "opengl" || rhi == "OPENGL") return RHI_API_TYPE::OPENGL;
    if (rhi == "Vulkan" || rhi == "vulkan" || rhi == "VULKAN") return RHI_API_TYPE::VULKAN;
    if (rhi == "Metal" || rhi == "metal" || rhi == "METAL")
    {
        std::cout << "[Kiwi] Metal is only available on Apple platforms. Using DX11." << std::endl;
        return RHI_API_TYPE::DX11;
    }
    return RHI_API_TYPE::DX11;
#endif
}

void KiwiEngineApp::OnInit()
{
    std::cout << "[Kiwi] Initializing Scene Editor..." << std::endl;

    ResolveAssetDirectories();

    // ---- Init ImGui context (once) ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#if !defined(__APPLE__)
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
#endif
    ImGui::StyleColorsDark();

    // ---- Init RHI-specific resources ----
    InitRHIResources();

    // ---- Load default scene from file ----
    std::string defaultScene = m_ScenesDir + "/Default.json";
    if (std::filesystem::exists(defaultScene))
    {
        m_Scene.LoadFromFile(defaultScene);
        std::cout << "[Kiwi] Loaded default scene: " << defaultScene << std::endl;
    }
    else
    {
        CreateDefaultScene(defaultScene);
    }

    RebuildAllGPUBuffers();

    // ---- Update camera matrices ----
    UpdateCameraFromScene();

    GetWindow()->SetResizeCallback([this](uint32_t width, uint32_t height) {
        UpdateCameraProjection();
    });

    m_Gizmo.CreateGPUResources(GetDevice());
    m_TextureManager.Initialize(GetDevice(), GetContext());
    m_MaterialLibrary.Initialize(m_MaterialsDir);
    m_EditorInput.Init(GetWindow(), &m_Scene);

    std::cout << "[Kiwi] Scene Editor initialized!" << std::endl;
}

// Finds Shaders/, Scenes/, Textures/... next to the executable or inside the app bundle.
void KiwiEngineApp::ResolveAssetDirectories()
{
    std::string exeDir = GetExecutableDirectory();
    // Writable folders must not be created inside Foo.app/, or codesign
    // reports "unsealed contents present in the bundle root".
    std::string outsideDir = exeDir;
    const std::string bundleMarker = ".app/Contents/MacOS";
    auto bundlePos = exeDir.rfind(bundleMarker);
    if (bundlePos != std::string::npos)
    {
        std::string appPath = exeDir.substr(0, bundlePos + 4);
        auto slash = appPath.find_last_of('/');
        if (slash != std::string::npos)
            outsideDir = appPath.substr(0, slash);
    }
    auto resolveDir = [&](const std::string& name, bool create) {
        namespace fs = std::filesystem;
        std::string candidates[] = {
            exeDir + "/../Resources/" + name,
            exeDir + "/" + name,
            outsideDir + "/" + name,
        };
        for (const auto& path : candidates)
        {
            if (fs::exists(path))
                return path;
        }
        std::string created = outsideDir + "/" + name;
        if (create)
            fs::create_directories(created);
        return created;
    };

    m_ShaderDir = resolveDir("Shaders", false);
    m_PostProcessShaderDir = resolveDir("PostProcessShaders", false);
    if (GetCurrentRHIType() == RHI_API_TYPE::METAL)
    {
        std::string metalPost = resolveDir("MetalPostProcess", false);
        if (std::filesystem::exists(metalPost))
            m_PostProcessShaderDir = metalPost;
    }
    std::cout << "[Kiwi] Shader directory: " << m_ShaderDir << std::endl;
    std::cout << "[Kiwi] PostProcess shader directory: " << m_PostProcessShaderDir << std::endl;

    m_ScenesDir = resolveDir("Scenes", true);
    std::cout << "[Kiwi] Scenes directory: " << m_ScenesDir << std::endl;
    m_TexturesDir = resolveDir("Textures", true);
    m_GLShaderDir = resolveDir("GLShaders", false);
    m_MaterialsDir = resolveDir("Materials", true);
}

// First run: build a GPU Scene debug scene (multiple primitive types, materials, lights
// and transforms, to exercise GPU Scene offset binding) and save it to savePath.
void KiwiEngineApp::CreateDefaultScene(const std::string& savePath)
{
    m_Scene.SetName("GPU Scene Debug");

    // ---- Camera ----
    auto* camObj = m_Scene.AddCameraObject("Main Camera");
    auto* cam = camObj->GetComponent<CameraComponent>();
    cam->Position = Vec3(0.0f, 5.0f, -12.0f);
    cam->Rotation = Vec3(25.0f, 0.0f, 0.0f);
    cam->FieldOfView = 45.0f;

    // ---- Directional Light (Sun) ----
    auto* lightObj = m_Scene.AddDirectionalLightObject("Sun Light");
    auto* sunLight = lightObj->GetComponent<DirectionalLightComponent>();
    if (sunLight)
    {
        sunLight->Rotation = { 50.0f, -30.0f, 0.0f };
        sunLight->LightColor = { 1.0f, 0.95f, 0.85f };
        sunLight->Intensity = 3.0f;
    }

    // ---- Point Light (warm fill) ----
    auto* pointLightObj = m_Scene.AddPointLightObject("Point Light Warm");
    auto* pointLight = pointLightObj->GetComponent<PointLightComponent>();
    if (pointLight)
    {
        pointLight->Position = { -3.0f, 3.0f, -2.0f };
        pointLight->LightColor = { 1.0f, 0.7f, 0.3f };
        pointLight->Intensity = 5.0f;
        pointLight->Radius = 12.0f;
    }

    // ---- Point Light (cool fill) ----
    auto* pointLightObj2 = m_Scene.AddPointLightObject("Point Light Cool");
    auto* pointLight2 = pointLightObj2->GetComponent<PointLightComponent>();
    if (pointLight2)
    {
        pointLight2->Position = { 4.0f, 2.5f, 1.0f };
        pointLight2->LightColor = { 0.3f, 0.5f, 1.0f };
        pointLight2->Intensity = 4.0f;
        pointLight2->Radius = 10.0f;
    }

    // ---- Ground (large floor) ----
    auto* floor = m_Scene.AddMeshObject(EPrimitiveType::Floor, "Ground");
    auto* floorMesh = floor->GetComponent<MeshComponent>();
    if (floorMesh) floorMesh->Scale = { 3.0f, 1.0f, 3.0f };

    // ---- Row of cubes (different positions — tests GPU Scene offset correctness) ----
    const float cubeSpacing = 2.5f;
    for (int i = 0; i < 5; ++i)
    {
        float x = (i - 2) * cubeSpacing;
        std::string name = "Cube_" + std::to_string(i + 1);
        auto* cubeObj = m_Scene.AddMeshObject(EPrimitiveType::Cube, name);
        auto* mesh = cubeObj->GetComponent<MeshComponent>();
        if (mesh)
        {
            mesh->Position = { x, 0.5f, 0.0f };
            // Alternate scales to test different transforms
            float s = 0.5f + 0.3f * i;
            mesh->Scale = { s, s, s };
        }
    }

    // ---- Spheres (back row — tests different primitive type) ----
    for (int i = 0; i < 4; ++i)
    {
        float x = (i - 1.5f) * 3.0f;
        std::string name = "Sphere_" + std::to_string(i + 1);
        auto* sphereObj = m_Scene.AddMeshObject(EPrimitiveType::Sphere, name);
        auto* mesh = sphereObj->GetComponent<MeshComponent>();
        if (mesh)
        {
            mesh->Position = { x, 0.8f, 4.0f };
            mesh->Scale = { 0.8f, 0.8f, 0.8f };
        }
    }

    // ---- Cylinders (side columns — tests yet another primitive) ----
    for (int i = 0; i < 2; ++i)
    {
        float x = (i == 0) ? -6.0f : 6.0f;
        std::string name = "Column_" + std::to_string(i + 1);
        auto* cylObj = m_Scene.AddMeshObject(EPrimitiveType::Cylinder, name);
        auto* mesh = cylObj->GetComponent<MeshComponent>();
        if (mesh)
        {
            mesh->Position = { x, 1.5f, 0.0f };
            mesh->Scale = { 0.4f, 1.5f, 0.4f };
        }
    }

    // ---- Rotated cube (tests rotation in GPU Scene) ----
    auto* rotCubeObj = m_Scene.AddMeshObject(EPrimitiveType::Cube, "Rotated_Cube");
    auto* rotMesh = rotCubeObj->GetComponent<MeshComponent>();
    if (rotMesh)
    {
        rotMesh->Position = { 0.0f, 1.5f, -4.0f };
        rotMesh->Rotation = { 30.0f, 45.0f, 15.0f };
        rotMesh->Scale = { 1.2f, 1.2f, 1.2f };
    }

    m_Scene.SaveToFile(savePath);
    std::cout << "[Kiwi] Created GPU Scene debug scene ("
              << m_Scene.GetObjects().size() << " objects)" << std::endl;
}

void KiwiEngineApp::OnUpdate(float deltaTime)
{
    m_TotalTime += deltaTime;

    // Update window title with scene name (only when changed)
    UpdateWindowTitle();
    // Camera fly navigation: hold right mouse button + WASD / arrow keys
    m_EditorInput.Update(deltaTime);

    // Update camera matrices each frame
    UpdateCameraFromScene();

    // Collect light data from scene each frame
    CollectLightsFromScene();

    if (!ImGui::GetIO().WantCaptureMouse)
        HandleViewportMouse();
}

GizmoViewInfo KiwiEngineApp::MakeGizmoViewInfo() const
{
    GizmoViewInfo view;
    view.View = m_ViewMatrix;
    view.Projection = m_ProjectionMatrix;
    view.CameraPosition = m_CameraPosition;
    view.ScreenWidth = GetWindow()->GetWidth();
    view.ScreenHeight = GetWindow()->GetHeight();
    return view;
}

// Left click: grab a gizmo handle of the selected object, otherwise pick a new object.
void KiwiEngineApp::HandleViewportMouse()
{
    const auto& mouse = GetWindow()->GetMouseState();
    const GizmoViewInfo view = MakeGizmoViewInfo();

    if (mouse.LeftClicked)
    {
        SceneObject* sel = m_Scene.GetSelectedObject();
        if (!sel || !m_Gizmo.TryBeginDrag(*sel, mouse.X, mouse.Y, view))
            PickObject(mouse.X, mouse.Y);
    }

    if (m_Gizmo.IsDragging())
    {
        SceneObject* sel = m_Scene.GetSelectedObject();
        if (!mouse.LeftDown)
            m_Gizmo.EndDrag();
        else if (sel)
            m_Gizmo.UpdateDrag(*sel, mouse.X, mouse.Y, view);
    }
}

void KiwiEngineApp::OnRender()
{
    InitView();
    UploadViewUB();

    auto ctx = GetContext();
    auto swapChain = GetSwapChain();
    auto device = GetDevice();

    // ---- Begin frame (DX12: Reset + RootSig + DescriptorHeaps + Barrier; DX11: no-op) ----
    ctx->BeginFrame(swapChain);

    // ---- Collect user post-process effects ----
    std::vector<PostProcessMaterial*> activeEffects;
    CollectActivePostProcessEffects(activeEffects);

    // Always use offscreen RT for HDR pipeline (Tonemap is always-on)
    bool hasPostProcess = (m_OffscreenRT[0] != nullptr);

    // ---- Ensure offscreen RT size matches window ----
    uint32_t winW = GetWindow()->GetWidth();
    uint32_t winH = GetWindow()->GetHeight();
    if (m_OffscreenWidth != winW || m_OffscreenHeight != winH)
    {
        CreateOffscreenRenderTargets(device, winW, winH);
        hasPostProcess = (m_OffscreenRT[0] != nullptr);
    }

    // ---- Ensure G-Buffer size matches window ----
    if (m_GBufferWidth != winW || m_GBufferHeight != winH)
    {
        CreateGBufferResources(device, winW, winH);
    }

    // ---- Viewport and scissor (shared) ----
    Viewport vp;
    vp.TopLeftX = 0; vp.TopLeftY = 0;
    vp.Width = (float)winW;
    vp.Height = (float)winH;
    vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;

    ScissorRect sr;
    sr.Left = 0; sr.Top = 0;
    sr.Right = (int32_t)winW;
    sr.Bottom = (int32_t)winH;

    m_PassTimer.BeginFrame();

    // GL/Vulkan have no G-Buffer. Buffer visualization needs the deferred path.
    bool canDeferred = IsDeferredRHI(GetCurrentRHIType());
    if (!canDeferred)
        m_RenderPath = ERenderPath::Forward;
    if (m_RenderPath == ERenderPath::Forward && IsBufferVisualization(m_ViewMode))
        m_ViewMode = EViewMode::Lit;
    bool useDeferredPipeline = canDeferred && m_RenderPath == ERenderPath::Deferred;
    if (m_RenderPath == ERenderPath::RayTracing && IsBufferVisualization(m_ViewMode))
        m_ViewMode = EViewMode::Lit;

    // Determine the final scene render target (before post-process)
    // If post-process active, render to offscreen RT[0]; else to backbuffer
    RHITextureView* sceneRTV = nullptr;
    if (hasPostProcess)
    {
        sceneRTV = m_OffscreenRTV[0].get();
        ctx->ResourceBarrier(m_OffscreenRT[0].get(),
            RESOURCE_STATE_COMMON, RESOURCE_STATE_RENDER_TARGET);
    }
    else
    {
        sceneRTV = swapChain->GetBackBufferRTV(swapChain->GetCurrentBackBufferIndex());
    }

    if (m_RenderPath == ERenderPath::RayTracing)
        RenderRayTracing(ctx, sceneRTV, vp, sr);
    else if (useDeferredPipeline && m_GBufferPSO && m_GBufferRT[0])
        RenderDeferred(ctx, sceneRTV, vp, sr);
    else
        RenderForward(ctx, sceneRTV, vp, sr);


    // ---- Post-Process Pass (always runs — HDR Tonemap is built-in) ----
    if (hasPostProcess)
    {
        ctx->BeginEvent("Post-Process Pass");
        m_PassTimer.Begin("Post-Process Pass");
        ExecutePostProcessPasses(ctx, device, activeEffects, swapChain);
        m_PassTimer.End();
        ctx->EndEvent();
    }

    // ---- ImGui ----
    ctx->BeginEvent("ImGui Pass");
    m_PassTimer.Begin("ImGui Pass");
    // ImGui always renders to the backbuffer
    auto backBufferRTV = swapChain->GetBackBufferRTV(swapChain->GetCurrentBackBufferIndex());
    ctx->SetRenderTargets(&backBufferRTV, 1, nullptr);
    ctx->SetViewports(&vp, 1);
    ctx->SetScissorRects(&sr, 1);

    device->ImGuiNewFrame();
    ImGui::NewFrame();

    DrawEditorUI();

    ImGui::Render();
    device->ImGuiRenderDrawData(ctx);

    // Multi-viewport: render windows that have been dragged outside the main window
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    m_PassTimer.End();
    ctx->EndEvent();

    m_PassTimer.EndFrame();

    // ---- Hot-reload shaders after all rendering is done ----
    if (m_PendingShaderReload)
    {
        m_PendingShaderReload = false;
        ReloadModifiedShaders();
    }

    // ---- End frame (DX12: BackBuffer->Present barrier; DX11: no-op) ----
    ctx->EndFrame(swapChain);

    ctx->Flush();
}

void KiwiEngineApp::UpdateWindowTitle()
{
    std::string title = "Kiwi Engine - " + m_Scene.GetName();
    if (title != m_LastWindowTitle)
    {
        m_LastWindowTitle = title;
        GetWindow()->SetTitle(title);
    }
}

void KiwiEngineApp::UpdateCameraFromScene()
{
    auto* cam = m_Scene.GetActiveCamera();
    if (cam)
    {
        cam->UpdateViewMatrix();
        float aspect = (float)GetWindow()->GetWidth() / (float)GetWindow()->GetHeight();
        cam->UpdateProjectionMatrix(aspect);

        m_ViewMatrix = cam->ViewMatrix;
        m_ProjectionMatrix = cam->ProjectionMatrix;
        m_CameraPosition = cam->Position;
    }
}

void KiwiEngineApp::UpdateCameraProjection()
{
    auto* cam = m_Scene.GetActiveCamera();
    if (cam)
    {
        float aspect = (float)GetWindow()->GetWidth() / (float)GetWindow()->GetHeight();
        cam->UpdateProjectionMatrix(aspect);
        m_ProjectionMatrix = cam->ProjectionMatrix;
    }
}

// Collect all active light components from the scene into the GPU cache
void KiwiEngineApp::CollectLightsFromScene()
{
    m_NumActiveLights = 0;
    memset(m_LightDataCache, 0, sizeof(m_LightDataCache));

    for (auto& objPtr : m_Scene.GetObjects())
    {
        if (m_NumActiveLights >= MAX_LIGHTS) break;

        auto& obj = *objPtr;

        // Check all light components on this object
        auto lights = obj.GetComponents<LightComponent>();
        for (auto* light : lights)
        {
            if (!light || !light->Enabled || !light->AffectWorld) continue;
            if (m_NumActiveLights >= MAX_LIGHTS) break;

            auto& gpuLight = m_LightDataCache[m_NumActiveLights];

            // Color * Intensity
            gpuLight.ColorIntensity[0] = light->LightColor.x * light->Intensity;
            gpuLight.ColorIntensity[1] = light->LightColor.y * light->Intensity;
            gpuLight.ColorIntensity[2] = light->LightColor.z * light->Intensity;

            if (light->GetLightType() == ELightType::Directional)
            {
                gpuLight.Type = 0; // Directional
                Vec3 fwd = light->GetForward();
                gpuLight.DirectionOrPos[0] = fwd.x;
                gpuLight.DirectionOrPos[1] = fwd.y;
                gpuLight.DirectionOrPos[2] = fwd.z;
                gpuLight.Radius = 0.0f;
            }
            else // Point
            {
                gpuLight.Type = 1; // Point
                gpuLight.DirectionOrPos[0] = light->Position.x;
                gpuLight.DirectionOrPos[1] = light->Position.y;
                gpuLight.DirectionOrPos[2] = light->Position.z;
                auto* pointLight = dynamic_cast<PointLightComponent*>(light);
                gpuLight.Radius = pointLight ? pointLight->Radius : 10.0f;
            }

            m_NumActiveLights++;
        }
    }

    // Deferred lighting draws directional and point lights as two instanced ranges of g_Lights.
    auto* lightsEnd = std::stable_partition(m_LightDataCache, m_LightDataCache + m_NumActiveLights,
        [](const GPULightData& light) { return light.Type == 0; });
    m_NumDirectionalLights = (int)(lightsEnd - m_LightDataCache);
}

void KiwiEngineApp::InitView()
{
    m_RenderList.clear();

    auto& objects = m_Scene.GetObjects();
    if (objects.empty()) return;

    // Build frustum from current view-projection
    Mat4 vp = m_ViewMatrix * m_ProjectionMatrix;
    Frustum frustum;
    frustum.ExtractFromViewProjection(vp);

    // Get camera position for distance calculation
    Vec3 camPos = m_CameraPosition;

    // Frustum cull and collect visible mesh components
    for (size_t i = 0; i < objects.size(); i++)
    {
        auto& obj = *objects[i];
        auto* meshComp = obj.GetComponent<MeshComponent>();
        if (!meshComp || !meshComp->Enabled) continue;

        // Compute world AABB
        AABB worldAABB;
        ComputeWorldAABB(*meshComp, worldAABB.Min, worldAABB.Max);

        // Frustum test
        if (!frustum.TestAABB(worldAABB))
            continue; // Object is completely outside frustum — skip

        // Compute distance from object center to camera
        Vec3 center = worldAABB.GetCenter();
        Vec3 diff = center - camPos;
        float distSq = diff.Dot(diff); // Squared distance (avoid sqrt for perf)

        RenderItem item;
        item.ObjectIndex = i;
        item.MeshComp = meshComp;
        item.SortOrder = meshComp->SortOrder;
        item.DistToCamera = distSq;
        // DC merge keys: group by mesh type + material
        item.MeshID = (uint32_t)meshComp->PrimitiveType;
        item.MaterialName = meshComp->Material.Parent.c_str();
        item.BatchKey = MakeMeshBatchKey(item, m_MaterialLibrary);
        m_RenderList.push_back(item);
    }

    // Same key as mesh batching. Distance only orders draws inside one batch.
    std::sort(m_RenderList.begin(), m_RenderList.end(),
        [](const RenderItem& a, const RenderItem& b)
        {
            int keyCmp = a.BatchKey.Compare(b.BatchKey);
            if (keyCmp != 0)
                return keyCmp < 0;
            return a.DistToCamera < b.DistToCamera;
        });
}

void KiwiEngineApp::RebuildAllGPUBuffers()
{
    auto device = GetDevice();
    auto& objects = m_Scene.GetObjects();

    // ---- Shared Mesh Pool: same EPrimitiveType shares one VB/IB ----
    m_SharedMeshPool.clear();
    m_GPUMeshes.resize(objects.size());

    // Map: PrimitiveType -> index in m_SharedMeshPool
    std::unordered_map<int, size_t> meshTypeToPoolIndex;

    for (size_t i = 0; i < objects.size(); i++)
    {
        auto& obj = *objects[i];
        auto& gpu = m_GPUMeshes[i];

        auto* meshComp = obj.GetComponent<MeshComponent>();
        if (!meshComp)
        {
            gpu.VertexBuffer.reset();
            gpu.IndexBuffer.reset();
            gpu.VertexCount = 0;
            gpu.IndexCount = 0;
            continue;
        }

        int primType = (int)meshComp->PrimitiveType;

        // Check if we already have a shared VB/IB for this primitive type
        auto it = meshTypeToPoolIndex.find(primType);
        if (it != meshTypeToPoolIndex.end())
        {
            // Reuse existing shared mesh — point to same VB/IB
            auto& shared = m_SharedMeshPool[it->second];
            gpu.VertexBuffer.reset();  // No owned buffer — use shared
            gpu.IndexBuffer.reset();
            gpu.VertexCount = shared.VertexCount;
            gpu.IndexCount  = shared.IndexCount;
        }
        else
        {
            // First instance of this primitive type — create VB/IB and share
            gpu.VertexCount = meshComp->MeshData.GetVertexCount();
            gpu.IndexCount = meshComp->MeshData.GetIndexCount();

            if (gpu.VertexCount == 0 || gpu.IndexCount == 0) continue;

            std::string typeName = std::to_string(primType);
            std::string vbName = "SharedVB_Type" + typeName;
            std::string ibName = "SharedIB_Type" + typeName;

            BufferDesc vbDesc;
            vbDesc.SizeInBytes = gpu.VertexCount * sizeof(Vertex);
            vbDesc.BindFlags = BUFFER_USAGE_VERTEX;
            vbDesc.Usage = EResourceUsage::Immutable;
            vbDesc.DebugName = vbName.c_str();
            gpu.VertexBuffer = device->CreateBuffer(vbDesc, meshComp->MeshData.GetVertices().data());

            BufferDesc ibDesc;
            ibDesc.SizeInBytes = gpu.IndexCount * sizeof(uint32_t);
            ibDesc.BindFlags = BUFFER_USAGE_INDEX;
            ibDesc.Usage = EResourceUsage::Immutable;
            ibDesc.DebugName = ibName.c_str();
            gpu.IndexBuffer = device->CreateBuffer(ibDesc, meshComp->MeshData.GetIndices().data());

            // Register in shared pool
            SharedMeshEntry entry;
            entry.VertexBuffer = gpu.VertexBuffer.get();
            entry.IndexBuffer  = gpu.IndexBuffer.get();
            entry.VertexCount  = gpu.VertexCount;
            entry.IndexCount   = gpu.IndexCount;
            entry.MeshID       = primType;

            meshTypeToPoolIndex[primType] = m_SharedMeshPool.size();
            m_SharedMeshPool.push_back(entry);
        }
    }

    std::cout << "[Kiwi] Shared Mesh Pool: " << m_SharedMeshPool.size()
              << " unique meshes for " << objects.size() << " objects" << std::endl;
}

// Get shared mesh entry for a given object (returns non-owning pointers)
SharedMeshEntry KiwiEngineApp::GetSharedMesh(size_t objectIndex) const
{
    if (objectIndex >= m_GPUMeshes.size()) return {};
    auto& gpu = m_GPUMeshes[objectIndex];

    // If this GPUMeshData owns its own VB/IB, return it directly
    if (gpu.VertexBuffer)
    {
        SharedMeshEntry e;
        e.VertexBuffer = gpu.VertexBuffer.get();
        e.IndexBuffer  = gpu.IndexBuffer.get();
        e.VertexCount  = gpu.VertexCount;
        e.IndexCount   = gpu.IndexCount;
        return e;
    }

    // Otherwise, find shared entry by primitive type
    auto& objects = m_Scene.GetObjects();
    if (objectIndex >= objects.size()) return {};
    auto* meshComp = objects[objectIndex]->GetComponent<MeshComponent>();
    if (!meshComp) return {};

    int primType = (int)meshComp->PrimitiveType;
    for (auto& entry : m_SharedMeshPool)
    {
        if (entry.MeshID == (uint32_t)primType)
            return entry;
    }
    return {};
}

void KiwiEngineApp::PickObject(int mouseX, int mouseY)
{
    uint32_t w = GetWindow()->GetWidth();
    uint32_t h = GetWindow()->GetHeight();

    Ray ray = ScreenToRay(mouseX, mouseY, w, h, m_ViewMatrix, m_ProjectionMatrix);

    float closestT = 1e30f;
    int32_t closestID = -1;

    for (auto& objPtr : m_Scene.GetObjects())
    {
        auto& obj = *objPtr;
        auto* meshComp = obj.GetComponent<MeshComponent>();
        if (!meshComp) continue;

        Vec3 aabbMin, aabbMax;
        ComputeWorldAABB(*meshComp, aabbMin, aabbMax);

        float t;
        if (RayIntersectsAABB(ray, aabbMin, aabbMax, t))
        {
            if (t < closestT)
            {
                closestT = t;
                closestID = (int32_t)obj.ID;
            }
        }
    }

    if (closestID >= 0)
        m_Scene.SelectObject((uint32_t)closestID);
    else
        m_Scene.DeselectAll();
}
