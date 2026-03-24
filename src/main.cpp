#include "Core/Application.h"
#include "Core/Window.h"
#include "Scene/Mesh.h"
#include "Scene/Shaders.h"
#include "Math/Math.h"
#include "RHI/RHI.h"
#include "RHI/DX11/DX11Device.h"

#include <iostream>
#include <memory>

using namespace Kiwi;

// ============================================================
// KiwiEngineApp - 主应用程序
// ============================================================

class KiwiEngineApp : public Application
{
public:
    KiwiEngineApp()
        : Application(
            WindowDesc{ "Kiwi Engine - DX11 RHI", 1280, 720 },
            RHIInitParams{ RHI_API_TYPE::DX11, true })
    {
    }

protected:
    void OnInit() override
    {
        std::cout << "[Kiwi] Initializing KiwiEngine..." << std::endl;
        std::cout << "[Kiwi] RHI Backend: DX11" << std::endl;

        // ---- 编译着色器 ----
        auto device = GetDevice();

        std::cout << "[Kiwi] Compiling shaders..." << std::endl;

        m_VertexShader = static_cast<DX11Device*>(device)->CompileShader(
            EShaderType::Vertex,
            g_VertexShaderHLSL,
            "main",
            "vs_5_0");

        m_PixelShader = static_cast<DX11Device*>(device)->CompileShader(
            EShaderType::Pixel,
            g_PixelShaderHLSL,
            "main",
            "ps_5_0");

        std::cout << "[Kiwi] Shaders compiled successfully." << std::endl;

        // ---- 创建输入布局 ----
        InputElementDesc inputElements[] = {
            { "POSITION", 0, EFormat::R32G32B32_FLOAT, (uint32_t)offsetof(Vertex, Position), 0, 0 },
            { "NORMAL",   0, EFormat::R32G32B32_FLOAT, (uint32_t)offsetof(Vertex, Normal),   0, 0 },
            { "COLOR",    0, EFormat::R32G32B32A32_FLOAT, (uint32_t)offsetof(Vertex, Color), 0, 0 },
        };

        m_InputLayout = device->CreateInputLayout(
            inputElements, _countof(inputElements),
            m_VertexShader.get());

        // ---- 创建 Cube 网格 ----
        std::cout << "[Kiwi] Creating cube mesh..." << std::endl;
        m_Mesh = Mesh::CreateCube(1.5f);
        std::cout << "[Kiwi] Cube: " << m_Mesh.GetVertexCount() << " vertices, "
                  << m_Mesh.GetIndexCount() << " indices." << std::endl;

        // ---- 创建顶点缓冲 ----
        BufferDesc vbDesc;
        vbDesc.SizeInBytes = m_Mesh.GetVertexCount() * sizeof(Vertex);
        vbDesc.BindFlags = BUFFER_USAGE_VERTEX;
        vbDesc.Usage = EResourceUsage::Immutable;

        m_VertexBuffer = device->CreateBuffer(
            vbDesc, m_Mesh.GetVertices().data());

        // ---- 创建索引缓冲 ----
        BufferDesc ibDesc;
        ibDesc.SizeInBytes = m_Mesh.GetIndexCount() * sizeof(uint32_t);
        ibDesc.BindFlags = BUFFER_USAGE_INDEX;
        ibDesc.Usage = EResourceUsage::Immutable;

        m_IndexBuffer = device->CreateBuffer(
            ibDesc, m_Mesh.GetIndices().data());

        // ---- 创建常量缓冲 ----
        BufferDesc cbDesc;
        cbDesc.SizeInBytes = sizeof(ConstantBufferData);
        cbDesc.BindFlags = BUFFER_USAGE_CONSTANT;
        cbDesc.Usage = EResourceUsage::Dynamic;

        m_ConstantBuffer = device->CreateBuffer(cbDesc);

        // ---- 创建管线状态 ----
        m_PipelineState = device->CreatePipelineState();

        // ---- 初始化变换矩阵 ----
        m_CameraPosition = Vec3(0.0f, 1.5f, -4.0f);
        m_CameraTarget = Vec3(0.0f, 0.0f, 0.0f);
        m_CameraUp = Vec3(0.0f, 1.0f, 0.0f);

        m_ViewMatrix = Mat4::LookAt(m_CameraPosition, m_CameraTarget, m_CameraUp);

        uint32_t w = GetWindow()->GetWidth();
        uint32_t h = GetWindow()->GetHeight();
        float aspect = (float)w / (float)h;
        m_ProjectionMatrix = Mat4::Perspective(
            DegToRad(45.0f), aspect, 0.1f, 100.0f);

        m_RotationAngle = 0.0f;

        // 设置 resize 回调
        GetWindow()->SetResizeCallback([this](uint32_t width, uint32_t height) {
            float aspect = (float)width / (float)height;
            m_ProjectionMatrix = Mat4::Perspective(
                DegToRad(45.0f), aspect, 0.1f, 100.0f);
        });

        std::cout << "[Kiwi] Initialization complete!" << std::endl;
    }

