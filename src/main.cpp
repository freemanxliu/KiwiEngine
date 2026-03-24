#include "Core/Application.h"
#include "Core/Window.h"
#include "Scene/Mesh.h"
#include "Scene/Shaders.h"
#include "Scene/Scene.h"
#include "Scene/SceneObject.h"
#include "Math/Math.h"
#include "RHI/RHI.h"
#include "RHI/DX11/DX11Device.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include <iostream>
#include <memory>
#include <cmath>
#include <algorithm>

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
    // NDC coordinates
    float ndcX = (2.0f * mouseX / screenW) - 1.0f;
    float ndcY = 1.0f - (2.0f * mouseY / screenH);

    // In our row-major LH projection: x' = x * proj[0][0], y' = y * proj[1][1]
    // Unproject to view space
    float viewX = ndcX / proj.m[0][0];
    float viewY = ndcY / proj.m[1][1];

    // Ray in view space: origin at 0, direction (viewX, viewY, 1) for LH
    Vec3 rayDirView = { viewX, viewY, 1.0f };

    // We need the inverse of the view matrix to transform to world space
    // For a rigid view matrix, the inverse is:
    // R^T applied to direction, then add eye position
    // view matrix row-major:
    // [xAxis.x, yAxis.x, zAxis.x, 0]
    // [xAxis.y, yAxis.y, zAxis.y, 0]
    // [xAxis.z, yAxis.z, zAxis.z, 0]
    // [tx,      ty,      tz,      1]

    // Extracting axes from view matrix (transposed rotation part)
    Vec3 right  = { view.m[0][0], view.m[1][0], view.m[2][0] };
    Vec3 up     = { view.m[0][1], view.m[1][1], view.m[2][1] };
    Vec3 fwd    = { view.m[0][2], view.m[1][2], view.m[2][2] };

    // Camera position: solve for eye from translation
    Vec3 eye;
    eye.x = -(view.m[3][0] * right.x + view.m[3][1] * up.x + view.m[3][2] * fwd.x);
    eye.y = -(view.m[3][0] * right.y + view.m[3][1] * up.y + view.m[3][2] * fwd.y);
    eye.z = -(view.m[3][0] * right.z + view.m[3][1] * up.z + view.m[3][2] * fwd.z);

    // Transform ray direction to world space
    Vec3 rayDirWorld;
    rayDirWorld.x = rayDirView.x * right.x + rayDirView.y * up.x + rayDirView.z * fwd.x;
    rayDirWorld.y = rayDirView.x * right.y + rayDirView.y * up.y + rayDirView.z * fwd.y;
    rayDirWorld.z = rayDirView.x * right.z + rayDirView.y * up.z + rayDirView.z * fwd.z;

    return { eye, rayDirWorld.Normalize() };
}

// Simple AABB intersection
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

// Compute AABB for an object in world space
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
        // Transform vertex by world matrix (row-major, row vector * matrix)
        float wx = v.Position.x * world.m[0][0] + v.Position.y * world.m[1][0] + v.Position.z * world.m[2][0] + world.m[3][0];
        float wy = v.Position.x * world.m[0][1] + v.Position.y * world.m[1][1] + v.Position.z * world.m[2][1] + world.m[3][1];
        float wz = v.Position.x * world.m[0][2] + v.Position.y * world.m[1][2] + v.Position.z * world.m[2][2] + world.m[3][2];

        outMin.x = std::min(outMin.x, wx); outMin.y = std::min(outMin.y, wy); outMin.z = std::min(outMin.z, wz);
        outMax.x = std::max(outMax.x, wx); outMax.y = std::max(outMax.y, wy); outMax.z = std::max(outMax.z, wz);
    }
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
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

