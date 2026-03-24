#include "Core/Application.h"
#include "Core/Window.h"
#include "Scene/Mesh.h"
#include "Scene/Shaders.h"
#include "Scene/Scene.h"
#include "Scene/SceneObject.h"
#include "Math/Math.h"
#include "RHI/RHI.h"
#include "RHI/DX11/DX11Device.h"
#include "RHI/DX12/DX12Device.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_dx12.h>

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
                PickObject(mouse.X, mouse.Y);
            }
        }
    }

    void OnRender() override
    {
        auto ctx = GetContext();
        auto swapChain = GetSwapChain();
        bool isDX12 = (GetCurrentRHIType() == RHI_API_TYPE::DX12);

        // ---- DX12: Reset command list and transition back buffer ----
        if (isDX12)
        {
            auto dx12Ctx = static_cast<DX12CommandContext*>(ctx);
            dx12Ctx->Reset();

            // Set root signature and descriptor heaps
            auto dx12Device = static_cast<DX12Device*>(GetDevice());
            dx12Ctx->SetRootSignature(dx12Device->GetRootSignature());
            ID3D12DescriptorHeap* heaps[] = { dx12Device->GetSRVHeap() };
            dx12Ctx->SetDescriptorHeaps(heaps, 1);

            // Transition back buffer to render target
            auto backBuffer = swapChain->GetBackBuffer(swapChain->GetCurrentBackBufferIndex());
            dx12Ctx->ResourceBarrier(backBuffer,
                D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        }

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

        // ---- Setup pipeline ----
        if (isDX12 && m_DX12PSO)
        {
            ctx->SetPipelineState(m_DX12PSO.get());
        }
        else if (!isDX12)
        {
            ctx->SetPipelineState(m_PipelineState.get());
            ctx->SetVertexShader(m_VertexShader.get());
            ctx->SetPixelShader(m_PixelShader.get());
            ctx->SetInputLayout(m_InputLayout.get());
        }
        ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);

        // ---- Draw all scene objects ----
        auto& objects = m_Scene.GetObjects();
        for (size_t i = 0; i < objects.size(); i++)
        {
            auto& obj = objects[i];

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

            ctx->DrawIndexed(gpuMesh.IndexCount, 0, 0);
        }

        // ---- ImGui Render ----
        if (isDX12)
        {
            ImGui_ImplDX12_NewFrame();
        }
        else
        {
            ImGui_ImplDX11_NewFrame();
        }
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        DrawMenuBar();
        DrawUI();

        ImGui::Render();

        if (isDX12)
        {
            auto dx12Ctx = static_cast<DX12CommandContext*>(ctx);
            auto dx12Device = static_cast<DX12Device*>(GetDevice());
            ID3D12DescriptorHeap* heaps[] = { dx12Device->GetSRVHeap() };
            dx12Ctx->GetCommandList()->SetDescriptorHeaps(1, heaps);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dx12Ctx->GetCommandList());

            // Transition back buffer to present
            auto backBuffer = swapChain->GetBackBuffer(swapChain->GetCurrentBackBufferIndex());
            dx12Ctx->ResourceBarrier(backBuffer,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        }
        else
        {
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }

        ctx->Flush();
    }

    // ---- RHI Switch callbacks ----

    void OnRHIShutdown() override
    {
        std::cout << "[Kiwi] Releasing GPU resources for RHI switch..." << std::endl;

        // Release all GPU resources
        m_GPUMeshes.clear();
        m_ConstantBuffer.reset();
        m_VertexShader.reset();
        m_PixelShader.reset();
        m_InputLayout.reset();
        m_PipelineState.reset();
        m_DX12PSO.reset();

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
    }

