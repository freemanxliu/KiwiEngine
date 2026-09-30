#include "KiwiEngineApp.h"

#include "Core/EngineConfig.h"
#include "Core/Platform.h"
#include "RHI/ImGuiRHI.h"
#include "Math/RayMath.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <iostream>

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
    std::string defaultScene = ScenesDir + "/Default.json";
    if (std::filesystem::exists(defaultScene))
    {
        Scene.LoadFromFile(defaultScene);
        std::cout << "[Kiwi] Loaded default scene: " << defaultScene << std::endl;
    }
    else
    {
        CreateDefaultScene(defaultScene);
    }

    // ---- Update camera matrices ----
    UpdateCameraFromScene();

    GetWindow()->SetResizeCallback([this](uint32_t width, uint32_t height) {
        UpdateCameraProjection();
    });

    Gizmo.CreateGPUResources(GetDevice());
    TextureManager.Initialize(GetDevice());
    MaterialLibrary.Initialize(MaterialsDir);
    EditorInput.Init(GetWindow(), &Scene);

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

    ShaderDir = resolveDir("Shaders", false);
    PostProcessShaderDir = resolveDir("PostProcessShaders", false);
    if (GetCurrentRHIType() == RHI_API_TYPE::METAL)
    {
        std::string metalPost = resolveDir("MetalPostProcess", false);
        if (std::filesystem::exists(metalPost))
            PostProcessShaderDir = metalPost;
    }
    std::cout << "[Kiwi] Shader directory: " << ShaderDir << std::endl;
    std::cout << "[Kiwi] PostProcess shader directory: " << PostProcessShaderDir << std::endl;

    ScenesDir = resolveDir("Scenes", true);
    std::cout << "[Kiwi] Scenes directory: " << ScenesDir << std::endl;
    TexturesDir = resolveDir("Textures", true);
    GLShaderDir = resolveDir("GLShaders", false);
    MaterialsDir = resolveDir("Materials", true);
}

// First run: build a GPU Scene debug scene (multiple primitive types, materials, lights
// and transforms, to exercise GPU Scene offset binding) and save it to savePath.
void KiwiEngineApp::CreateDefaultScene(const std::string& savePath)
{
    Scene.SetName("GPU Scene Debug");

    // ---- Camera ----
    auto* camObj = Scene.AddCameraObject("Main Camera");
    auto* cam = camObj->GetComponent<CameraComponent>();
    cam->Position = Vec3(0.0f, 5.0f, -12.0f);
    cam->Rotation = Vec3(25.0f, 0.0f, 0.0f);
    cam->FieldOfView = 45.0f;

    // ---- Directional Light (Sun) ----
    auto* lightObj = Scene.AddDirectionalLightObject("Sun Light");
    auto* sunLight = lightObj->GetComponent<DirectionalLightComponent>();
    if (sunLight)
    {
        sunLight->Rotation = { 50.0f, -30.0f, 0.0f };
        sunLight->LightColor = { 1.0f, 0.95f, 0.85f };
        sunLight->Intensity = 3.0f;
    }

    // ---- Point Light (warm fill) ----
    auto* pointLightObj = Scene.AddPointLightObject("Point Light Warm");
    auto* pointLight = pointLightObj->GetComponent<PointLightComponent>();
    if (pointLight)
    {
        pointLight->Position = { -3.0f, 3.0f, -2.0f };
        pointLight->LightColor = { 1.0f, 0.7f, 0.3f };
        pointLight->Intensity = 5.0f;
        pointLight->Radius = 12.0f;
    }

    // ---- Point Light (cool fill) ----
    auto* pointLightObj2 = Scene.AddPointLightObject("Point Light Cool");
    auto* pointLight2 = pointLightObj2->GetComponent<PointLightComponent>();
    if (pointLight2)
    {
        pointLight2->Position = { 4.0f, 2.5f, 1.0f };
        pointLight2->LightColor = { 0.3f, 0.5f, 1.0f };
        pointLight2->Intensity = 4.0f;
        pointLight2->Radius = 10.0f;
    }

    // ---- Ground (large floor) ----
    auto* floor = Scene.AddMeshObject(EPrimitiveType::Floor, "Ground");
    auto* floorMesh = floor->GetComponent<PrimitiveComponent>();
    if (floorMesh) floorMesh->Scale = { 3.0f, 1.0f, 3.0f };

    // ---- Row of cubes (different positions — tests GPU Scene offset correctness) ----
    const float cubeSpacing = 2.5f;
    for (int i = 0; i < 5; ++i)
    {
        float x = (i - 2) * cubeSpacing;
        std::string name = "Cube_" + std::to_string(i + 1);
        auto* cubeObj = Scene.AddMeshObject(EPrimitiveType::Cube, name);
        auto* mesh = cubeObj->GetComponent<PrimitiveComponent>();
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
        auto* sphereObj = Scene.AddMeshObject(EPrimitiveType::Sphere, name);
        auto* mesh = sphereObj->GetComponent<PrimitiveComponent>();
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
        auto* cylObj = Scene.AddMeshObject(EPrimitiveType::Cylinder, name);
        auto* mesh = cylObj->GetComponent<PrimitiveComponent>();
        if (mesh)
        {
            mesh->Position = { x, 1.5f, 0.0f };
            mesh->Scale = { 0.4f, 1.5f, 0.4f };
        }
    }

    // ---- Rotated cube (tests rotation in GPU Scene) ----
    auto* rotCubeObj = Scene.AddMeshObject(EPrimitiveType::Cube, "Rotated_Cube");
    auto* rotMesh = rotCubeObj->GetComponent<PrimitiveComponent>();
    if (rotMesh)
    {
        rotMesh->Position = { 0.0f, 1.5f, -4.0f };
        rotMesh->Rotation = { 30.0f, 45.0f, 15.0f };
        rotMesh->Scale = { 1.2f, 1.2f, 1.2f };
    }

    Scene.SaveToFile(savePath);
    std::cout << "[Kiwi] Created GPU Scene debug scene ("
              << Scene.GetObjects().size() << " objects)" << std::endl;
}