protected:
    void OnInit() override
    {
        std::cout << "[Kiwi] Initializing Scene Editor..." << std::endl;

        auto device = GetDevice();
        auto dx11Device = static_cast<DX11Device*>(device);

        // ---- Compile shaders ----
        m_VertexShader = dx11Device->CompileShader(
            EShaderType::Vertex, g_VertexShaderHLSL, "main", "vs_5_0");
        m_PixelShader = dx11Device->CompileShader(
            EShaderType::Pixel, g_PixelShaderHLSL, "main", "ps_5_0");

        // ---- Input layout ----
        InputElementDesc inputElements[] = {
            { "POSITION", 0, EFormat::R32G32B32_FLOAT, (uint32_t)offsetof(Vertex, Position), 0, 0 },
            { "NORMAL",   0, EFormat::R32G32B32_FLOAT, (uint32_t)offsetof(Vertex, Normal),   0, 0 },
            { "COLOR",    0, EFormat::R32G32B32A32_FLOAT, (uint32_t)offsetof(Vertex, Color), 0, 0 },
        };
        m_InputLayout = device->CreateInputLayout(inputElements, _countof(inputElements), m_VertexShader.get());

        // ---- Constant buffer ----
        BufferDesc cbDesc;
        cbDesc.SizeInBytes = sizeof(ConstantBufferData);
        cbDesc.BindFlags = BUFFER_USAGE_CONSTANT;
        cbDesc.Usage = EResourceUsage::Dynamic;
        m_ConstantBuffer = device->CreateBuffer(cbDesc);

        // ---- Pipeline state ----
        m_PipelineState = device->CreatePipelineState();

        // ---- Camera ----
        m_CameraPosition = Vec3(0.0f, 3.0f, -6.0f);
        m_CameraTarget = Vec3(0.0f, 0.5f, 0.0f);
        m_CameraUp = Vec3(0.0f, 1.0f, 0.0f);

        m_ViewMatrix = Mat4::LookAt(m_CameraPosition, m_CameraTarget, m_CameraUp);

        uint32_t w = GetWindow()->GetWidth();
        uint32_t h = GetWindow()->GetHeight();
        float aspect = (float)w / (float)h;
        m_ProjectionMatrix = Mat4::Perspective(DegToRad(45.0f), aspect, 0.1f, 100.0f);

        // Resize callback
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

        // Create GPU buffers for initial objects
        RebuildAllGPUBuffers();

        // ---- Init ImGui ----
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();

        // ImGui DX11 backend
        ImGui_ImplWin32_Init(GetWindow()->GetHWND());
        ImGui_ImplDX11_Init(
            dx11Device->GetD3DDevice(),
            dx11Device->GetD3DContext());

        std::cout << "[Kiwi] Scene Editor initialized!" << std::endl;
    }

    void OnUpdate(float deltaTime) override
    {
        (void)deltaTime;

        // Handle mouse picking (only when ImGui doesn't want the mouse)
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureMouse)
        {
            const auto& mouse = GetWindow()->GetMouseState();
            if (mouse.LeftClicked)
            {
                PickObject(mouse.X, mouse.Y);
            }
        }
    }

    void OnRender() override
    {
        auto ctx = GetContext();
        auto swapChain = GetSwapChain();

        // ---- Set render targets ----
        auto backBufferRTV = swapChain->GetBackBufferRTV(swapChain->GetCurrentBackBufferIndex());
        ctx->SetRenderTargets(&backBufferRTV, 1, GetDSV());

        // ---- Clear ----
        ClearColorValue clearColor = { 0.12f, 0.12f, 0.18f, 1.0f };
        ctx->ClearRenderTargetView(backBufferRTV, clearColor);
        ClearDepthStencilValue depthClear = { 1.0f, 0 };
        ctx->ClearDepthStencilView(GetDSV(), depthClear, 0x03);

        // ---- Setup pipeline ----
        ctx->SetPipelineState(m_PipelineState.get());
        ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);
        ctx->SetVertexShader(m_VertexShader.get());
        ctx->SetPixelShader(m_PixelShader.get());
        ctx->SetInputLayout(m_InputLayout.get());

        // ---- Draw all scene objects ----
        auto& objects = m_Scene.GetObjects();
        for (size_t i = 0; i < objects.size(); i++)
        {
            auto& obj = objects[i];

            // Check if GPU buffers exist
            if (i >= m_GPUMeshes.size() || !m_GPUMeshes[i].VertexBuffer)
                continue;

            auto& gpuMesh = m_GPUMeshes[i];

            // Vertex buffer
            VertexBufferView vbView;
            vbView.BufferLocation = 0;
            vbView.SizeInBytes = gpuMesh.VertexCount * sizeof(Vertex);
            vbView.StrideInBytes = sizeof(Vertex);

            RHIBuffer* vbPtr = gpuMesh.VertexBuffer.get();
            ctx->SetVertexBuffers(0, &vbPtr, &vbView, 1);

            // Index buffer
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
            cbd.Selected = obj.Selected ? 1.0f : 0.0f;

            void* mapped = m_ConstantBuffer->Map();
            if (mapped)
            {
                memcpy(mapped, &cbd, sizeof(cbd));
                m_ConstantBuffer->Unmap();
            }
            ctx->SetConstantBuffer(0, m_ConstantBuffer.get());

            // Draw
            ctx->DrawIndexed(gpuMesh.IndexCount, 0, 0);
        }

        // ---- ImGui Render ----
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        DrawUI();

        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        ctx->Flush();
    }

private:

    // ============================================================
    // UI
    // ============================================================

    void DrawUI()
    {
        // Side panel
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(320, (float)GetWindow()->GetHeight()), ImGuiCond_Always);

        ImGui::Begin("Scene Panel", nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

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
        ImGui::Text("Shader: Default Phong");
        ImGui::TextWrapped("Vertex: vs_5_0 | Pixel: ps_5_0\nLambert Diffuse + Blinn-Phong Specular");

        ImGui::Separator();
        ImGui::Text("Mesh Info");
        ImGui::Text("  Vertices: %u", sel->MeshData.GetVertexCount());
        ImGui::Text("  Indices:  %u", sel->MeshData.GetIndexCount());
        ImGui::Text("  Triangles: %u", sel->MeshData.GetIndexCount() / 3);

        (void)changed; // Transform changes apply live
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
                // Place new object slightly above ground
                if (entry.type != EPrimitiveType::Floor)
                {
                    obj->TransformData.Position.y = 0.5f;
                    // Offset randomly to avoid overlap
                    obj->TransformData.Position.x = (float)(rand() % 60 - 30) * 0.1f;
                    obj->TransformData.Position.z = (float)(rand() % 60 - 30) * 0.1f;
                }
                RebuildAllGPUBuffers();
                m_Scene.SelectObject(obj->ID);
            }
        }
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

    // RHI resources
    std::unique_ptr<RHIShader>        m_VertexShader;
    std::unique_ptr<RHIShader>        m_PixelShader;
    std::unique_ptr<RHIInputLayout>   m_InputLayout;
    std::unique_ptr<RHIBuffer>        m_ConstantBuffer;
    std::unique_ptr<RHIPipelineState> m_PipelineState;

    // Camera
    Mat4 m_ViewMatrix;
    Mat4 m_ProjectionMatrix;
    Vec3 m_CameraPosition;
    Vec3 m_CameraTarget;
    Vec3 m_CameraUp;
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
        std::cout << "========================================" << std::endl;
        std::cout << std::endl;

        KiwiEngineApp app;
        app.Run();

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