private:

    // ============================================================
    // ImGui backend management
    // ============================================================

    void ShutdownImGui()
    {
        if (m_ImGuiDX11Initialized)
        {
            ImGui_ImplDX11_Shutdown();
            m_ImGuiDX11Initialized = false;
        }
        if (m_ImGuiDX12Initialized)
        {
            ImGui_ImplDX12_Shutdown();
            m_ImGuiDX12Initialized = false;
        }
        if (m_ImGuiWin32Initialized)
        {
            ImGui_ImplWin32_Shutdown();
            m_ImGuiWin32Initialized = false;
        }
    }

    void InitImGuiForDX11()
    {
        auto dx11Device = static_cast<DX11Device*>(GetDevice());

        if (!m_ImGuiWin32Initialized)
        {
            ImGui_ImplWin32_Init(GetWindow()->GetHWND());
            m_ImGuiWin32Initialized = true;
        }

        ImGui_ImplDX11_Init(dx11Device->GetD3DDevice(), dx11Device->GetD3DContext());
        m_ImGuiDX11Initialized = true;
    }

    void InitImGuiForDX12()
    {
        auto dx12Device = static_cast<DX12Device*>(GetDevice());

        if (!m_ImGuiWin32Initialized)
        {
            ImGui_ImplWin32_Init(GetWindow()->GetHWND());
            m_ImGuiWin32Initialized = true;
        }

        ImGui_ImplDX12_Init(
            dx12Device->GetD3DDevice(),
            2, // num frames in flight
            DXGI_FORMAT_R8G8B8A8_UNORM,
            dx12Device->GetSRVHeap(),
            dx12Device->GetSRVHeap()->GetCPUDescriptorHandleForHeapStart(),
            dx12Device->GetSRVHeap()->GetGPUDescriptorHandleForHeapStart());
        m_ImGuiDX12Initialized = true;
    }

    // ============================================================
    // RHI Resource Initialization
    // ============================================================

    void InitRHIResources()
    {
        auto device = GetDevice();
        bool isDX12 = (GetCurrentRHIType() == RHI_API_TYPE::DX12);

        if (isDX12)
        {
            auto dx12Device = static_cast<DX12Device*>(device);

            // Compile shaders
            m_VertexShader = dx12Device->CompileShader(
                EShaderType::Vertex, g_VertexShaderHLSL, "main", "vs_5_0");
            m_PixelShader = dx12Device->CompileShader(
                EShaderType::Pixel, g_PixelShaderHLSL, "main", "ps_5_0");

            // Create input layout
            InputElementDesc inputElements[] = {
                { "POSITION", 0, EFormat::R32G32B32_FLOAT, (uint32_t)offsetof(Vertex, Position), 0, 0 },
                { "NORMAL",   0, EFormat::R32G32B32_FLOAT, (uint32_t)offsetof(Vertex, Normal),   0, 0 },
                { "COLOR",    0, EFormat::R32G32B32A32_FLOAT, (uint32_t)offsetof(Vertex, Color), 0, 0 },
            };
            m_InputLayout = device->CreateInputLayout(inputElements, 3, m_VertexShader.get());

            // Create DX12 PSO
            CreateDX12PSO();

            // Constant buffer
            BufferDesc cbDesc;
            cbDesc.SizeInBytes = sizeof(ConstantBufferData);
            cbDesc.BindFlags = BUFFER_USAGE_CONSTANT;
            cbDesc.Usage = EResourceUsage::Dynamic;
            m_ConstantBuffer = device->CreateBuffer(cbDesc);

            // Init ImGui for DX12
            InitImGuiForDX12();
        }
        else
        {
            auto dx11Device = static_cast<DX11Device*>(device);

            // Compile shaders
            m_VertexShader = dx11Device->CompileShader(
                EShaderType::Vertex, g_VertexShaderHLSL, "main", "vs_5_0");
            m_PixelShader = dx11Device->CompileShader(
                EShaderType::Pixel, g_PixelShaderHLSL, "main", "ps_5_0");

            // Input layout
            InputElementDesc inputElements[] = {
                { "POSITION", 0, EFormat::R32G32B32_FLOAT, (uint32_t)offsetof(Vertex, Position), 0, 0 },
                { "NORMAL",   0, EFormat::R32G32B32_FLOAT, (uint32_t)offsetof(Vertex, Normal),   0, 0 },
                { "COLOR",    0, EFormat::R32G32B32A32_FLOAT, (uint32_t)offsetof(Vertex, Color), 0, 0 },
            };
            m_InputLayout = device->CreateInputLayout(inputElements, 3, m_VertexShader.get());

            // Constant buffer
            BufferDesc cbDesc;
            cbDesc.SizeInBytes = sizeof(ConstantBufferData);
            cbDesc.BindFlags = BUFFER_USAGE_CONSTANT;
            cbDesc.Usage = EResourceUsage::Dynamic;
            m_ConstantBuffer = device->CreateBuffer(cbDesc);

            // Pipeline state
            m_PipelineState = device->CreatePipelineState();

            // Init ImGui for DX11
            InitImGuiForDX11();
        }
    }

    void CreateDX12PSO()
    {
        auto dx12Device = static_cast<DX12Device*>(GetDevice());
        auto dx12InputLayout = static_cast<DX12InputLayout*>(m_InputLayout.get());
        auto vsShader = static_cast<DX12Shader*>(m_VertexShader.get());
        auto psShader = static_cast<DX12Shader*>(m_PixelShader.get());

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = dx12Device->GetRootSignature();

        // Shaders
        psoDesc.VS.pShaderBytecode = vsShader->GetBlob()->GetBufferPointer();
        psoDesc.VS.BytecodeLength = vsShader->GetBlob()->GetBufferSize();
        psoDesc.PS.pShaderBytecode = psShader->GetBlob()->GetBufferPointer();
        psoDesc.PS.BytecodeLength = psShader->GetBlob()->GetBufferSize();

        // Input layout
        const auto& elements = dx12InputLayout->GetElements();
        psoDesc.InputLayout.pInputElementDescs = elements.data();
        psoDesc.InputLayout.NumElements = (UINT)elements.size();

        // Rasterizer
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
        psoDesc.RasterizerState.DepthClipEnable = TRUE;

        // Blend
        psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        // Depth stencil
        psoDesc.DepthStencilState.DepthEnable = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        psoDesc.DepthStencilState.StencilEnable = FALSE;

        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        psoDesc.SampleDesc.Count = 1;

        ComPtr<ID3D12PipelineState> pso;
        HRESULT hr = dx12Device->GetD3DDevice()->CreateGraphicsPipelineState(
            &psoDesc, IID_PPV_ARGS(&pso));
        if (FAILED(hr))
        {
            char msg[256];
            snprintf(msg, sizeof(msg), "Failed to create DX12 PSO (HRESULT: 0x%08X)", hr);
            throw std::runtime_error(msg);
        }

        m_DX12PSO = std::make_unique<DX12PipelineState>(pso.Get());
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
                    bool isDX11 = (GetCurrentRHIType() == RHI_API_TYPE::DX11);
                    bool isDX12 = (GetCurrentRHIType() == RHI_API_TYPE::DX12);

                    if (ImGui::MenuItem("Direct3D 11", nullptr, isDX11, !isDX11))
                    {
                        m_PendingRHISwitch = true;
                        m_PendingRHIType = RHI_API_TYPE::DX11;
                    }
                    if (ImGui::MenuItem("Direct3D 12", nullptr, isDX12, !isDX12))
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
        ImGui::Text("Shader: Default Phong");
        ImGui::TextWrapped("Vertex: vs_5_0 | Pixel: ps_5_0\nLambert Diffuse + Blinn-Phong Specular");

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

    // RHI resources (shared interface)
    std::unique_ptr<RHIShader>        m_VertexShader;
    std::unique_ptr<RHIShader>        m_PixelShader;
    std::unique_ptr<RHIInputLayout>   m_InputLayout;
    std::unique_ptr<RHIBuffer>        m_ConstantBuffer;
    std::unique_ptr<RHIPipelineState> m_PipelineState;  // DX11
    std::unique_ptr<DX12PipelineState> m_DX12PSO;        // DX12

    // Camera
    Mat4 m_ViewMatrix;
    Mat4 m_ProjectionMatrix;
    Vec3 m_CameraPosition;
    Vec3 m_CameraTarget;
    Vec3 m_CameraUp;

    // ImGui state tracking
    bool m_ImGuiWin32Initialized = false;
    bool m_ImGuiDX11Initialized = false;
    bool m_ImGuiDX12Initialized = false;
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