void KiwiEngineApp::OnUpdate(float deltaTime)
{
    TotalTime += deltaTime;

    // Update window title with scene name (only when changed)
    UpdateWindowTitle();
    // Camera fly navigation: hold right mouse button + WASD / arrow keys
    EditorInput.Update(deltaTime);

    // Update camera matrices each frame
    UpdateCameraFromScene();

    if (!ImGui::GetIO().WantCaptureMouse)
        HandleViewportMouse();
}

GizmoViewInfo KiwiEngineApp::MakeGizmoViewInfo() const
{
    GizmoViewInfo view;
    view.View = ViewMatrix;
    view.Projection = ProjectionMatrix;
    view.CameraPosition = CameraPosition;
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
        SceneObject* sel = Scene.GetSelectedObject();
        if (!sel || !Gizmo.TryBeginDrag(*sel, mouse.X, mouse.Y, view))
            PickObject(mouse.X, mouse.Y);
    }

    if (Gizmo.IsDragging())
    {
        SceneObject* sel = Scene.GetSelectedObject();
        if (!mouse.LeftDown)
            Gizmo.EndDrag();
        else if (sel)
            Gizmo.UpdateDrag(*sel, mouse.X, mouse.Y, view);
    }
}

// Game thread: finish the editor frame, snapshot everything the renderer needs and hand the frame over.
void KiwiEngineApp::OnRender()
{
    SanitizeRenderPath();

    // Shader reload rebuilds pipelines, material shader maps and the post-process library, which both threads read.
    if (PendingShaderReload)
    {
        PendingShaderReload = false;
        FlushRenderingCommands();
        ReloadModifiedShaders();
    }

    // ---- ImGui: built here, drawn from a copy on the RHI thread ----
    RHIDevice* device = GetDevice();
    device->ImGuiNewFrame();
    ImGui::NewFrame();
    DrawEditorUI();
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    device->ImGuiUpdateTextures(drawData);

    // Multi-viewport: windows dragged outside the main window are rendered right away by the platform backend.
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        std::lock_guard<std::recursive_mutex> Guard(GetRHILock());
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    // UI edits above are part of this frame.
    RenderScene.SendAllEndOfFrameUpdates(MaterialLibrary);

    auto params = std::make_shared<FrameRenderParams>();
    BuildFrameRenderParams(*params);
    params->ImGuiDrawData = CloneImDrawData(drawData);
    if (ImDrawData* Snapshot = params->ImGuiDrawData.get(); Snapshot && Snapshot->DisplaySize.x > 0.0f && Snapshot->DisplaySize.y > 0.0f)
    {
        // ImGui reads the live window size, which can run ahead of the swap chain until the next flush + resize; its framebuffer must not exceed the back buffer.
        Snapshot->FramebufferScale.x = std::min(Snapshot->FramebufferScale.x, (float)params->ViewWidth / Snapshot->DisplaySize.x);
        Snapshot->FramebufferScale.y = std::min(Snapshot->FramebufferScale.y, (float)params->ViewHeight / Snapshot->DisplaySize.y);
    }
    EnqueueRenderCommand([this, params] { RenderFrame_RenderThread(*params); });
}

