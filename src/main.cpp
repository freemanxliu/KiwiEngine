#include "Core/Application.h"
#include "Core/Window.h"
#include "Core/EngineConfig.h"
#include "Scene/Mesh.h"
#include "Scene/Shaders.h"
#include "Scene/ShaderLibrary.h"
#include "Scene/Scene.h"
#include "Scene/SceneObject.h"
#include "Scene/MeshComponent.h"
#include "Scene/CameraComponent.h"
#include "Scene/LightComponent.h"
#include "Scene/PostProcessComponent.h"
#include "Scene/PostProcessShaders.h"
#include "Scene/PostProcessShaderLibrary.h"
#include "Scene/ViewMode.h"
#include "Math/Math.h"
#include "RHI/RHI.h"
#include "Debug/RenderDocIntegration.h"

#include <imgui.h>

#include <iostream>
#include <memory>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <string>
#include <fstream>
#include <sstream>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

using namespace Kiwi;

// ============================================================
// Pass Timer — High-resolution CPU timing for render passes
// ============================================================

struct PassTimingEntry
{
    std::string Name;
    double      TimeMs = 0.0;   // Last measured time in milliseconds
};

class PassTimer
{
public:
    PassTimer()
    {
        QueryPerformanceFrequency(&m_Frequency);
    }

    void Begin(const std::string& name)
    {
        m_CurrentName = name;
        QueryPerformanceCounter(&m_StartTime);
    }

    void End()
    {
        LARGE_INTEGER endTime;
        QueryPerformanceCounter(&endTime);
        double elapsed = (double)(endTime.QuadPart - m_StartTime.QuadPart) /
                         (double)m_Frequency.QuadPart * 1000.0;

        // Update or insert entry
        bool found = false;
        for (auto& entry : m_Entries)
        {
            if (entry.Name == m_CurrentName)
            {
                // Smooth with exponential moving average (alpha=0.1)
                entry.TimeMs = entry.TimeMs * 0.9 + elapsed * 0.1;
                found = true;
                break;
            }
        }
        if (!found)
        {
            m_Entries.push_back({ m_CurrentName, elapsed });
        }

        m_TotalMs = m_TotalMs * 0.9 + elapsed * 0.1;
    }

    void FrameReset()
    {
        m_TotalMs = 0.0;
        // Don't clear entries — keep smoothed values
    }

    void BeginFrame()
    {
        // Reset per-frame total, but keep smoothed per-pass data
        m_FrameTotalMs = 0.0;
    }

    void EndFrame()
    {
        m_FrameTotalMs = 0.0;
        for (auto& e : m_Entries)
            m_FrameTotalMs += e.TimeMs;
    }

    const std::vector<PassTimingEntry>& GetEntries() const { return m_Entries; }
    double GetFrameTotalMs() const { return m_FrameTotalMs; }

private:
    LARGE_INTEGER m_Frequency = {};
    LARGE_INTEGER m_StartTime = {};
    std::string   m_CurrentName;
    std::vector<PassTimingEntry> m_Entries;
    double m_TotalMs = 0.0;
    double m_FrameTotalMs = 0.0;
};

// ============================================================
// GPU Mesh Data — holds buffers for a single mesh
// ============================================================

struct GPUMeshData
{
    std::unique_ptr<RHIBuffer> VertexBuffer;
    std::unique_ptr<RHIBuffer> IndexBuffer;
    uint32_t VertexCount = 0;
    uint32_t IndexCount = 0;
};

// ============================================================
// Ray Picking
// ============================================================

struct Ray
{
    Vec3 Origin;
    Vec3 Direction;
};

static Ray ScreenToRay(int mouseX, int mouseY, uint32_t screenW, uint32_t screenH,
                       const Mat4& view, const Mat4& proj)
{
    float ndcX = (2.0f * mouseX / screenW) - 1.0f;
    float ndcY = 1.0f - (2.0f * mouseY / screenH);

    float viewX = ndcX / proj.m[0][0];
    float viewY = ndcY / proj.m[1][1];

    Vec3 rayDirView = { viewX, viewY, 1.0f };

    Vec3 right  = { view.m[0][0], view.m[1][0], view.m[2][0] };
    Vec3 up     = { view.m[0][1], view.m[1][1], view.m[2][1] };
    Vec3 fwd    = { view.m[0][2], view.m[1][2], view.m[2][2] };

    Vec3 eye;
    eye.x = -(view.m[3][0] * right.x + view.m[3][1] * up.x + view.m[3][2] * fwd.x);
    eye.y = -(view.m[3][0] * right.y + view.m[3][1] * up.y + view.m[3][2] * fwd.y);
    eye.z = -(view.m[3][0] * right.z + view.m[3][1] * up.z + view.m[3][2] * fwd.z);

    Vec3 rayDirWorld;
    rayDirWorld.x = rayDirView.x * right.x + rayDirView.y * up.x + rayDirView.z * fwd.x;
    rayDirWorld.y = rayDirView.x * right.y + rayDirView.y * up.y + rayDirView.z * fwd.y;
    rayDirWorld.z = rayDirView.x * right.z + rayDirView.y * up.z + rayDirView.z * fwd.z;

    return { eye, rayDirWorld.Normalize() };
}

static bool RayIntersectsAABB(const Ray& ray, const Vec3& aabbMin, const Vec3& aabbMax, float& tOut)
{
    float tmin = -1e30f;
    float tmax = 1e30f;

    float invDir[3] = {
        (std::abs(ray.Direction.x) > 1e-6f) ? 1.0f / ray.Direction.x : 1e30f,
        (std::abs(ray.Direction.y) > 1e-6f) ? 1.0f / ray.Direction.y : 1e30f,
        (std::abs(ray.Direction.z) > 1e-6f) ? 1.0f / ray.Direction.z : 1e30f
    };

    float origin[3] = { ray.Origin.x, ray.Origin.y, ray.Origin.z };
    float bmin[3] = { aabbMin.x, aabbMin.y, aabbMin.z };
    float bmax[3] = { aabbMax.x, aabbMax.y, aabbMax.z };

    for (int i = 0; i < 3; i++)
    {
        float t1 = (bmin[i] - origin[i]) * invDir[i];
        float t2 = (bmax[i] - origin[i]) * invDir[i];
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }

    if (tmax < 0.0f) return false;
    tOut = (tmin >= 0.0f) ? tmin : tmax;
    return true;
}

static void ComputeWorldAABB(const MeshComponent& mesh, Vec3& outMin, Vec3& outMax)
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

// ============================================================
// Gizmo — Translation Arrows (3 axis cylinders + cones)
// ============================================================

enum class EGizmoAxis { None = 0, X, Y, Z };

struct GizmoMeshData
{
    std::vector<Vertex> Vertices;
    std::vector<uint32_t> Indices;
};

static GizmoMeshData CreateGizmoArrow(const Vec3& axisDir, const Vec4& color, float length = 1.2f, float shaftRadius = 0.02f, float headRadius = 0.06f, float headLength = 0.2f)
{
    GizmoMeshData gizmo;
    const uint32_t segments = 12;
    float shaftLength = length - headLength;

    // Build an orthonormal basis where Z = axisDir
    Vec3 up = axisDir;
    Vec3 right;
    if (std::abs(up.y) < 0.99f)
        right = Vec3(0, 1, 0).Cross(up).Normalize();
    else
        right = Vec3(1, 0, 0).Cross(up).Normalize();
    Vec3 forward = up.Cross(right).Normalize();

    auto addVert = [&](Vec3 pos, Vec3 norm)
    {
        gizmo.Vertices.push_back({ pos, norm, { 1.0f, 1.0f, 1.0f, 1.0f } });
    };

    // --- Shaft (cylinder along axis) ---
    for (uint32_t i = 0; i <= segments; i++)
    {
        float theta = (float)i / segments * 2.0f * PI;
        float ct = cosf(theta), st = sinf(theta);
        Vec3 circleDir = right * ct + forward * st;
        Vec3 normal = circleDir;
        Vec3 bottom = circleDir * shaftRadius;
        Vec3 top = circleDir * shaftRadius + up * shaftLength;
        addVert(bottom, normal);
        addVert(top, normal);
    }
    for (uint32_t i = 0; i < segments; i++)
    {
        uint32_t b = i * 2;
        gizmo.Indices.push_back(b);
        gizmo.Indices.push_back(b + 2);
        gizmo.Indices.push_back(b + 1);
        gizmo.Indices.push_back(b + 1);
        gizmo.Indices.push_back(b + 2);
        gizmo.Indices.push_back(b + 3);
    }

    // --- Cone (arrow head) ---
    uint32_t coneBase = (uint32_t)gizmo.Vertices.size();
    // Tip vertex
    Vec3 tip = up * length;
    addVert(tip, up);

    // Base ring
    for (uint32_t i = 0; i <= segments; i++)
    {
        float theta = (float)i / segments * 2.0f * PI;
        float ct = cosf(theta), st = sinf(theta);
        Vec3 circleDir = right * ct + forward * st;
        Vec3 pos = circleDir * headRadius + up * shaftLength;
        Vec3 norm = (circleDir + up * (headRadius / headLength)).Normalize();
        addVert(pos, norm);
    }
    for (uint32_t i = 0; i < segments; i++)
    {
        gizmo.Indices.push_back(coneBase);          // tip
        gizmo.Indices.push_back(coneBase + 1 + i);
        gizmo.Indices.push_back(coneBase + 2 + i);
    }

    // Bottom cap of cone
    uint32_t capCenter = (uint32_t)gizmo.Vertices.size();
    addVert(up * shaftLength, up.Negate());
    for (uint32_t i = 0; i <= segments; i++)
    {
        float theta = (float)i / segments * 2.0f * PI;
        float ct = cosf(theta), st = sinf(theta);
        Vec3 circleDir = right * ct + forward * st;
        Vec3 pos = circleDir * headRadius + up * shaftLength;
        addVert(pos, up.Negate());
    }
    for (uint32_t i = 0; i < segments; i++)
    {
        gizmo.Indices.push_back(capCenter);
        gizmo.Indices.push_back(capCenter + 2 + i);
        gizmo.Indices.push_back(capCenter + 1 + i);
    }

    return gizmo;
}

// Ray-axis closest point for gizmo dragging
// Returns the parameter t along 'axis' direction starting from 'axisOrigin'
// such that (axisOrigin + axis * t) is closest to the ray.
static float RayAxisClosestParam(const Ray& ray, const Vec3& axisOrigin, const Vec3& axisDir)
{
    Vec3 w = ray.Origin - axisOrigin;
    float a = axisDir.Dot(axisDir);       // == 1 if normalized
    float b = axisDir.Dot(ray.Direction);
    float c = ray.Direction.Dot(ray.Direction); // == 1 if normalized
    float d = axisDir.Dot(w);
    float e = ray.Direction.Dot(w);

    float denom = a * c - b * b;
    if (std::abs(denom) < 1e-6f) return 0.0f;
    return (b * e - c * d) / denom;
}