    void OnUpdate(float deltaTime) override
    {
        // 旋转 Cube
        m_RotationAngle += deltaTime * 45.0f; // 45度/秒

        // 计算世界矩阵
        Mat4 rotationX = Mat4::RotationX(DegToRad(m_RotationAngle));
        Mat4 rotationY = Mat4::RotationY(DegToRad(m_RotationAngle * 0.7f));
        Mat4 rotationZ = Mat4::RotationZ(DegToRad(m_RotationAngle * 0.3f));
        m_WorldMatrix = rotationZ * rotationX * rotationY;
    }

    void OnRender() override
    {
        auto ctx = GetContext();
        auto swapChain = GetSwapChain();

        // ---- 设置渲染目标 ----
        auto backBufferRTV = swapChain->GetBackBufferRTV(swapChain->GetCurrentBackBufferIndex());

        ctx->SetRenderTargets(&backBufferRTV, 1, GetDSV());

        // ---- 清除 ----
        ClearColorValue clearColor = { 0.1f, 0.1f, 0.15f, 1.0f };
        ctx->ClearRenderTargetView(backBufferRTV, clearColor);

        ClearDepthStencilValue depthClear = { 1.0f, 0 };
        ctx->ClearDepthStencilView(GetDSV(), depthClear, 0x03);

        // ---- 设置管线 ----
        ctx->SetPipelineState(m_PipelineState.get());
        ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);

        // ---- 设置着色器 ----
        ctx->SetVertexShader(m_VertexShader.get());
        ctx->SetPixelShader(m_PixelShader.get());

        // ---- 设置输入布局 ----
        ctx->SetInputLayout(m_InputLayout.get());

        // ---- 设置顶点缓冲 ----
        VertexBufferView vbView;
        vbView.BufferLocation = 0;
        vbView.SizeInBytes = m_Mesh.GetVertexCount() * sizeof(Vertex);
        vbView.StrideInBytes = sizeof(Vertex);

        RHIBuffer* vbPtr = m_VertexBuffer.get();
        ctx->SetVertexBuffers(0, &vbPtr, &vbView, 1);

        // ---- 设置索引缓冲 ----
        IndexBufferView ibView;
        ibView.BufferLocation = 0;
        ibView.SizeInBytes = m_Mesh.GetIndexCount() * sizeof(uint32_t);
        ibView.Format = EFormat::R32_UINT; // 32-bit index

        ctx->SetIndexBuffer(m_IndexBuffer.get(), &ibView);

        // ---- 更新常量缓冲 ----
        ConstantBufferData cbd;

        // 拷贝矩阵到常量缓冲（row-major）
        memcpy(cbd.WorldMatrix, m_WorldMatrix.m, sizeof(m_WorldMatrix.m));
        memcpy(cbd.ViewMatrix, m_ViewMatrix.m, sizeof(m_ViewMatrix.m));
        memcpy(cbd.ProjectionMatrix, m_ProjectionMatrix.m, sizeof(m_ProjectionMatrix.m));

        void* mapped = m_ConstantBuffer->Map();
        if (mapped)
        {
            memcpy(mapped, &cbd, sizeof(cbd));
            m_ConstantBuffer->Unmap();
        }

        ctx->SetConstantBuffer(0, m_ConstantBuffer.get());

        // ---- 绘制 ----
        ctx->DrawIndexed(m_Mesh.GetIndexCount(), 0, 0);

        // ---- 提交命令 ----
        ctx->Flush();
    }

private:
    // 网格
    Mesh m_Mesh;

    // RHI 资源
    std::unique_ptr<RHIShader>          m_VertexShader;
    std::unique_ptr<RHIShader>          m_PixelShader;
    std::unique_ptr<RHIInputLayout>     m_InputLayout;
    std::unique_ptr<RHIBuffer>          m_VertexBuffer;
    std::unique_ptr<RHIBuffer>          m_IndexBuffer;
    std::unique_ptr<RHIBuffer>          m_ConstantBuffer;
    std::unique_ptr<RHIPipelineState>   m_PipelineState;

    // 变换矩阵
    Mat4 m_WorldMatrix;
    Mat4 m_ViewMatrix;
    Mat4 m_ProjectionMatrix;

    // 相机
    Vec3 m_CameraPosition;
    Vec3 m_CameraTarget;
    Vec3 m_CameraUp;

    // 动画
    float m_RotationAngle;
};

// ============================================================
// Main
// ============================================================

int main()
{
    try
    {
        std::cout << "========================================" << std::endl;
        std::cout << "  Kiwi Engine - DX11 RHI Renderer" << std::endl;
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