void KiwiEngineApp::BuildFrameRenderParams(FrameRenderParams& Out)
{
    Out.RenderPath = RenderPath;
    Out.ViewMode = ViewMode;
    Out.ViewMatrix = ViewMatrix;
    Out.ProjectionMatrix = ProjectionMatrix;
    Out.CameraPosition = CameraPosition;
    if (const CameraComponent* Cam = Scene.GetActiveCamera())
    {
        Out.bHasCamera = true;
        Out.FieldOfView = Cam->FieldOfView;
        Out.NearPlane = Cam->NearPlane;
        Out.FarPlane = Cam->FarPlane;
    }
    Out.ViewWidth = GetSwapChainWidth();
    Out.ViewHeight = GetSwapChainHeight();
    Out.TotalTime = TotalTime;
    CollectActivePostProcessEffects(Out.PostProcessEffects);
    Out.Gizmo = Gizmo.MakeDrawState(Scene.GetSelectedObject());
    Out.RayTracingSamplesPerPixel = RayTracingSamplesPerPixel;
    Out.RayTracingResolutionPercent = RayTracingResolutionPercent;
}

RenderStats KiwiEngineApp::GetRenderStats() const
{
    std::lock_guard<std::mutex> Lock(RenderStatsMutex);
    return PublishedRenderStats;
}

RHITextureView* KiwiEngineApp::GetBackBufferRTV()
{
    return GetContext()->GetBackBufferRTV(GetSwapChain());
}

void KiwiEngineApp::RenderFrame_RenderThread(FrameRenderParams& Params)
{
    RenderParams = std::move(Params);
    Kiwi::RenderingThread& renderingThread = GetRenderingThread();
    RHICommandList* ctx = GetContext();
    auto swapChain = GetSwapChain();
    auto device = GetDevice();

    // ---- Size-dependent targets. Submitted frames may still use the old ones. ----
    uint32_t winW = RenderParams.ViewWidth;
    uint32_t winH = RenderParams.ViewHeight;
    if (OffscreenWidth != winW || OffscreenHeight != winH || GBufferWidth != winW || GBufferHeight != winH)
    {
        renderingThread.WaitForRHIThread();
        if (OffscreenWidth != winW || OffscreenHeight != winH)
            CreateOffscreenRenderTargets(device, winW, winH);
        if (GBufferWidth != winW || GBufferHeight != winH)
            CreateGBufferResources(device, winW, winH);
    }
    // Always use offscreen RT for HDR pipeline (Tonemap is always-on)
    bool hasPostProcess = (OffscreenRT[0] != nullptr);

    PrepareSceneRenderer(ResolveRenderPath());
    ViewInfo& view = GetViewInfo();

    // ---- Begin frame (DX12: Reset + RootSig + DescriptorHeaps + Barrier; DX11: no-op) ----
    ctx->BeginFrame(swapChain);

    // Apply queued primitive and light changes and upload GPU Scene once per frame, independent of the render path.
    RenderScene.Update();

    view.UpdateViewUniformBuffer(RenderScene.GetLightData(), RenderScene.GetNumLights(), RenderScene.GetNumDirectionalLights());

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

    PassTimer.BeginFrame();

    // Determine the final scene render target (before post-process)
    // If post-process active, render to offscreen RT[0]; else to backbuffer
    RHITextureView* sceneRTV = nullptr;
    if (hasPostProcess)
    {
        sceneRTV = OffscreenRTV[0].get();
        ctx->ResourceBarrier(OffscreenRT[0].get(),
            RESOURCE_STATE_COMMON, RESOURCE_STATE_RENDER_TARGET);
    }
    else
    {
        sceneRTV = GetBackBufferRTV();
    }

    SceneRenderer->Render(ctx, sceneRTV, vp, sr);

    // ---- Post-Process Pass (always runs — HDR Tonemap is built-in) ----
    if (hasPostProcess)
    {
        ctx->BeginEvent("Post-Process Pass");
        PassTimer.Begin("Post-Process Pass");
        ExecutePostProcessPasses(ctx, RenderParams.PostProcessEffects);
        PassTimer.End();
        ctx->EndEvent();
    }

    // ---- ImGui ----
    ctx->BeginEvent("ImGui Pass");
    PassTimer.Begin("ImGui Pass");
    // ImGui always renders to the backbuffer
    auto backBufferRTV = GetBackBufferRTV();
    ctx->SetRenderTargets(&backBufferRTV, 1, nullptr);
    ctx->SetViewports(&vp, 1);
    ctx->SetScissorRects(&sr, 1);
    ctx->RenderImGui(std::move(RenderParams.ImGuiDrawData));
    PassTimer.End();
    ctx->EndEvent();

    PassTimer.EndFrame();

    // ---- End frame (DX12: BackBuffer->Present barrier; DX11: no-op) ----
    ctx->EndFrame(swapChain);
    ctx->Flush();
    ctx->Present(swapChain, 1); // VSync ON

    PublishRenderStats();
    renderingThread.SubmitCommandList(swapChain);
}