// Check if ray is close enough to a gizmo axis to pick it
static bool RayPicksGizmoAxis(const Ray& ray, const Vec3& axisOrigin, const Vec3& axisDir,
                               float axisLength, float pickRadius, float& outT)
{
    float tAxis = RayAxisClosestParam(ray, axisOrigin, axisDir);
    if (tAxis < 0.0f || tAxis > axisLength) return false;

    // Find closest point on ray to the axis point
    Vec3 axisPoint = axisOrigin + axisDir * tAxis;

    // Project axisPoint onto ray to get tRay
    float tRay = (axisPoint - ray.Origin).Dot(ray.Direction);
    if (tRay < 0.0f) return false;

    Vec3 rayPoint = ray.Origin + ray.Direction * tRay;
    Vec3 diff = axisPoint - rayPoint;
    float dist = diff.Length();

    if (dist < pickRadius)
    {
        outT = tRay;
        return true;
    }
    return false;
}

// ============================================================
// KiwiEngineApp
// ============================================================

class KiwiEngineApp : public Application
{
public:
    KiwiEngineApp()
        : Application(
            WindowDesc{ "Kiwi Engine - Scene Editor", 1280, 720 },
            RHIInitParams{ RHI_API_TYPE::DX11, true })
    {
    }

    ~KiwiEngineApp()
    {
        ShutdownImGui();
        ImGui::DestroyContext();
    }

protected:
    void OnInit() override
    {
        std::cout << "[Kiwi] Initializing Scene Editor..." << std::endl;

        // ---- Determine Shaders directory ----
        // Try "Shaders" relative to exe, fallback to source directory
        {
            char exePath[MAX_PATH] = {};
            GetModuleFileNameA(nullptr, exePath, MAX_PATH);
            std::string exeDir(exePath);
            size_t lastSlash = exeDir.find_last_of("\\/");
            if (lastSlash != std::string::npos)
                exeDir = exeDir.substr(0, lastSlash);
            m_ShaderDir = exeDir + "\\Shaders";

            // PostProcessShaders directory (same discovery logic)
            m_PostProcessShaderDir = exeDir + "\\PostProcessShaders";

            // Fallback: try source directory relative path
            namespace fs = std::filesystem;
            if (!fs::exists(m_ShaderDir))
            {
                // Assume exe is in build/bin/ and source is ../../Shaders
                std::string fallback = exeDir + "\\..\\..\\Shaders";
                if (fs::exists(fallback))
                    m_ShaderDir = fallback;
            }
            if (!fs::exists(m_PostProcessShaderDir))
            {
                std::string fallback = exeDir + "\\..\\..\\PostProcessShaders";
                if (fs::exists(fallback))
                    m_PostProcessShaderDir = fallback;
            }
            std::cout << "[Kiwi] Shader directory: " << m_ShaderDir << std::endl;
            std::cout << "[Kiwi] PostProcess shader directory: " << m_PostProcessShaderDir << std::endl;
        }

        // ---- Init ImGui context (once) ----
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();

        // ---- Init RHI-specific resources ----
        InitRHIResources();

        // ---- Default scene ----
        m_Scene.SetName("Default Scene");

        // Add camera object
        auto* camObj = m_Scene.AddCameraObject("Main Camera");
        auto* cam = camObj->GetComponent<CameraComponent>();
        cam->Position = Vec3(0.0f, 3.0f, -6.0f);
        cam->Rotation = Vec3(20.0f, 0.0f, 0.0f); // Look slightly down
        cam->FieldOfView = 45.0f;

        // Add default directional light (sun-like)
        auto* lightObj = m_Scene.AddDirectionalLightObject("Sun Light");
        auto* sunLight = lightObj->GetComponent<DirectionalLightComponent>();
        if (sunLight)
        {
            sunLight->Rotation = { 50.0f, -30.0f, 0.0f };
            sunLight->LightColor = { 1.0f, 1.0f, 0.9f };
            sunLight->Intensity = 1.0f;
        }

        // Add scene objects
        auto* floor = m_Scene.AddMeshObject(EPrimitiveType::Floor, "Ground");
        (void)floor;
        auto* cube = m_Scene.AddMeshObject(EPrimitiveType::Cube, "Cube_1");
        auto* cubeMesh = cube->GetComponent<MeshComponent>();
        if (cubeMesh) cubeMesh->Position = { 0.0f, 0.5f, 0.0f };

        RebuildAllGPUBuffers();

        // ---- Update camera matrices ----
        UpdateCameraFromScene();

        GetWindow()->SetResizeCallback([this](uint32_t width, uint32_t height) {
            UpdateCameraProjection();
        });

        // ---- Init Gizmo ----
        InitGizmoMeshes();
        BuildGizmoGPUBuffers();

        std::cout << "[Kiwi] Scene Editor initialized!" << std::endl;
    }

