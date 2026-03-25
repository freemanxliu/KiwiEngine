#include "Core/Application.h"
#include "Core/Window.h"
#include "Core/EngineConfig.h"
#include "Scene/Mesh.h"
#include "Scene/Shaders.h"
#include "Scene/ShaderLibrary.h"
#include "Scene/Scene.h"
#include "Scene/SceneObject.h"
#include "Math/Math.h"
#include "RHI/RHI.h"
#include "Debug/RenderDocIntegration.h"

#include <imgui.h>

#include <iostream>
#include <memory>
#include <cmath>
#include <algorithm>
#include <filesystem>

using namespace Kiwi;

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

static void ComputeWorldAABB(const SceneObject& obj, Vec3& outMin, Vec3& outMax)
{
    Mat4 world = obj.TransformData.GetWorldMatrix();
    const auto& verts = obj.MeshData.GetVertices();

    if (verts.empty())
    {
        outMin = outMax = obj.TransformData.Position;
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

            // Fallback: try source directory relative path
            namespace fs = std::filesystem;
            if (!fs::exists(m_ShaderDir))
            {
                // Assume exe is in build/bin/ and source is ../../Shaders
                std::string fallback = exeDir + "\\..\\..\\Shaders";
                if (fs::exists(fallback))
                    m_ShaderDir = fallback;
            }
            std::cout << "[Kiwi] Shader directory: " << m_ShaderDir << std::endl;
        }

        // ---- Init ImGui context (once) ----
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();

        // ---- Init RHI-specific resources ----
        InitRHIResources();

        // ---- Camera ----
        m_CameraPosition = Vec3(0.0f, 3.0f, -6.0f);
        m_CameraTarget = Vec3(0.0f, 0.5f, 0.0f);
        m_CameraUp = Vec3(0.0f, 1.0f, 0.0f);

        m_ViewMatrix = Mat4::LookAt(m_CameraPosition, m_CameraTarget, m_CameraUp);

        uint32_t w = GetWindow()->GetWidth();
        uint32_t h = GetWindow()->GetHeight();
        float aspect = (float)w / (float)h;
        m_ProjectionMatrix = Mat4::Perspective(DegToRad(45.0f), aspect, 0.1f, 100.0f);

        GetWindow()->SetResizeCallback([this](uint32_t width, uint32_t height) {
            float aspect = (float)width / (float)height;
            m_ProjectionMatrix = Mat4::Perspective(DegToRad(45.0f), aspect, 0.1f, 100.0f);
        });

        // ---- Default scene ----
        m_Scene.SetName("Default Scene");
        auto* floor = m_Scene.AddObject(EPrimitiveType::Floor, "Ground");
        (void)floor;
        auto* cube = m_Scene.AddObject(EPrimitiveType::Cube, "Cube_1");
        cube->TransformData.Position = { 0.0f, 0.5f, 0.0f };

        RebuildAllGPUBuffers();

        // ---- Init Gizmo ----
        InitGizmoMeshes();
        BuildGizmoGPUBuffers();

        std::cout << "[Kiwi] Scene Editor initialized!" << std::endl;
    }

    void OnUpdate(float deltaTime) override
    {
        (void)deltaTime;

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
                        m_DragStartPos = sel->TransformData.Position;
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

                    sel->TransformData.Position = m_DragStartPos + axisDir * delta;
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
        auto ctx = GetContext();
        auto swapChain = GetSwapChain();
        auto device = GetDevice();

        // ---- Begin frame (DX12: Reset + RootSig + DescriptorHeaps + Barrier; DX11: no-op) ----
        ctx->BeginFrame(swapChain);

        // ---- Set render targets ----
        auto backBufferRTV = swapChain->GetBackBufferRTV(swapChain->GetCurrentBackBufferIndex());
        ctx->SetRenderTargets(&backBufferRTV, 1, GetDSV());

        // ---- Set viewport and scissor ----
        Viewport vp;
        vp.TopLeftX = 0; vp.TopLeftY = 0;
        vp.Width = (float)GetWindow()->GetWidth();
        vp.Height = (float)GetWindow()->GetHeight();
        vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
        ctx->SetViewports(&vp, 1);

        ScissorRect sr;
        sr.Left = 0; sr.Top = 0;
        sr.Right = (int32_t)GetWindow()->GetWidth();
        sr.Bottom = (int32_t)GetWindow()->GetHeight();
        ctx->SetScissorRects(&sr, 1);

        // ---- Clear ----
        ClearColorValue clearColor = { 0.12f, 0.12f, 0.18f, 1.0f };
        ctx->ClearRenderTargetView(backBufferRTV, clearColor);
        ClearDepthStencilValue depthClear = { 1.0f, 0 };
        ctx->ClearDepthStencilView(GetDSV(), depthClear, 0x03);

        // ---- Setup pipeline (shared state) ----
        ctx->SetPipelineState(m_PipelineState.get());
        ctx->SetInputLayout(m_InputLayout.get());
        ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);

        // ---- Draw all scene objects (per-object shader switching) ----
        auto& objects = m_Scene.GetObjects();
        std::string lastShaderName; // Track last bound shader to avoid redundant switches
        for (size_t i = 0; i < objects.size(); i++)
        {
            auto& obj = objects[i];

            if (i >= m_GPUMeshes.size() || !m_GPUMeshes[i].VertexBuffer)
                continue;

            // --- Per-object shader switching ---
            const std::string& shaderName = obj.ShaderName;
            if (shaderName != lastShaderName)
            {
                CompiledShader* shader = m_ShaderLibrary.GetShader(shaderName);
                if (!shader) shader = m_ShaderLibrary.GetDefault();

                if (shader)
                {
                    // Unified: PSO for DX12, Set*Shader for DX11 — both are safe to call
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

            // Update constant buffer
            Mat4 worldMatrix = obj.TransformData.GetWorldMatrix();

            ConstantBufferData cbd = {};
            memcpy(cbd.WorldMatrix, worldMatrix.m, sizeof(worldMatrix.m));
            memcpy(cbd.ViewMatrix, m_ViewMatrix.m, sizeof(m_ViewMatrix.m));
            memcpy(cbd.ProjectionMatrix, m_ProjectionMatrix.m, sizeof(m_ProjectionMatrix.m));
            cbd.ObjectColor[0] = obj.Color.x;
            cbd.ObjectColor[1] = obj.Color.y;
            cbd.ObjectColor[2] = obj.Color.z;
            cbd.ObjectColor[3] = obj.Color.w;
            cbd.Selected = 0.0f; // No shader highlight; gizmo used instead

            void* mapped = m_ConstantBuffer->Map();
            if (mapped)
            {
                memcpy(mapped, &cbd, sizeof(cbd));
                m_ConstantBuffer->Unmap();
            }
            ctx->SetConstantBuffer(0, m_ConstantBuffer.get());

            ctx->DrawIndexed(gpuMesh.IndexCount, 0, 0);
        }

        // ---- Draw Gizmo for selected object ----
        DrawGizmo(ctx);

        // ---- ImGui ----
        device->ImGuiNewFrame();
        ImGui::NewFrame();

        DrawMenuBar();
        DrawRenderDocOverlay();
        DrawUI();

        ImGui::Render();
        device->ImGuiRenderDrawData(ctx);

        // ---- End frame (DX12: BackBuffer→Present barrier; DX11: no-op) ----
        ctx->EndFrame(swapChain);

        ctx->Flush();
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
        };
        m_InputLayout = device->CreateInputLayout(inputElements, 3, tempVS.get());

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

        // Init ImGui backend
        device->InitImGui(GetWindow()->GetHWND());
    }

    // DX12 PSO creation is now handled by ShaderLibrary

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
                ImGui::EndMenu();
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
            // Normal: deep blue (#384C6C), Hover: brighter blue (#4A6A9A), Active: darker (#2A3A52)
            ImVec4 btnColor    = capturing ? ImVec4(0.7f, 0.3f, 0.1f, 0.95f)  // orange when capturing
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

            // RenderDoc-style icon: a stylized lens/camera symbol
            // Using a circle + dot to mimic RenderDoc's lens icon
            const char* icon = capturing ? "..." : "RD";

            if (ImGui::Button(icon, ImVec2(btnSize, btnSize)))
            {
                if (!capturing)
                {
                    // Trigger capture and automatically open in RenderDoc
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

            // Draw the RenderDoc lens icon on top of the button using ImDrawList
            ImVec2 btnMin = ImGui::GetItemRectMin();
            ImVec2 btnMax = ImGui::GetItemRectMax();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 center = ImVec2((btnMin.x + btnMax.x) * 0.5f, (btnMin.y + btnMax.y) * 0.5f);

            // Outer ring (lens outline) - RenderDoc's signature look
            float outerR = btnSize * 0.35f;
            float innerR = btnSize * 0.18f;
            ImU32 white = IM_COL32(255, 255, 255, 220);
            drawList->AddCircle(center, outerR, white, 24, 2.0f);
            // Inner filled circle (lens center)
            drawList->AddCircleFilled(center, innerR, white);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);  // WindowPadding, WindowBorderSize
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
        for (auto& obj : m_Scene.GetObjects())
        {
            bool selected = obj.Selected;
            if (ImGui::Selectable(obj.Name.c_str(), &selected))
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
        ImGui::Text("Type: %s", PrimitiveTypeToString(sel->PrimitiveType));

        ImGui::Separator();
        ImGui::Text("Transform");

        bool changed = false;
        changed |= ImGui::DragFloat3("Position", &sel->TransformData.Position.x, 0.05f);
        changed |= ImGui::DragFloat3("Rotation", &sel->TransformData.Rotation.x, 1.0f, -360.0f, 360.0f);
        changed |= ImGui::DragFloat3("Scale",    &sel->TransformData.Scale.x, 0.05f, 0.01f, 100.0f);

        ImGui::Separator();
        ImGui::Text("Appearance");
        ImGui::ColorEdit4("Color", &sel->Color.x);

        ImGui::Separator();
        ImGui::Text("Shader");
        {
            const auto& shaderNames = m_ShaderLibrary.GetShaderNames();
            // Find current index
            int currentIdx = 0;
            for (int si = 0; si < (int)shaderNames.size(); si++)
            {
                if (shaderNames[si] == sel->ShaderName)
                {
                    currentIdx = si;
                    break;
                }
            }
            // Build combo items
            if (ImGui::BeginCombo("##ShaderCombo", sel->ShaderName.c_str()))
            {
                for (int si = 0; si < (int)shaderNames.size(); si++)
                {
                    bool isSelected = (si == currentIdx);
                    if (ImGui::Selectable(shaderNames[si].c_str(), isSelected))
                    {
                        sel->ShaderName = shaderNames[si];
                    }
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        ImGui::Separator();
        ImGui::Text("Mesh Info");
        ImGui::Text("  Vertices: %u", sel->MeshData.GetVertexCount());
        ImGui::Text("  Indices:  %u", sel->MeshData.GetIndexCount());
        ImGui::Text("  Triangles: %u", sel->MeshData.GetIndexCount() / 3);

        (void)changed;
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
                auto* obj = m_Scene.AddObject(entry.type);
                if (entry.type != EPrimitiveType::Floor)
                {
                    obj->TransformData.Position.y = 0.5f;
                    obj->TransformData.Position.x = (float)(rand() % 60 - 30) * 0.1f;
                    obj->TransformData.Position.z = (float)(rand() % 60 - 30) * 0.1f;
                }
                RebuildAllGPUBuffers();
                m_Scene.SelectObject(obj->ID);
            }
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

        // Gizmo always uses the Default shader (which has unlit mode when g_Selected > 1.5)
        CompiledShader* defaultShader = m_ShaderLibrary.GetDefault();
        if (defaultShader)
        {
            if (defaultShader->PSO)
                ctx->SetPipelineState(defaultShader->PSO.get());
            ctx->SetVertexShader(defaultShader->VertexShader.get());
            ctx->SetPixelShader(defaultShader->PixelShader.get());
        }

        Vec3 gizmoPos = sel->TransformData.Position;
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

        Vec3 gizmoPos = sel->TransformData.Position;
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

        for (auto& obj : m_Scene.GetObjects())
        {
            Vec3 aabbMin, aabbMax;
            ComputeWorldAABB(obj, aabbMin, aabbMax);

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
            auto& obj = objects[i];
            auto& gpu = m_GPUMeshes[i];

            gpu.VertexCount = obj.MeshData.GetVertexCount();
            gpu.IndexCount = obj.MeshData.GetIndexCount();

            if (gpu.VertexCount == 0 || gpu.IndexCount == 0) continue;

            BufferDesc vbDesc;
            vbDesc.SizeInBytes = gpu.VertexCount * sizeof(Vertex);
            vbDesc.BindFlags = BUFFER_USAGE_VERTEX;
            vbDesc.Usage = EResourceUsage::Immutable;
            gpu.VertexBuffer = device->CreateBuffer(vbDesc, obj.MeshData.GetVertices().data());

            BufferDesc ibDesc;
            ibDesc.SizeInBytes = gpu.IndexCount * sizeof(uint32_t);
            ibDesc.BindFlags = BUFFER_USAGE_INDEX;
            ibDesc.Usage = EResourceUsage::Immutable;
            gpu.IndexBuffer = device->CreateBuffer(ibDesc, obj.MeshData.GetIndices().data());
        }
    }

    // ============================================================
    // Members
    // ============================================================

    Scene m_Scene;
    std::vector<GPUMeshData> m_GPUMeshes;

    // Shader Library — manages all loaded shaders
    ShaderLibrary m_ShaderLibrary;
    std::string m_ShaderDir; // Path to Shaders/ folder

    // RHI resources (shared interface)
    std::unique_ptr<RHIInputLayout>   m_InputLayout;
    std::unique_ptr<RHIBuffer>        m_ConstantBuffer;
    std::unique_ptr<RHIPipelineState> m_PipelineState;  // DX11

    // Camera
    Mat4 m_ViewMatrix;
    Mat4 m_ProjectionMatrix;
    Vec3 m_CameraPosition;
    Vec3 m_CameraTarget;
    Vec3 m_CameraUp;

    // RenderDoc state
    bool m_CaptureTriggered = false;
    bool m_AutoOpenRenderDoc = false;
    uint32_t m_LastCaptureCount = 0;

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