void KiwiEngineApp::PublishRenderStats()
{
    std::lock_guard<std::mutex> Lock(RenderStatsMutex);
    PublishedRenderStats.PassTimings = PassTimer.GetEntries();
    PublishedRenderStats.FrameTotalMs = PassTimer.GetFrameTotalMs();
    PublishedRenderStats.VisibleItems = SceneRenderer ? (uint32_t)GetViewInfo().VisibleItems.size() : 0;
    PublishedRenderStats.RayTraceWidth = RayTraceWidth;
    PublishedRenderStats.RayTraceHeight = RayTraceHeight;
}

void KiwiEngineApp::UpdateWindowTitle()
{
    std::string title = "Kiwi Engine - " + Scene.GetName();
    if (title != LastWindowTitle)
    {
        LastWindowTitle = title;
        GetWindow()->SetTitle(title);
    }
}

void KiwiEngineApp::UpdateCameraFromScene()
{
    auto* cam = Scene.GetActiveCamera();
    if (cam)
    {
        cam->UpdateViewMatrix();
        float aspect = (float)GetWindow()->GetWidth() / (float)GetWindow()->GetHeight();
        cam->UpdateProjectionMatrix(aspect);

        ViewMatrix = cam->ViewMatrix;
        ProjectionMatrix = cam->ProjectionMatrix;
        CameraPosition = cam->Position;
    }
}

void KiwiEngineApp::UpdateCameraProjection()
{
    auto* cam = Scene.GetActiveCamera();
    if (cam)
    {
        float aspect = (float)GetWindow()->GetWidth() / (float)GetWindow()->GetHeight();
        cam->UpdateProjectionMatrix(aspect);
        ProjectionMatrix = cam->ProjectionMatrix;
    }
}

void KiwiEngineApp::PickObject(int mouseX, int mouseY)
{
    uint32_t w = GetWindow()->GetWidth();
    uint32_t h = GetWindow()->GetHeight();

    Ray ray = ScreenToRay(mouseX, mouseY, w, h, ViewMatrix, ProjectionMatrix);

    float closestT = 1e30f;
    int32_t closestID = -1;

    for (auto& objPtr : Scene.GetObjects())
    {
        auto& obj = *objPtr;
        auto* meshComp = obj.GetComponent<PrimitiveComponent>();
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
        Scene.SelectObject((uint32_t)closestID);
    else
        Scene.DeselectAll();
}