    void OnUpdate(float deltaTime) override
    {
        m_TotalTime += deltaTime;

        // Update camera matrices each frame
        UpdateCameraFromScene();

        // Collect light data from scene each frame
        CollectLightsFromScene();

        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureMouse)
        {
            const auto& mouse = GetWindow()->GetMouseState();

            if (mouse.LeftClicked)
            {
                // Try to pick gizmo axis first
                if (m_Scene.GetSelectedObject())
                {
                    m_DragAxis = PickGizmoAxis(mouse.X, mouse.Y);
                    if (m_DragAxis != EGizmoAxis::None)
                    {
                        m_IsDragging = true;
                        m_DragStartMouseX = mouse.X;
                        m_DragStartMouseY = mouse.Y;
                        auto* sel = m_Scene.GetSelectedObject();
                        // Get position from primary component
                        m_DragStartPos = sel->GetPosition();
                    }
                    else
                    {
                        PickObject(mouse.X, mouse.Y);
                    }
                }
                else
                {
                    PickObject(mouse.X, mouse.Y);
                }
            }

            // Handle dragging
            if (m_IsDragging && mouse.LeftDown)
            {
                auto* sel = m_Scene.GetSelectedObject();
                if (sel && m_DragAxis != EGizmoAxis::None)
                {
                    uint32_t w = GetWindow()->GetWidth();
                    uint32_t h = GetWindow()->GetHeight();

                    Ray rayNow = ScreenToRay(mouse.X, mouse.Y, w, h, m_ViewMatrix, m_ProjectionMatrix);
                    Ray rayStart = ScreenToRay(m_DragStartMouseX, m_DragStartMouseY, w, h, m_ViewMatrix, m_ProjectionMatrix);

                    Vec3 axisDir;
                    switch (m_DragAxis)
                    {
                    case EGizmoAxis::X: axisDir = { 1, 0, 0 }; break;
                    case EGizmoAxis::Y: axisDir = { 0, 1, 0 }; break;
                    case EGizmoAxis::Z: axisDir = { 0, 0, 1 }; break;
                    default: break;
                    }

                    float tNow = RayAxisClosestParam(rayNow, m_DragStartPos, axisDir);
                    float tStart = RayAxisClosestParam(rayStart, m_DragStartPos, axisDir);
                    float delta = tNow - tStart;

                    sel->GetPosition() = m_DragStartPos + axisDir * delta;
                }
            }

            if (!mouse.LeftDown && m_IsDragging)
            {
                m_IsDragging = false;
                m_DragAxis = EGizmoAxis::None;
            }
        }
    }

    void OnRender() override
    {
        InitView();

        auto ctx = GetContext();
        auto swapChain = GetSwapChain();
        auto device = GetDevice();

        // ---- Begin frame (DX12: Reset + RootSig + DescriptorHeaps + Barrier; DX11: no-op) ----
        ctx->BeginFrame(swapChain);

        // ---- Check for active post-process effects ----
        bool hasPostProcess = false;
        std::vector<PostProcessMaterial*> activeEffects;
        CollectActivePostProcessEffects(activeEffects);
        hasPostProcess = !activeEffects.empty() && m_OffscreenRT[0] != nullptr;

        // ---- Ensure offscreen RT size matches window ----
        uint32_t winW = GetWindow()->GetWidth();
        uint32_t winH = GetWindow()->GetHeight();
        if (hasPostProcess && (m_OffscreenWidth != winW || m_OffscreenHeight != winH))
        {
            CreateOffscreenRenderTargets(device, winW, winH);
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

        // ---- Choose rendering path based on ViewMode ----
        bool useDeferredPipeline = (m_ViewMode == EViewMode::Lit ||
                                     m_ViewMode == EViewMode::BaseColor ||
                                     m_ViewMode == EViewMode::Roughness ||
                                     m_ViewMode == EViewMode::Metallic);

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

        if (useDeferredPipeline && m_GBufferPSO && m_GBufferRT[0])
        {
            // ================================================================
            // DEFERRED RENDERING PATH
            // ================================================================

            // ==== PASS 1: G-Buffer Geometry Pass ====
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

            // ==== PASS 2: Deferred Lighting / Buffer Visualization ====
            if (m_ViewMode == EViewMode::Lit)
            {
                // Full deferred lighting pass
                ctx->BeginEvent("Deferred Lighting Pass");
                m_PassTimer.Begin("Deferred Lighting Pass");

                ctx->SetRenderTargets(&sceneRTV, 1, nullptr);
                ctx->SetViewports(&vp, 1);
                ctx->SetScissorRects(&sr, 1);

                ClearColorValue clearColor = { 0.12f, 0.12f, 0.18f, 1.0f };
                ctx->ClearRenderTargetView(sceneRTV, clearColor);

                // Set deferred lighting pipeline
                ctx->SetPipelineState(m_DeferredLightingPSO.get());
                ctx->SetVertexShader(m_DeferredLightingVS.get());
                ctx->SetPixelShader(m_DeferredLightingPS.get());
                ctx->SetInputLayout(nullptr);
                ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);

                // Bind G-Buffer SRVs (t0=Position, t1=Normal, t2=Albedo)
                ctx->SetShaderResourceView(0, m_GBufferSRV[0].get());
                ctx->SetShaderResourceView(1, m_GBufferSRV[1].get());
                ctx->SetShaderResourceView(2, m_GBufferSRV[2].get());

                // Bind sampler (DX11 only; DX12 uses static sampler s0)
                ctx->SetSampler(0, m_PostProcessSampler.get());

                // Update CB with lighting data
                UpdateDeferredLightingCB();

                // Draw fullscreen triangle
                ctx->Draw(3, 0);

                // Unbind SRVs
                ctx->SetShaderResourceView(0, nullptr);
                ctx->SetShaderResourceView(1, nullptr);
                ctx->SetShaderResourceView(2, nullptr);

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

            // ==== PASS 3: Forward Gizmo Pass (on top of deferred result) ====
            ctx->BeginEvent("Gizmo Pass");
            m_PassTimer.Begin("Gizmo Pass");

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
        else
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

            // ---- Draw with Unlit shader ----
            ctx->BeginEvent("Forward Unlit Pass");
            m_PassTimer.Begin("Forward Unlit Pass");
            DrawSceneMeshesForward(ctx, "Unlit");
            m_PassTimer.End();
            ctx->EndEvent();

            // ---- Draw Gizmo ----
            ctx->BeginEvent("Gizmo Pass");
            m_PassTimer.Begin("Gizmo Pass");
            DrawGizmo(ctx);
            m_PassTimer.End();
            ctx->EndEvent();
        }

        // ---- Post-Process Pass ----
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

        DrawMenuBar();
        DrawRenderDocOverlay();
        DrawStatsOverlay();
        DrawUI();

        ImGui::Render();
        device->ImGuiRenderDrawData(ctx);
        m_PassTimer.End();
        ctx->EndEvent();

        m_PassTimer.EndFrame();

        // ---- End frame (DX12: BackBuffer->Present barrier; DX11: no-op) ----
        ctx->EndFrame(swapChain);

        ctx->Flush();
    }

    // ============================================================
    // Scene Mesh Drawing
    // ============================================================

    // Helper: Fill and upload per-object constant buffer
    void UploadObjectCB(RHICommandContext* ctx, MeshComponent* meshComp)
    {
        Mat4 worldMatrix = meshComp->GetWorldMatrix();

        ConstantBufferData cbd = {};
        memcpy(cbd.WorldMatrix, worldMatrix.m, sizeof(worldMatrix.m));
        memcpy(cbd.ViewMatrix, m_ViewMatrix.m, sizeof(m_ViewMatrix.m));
        memcpy(cbd.ProjectionMatrix, m_ProjectionMatrix.m, sizeof(m_ProjectionMatrix.m));
        cbd.ObjectColor[0] = meshComp->Color.x;
        cbd.ObjectColor[1] = meshComp->Color.y;
        cbd.ObjectColor[2] = meshComp->Color.z;
        cbd.ObjectColor[3] = meshComp->Color.w;
        cbd.Selected = 0.0f;
        cbd.NumLights = m_NumActiveLights;
        cbd.CameraPos[0] = m_CameraPosition.x;
        cbd.CameraPos[1] = m_CameraPosition.y;
        cbd.CameraPos[2] = m_CameraPosition.z;
        cbd.Roughness = meshComp->Roughness;
        cbd.Metallic = meshComp->Metallic;
        memcpy(cbd.Lights, m_LightDataCache, sizeof(m_LightDataCache));

        void* mapped = m_ConstantBuffer->Map();
        if (mapped)
        {
            memcpy(mapped, &cbd, sizeof(cbd));
            m_ConstantBuffer->Unmap();
        }
        ctx->SetConstantBuffer(0, m_ConstantBuffer.get());
    }

    // Deferred path: All objects rendered with G-Buffer shader (PSO already set)
    void DrawSceneMeshesDeferred(RHICommandContext* ctx)
    {
        for (const auto& renderItem : m_RenderList)
        {
            size_t i = renderItem.ObjectIndex;
            auto* meshComp = renderItem.MeshComp;
            if (!meshComp) continue;

            if (i >= m_GPUMeshes.size() || !m_GPUMeshes[i].VertexBuffer)
                continue;

            auto& gpuMesh = m_GPUMeshes[i];

            VertexBufferView vbView;
            vbView.BufferLocation = 0;
            vbView.SizeInBytes = gpuMesh.VertexCount * sizeof(Vertex);
            vbView.StrideInBytes = sizeof(Vertex);

            RHIBuffer* vbPtr = gpuMesh.VertexBuffer.get();
            ctx->SetVertexBuffers(0, &vbPtr, &vbView, 1);

            IndexBufferView ibView;
            ibView.BufferLocation = 0;
            ibView.SizeInBytes = gpuMesh.IndexCount * sizeof(uint32_t);
            ibView.Format = EFormat::R32_UINT;
            ctx->SetIndexBuffer(gpuMesh.IndexBuffer.get(), &ibView);

            UploadObjectCB(ctx, meshComp);
            ctx->DrawIndexed(gpuMesh.IndexCount, 0, 0);
        }
    }

    // Forward path: Objects rendered with per-object shaders (or forced shader)
    void DrawSceneMeshesForward(RHICommandContext* ctx, const char* forceShaderName = nullptr)
    {
        std::string lastShaderName;
        for (const auto& renderItem : m_RenderList)
        {
            size_t i = renderItem.ObjectIndex;
            auto* meshComp = renderItem.MeshComp;
            if (!meshComp) continue;

            if (i >= m_GPUMeshes.size() || !m_GPUMeshes[i].VertexBuffer)
                continue;

            // --- Per-object shader switching ---
            const std::string& shaderName = forceShaderName
                ? std::string(forceShaderName)
                : meshComp->ShaderName;
            if (shaderName != lastShaderName)
            {
                CompiledShader* shader = m_ShaderLibrary.GetShader(shaderName);
                if (!shader) shader = m_ShaderLibrary.GetDefault();

                if (shader)
                {
                    if (shader->PSO)
                        ctx->SetPipelineState(shader->PSO.get());
                    ctx->SetVertexShader(shader->VertexShader.get());
                    ctx->SetPixelShader(shader->PixelShader.get());
                    lastShaderName = shaderName;
                }
            }

            auto& gpuMesh = m_GPUMeshes[i];

            VertexBufferView vbView;
            vbView.BufferLocation = 0;
            vbView.SizeInBytes = gpuMesh.VertexCount * sizeof(Vertex);
            vbView.StrideInBytes = sizeof(Vertex);

            RHIBuffer* vbPtr = gpuMesh.VertexBuffer.get();
            ctx->SetVertexBuffers(0, &vbPtr, &vbView, 1);

            IndexBufferView ibView;
            ibView.BufferLocation = 0;
            ibView.SizeInBytes = gpuMesh.IndexCount * sizeof(uint32_t);
            ibView.Format = EFormat::R32_UINT;
            ctx->SetIndexBuffer(gpuMesh.IndexBuffer.get(), &ibView);

            UploadObjectCB(ctx, meshComp);
            ctx->DrawIndexed(gpuMesh.IndexCount, 0, 0);
        }
    }

    // Update CB for deferred lighting fullscreen pass
    void UpdateDeferredLightingCB()
    {
        ConstantBufferData cbd = {};
        // World/View/Projection not needed for fullscreen triangle, but lighting data is
        Mat4 identity = Mat4::Identity();
        memcpy(cbd.WorldMatrix, identity.m, sizeof(identity.m));
        memcpy(cbd.ViewMatrix, m_ViewMatrix.m, sizeof(m_ViewMatrix.m));
        memcpy(cbd.ProjectionMatrix, m_ProjectionMatrix.m, sizeof(m_ProjectionMatrix.m));
        cbd.Selected = 0.0f;
        cbd.NumLights = m_NumActiveLights;
        cbd.CameraPos[0] = m_CameraPosition.x;
        cbd.CameraPos[1] = m_CameraPosition.y;
        cbd.CameraPos[2] = m_CameraPosition.z;
        cbd.Roughness = 0.0f;
        cbd.Metallic = 0.0f;
        memcpy(cbd.Lights, m_LightDataCache, sizeof(m_LightDataCache));

        void* mapped = m_ConstantBuffer->Map();
        if (mapped)
        {
            memcpy(mapped, &cbd, sizeof(cbd));
            m_ConstantBuffer->Unmap();
        }
        auto ctx = GetContext();
        ctx->SetConstantBuffer(0, m_ConstantBuffer.get());
    }

    // Update CB for buffer visualization fullscreen pass
    void UpdateBufferVisualizationCB()
    {
        ConstantBufferData cbd = {};
        Mat4 identity = Mat4::Identity();
        memcpy(cbd.WorldMatrix, identity.m, sizeof(identity.m));
        memcpy(cbd.ViewMatrix, m_ViewMatrix.m, sizeof(m_ViewMatrix.m));
        memcpy(cbd.ProjectionMatrix, m_ProjectionMatrix.m, sizeof(m_ProjectionMatrix.m));

        // Use g_Selected to pass the visualization mode
        switch (m_ViewMode)
        {
        case EViewMode::BaseColor: cbd.Selected = 0.0f; break;
        case EViewMode::Roughness: cbd.Selected = 1.0f; break;
        case EViewMode::Metallic:  cbd.Selected = 2.0f; break;
        default:                   cbd.Selected = 0.0f; break;
        }

        cbd.NumLights = 0;
        cbd.CameraPos[0] = m_CameraPosition.x;
        cbd.CameraPos[1] = m_CameraPosition.y;
        cbd.CameraPos[2] = m_CameraPosition.z;

        void* mapped = m_ConstantBuffer->Map();
        if (mapped)
        {
            memcpy(mapped, &cbd, sizeof(cbd));
            m_ConstantBuffer->Unmap();
        }
        auto ctx = GetContext();
        ctx->SetConstantBuffer(0, m_ConstantBuffer.get());
    }

    // ============================================================
    // Post-Process Pipeline
    // ============================================================

    void CollectActivePostProcessEffects(std::vector<PostProcessMaterial*>& outEffects)
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

    void ExecutePostProcessPasses(
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

        // If no effects ran (shouldn't happen), blit RT[0] to backbuffer
        // This case is handled by the hasPostProcess check in OnRender
    }

    // ============================================================
    // InitView — Frustum Culling + Render Sorting
    // ============================================================

    // Render item: references a MeshComponent that passed frustum culling
    struct RenderItem
    {
        size_t         ObjectIndex;   // Index into m_Scene.GetObjects() (for GPU buffer lookup)
        MeshComponent* MeshComp;      // The mesh component to render
        int32_t        SortOrder;     // Higher = rendered first
        float          DistToCamera;  // Distance from object center to camera
    };

    void InitView()
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
            m_RenderList.push_back(item);
        }

        // Sort: primary key = SortOrder (descending, higher first),
        //        secondary key = distance to camera (descending, far first = back-to-front)
        std::sort(m_RenderList.begin(), m_RenderList.end(),
            [](const RenderItem& a, const RenderItem& b)
            {
                if (a.SortOrder != b.SortOrder)
                    return a.SortOrder > b.SortOrder; // Higher SortOrder rendered first
                return a.DistToCamera > b.DistToCamera; // Farther objects rendered first (back-to-front)
            });
    }

    // ============================================================
    // Camera Management
    // ============================================================

    void UpdateCameraFromScene()
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

    void UpdateCameraProjection()
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
    void CollectLightsFromScene()
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
    }

    // ---- RHI Switch callbacks ----

    void OnRHIShutdown() override
    {
        std::cout << "[Kiwi] Releasing GPU resources for RHI switch..." << std::endl;

        // Release all GPU resources
        m_GPUMeshes.clear();
        m_ConstantBuffer.reset();
        m_InputLayout.reset();
        m_PipelineState.reset();

        // Release all shaders via ShaderLibrary
        m_ShaderLibrary.ReleaseAll();

        // Release post-process resources
        ReleasePostProcessResources();

        // Release deferred rendering resources
        ReleaseGBufferResources();

        // Release Gizmo GPU resources
        for (int i = 0; i < 3; i++)
        {
            m_GizmoVB[i].reset();
            m_GizmoIB[i].reset();
        }

        // Shutdown ImGui backend
        ShutdownImGui();
    }

    void OnRHIReady() override
    {
        std::cout << "[Kiwi] Rebuilding GPU resources after RHI switch..." << std::endl;

        // Reinit RHI-specific resources
        InitRHIResources();

        // Rebuild GPU mesh buffers
        RebuildAllGPUBuffers();

        // Rebuild Gizmo GPU buffers
        BuildGizmoGPUBuffers();
    }

    
private:

    // ============================================================
    // ImGui backend management
    // ============================================================

    void ShutdownImGui()
    {
        auto device = GetDevice();
        if (device)
            device->ShutdownImGui();
    }

    // ============================================================
    // RHI Resource Initialization
    // ============================================================

    void InitRHIResources()
    {
        auto device = GetDevice();

        // We need a temporary VS to create the shared input layout
        auto tempVS = device->CompileShader(
            EShaderType::Vertex, g_VertexShaderHLSL, "main", "vs_5_0");

        // Create input layout (shared across all shaders — same vertex format)
        InputElementDesc inputElements[] = {
            { "POSITION", 0, EFormat::R32G32B32_FLOAT, (uint32_t)offsetof(Vertex, Position), 0, 0 },
            { "NORMAL",   0, EFormat::R32G32B32_FLOAT, (uint32_t)offsetof(Vertex, Normal),   0, 0 },
            { "COLOR",    0, EFormat::R32G32B32A32_FLOAT, (uint32_t)offsetof(Vertex, Color), 0, 0 },
            { "TEXCOORD", 0, EFormat::R32G32_FLOAT,    (uint32_t)offsetof(Vertex, TexCoord), 0, 0 },
        };
        m_InputLayout = device->CreateInputLayout(inputElements, 4, tempVS.get());

        // Constant buffer
        BufferDesc cbDesc;
        cbDesc.SizeInBytes = sizeof(ConstantBufferData);
        cbDesc.BindFlags = BUFFER_USAGE_CONSTANT;
        cbDesc.Usage = EResourceUsage::Dynamic;
        m_ConstantBuffer = device->CreateBuffer(cbDesc);

        // Pipeline state (DX11: empty wrapper, DX12: managed per-shader)
        m_PipelineState = device->CreatePipelineState();

        // Initialize ShaderLibrary (fully backend-agnostic)
        m_ShaderLibrary.Initialize(m_ShaderDir, device, m_InputLayout.get());

        // Initialize post-process resources
        InitPostProcessResources(device);

        // Initialize deferred rendering resources
        CompileDeferredShaders(device);
        CreateGBufferResources(device, GetWindow()->GetWidth(), GetWindow()->GetHeight());

        // Init ImGui backend
        device->InitImGui(GetWindow()->GetHWND());
    }

    // DX12 PSO creation is now handled by ShaderLibrary

    void InitPostProcessResources(RHIDevice* device)
    {
        // PostProcess shader library
        m_PostProcessLibrary.Initialize(m_PostProcessShaderDir, device);

        // Post-process constant buffer
        BufferDesc ppCbDesc;
        ppCbDesc.SizeInBytes = sizeof(PostProcessCBData);
        ppCbDesc.BindFlags = BUFFER_USAGE_CONSTANT;
        ppCbDesc.Usage = EResourceUsage::Dynamic;
        m_PostProcessCB = device->CreateBuffer(ppCbDesc);

        // DX11 sampler for post-process (linear clamp)
        // DX12 uses static sampler in root signature, so this is only for DX11
        m_PostProcessSampler = device->CreateSampler();

        // Compile passthrough shader (for final blit from offscreen to backbuffer)
        m_PassthroughVS = device->CompileShader(
            EShaderType::Vertex, g_PostProcessVS, "VSMain", "vs_5_0");
        m_PassthroughPS = device->CompileShader(
            EShaderType::Pixel, g_PostProcessPassthroughPS, "PSMain", "ps_5_0");
        if (m_PassthroughVS && m_PassthroughPS)
        {
            m_PassthroughPSO = device->CreateGraphicsPipelineState(
                m_PassthroughVS.get(), m_PassthroughPS.get(), nullptr);
        }

        // Create offscreen render targets
        CreateOffscreenRenderTargets(
            device, GetWindow()->GetWidth(), GetWindow()->GetHeight());
    }

    void CreateOffscreenRenderTargets(RHIDevice* device, uint32_t width, uint32_t height)
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
            rtDesc.Format = EFormat::R8G8B8A8_UNORM;
            rtDesc.BindFlags = TEXTURE_BIND_RENDER_TARGET | TEXTURE_BIND_SHADER_RESOURCE;
            rtDesc.Usage = EResourceUsage::Default;
            rtDesc.MipLevels = 1;
            rtDesc.SampleCount = 1;

            m_OffscreenRT[i] = device->CreateTexture(rtDesc);
            m_OffscreenRTV[i] = device->CreateTextureView(
                m_OffscreenRT[i].get(), EDescriptorHeapType::RTV);
            m_OffscreenSRV[i] = device->CreateTextureView(
                m_OffscreenRT[i].get(), EDescriptorHeapType::CBV_SRV_UAV);
        }

        std::cout << "[Kiwi] Offscreen RT created: " << width << "x" << height << std::endl;
    }

    void ReleasePostProcessResources()
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

    // ============================================================
    // Deferred Rendering: G-Buffer Resource Management
    // ============================================================

    void CreateGBufferResources(RHIDevice* device, uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0) return;

        // Release existing
        ReleaseGBufferResources();

        m_GBufferWidth = width;
        m_GBufferHeight = height;

        // G-Buffer layout:
        // RT0: World Position (R16G16B16A16_FLOAT)
        // RT1: Normal (R16G16B16A16_FLOAT) — RGB=normal packed [0,1], A=roughness
        // RT2: Albedo (R8G8B8A8_UNORM) — RGB=albedo, A=metallic
        EFormat gbufferFormats[GBUFFER_COUNT] = {
            EFormat::R16G16B16A16_FLOAT,   // Position
            EFormat::R16G16B16A16_FLOAT,   // Normal + Roughness
            EFormat::R8G8B8A8_UNORM,       // Albedo + Metallic
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

            m_GBufferRT[i] = device->CreateTexture(desc);
            m_GBufferRTV[i] = device->CreateTextureView(
                m_GBufferRT[i].get(), EDescriptorHeapType::RTV);
            m_GBufferSRV[i] = device->CreateTextureView(
                m_GBufferRT[i].get(), EDescriptorHeapType::CBV_SRV_UAV);
        }

        std::cout << "[Kiwi] G-Buffer created: " << width << "x" << height << std::endl;
    }

    void ReleaseGBufferResources()
    {
        for (int i = 0; i < GBUFFER_COUNT; i++)
        {
            m_GBufferSRV[i].reset();
            m_GBufferRTV[i].reset();
            m_GBufferRT[i].reset();
        }
        m_GBufferVS.reset();
        m_GBufferPS.reset();
        m_GBufferPSO.reset();
        m_DeferredLightingVS.reset();
        m_DeferredLightingPS.reset();
        m_DeferredLightingPSO.reset();
        m_BufferVisVS.reset();
        m_BufferVisPS.reset();
        m_BufferVisPSO.reset();
    }

    std::string ReadShaderFile(const std::string& filePath)
    {
        std::ifstream file(filePath);
        if (!file.is_open())
        {
            std::cerr << "[Kiwi] Failed to read shader file: " << filePath << std::endl;
            return "";
        }
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    void CompileDeferredShaders(RHIDevice* device)
    {
        // --- Compile G-Buffer Pass shader ---
        std::string gbufferPath = m_ShaderDir + "/GBufferPass.hlsl";
        std::string gbufferSrc = ReadShaderFile(gbufferPath);
        if (!gbufferSrc.empty())
        {
            m_GBufferVS = device->CompileShader(
                EShaderType::Vertex, gbufferSrc.c_str(), "VSMain", "vs_5_0");
            m_GBufferPS = device->CompileShader(
                EShaderType::Pixel, gbufferSrc.c_str(), "PSMain", "ps_5_0");

            if (m_GBufferVS && m_GBufferPS)
            {
                // Create MRT PSO for G-Buffer (3 render targets)
                PipelineStateDesc gbufferPSODesc;
                gbufferPSODesc.NumRenderTargets = 3;
                gbufferPSODesc.RTVFormats[0] = EFormat::R16G16B16A16_FLOAT; // Position
                gbufferPSODesc.RTVFormats[1] = EFormat::R16G16B16A16_FLOAT; // Normal + Roughness
                gbufferPSODesc.RTVFormats[2] = EFormat::R8G8B8A8_UNORM;     // Albedo + Metallic
                gbufferPSODesc.DSVFormat = EFormat::D24_UNORM_S8_UINT;
                gbufferPSODesc.DepthEnabled = true;
                gbufferPSODesc.DepthWrite = true;

                m_GBufferPSO = device->CreateGraphicsPipelineState(
                    m_GBufferVS.get(), m_GBufferPS.get(), m_InputLayout.get(), gbufferPSODesc);

                std::cout << "[Kiwi] G-Buffer shader compiled successfully" << std::endl;
            }
            else
            {
                std::cerr << "[Kiwi] Failed to compile G-Buffer shaders" << std::endl;
            }
        }

        // --- Compile Deferred Lighting shader ---
        std::string lightingPath = m_ShaderDir + "/DeferredLighting.hlsl";
        std::string lightingSrc = ReadShaderFile(lightingPath);
        if (!lightingSrc.empty())
        {
            m_DeferredLightingVS = device->CompileShader(
                EShaderType::Vertex, lightingSrc.c_str(), "VSMain", "vs_5_0");
            m_DeferredLightingPS = device->CompileShader(
                EShaderType::Pixel, lightingSrc.c_str(), "PSMain", "ps_5_0");

            if (m_DeferredLightingVS && m_DeferredLightingPS)
            {
                // Fullscreen pass: no input layout, no depth
                PipelineStateDesc lightingPSODesc;
                lightingPSODesc.NumRenderTargets = 1;
                lightingPSODesc.RTVFormats[0] = EFormat::R8G8B8A8_UNORM;
                lightingPSODesc.DepthEnabled = false;
                lightingPSODesc.DepthWrite = false;

                m_DeferredLightingPSO = device->CreateGraphicsPipelineState(
                    m_DeferredLightingVS.get(), m_DeferredLightingPS.get(),
                    nullptr, lightingPSODesc);

                std::cout << "[Kiwi] Deferred Lighting shader compiled successfully" << std::endl;
            }
            else
            {
                std::cerr << "[Kiwi] Failed to compile Deferred Lighting shaders" << std::endl;
            }
        }

        // --- Compile Buffer Visualization shader ---
        std::string bufferVisPath = m_ShaderDir + "/BufferVisualization.hlsl";
        std::string bufferVisSrc = ReadShaderFile(bufferVisPath);
        if (!bufferVisSrc.empty())
        {
            m_BufferVisVS = device->CompileShader(
                EShaderType::Vertex, bufferVisSrc.c_str(), "VSMain", "vs_5_0");
            m_BufferVisPS = device->CompileShader(
                EShaderType::Pixel, bufferVisSrc.c_str(), "PSMain", "ps_5_0");

            if (m_BufferVisVS && m_BufferVisPS)
            {
                PipelineStateDesc visPSODesc;
                visPSODesc.NumRenderTargets = 1;
                visPSODesc.RTVFormats[0] = EFormat::R8G8B8A8_UNORM;
                visPSODesc.DepthEnabled = false;
                visPSODesc.DepthWrite = false;

                m_BufferVisPSO = device->CreateGraphicsPipelineState(
                    m_BufferVisVS.get(), m_BufferVisPS.get(),
                    nullptr, visPSODesc);

                std::cout << "[Kiwi] Buffer Visualization shader compiled successfully" << std::endl;
            }
        }
    }

    // ============================================================
    // Menu Bar (fixed at top)
    // ============================================================

    void DrawMenuBar()
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("Rendering"))
            {
                if (ImGui::BeginMenu("RHI"))
                {
                    auto currentRHI = GetCurrentRHIType();

                    if (ImGui::MenuItem("Direct3D 11", nullptr,
                        currentRHI == RHI_API_TYPE::DX11, currentRHI != RHI_API_TYPE::DX11))
                    {
                        m_PendingRHISwitch = true;
                        m_PendingRHIType = RHI_API_TYPE::DX11;
                    }
                    if (ImGui::MenuItem("Direct3D 12", nullptr,
                        currentRHI == RHI_API_TYPE::DX12, currentRHI != RHI_API_TYPE::DX12))
                    {
                        m_PendingRHISwitch = true;
                        m_PendingRHIType = RHI_API_TYPE::DX12;
                    }

                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("View Mode"))
                {
                    if (ImGui::MenuItem("Lit", nullptr, m_ViewMode == EViewMode::Lit))
                        m_ViewMode = EViewMode::Lit;

                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Buffer Visualization");

                    if (ImGui::MenuItem("BaseColor", nullptr, m_ViewMode == EViewMode::BaseColor))
                        m_ViewMode = EViewMode::BaseColor;
                    if (ImGui::MenuItem("Roughness", nullptr, m_ViewMode == EViewMode::Roughness))
                        m_ViewMode = EViewMode::Roughness;
                    if (ImGui::MenuItem("Metallic", nullptr, m_ViewMode == EViewMode::Metallic))
                        m_ViewMode = EViewMode::Metallic;

                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Debug");

                    if (ImGui::MenuItem("Unlit", nullptr, m_ViewMode == EViewMode::Unlit))
                        m_ViewMode = EViewMode::Unlit;

                    ImGui::EndMenu();
                }

                ImGui::EndMenu();
            }

            // Show current view mode indicator in menu bar
            if (m_ViewMode != EViewMode::Lit)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                    "[%s]", GetViewModeName(m_ViewMode));
            }

            ImGui::EndMainMenuBar();
        }
    }

    // ============================================================
    // RenderDoc Capture Button (compact, top-right corner)
    // ============================================================

    void DrawRenderDocOverlay()
    {
        auto& rdoc = RenderDocIntegration::Get();
        if (!rdoc.IsAvailable()) return;

        float menuBarHeight = ImGui::GetFrameHeight();
        float windowWidth = (float)GetWindow()->GetWidth();

        // Compact button size - just an icon button
        float btnSize = 32.0f;

        ImGui::SetNextWindowPos(
            ImVec2(windowWidth - btnSize - 12.0f, menuBarHeight + 6.0f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);  // transparent background

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        if (ImGui::Begin("##RenderDocBtn", nullptr, flags))
        {
            bool capturing = rdoc.IsFrameCapturing();

            // RenderDoc brand colors: dark blue background, white icon
            ImVec4 btnColor    = capturing ? ImVec4(0.7f, 0.3f, 0.1f, 0.95f)
                                           : ImVec4(0.22f, 0.30f, 0.42f, 0.95f);
            ImVec4 hoverColor  = capturing ? ImVec4(0.8f, 0.4f, 0.2f, 1.0f)
                                           : ImVec4(0.29f, 0.42f, 0.60f, 1.0f);
            ImVec4 activeColor = capturing ? ImVec4(0.6f, 0.2f, 0.1f, 1.0f)
                                           : ImVec4(0.16f, 0.23f, 0.32f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Button, btnColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

            const char* icon = capturing ? "..." : "RD";

            if (ImGui::Button(icon, ImVec2(btnSize, btnSize)))
            {
                if (!capturing)
                {
                    rdoc.TriggerCapture();
                    m_CaptureTriggered = true;
                    m_AutoOpenRenderDoc = true;
                }
            }

            ImGui::PopStyleVar(1);  // FrameRounding
            ImGui::PopStyleColor(4);

            // Tooltip on hover
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                uint32_t numCaptures = rdoc.GetNumCaptures();
                ImGui::Text("RenderDoc Capture");
                if (numCaptures > 0)
                    ImGui::Text("Captures: %u", numCaptures);
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                    "Click to capture & open");
                ImGui::EndTooltip();
            }

            // Auto-open RenderDoc after capture completes
            uint32_t numCaptures = rdoc.GetNumCaptures();
            if (m_CaptureTriggered && numCaptures > m_LastCaptureCount)
            {
                m_LastCaptureCount = numCaptures;
                m_CaptureTriggered = false;

                if (m_AutoOpenRenderDoc)
                {
                    m_AutoOpenRenderDoc = false;
                    rdoc.LaunchReplayUI();
                }
            }

            // Draw the RenderDoc lens icon
            ImVec2 btnMin = ImGui::GetItemRectMin();
            ImVec2 btnMax = ImGui::GetItemRectMax();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 center = ImVec2((btnMin.x + btnMax.x) * 0.5f, (btnMin.y + btnMax.y) * 0.5f);

            float outerR = btnSize * 0.35f;
            float innerR = btnSize * 0.18f;
            ImU32 white = IM_COL32(255, 255, 255, 220);
            drawList->AddCircle(center, outerR, white, 24, 2.0f);
            drawList->AddCircleFilled(center, innerR, white);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);  // WindowPadding, WindowBorderSize
    }

    // ============================================================
    // Stats Overlay (compact button + expandable stats panel)
    // ============================================================

    void DrawStatsOverlay()
    {
        float menuBarHeight = ImGui::GetFrameHeight();
        float windowWidth = (float)GetWindow()->GetWidth();
        float btnSize = 32.0f;

        // Position: to the left of the RenderDoc button
        // RenderDoc is at (windowWidth - 32 - 12), so stats button is at (windowWidth - 32*2 - 12 - 6)
        float rdocBtnX = windowWidth - btnSize - 12.0f;
        float statsBtnX = rdocBtnX - btnSize - 6.0f;

        ImGui::SetNextWindowPos(
            ImVec2(statsBtnX, menuBarHeight + 6.0f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        if (ImGui::Begin("##StatsBtn", nullptr, flags))
        {
            // Button style: teal/cyan for stats
            ImVec4 btnColor    = m_ShowStats ? ImVec4(0.15f, 0.50f, 0.45f, 0.95f)
                                             : ImVec4(0.25f, 0.32f, 0.38f, 0.95f);
            ImVec4 hoverColor  = m_ShowStats ? ImVec4(0.20f, 0.60f, 0.55f, 1.0f)
                                             : ImVec4(0.32f, 0.42f, 0.50f, 1.0f);
            ImVec4 activeColor = m_ShowStats ? ImVec4(0.10f, 0.40f, 0.35f, 1.0f)
                                             : ImVec4(0.18f, 0.25f, 0.30f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Button, btnColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

            if (ImGui::Button("##StatsIcon", ImVec2(btnSize, btnSize)))
            {
                m_ShowStats = !m_ShowStats;
            }

            ImGui::PopStyleVar(1);  // FrameRounding
            ImGui::PopStyleColor(4);

            // Tooltip
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Render Stats");
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                    m_ShowStats ? "Click to hide stats" : "Click to show stats");
                ImGui::EndTooltip();
            }

            // Draw bar chart icon on the button
            ImVec2 btnMin = ImGui::GetItemRectMin();
            ImVec2 btnMax = ImGui::GetItemRectMax();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImU32 white = IM_COL32(255, 255, 255, 220);

            float cx = (btnMin.x + btnMax.x) * 0.5f;
            float cy = (btnMin.y + btnMax.y) * 0.5f;
            float barW = 3.0f;
            float gap = 2.0f;
            float baseY = cy + 6.0f;

            // Three bars of different heights (bar chart icon)
            float heights[] = { 8.0f, 14.0f, 11.0f };
            float startX = cx - (barW * 3 + gap * 2) * 0.5f;
            for (int i = 0; i < 3; i++)
            {
                float x = startX + i * (barW + gap);
                drawList->AddRectFilled(
                    ImVec2(x, baseY - heights[i]),
                    ImVec2(x + barW, baseY),
                    white, 1.0f);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);

        // ---- Stats Panel (expanded view) ----
        if (m_ShowStats)
        {
            DrawStatsPanel();
        }
    }

    void DrawStatsPanel()
    {
        float menuBarHeight = ImGui::GetFrameHeight();
        float windowWidth = (float)GetWindow()->GetWidth();
        float panelWidth = 240.0f;

        ImGui::SetNextWindowPos(
            ImVec2(windowWidth - panelWidth - 8.0f, menuBarHeight + 44.0f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(panelWidth, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.85f);

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.14f, 0.18f, 0.92f));

        if (ImGui::Begin("##StatsPanel", nullptr, flags))
        {
            // Header
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.75f, 1.0f), "Render Stats");
            ImGui::Separator();

            // FPS
            ImGuiIO& io = ImGui::GetIO();
            ImGui::Text("FPS: %.1f (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);

            // RHI backend
            const char* rhiName = (GetCurrentRHIType() == RHI_API_TYPE::DX11) ? "DX11" : "DX12";
            ImGui::Text("RHI: %s", rhiName);

            ImGui::Separator();

            // Pass timings
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Pass Timings (CPU)");

            const auto& entries = m_PassTimer.GetEntries();
            double totalMs = m_PassTimer.GetFrameTotalMs();

            // Bar width for visual proportions
            float maxBarWidth = panelWidth - 100.0f;

            for (const auto& entry : entries)
            {
                // Color-code by pass name
                ImVec4 barColor = ImVec4(0.4f, 0.6f, 0.8f, 0.8f); // default blue
                if (entry.Name.find("Geometry") != std::string::npos)
                    barColor = ImVec4(0.3f, 0.75f, 0.4f, 0.8f);   // green
                else if (entry.Name.find("Gizmo") != std::string::npos)
                    barColor = ImVec4(0.9f, 0.7f, 0.2f, 0.8f);    // yellow
                else if (entry.Name.find("Post-Process") != std::string::npos)
                    barColor = ImVec4(0.7f, 0.4f, 0.8f, 0.8f);    // purple
                else if (entry.Name.find("ImGui") != std::string::npos)
                    barColor = ImVec4(0.8f, 0.45f, 0.3f, 0.8f);   // orange

                // Pass name and time
                ImGui::Text("%s", entry.Name.c_str());
                ImGui::SameLine(140.0f);
                ImGui::Text("%.3f ms", entry.TimeMs);

                // Proportion bar
                float fraction = (totalMs > 0.001) ? (float)(entry.TimeMs / totalMs) : 0.0f;
                float barWidth = fraction * maxBarWidth;
                if (barWidth < 2.0f) barWidth = 2.0f;

                ImVec2 cursor = ImGui::GetCursorScreenPos();
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(
                    cursor,
                    ImVec2(cursor.x + barWidth, cursor.y + 4.0f),
                    ImGui::GetColorU32(barColor),
                    2.0f);
                // Also draw background bar
                drawList->AddRectFilled(
                    ImVec2(cursor.x + barWidth, cursor.y),
                    ImVec2(cursor.x + maxBarWidth, cursor.y + 4.0f),
                    IM_COL32(60, 60, 60, 100),
                    2.0f);
                ImGui::Dummy(ImVec2(maxBarWidth, 6.0f));
            }

            if (!entries.empty())
            {
                ImGui::Separator();
                ImGui::Text("Total");
                ImGui::SameLine(140.0f);
                ImGui::Text("%.3f ms", totalMs);
            }

            // Render list info
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Scene");
            ImGui::Text("Objects: %d", (int)m_Scene.GetObjects().size());
            ImGui::Text("Visible: %d", (int)m_RenderList.size());
        }
        ImGui::End();
        ImGui::PopStyleColor(1);
        ImGui::PopStyleVar(2);
    }

    // ============================================================
    // UI
    // ============================================================

    void DrawUI()
    {
        float menuBarHeight = ImGui::GetFrameHeight();

        // Side panel below menu bar
        ImGui::SetNextWindowPos(ImVec2(0, menuBarHeight), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(320, (float)GetWindow()->GetHeight() - menuBarHeight), ImGuiCond_Always);

        ImGui::Begin("Scene Panel", nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        // Show current RHI
        const char* rhiName = (GetCurrentRHIType() == RHI_API_TYPE::DX11) ? "Direct3D 11" : "Direct3D 12";
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "RHI: %s", rhiName);
        ImGui::Separator();

        // Menu bar within panel
        if (ImGui::Button("New Scene"))
        {
            m_Scene.Clear();
            m_GPUMeshes.clear();
            m_Scene.SetName("New Scene");
        }
        ImGui::SameLine();
        if (ImGui::Button("Save"))
        {
            m_Scene.SaveToFile("scene.json");
        }
        ImGui::SameLine();
        if (ImGui::Button("Load"))
        {
            if (m_Scene.LoadFromFile("scene.json"))
            {
                RebuildAllGPUBuffers();
            }
        }

        ImGui::Separator();

        // Scene object list
        ImGui::Text("Objects (%d)", (int)m_Scene.GetObjects().size());
        ImGui::BeginChild("ObjectList", ImVec2(0, 120), true);
        for (auto& objPtr : m_Scene.GetObjects())
        {
            auto& obj = *objPtr;
            bool selected = obj.Selected;

            // Icon prefix based on component type
            const char* icon = "";
            if (obj.HasComponent<CameraComponent>())
            {
                auto* cam = obj.GetComponent<CameraComponent>();
                icon = (cam && cam->IsMainCamera) ? "[C*] " : "[C] ";
            }
            else if (obj.HasComponent<LightComponent>()) icon = "[L] ";
            else if (obj.HasComponent<MeshComponent>()) icon = "[M] ";
            else if (obj.HasComponent<PostProcessComponent>()) icon = "[PP] ";

            std::string label = std::string(icon) + obj.Name;
            if (ImGui::Selectable(label.c_str(), &selected))
            {
                m_Scene.SelectObject(obj.ID);
            }
        }
        ImGui::EndChild();

        if (ImGui::Button("Delete Selected") && m_Scene.GetSelectedObject())
        {
            uint32_t selID = (uint32_t)m_Scene.GetSelectedID();
            m_Scene.RemoveObject(selID);
            RebuildAllGPUBuffers();
        }

        ImGui::Separator();

        // Tabs
        if (ImGui::BeginTabBar("MainTabs"))
        {
            if (ImGui::BeginTabItem("Detail"))
            {
                DrawDetailTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Placer"))
            {
                DrawPlacerTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    void DrawDetailTab()
    {
        SceneObject* sel = m_Scene.GetSelectedObject();
        if (!sel)
        {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No object selected.");
            ImGui::TextWrapped("Click an object in the viewport or select from the list above.");
            return;
        }

        ImGui::Text("Name: %s", sel->Name.c_str());
        ImGui::Text("Components: %d", (int)sel->Components.size());

        // Draw UI for each component
        for (size_t ci = 0; ci < sel->Components.size(); ci++)
        {
            auto& comp = *sel->Components[ci];
            ImGui::Separator();

            // Component header with type name
            bool compOpen = ImGui::TreeNodeEx(
                (std::string(comp.GetTypeName()) + "##" + std::to_string(ci)).c_str(),
                ImGuiTreeNodeFlags_DefaultOpen);

            if (compOpen)
            {
                // Enable/Disable toggle
                ImGui::Checkbox(("Enabled##comp" + std::to_string(ci)).c_str(), &comp.Enabled);

                // Transform — every component has this
                ImGui::Text("Transform");
                bool changed = false;
                changed |= ImGui::DragFloat3(("Position##" + std::to_string(ci)).c_str(), &comp.Position.x, 0.05f);
                changed |= ImGui::DragFloat3(("Rotation##" + std::to_string(ci)).c_str(), &comp.Rotation.x, 1.0f, -360.0f, 360.0f);
                changed |= ImGui::DragFloat3(("Scale##" + std::to_string(ci)).c_str(), &comp.Scale.x, 0.05f, 0.01f, 100.0f);

                // Type-specific UI
                if (comp.GetType() == EComponentType::Mesh)
                {
                    auto& mesh = static_cast<MeshComponent&>(comp);

                    ImGui::Separator();
                    ImGui::Text("Appearance");
                    ImGui::ColorEdit4(("Color##" + std::to_string(ci)).c_str(), &mesh.Color.x);

                    ImGui::Separator();
                    ImGui::Text("Material");
                    ImGui::SliderFloat(("Roughness##" + std::to_string(ci)).c_str(), &mesh.Roughness, 0.0f, 1.0f);
                    ImGui::SliderFloat(("Metallic##" + std::to_string(ci)).c_str(), &mesh.Metallic, 0.0f, 1.0f);

                    ImGui::Separator();
                    ImGui::Text("Rendering");
                    ImGui::DragInt(("Sort Order##" + std::to_string(ci)).c_str(), &mesh.SortOrder, 0.5f, -1000, 1000);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Higher values are rendered first.\nObjects with same order are sorted back-to-front.");

                    ImGui::Separator();
                    ImGui::Text("Shader");
                    {
                        const auto& shaderNames = m_ShaderLibrary.GetShaderNames();
                        int currentIdx = 0;
                        for (int si = 0; si < (int)shaderNames.size(); si++)
                        {
                            if (shaderNames[si] == mesh.ShaderName)
                            {
                                currentIdx = si;
                                break;
                            }
                        }
                        if (ImGui::BeginCombo(("##ShaderCombo" + std::to_string(ci)).c_str(), mesh.ShaderName.c_str()))
                        {
                            for (int si = 0; si < (int)shaderNames.size(); si++)
                            {
                                bool isSelected = (si == currentIdx);
                                if (ImGui::Selectable(shaderNames[si].c_str(), isSelected))
                                {
                                    mesh.ShaderName = shaderNames[si];
                                }
                                if (isSelected)
                                    ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                    }

                    ImGui::Separator();
                    ImGui::Text("Mesh Info");
                    ImGui::Text("  Vertices: %u", mesh.MeshData.GetVertexCount());
                    ImGui::Text("  Indices:  %u", mesh.MeshData.GetIndexCount());
                    ImGui::Text("  Triangles: %u", mesh.MeshData.GetIndexCount() / 3);
                }
                else if (comp.GetType() == EComponentType::Camera)
                {
                    auto& cam = static_cast<CameraComponent&>(comp);

                    ImGui::Separator();
                    ImGui::Text("Camera Settings");

                    // Main Camera toggle — mutually exclusive
                    bool isMain = cam.IsMainCamera;
                    if (ImGui::Checkbox(("Main Camera##" + std::to_string(ci)).c_str(), &isMain))
                    {
                        if (isMain)
                        {
                            // Set this camera as main (clears all others)
                            m_Scene.SetMainCamera(&cam);
                        }
                        else
                        {
                            // Unchecking: clear the flag (no main camera)
                            cam.IsMainCamera = false;
                        }
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("The Main Camera drives the engine's rendering viewpoint.\nOnly one camera can be the Main Camera at a time.");

                    if (cam.IsMainCamera)
                    {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "(Active)");
                    }

                    // Projection type
                    const char* projNames[] = { "Perspective", "Orthographic" };
                    int projIdx = (cam.Projection == ECameraProjection::Perspective) ? 0 : 1;
                    if (ImGui::Combo(("Projection##" + std::to_string(ci)).c_str(), &projIdx, projNames, 2))
                    {
                        cam.Projection = (projIdx == 0) ? ECameraProjection::Perspective : ECameraProjection::Orthographic;
                    }

                    if (cam.Projection == ECameraProjection::Perspective)
                    {
                        ImGui::DragFloat(("FOV##" + std::to_string(ci)).c_str(), &cam.FieldOfView, 0.5f, 10.0f, 120.0f);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Field of View in degrees.\nSmaller = zoom in, Larger = wide angle.");
                    }
                    else
                    {
                        ImGui::DragFloat(("Ortho Width##" + std::to_string(ci)).c_str(), &cam.OrthoWidth, 0.1f, 0.1f, 100.0f);
                        ImGui::DragFloat(("Ortho Height##" + std::to_string(ci)).c_str(), &cam.OrthoHeight, 0.1f, 0.1f, 100.0f);
                    }

                    ImGui::DragFloat(("Near Plane##" + std::to_string(ci)).c_str(), &cam.NearPlane, 0.01f, 0.001f, 10.0f);
                    ImGui::DragFloat(("Far Plane##" + std::to_string(ci)).c_str(), &cam.FarPlane, 1.0f, 1.0f, 10000.0f);
                }
                else if (comp.GetType() == EComponentType::Light)
                {
                    auto& light = static_cast<LightComponent&>(comp);

                    ImGui::Separator();
                    ImGui::Text("Light Type: %s", light.GetLightTypeName());

                    // Light Color
                    ImGui::ColorEdit3(("Light Color##" + std::to_string(ci)).c_str(), &light.LightColor.x);

                    // Intensity
                    ImGui::DragFloat(("Intensity##" + std::to_string(ci)).c_str(), &light.Intensity, 0.01f, 0.0f, 20.0f);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Light intensity multiplier.\n0 = off, 1 = normal, >1 = brighter");

                    // Affect World
                    ImGui::Checkbox(("Affect World##light" + std::to_string(ci)).c_str(), &light.AffectWorld);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("When disabled, this light will not affect any objects.");

                    // Type-specific: Point Light Radius
                    if (light.GetLightType() == ELightType::Point)
                    {
                        auto& pointLight = static_cast<PointLightComponent&>(light);
                        ImGui::Separator();
                        ImGui::Text("Point Light");
                        ImGui::DragFloat(("Radius##" + std::to_string(ci)).c_str(), &pointLight.Radius, 0.1f, 0.1f, 100.0f);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Maximum distance this light can reach.\nFragments beyond this distance receive no light.");
                    }
                    else // Directional
                    {
                        ImGui::Separator();
                        ImGui::Text("Directional Light");
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                            "Direction is controlled by the\nRotation above (forward vector).");
                    }
                }
                else if (comp.GetType() == EComponentType::PostProcess)
                {
                    auto& ppComp = static_cast<PostProcessComponent&>(comp);

                    ImGui::Separator();
                    ImGui::Text("Post-Process Effects");
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                        "Materials are applied in order (top to bottom).");

                    // Material list
                    int removeIdx = -1;
                    for (size_t mi = 0; mi < ppComp.Materials.size(); mi++)
                    {
                        auto& mat = ppComp.Materials[mi];
                        ImGui::PushID((int)(ci * 1000 + mi));

                        ImGui::Separator();

                        // Enable toggle
                        ImGui::Checkbox("##Enabled", &mat.Enabled);
                        ImGui::SameLine();

                        // Shader dropdown
                        const auto& ppShaderNames = m_PostProcessLibrary.GetShaderNames();
                        if (ImGui::BeginCombo("##Shader", mat.ShaderName.c_str()))
                        {
                            for (const auto& name : ppShaderNames)
                            {
                                bool isSelected = (name == mat.ShaderName);
                                if (ImGui::Selectable(name.c_str(), isSelected))
                                {
                                    mat.ShaderName = name;
                                }
                                if (isSelected)
                                    ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }

                        // Intensity slider
                        ImGui::DragFloat("Intensity", &mat.Intensity, 0.01f, 0.0f, 2.0f);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Effect intensity.\n0 = no effect, 1 = full effect.");

                        // Remove button
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
                        if (ImGui::Button("Remove"))
                        {
                            removeIdx = (int)mi;
                        }
                        ImGui::PopStyleColor();

                        // Move up/down buttons
                        ImGui::SameLine();
                        if (mi > 0)
                        {
                            if (ImGui::Button("Up"))
                            {
                                std::swap(ppComp.Materials[mi], ppComp.Materials[mi - 1]);
                            }
                            ImGui::SameLine();
                        }
                        if (mi < ppComp.Materials.size() - 1)
                        {
                            if (ImGui::Button("Down"))
                            {
                                std::swap(ppComp.Materials[mi], ppComp.Materials[mi + 1]);
                            }
                        }

                        ImGui::PopID();
                    }

                    if (removeIdx >= 0)
                        ppComp.RemoveMaterial((size_t)removeIdx);

                    ImGui::Separator();

                    // Add material button
                    const auto& ppShaderNames = m_PostProcessLibrary.GetShaderNames();
                    if (!ppShaderNames.empty())
                    {
                        if (ImGui::Button("+ Add Material", ImVec2(-1, 30)))
                        {
                            ppComp.AddMaterial(ppShaderNames[0]);
                        }
                    }
                    else
                    {
                        ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.2f, 1.0f),
                            "No post-process shaders found.\nAdd .hlsl files to PostProcessShaders/ folder.");
                    }
                }

                (void)changed;
                ImGui::TreePop();
            }
        }
    }

    void DrawPlacerTab()
    {
        ImGui::Text("Add objects to the scene:");
        ImGui::Separator();

        struct PlacerEntry
        {
            const char* label;
            EPrimitiveType type;
        };

        static PlacerEntry entries[] = {
            { "Cube",     EPrimitiveType::Cube },
            { "Sphere",   EPrimitiveType::Sphere },
            { "Cylinder", EPrimitiveType::Cylinder },
            { "Floor",    EPrimitiveType::Floor },
        };

        for (auto& entry : entries)
        {
            if (ImGui::Button(entry.label, ImVec2(280, 35)))
            {
                auto* obj = m_Scene.AddMeshObject(entry.type);
                auto* mesh = obj->GetComponent<MeshComponent>();
                if (mesh && entry.type != EPrimitiveType::Floor)
                {
                    mesh->Position.y = 0.5f;
                    mesh->Position.x = (float)(rand() % 60 - 30) * 0.1f;
                    mesh->Position.z = (float)(rand() % 60 - 30) * 0.1f;
                }
                RebuildAllGPUBuffers();
                m_Scene.SelectObject(obj->ID);
            }
        }

        ImGui::Separator();
        ImGui::Text("Special:");
        if (ImGui::Button("Camera", ImVec2(280, 35)))
        {
            auto* obj = m_Scene.AddCameraObject();
            auto* cam = obj->GetComponent<CameraComponent>();
            if (cam)
            {
                cam->Position = { 0.0f, 3.0f, -6.0f };
            }
            m_Scene.SelectObject(obj->ID);
        }

        ImGui::Separator();
        ImGui::Text("Lights:");
        if (ImGui::Button("Directional Light", ImVec2(280, 35)))
        {
            auto* obj = m_Scene.AddDirectionalLightObject();
            m_Scene.SelectObject(obj->ID);
        }
        if (ImGui::Button("Point Light", ImVec2(280, 35)))
        {
            auto* obj = m_Scene.AddPointLightObject();
            m_Scene.SelectObject(obj->ID);
        }

        ImGui::Separator();
        ImGui::Text("Effects:");
        if (ImGui::Button("Post Process", ImVec2(280, 35)))
        {
            auto* obj = m_Scene.AddPostProcessObject();
            // Add a default material if shaders are available
            auto* ppComp = obj->GetComponent<PostProcessComponent>();
            if (ppComp && !m_PostProcessLibrary.GetShaderNames().empty())
            {
                ppComp->AddMaterial(m_PostProcessLibrary.GetShaderNames()[0]);
            }
            m_Scene.SelectObject(obj->ID);
        }
    }

    // ============================================================
    // Gizmo
    // ============================================================

    void InitGizmoMeshes()
    {
        m_GizmoMeshData[0] = CreateGizmoArrow({ 1, 0, 0 }, { 1, 0, 0, 1 }); // X - Red
        m_GizmoMeshData[1] = CreateGizmoArrow({ 0, 1, 0 }, { 0, 1, 0, 1 }); // Y - Green
        m_GizmoMeshData[2] = CreateGizmoArrow({ 0, 0, 1 }, { 0, 0, 1, 1 }); // Z - Blue
    }

    void BuildGizmoGPUBuffers()
    {
        auto device = GetDevice();
        Vec4 colors[3] = {
            { 1, 0, 0, 1 }, // X - Red
            { 0, 1, 0, 1 }, // Y - Green
            { 0, 0, 1, 1 }, // Z - Blue
        };

        for (int i = 0; i < 3; i++)
        {
            auto& data = m_GizmoMeshData[i];
            m_GizmoVertexCount[i] = (uint32_t)data.Vertices.size();
            m_GizmoIndexCount[i] = (uint32_t)data.Indices.size();

            if (m_GizmoVertexCount[i] == 0) continue;

            BufferDesc vbDesc;
            vbDesc.SizeInBytes = m_GizmoVertexCount[i] * sizeof(Vertex);
            vbDesc.BindFlags = BUFFER_USAGE_VERTEX;
            vbDesc.Usage = EResourceUsage::Immutable;
            m_GizmoVB[i] = device->CreateBuffer(vbDesc, data.Vertices.data());

            BufferDesc ibDesc;
            ibDesc.SizeInBytes = m_GizmoIndexCount[i] * sizeof(uint32_t);
            ibDesc.BindFlags = BUFFER_USAGE_INDEX;
            ibDesc.Usage = EResourceUsage::Immutable;
            m_GizmoIB[i] = device->CreateBuffer(ibDesc, data.Indices.data());
        }
    }

    void DrawGizmo(RHICommandContext* ctx)
    {
        SceneObject* sel = m_Scene.GetSelectedObject();
        if (!sel) return;

        // Gizmo always uses the Default shader
        CompiledShader* defaultShader = m_ShaderLibrary.GetDefault();
        if (defaultShader)
        {
            if (defaultShader->PSO)
                ctx->SetPipelineState(defaultShader->PSO.get());
            ctx->SetVertexShader(defaultShader->VertexShader.get());
            ctx->SetPixelShader(defaultShader->PixelShader.get());
        }

        Vec3 gizmoPos = sel->GetPosition();
        Vec4 colors[3] = {
            { 1.0f, 0.2f, 0.2f, 1.0f }, // X - Red
            { 0.2f, 1.0f, 0.2f, 1.0f }, // Y - Green
            { 0.2f, 0.4f, 1.0f, 1.0f }, // Z - Blue
        };

        // Highlight the dragged axis
        if (m_IsDragging)
        {
            int axisIdx = (int)m_DragAxis - 1;
            if (axisIdx >= 0 && axisIdx < 3)
                colors[axisIdx] = { 1.0f, 1.0f, 0.3f, 1.0f }; // Yellow highlight
        }

        for (int i = 0; i < 3; i++)
        {
            if (!m_GizmoVB[i] || !m_GizmoIB[i]) continue;

            VertexBufferView vbView;
            vbView.BufferLocation = 0;
            vbView.SizeInBytes = m_GizmoVertexCount[i] * sizeof(Vertex);
            vbView.StrideInBytes = sizeof(Vertex);

            RHIBuffer* vbPtr = m_GizmoVB[i].get();
            ctx->SetVertexBuffers(0, &vbPtr, &vbView, 1);

            IndexBufferView ibView;
            ibView.BufferLocation = 0;
            ibView.SizeInBytes = m_GizmoIndexCount[i] * sizeof(uint32_t);
            ibView.Format = EFormat::R32_UINT;
            ctx->SetIndexBuffer(m_GizmoIB[i].get(), &ibView);

            // World matrix = translation to gizmo position
            Mat4 worldMatrix = Mat4::Translation(gizmoPos.x, gizmoPos.y, gizmoPos.z);

            ConstantBufferData cbd = {};
            memcpy(cbd.WorldMatrix, worldMatrix.m, sizeof(worldMatrix.m));
            memcpy(cbd.ViewMatrix, m_ViewMatrix.m, sizeof(m_ViewMatrix.m));
            memcpy(cbd.ProjectionMatrix, m_ProjectionMatrix.m, sizeof(m_ProjectionMatrix.m));
            cbd.ObjectColor[0] = colors[i].x;
            cbd.ObjectColor[1] = colors[i].y;
            cbd.ObjectColor[2] = colors[i].z;
            cbd.ObjectColor[3] = colors[i].w;
            cbd.Selected = 2.0f; // > 1.5 => unlit/gizmo mode in pixel shader
            cbd.NumLights = 0;   // Gizmo doesn't need lights
            cbd.CameraPos[0] = m_CameraPosition.x;
            cbd.CameraPos[1] = m_CameraPosition.y;
            cbd.CameraPos[2] = m_CameraPosition.z;

            void* mapped = m_ConstantBuffer->Map();
            if (mapped)
            {
                memcpy(mapped, &cbd, sizeof(cbd));
                m_ConstantBuffer->Unmap();
            }
            ctx->SetConstantBuffer(0, m_ConstantBuffer.get());

            ctx->DrawIndexed(m_GizmoIndexCount[i], 0, 0);
        }
    }

    EGizmoAxis PickGizmoAxis(int mouseX, int mouseY)
    {
        SceneObject* sel = m_Scene.GetSelectedObject();
        if (!sel) return EGizmoAxis::None;

        uint32_t w = GetWindow()->GetWidth();
        uint32_t h = GetWindow()->GetHeight();
        Ray ray = ScreenToRay(mouseX, mouseY, w, h, m_ViewMatrix, m_ProjectionMatrix);

        Vec3 gizmoPos = sel->GetPosition();
        float gizmoLength = 1.2f;
        float pickRadius = 0.08f;

        Vec3 axes[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
        EGizmoAxis axisTypes[3] = { EGizmoAxis::X, EGizmoAxis::Y, EGizmoAxis::Z };

        float closestT = 1e30f;
        EGizmoAxis result = EGizmoAxis::None;

        for (int i = 0; i < 3; i++)
        {
            float t;
            if (RayPicksGizmoAxis(ray, gizmoPos, axes[i], gizmoLength, pickRadius, t))
            {
                if (t < closestT)
                {
                    closestT = t;
                    result = axisTypes[i];
                }
            }
        }
        return result;
    }

    // ============================================================
    // Mouse Picking
    // ============================================================

    void PickObject(int mouseX, int mouseY)
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

    // ============================================================
    // GPU Buffer Management
    // ============================================================

    void RebuildAllGPUBuffers()
    {
        auto device = GetDevice();
        auto& objects = m_Scene.GetObjects();

        m_GPUMeshes.resize(objects.size());

        for (size_t i = 0; i < objects.size(); i++)
        {
            auto& obj = *objects[i];
            auto& gpu = m_GPUMeshes[i];

            // Only build GPU buffers for objects with MeshComponent
            auto* meshComp = obj.GetComponent<MeshComponent>();
            if (!meshComp)
            {
                gpu.VertexBuffer.reset();
                gpu.IndexBuffer.reset();
                gpu.VertexCount = 0;
                gpu.IndexCount = 0;
                continue;
            }

            gpu.VertexCount = meshComp->MeshData.GetVertexCount();
            gpu.IndexCount = meshComp->MeshData.GetIndexCount();

            if (gpu.VertexCount == 0 || gpu.IndexCount == 0) continue;

            BufferDesc vbDesc;
            vbDesc.SizeInBytes = gpu.VertexCount * sizeof(Vertex);
            vbDesc.BindFlags = BUFFER_USAGE_VERTEX;
            vbDesc.Usage = EResourceUsage::Immutable;
            gpu.VertexBuffer = device->CreateBuffer(vbDesc, meshComp->MeshData.GetVertices().data());

            BufferDesc ibDesc;
            ibDesc.SizeInBytes = gpu.IndexCount * sizeof(uint32_t);
            ibDesc.BindFlags = BUFFER_USAGE_INDEX;
            ibDesc.Usage = EResourceUsage::Immutable;
            gpu.IndexBuffer = device->CreateBuffer(ibDesc, meshComp->MeshData.GetIndices().data());
        }
    }

    // ============================================================
    // Members
    // ============================================================

    Scene m_Scene;
    std::vector<GPUMeshData> m_GPUMeshes;
    std::vector<RenderItem> m_RenderList; // Sorted visible objects from InitView()

    // Shader Library — manages all loaded shaders
    ShaderLibrary m_ShaderLibrary;
    std::string m_ShaderDir; // Path to Shaders/ folder

    // RHI resources (shared interface)
    std::unique_ptr<RHIInputLayout>   m_InputLayout;
    std::unique_ptr<RHIBuffer>        m_ConstantBuffer;
    std::unique_ptr<RHIPipelineState> m_PipelineState;  // DX11

    // Camera (cached from scene CameraComponent each frame)
    Mat4 m_ViewMatrix;
    Mat4 m_ProjectionMatrix;
    Vec3 m_CameraPosition;

    // Lights (cached from scene LightComponents each frame)
    GPULightData m_LightDataCache[MAX_LIGHTS] = {};
    int m_NumActiveLights = 0;

    // RenderDoc state
    bool m_CaptureTriggered = false;
    bool m_AutoOpenRenderDoc = false;
    uint32_t m_LastCaptureCount = 0;

    // Stats overlay state
    PassTimer m_PassTimer;
    bool m_ShowStats = false;

    // Gizmo state
    GizmoMeshData m_GizmoMeshData[3];                    // CPU mesh data for 3 axes
    std::unique_ptr<RHIBuffer> m_GizmoVB[3];             // GPU vertex buffers
    std::unique_ptr<RHIBuffer> m_GizmoIB[3];             // GPU index buffers
    uint32_t m_GizmoVertexCount[3] = {};
    uint32_t m_GizmoIndexCount[3] = {};

    // Dragging state
    bool m_IsDragging = false;
    EGizmoAxis m_DragAxis = EGizmoAxis::None;
    int m_DragStartMouseX = 0;
    int m_DragStartMouseY = 0;
    Vec3 m_DragStartPos;

    // ---- Post-Process Resources ----
    PostProcessShaderLibrary m_PostProcessLibrary;
    std::string m_PostProcessShaderDir;

    // Offscreen render targets (ping-pong buffers)
    std::unique_ptr<RHITexture>     m_OffscreenRT[2];
    std::unique_ptr<RHITextureView> m_OffscreenRTV[2];
    std::unique_ptr<RHITextureView> m_OffscreenSRV[2];
    uint32_t m_OffscreenWidth = 0;
    uint32_t m_OffscreenHeight = 0;

    // Post-process constant buffer
    std::unique_ptr<RHIBuffer> m_PostProcessCB;

    // DX11 sampler for post-process (DX12 uses static sampler in root signature)
    std::unique_ptr<RHISampler> m_PostProcessSampler;

    // Passthrough shader (compiled from built-in code)
    std::unique_ptr<RHIShader> m_PassthroughVS;
    std::unique_ptr<RHIShader> m_PassthroughPS;
    std::unique_ptr<RHIPipelineState> m_PassthroughPSO;

    // Time tracking for post-process effects
    float m_TotalTime = 0.0f;

    // ---- Deferred Rendering: G-Buffer Resources ----
    static constexpr int GBUFFER_COUNT = 3; // Position, Normal(+Roughness), Albedo(+Metallic)
    std::unique_ptr<RHITexture>     m_GBufferRT[GBUFFER_COUNT];
    std::unique_ptr<RHITextureView> m_GBufferRTV[GBUFFER_COUNT];
    std::unique_ptr<RHITextureView> m_GBufferSRV[GBUFFER_COUNT];
    uint32_t m_GBufferWidth = 0;
    uint32_t m_GBufferHeight = 0;

    // G-Buffer shaders (compiled separately from ShaderLibrary)
    std::unique_ptr<RHIShader> m_GBufferVS;
    std::unique_ptr<RHIShader> m_GBufferPS;
    std::unique_ptr<RHIPipelineState> m_GBufferPSO; // MRT PSO

    // Deferred Lighting shader (fullscreen pass)
    std::unique_ptr<RHIShader> m_DeferredLightingVS;
    std::unique_ptr<RHIShader> m_DeferredLightingPS;
    std::unique_ptr<RHIPipelineState> m_DeferredLightingPSO;

    // Buffer Visualization shader (fullscreen pass for debug ViewModes)
    std::unique_ptr<RHIShader> m_BufferVisVS;
    std::unique_ptr<RHIShader> m_BufferVisPS;
    std::unique_ptr<RHIPipelineState> m_BufferVisPSO;

    // ---- View Mode ----
    EViewMode m_ViewMode = EViewMode::Lit;
};

// ============================================================
// Main
// ============================================================

int main()
{
    try
    {
        std::cout << "========================================" << std::endl;
        std::cout << "  Kiwi Engine - Scene Editor" << std::endl;
        std::cout << "  RHI: Direct3D 11 / Direct3D 12" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << std::endl;

        // Load engine configuration
        auto& config = Kiwi::EngineConfig::Get();
        config.LoadDefaultConfig();

        // Initialize RenderDoc BEFORE creating any graphics device
        auto& rdoc = Kiwi::RenderDocIntegration::Get();
        bool rdocAvailable = rdoc.Initialize();
        if (rdocAvailable)
        {
            std::cout << "[Kiwi] RenderDoc attached - frame capture available." << std::endl;
        }
        std::cout << std::endl;

        KiwiEngineApp app;
        app.Run();

        // Shutdown RenderDoc
        rdoc.Shutdown();

        std::cout << std::endl;
        std::cout << "[Kiwi] Engine shutdown complete." << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Kiwi] Fatal Error: " << e.what() << std::endl;
        MessageBoxA(nullptr, e.what(), "Kiwi Engine Error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
