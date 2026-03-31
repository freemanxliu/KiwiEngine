#include "Core/Application.h"
#include "Core/Window.h"
#include "Core/EngineConfig.h"
#include "Core/EditorInput.h"
#include "Scene/Mesh.h"
#include "Scene/Shaders.h"
#include "Scene/GLShaders.h"
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
#include "Scene/TextureManager.h"
#include "Scene/Material.h"
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
#include <shellapi.h>

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
// Gizmo — Translation / Rotation / Scale
// ============================================================

enum class EGizmoAxis { None = 0, X, Y, Z };
enum class EGizmoMode { Translate = 0, Rotate, Scale };

struct GizmoMeshData
{
    std::vector<Vertex> Vertices;
    std::vector<uint32_t> Indices;
};

// ---- Generate a torus ring around the given normal axis (for rotation gizmo) ----
static GizmoMeshData CreateGizmoRing(const Vec3& axisNormal, const Vec4& color,
                                     float ringRadius = 1.0f, float tubeRadius = 0.03f,
                                     uint32_t ringSegs = 40, uint32_t tubeSegs = 8)
{
    GizmoMeshData gizmo;

    // Build orthonormal basis: axisNormal is the ring's "up"
    Vec3 N = axisNormal;
    Vec3 T; // tangent in ring plane
    if (std::abs(N.y) < 0.99f)
        T = Vec3(0, 1, 0).Cross(N).Normalize();
    else
        T = Vec3(1, 0, 0).Cross(N).Normalize();
    Vec3 B = N.Cross(T).Normalize();

    auto addVert = [&](Vec3 pos, Vec3 norm)
    {
        gizmo.Vertices.push_back({ pos, norm, { 1.0f, 1.0f, 1.0f, 1.0f } });
    };

    // Ring: for each ring segment, build a small tube cross-section
    for (uint32_t i = 0; i <= ringSegs; i++)
    {
        float phi = (float)i / ringSegs * 2.0f * PI;
        float cp = cosf(phi), sp = sinf(phi);
        Vec3 ringCenter = T * (ringRadius * cp) + B * (ringRadius * sp);
        // Outward radial direction in ring plane
        Vec3 radial = (T * cp + B * sp).Normalize();

        for (uint32_t j = 0; j <= tubeSegs; j++)
        {
            float theta = (float)j / tubeSegs * 2.0f * PI;
            float ct = cosf(theta), st = sinf(theta);
            Vec3 tubeDir = radial * ct + N * st;
            Vec3 pos = ringCenter + tubeDir * tubeRadius;
            addVert(pos, tubeDir);
        }
    }

    // Indices
    uint32_t stride = tubeSegs + 1;
    for (uint32_t i = 0; i < ringSegs; i++)
    {
        for (uint32_t j = 0; j < tubeSegs; j++)
        {
            uint32_t a = i * stride + j;
            uint32_t b = (i + 1) * stride + j;
            gizmo.Indices.push_back(a);
            gizmo.Indices.push_back(b);
            gizmo.Indices.push_back(a + 1);
            gizmo.Indices.push_back(a + 1);
            gizmo.Indices.push_back(b);
            gizmo.Indices.push_back(b + 1);
        }
    }
    return gizmo;
}

// ---- Generate a scale axis: shaft cylinder + cube end cap ----
static GizmoMeshData CreateGizmoScaleAxis(const Vec3& axisDir, const Vec4& color,
                                          float length = 1.2f, float shaftRadius = 0.02f,
                                          float cubeHalf = 0.07f)
{
    GizmoMeshData gizmo;
    const uint32_t segments = 12;
    float shaftLength = length - cubeHalf * 2.0f;

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

    // Shaft (cylinder)
    for (uint32_t i = 0; i <= segments; i++)
    {
        float theta = (float)i / segments * 2.0f * PI;
        float ct = cosf(theta), st = sinf(theta);
        Vec3 circleDir = right * ct + forward * st;
        Vec3 bottom = circleDir * shaftRadius;
        Vec3 top    = circleDir * shaftRadius + up * shaftLength;
        addVert(bottom, circleDir);
        addVert(top,    circleDir);
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

    // Cube end cap (6 faces of a box at the end)
    Vec3 c = up * length; // cube center
    Vec3 axes[3] = { right, up, forward };
    for (int face = 0; face < 6; face++)
    {
        int dim   = face / 2;
        float sign = (face % 2 == 0) ? 1.0f : -1.0f;
        Vec3 norm = axes[dim] * sign;
        Vec3 t1   = axes[(dim + 1) % 3];
        Vec3 t2   = axes[(dim + 2) % 3];
        uint32_t base = (uint32_t)gizmo.Vertices.size();
        addVert(c + norm * cubeHalf - t1 * cubeHalf - t2 * cubeHalf, norm);
        addVert(c + norm * cubeHalf + t1 * cubeHalf - t2 * cubeHalf, norm);
        addVert(c + norm * cubeHalf + t1 * cubeHalf + t2 * cubeHalf, norm);
        addVert(c + norm * cubeHalf - t1 * cubeHalf + t2 * cubeHalf, norm);
        gizmo.Indices.push_back(base);     gizmo.Indices.push_back(base + 1); gizmo.Indices.push_back(base + 2);
        gizmo.Indices.push_back(base);     gizmo.Indices.push_back(base + 2); gizmo.Indices.push_back(base + 3);
    }
    return gizmo;
}





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
    return (c * d - b * e) / denom;
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
            RHIInitParams{ GetDefaultRHIType(), true })
    {
    }

    static RHI_API_TYPE GetDefaultRHIType()
    {
        auto& config = Kiwi::EngineConfig::Get();
        std::string rhi = config.GetString("Rendering", "DefaultRHI", "DX11");
        std::cout << "[Kiwi] DefaultRHI from config: '" << rhi << "'" << std::endl;
        if (rhi == "DX12" || rhi == "dx12")   return RHI_API_TYPE::DX12;
        if (rhi == "OpenGL" || rhi == "opengl" || rhi == "OPENGL") return RHI_API_TYPE::OPENGL;
        if (rhi == "Vulkan" || rhi == "vulkan" || rhi == "VULKAN") return RHI_API_TYPE::VULKAN;
        return RHI_API_TYPE::DX11;
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

            // Scenes directory (same discovery logic)
            m_ScenesDir = exeDir + "\\Scenes";
            if (!fs::exists(m_ScenesDir))
            {
                std::string fallback = exeDir + "\\..\\..\\Scenes";
                if (fs::exists(fallback))
                    m_ScenesDir = fallback;
                else
                {
                    // Create Scenes/ next to the source tree
                    m_ScenesDir = fallback;
                    fs::create_directories(m_ScenesDir);
                }
            }
            std::cout << "[Kiwi] Scenes directory: " << m_ScenesDir << std::endl;

            // Textures directory
            m_TexturesDir = exeDir + "\\Textures";
            if (!fs::exists(m_TexturesDir))
            {
                std::string fallback = exeDir + "\\..\\..\\Textures";
                if (fs::exists(fallback))
                    m_TexturesDir = fallback;
                else
                {
                    m_TexturesDir = fallback;
                    fs::create_directories(m_TexturesDir);
                }
            }

            // GLShaders directory
            m_GLShaderDir = exeDir + "\\GLShaders";
            if (!fs::exists(m_GLShaderDir))
            {
                std::string fallback = exeDir + "\\..\\..\\GLShaders";
                if (fs::exists(fallback))
                    m_GLShaderDir = fallback;
            }

            // Materials directory
            m_MaterialsDir = exeDir + "\\Materials";
            if (!fs::exists(m_MaterialsDir))
            {
                std::string fallback = exeDir + "\\..\\..\\Materials";
                if (fs::exists(fallback))
                    m_MaterialsDir = fallback;
                else
                {
                    m_MaterialsDir = fallback;
                    fs::create_directories(m_MaterialsDir);
                }
            }
        }

        // ---- Init ImGui context (once) ----
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Docking support
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Multi-viewport: drag windows outside main window
        ImGui::StyleColorsDark();

        // ---- Init RHI-specific resources ----
        InitRHIResources();

        // ---- Load default scene from file ----
        {
            namespace fs = std::filesystem;
            std::string defaultScene = m_ScenesDir + "\\Default.json";
            if (fs::exists(defaultScene))
            {
                m_Scene.LoadFromFile(defaultScene);
                std::cout << "[Kiwi] Loaded default scene: " << defaultScene << std::endl;
            }
            else
            {
                // First run: create a minimal default scene in-memory
                m_Scene.SetName("Default Scene");

                auto* camObj = m_Scene.AddCameraObject("Main Camera");
                auto* cam = camObj->GetComponent<CameraComponent>();
                cam->Position = Vec3(0.0f, 3.0f, -6.0f);
                cam->Rotation = Vec3(20.0f, 0.0f, 0.0f);
                cam->FieldOfView = 45.0f;

                auto* lightObj = m_Scene.AddDirectionalLightObject("Sun Light");
                auto* sunLight = lightObj->GetComponent<DirectionalLightComponent>();
                if (sunLight)
                {
                    sunLight->Rotation = { 50.0f, -30.0f, 0.0f };
                    sunLight->LightColor = { 1.0f, 1.0f, 0.9f };
                    sunLight->Intensity = 1.0f;
                }

                auto* floor = m_Scene.AddMeshObject(EPrimitiveType::Floor, "Ground");
                (void)floor;
                auto* cube = m_Scene.AddMeshObject(EPrimitiveType::Cube, "Cube_1");
                auto* cubeMesh = cube->GetComponent<MeshComponent>();
                if (cubeMesh) cubeMesh->Position = { 0.0f, 0.5f, 0.0f };

                // Save as Default.json for future runs
                m_Scene.SaveToFile(defaultScene);
                std::cout << "[Kiwi] Created default scene: " << defaultScene << std::endl;
            }
        }

        RebuildAllGPUBuffers();

        // ---- Update camera matrices ----
        UpdateCameraFromScene();

        GetWindow()->SetResizeCallback([this](uint32_t width, uint32_t height) {
            UpdateCameraProjection();
        });

        // ---- Init Gizmo ----
        InitGizmoMeshes();
        BuildGizmoGPUBuffers();

        // ---- Init Texture Manager ----
        m_TextureManager.Initialize(GetDevice(), GetContext());

        // ---- Init Material Library ----
        m_MaterialLibrary.Initialize(m_MaterialsDir);

        // ---- Init Editor Input ----
        m_EditorInput.Init(GetWindow(), &m_Scene);

        std::cout << "[Kiwi] Scene Editor initialized!" << std::endl;
    }

    void OnUpdate(float deltaTime) override
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
                        m_DragStartPos      = sel->GetPosition();
                        m_DragStartRotation = sel->GetRotation();
                        m_DragStartScale    = sel->GetScale();

                        // For rotate: record starting angle projected on screen
                        if (m_GizmoMode == EGizmoMode::Rotate)
                        {
                            uint32_t w = GetWindow()->GetWidth();
                            uint32_t h = GetWindow()->GetHeight();
                            Vec2 originSS = WorldToScreen(m_DragStartPos, w, h, m_ViewMatrix, m_ProjectionMatrix);
                            Vec2 mp = { (float)mouse.X, (float)mouse.Y };
                            m_DragStartAngle = atan2f(mp.y - originSS.y, mp.x - originSS.x);
                        }
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

                    Vec3 axisDir;
                    switch (m_DragAxis)
                    {
                    case EGizmoAxis::X: axisDir = { 1, 0, 0 }; break;
                    case EGizmoAxis::Y: axisDir = { 0, 1, 0 }; break;
                    case EGizmoAxis::Z: axisDir = { 0, 0, 1 }; break;
                    default: break;
                    }

                    if (m_GizmoMode == EGizmoMode::Translate)
                    {
                        Ray rayNow   = ScreenToRay(mouse.X, mouse.Y, w, h, m_ViewMatrix, m_ProjectionMatrix);
                        Ray rayStart = ScreenToRay(m_DragStartMouseX, m_DragStartMouseY, w, h, m_ViewMatrix, m_ProjectionMatrix);
                        float tNow   = RayAxisClosestParam(rayNow,   m_DragStartPos, axisDir);
                        float tStart = RayAxisClosestParam(rayStart, m_DragStartPos, axisDir);
                        sel->GetPosition() = m_DragStartPos + axisDir * (tNow - tStart);
                    }
                    else if (m_GizmoMode == EGizmoMode::Rotate)
                    {
                        // Compute angle delta on screen: atan2 of cursor vs gizmo origin SS
                        Vec2 originSS = WorldToScreen(m_DragStartPos, w, h, m_ViewMatrix, m_ProjectionMatrix);
                        Vec2 mp = { (float)mouse.X, (float)mouse.Y };
                        float curAngle = atan2f(mp.y - originSS.y, mp.x - originSS.x);
                        float deltaRad = curAngle - m_DragStartAngle;
                        float deltaDeg = deltaRad * (180.0f / PI);

                        Vec3 newRot = m_DragStartRotation;
                        if      (m_DragAxis == EGizmoAxis::X) newRot.x += deltaDeg;
                        else if (m_DragAxis == EGizmoAxis::Y) newRot.y += deltaDeg;
                        else if (m_DragAxis == EGizmoAxis::Z) newRot.z += deltaDeg;
                        sel->GetRotation() = newRot;
                    }
                    else if (m_GizmoMode == EGizmoMode::Scale)
                    {
                        // Project axis on screen: delta pixels along axis screen direction → scale
                        Ray rayNow   = ScreenToRay(mouse.X, mouse.Y, w, h, m_ViewMatrix, m_ProjectionMatrix);
                        Ray rayStart = ScreenToRay(m_DragStartMouseX, m_DragStartMouseY, w, h, m_ViewMatrix, m_ProjectionMatrix);
                        float tNow   = RayAxisClosestParam(rayNow,   m_DragStartPos, axisDir);
                        float tStart = RayAxisClosestParam(rayStart, m_DragStartPos, axisDir);
                        float delta  = tNow - tStart; // world units dragged

                        Vec3 newScale = m_DragStartScale;
                        // Scale in the dragged axis
                        if      (m_DragAxis == EGizmoAxis::X) newScale.x = std::max(0.001f, m_DragStartScale.x + delta);
                        else if (m_DragAxis == EGizmoAxis::Y) newScale.y = std::max(0.001f, m_DragStartScale.y + delta);
                        else if (m_DragAxis == EGizmoAxis::Z) newScale.z = std::max(0.001f, m_DragStartScale.z + delta);
                        sel->GetScale() = newScale;
                    }
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
        // GL/Vulkan backend: always use forward path (deferred shaders not yet implemented)
        bool isGLBackend = (GetCurrentRHIType() == RHI_API_TYPE::OPENGL);
        bool isVulkanBackend = (GetCurrentRHIType() == RHI_API_TYPE::VULKAN);
        bool useDeferredPipeline = !isGLBackend && !isVulkanBackend &&
                                    (m_ViewMode == EViewMode::Lit ||
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

            // ==== PASS 0: Shadow Pass (CSM) ====
            UpdateShadowData();
            RenderShadowPass(ctx);

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
            // Transition depth buffer to shader resource for deferred lighting
            ctx->ResourceBarrier(GetDepthTexture(),
                RESOURCE_STATE_DEPTH_WRITE, RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

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

                // Bind G-Buffer SRVs (t0=GBufferA, t1=GBufferB, t2=GBufferC)
                ctx->SetShaderResourceView(0, m_GBufferSRV[0].get());
                ctx->SetShaderResourceView(1, m_GBufferSRV[1].get());
                ctx->SetShaderResourceView(2, m_GBufferSRV[2].get());

                // Bind shadow atlas SRV (t3 = single atlas for all cascades)
                ctx->SetShaderResourceView(3, m_ShadowAtlasSRV.get());

                // Bind depth buffer SRV for position reconstruction (t7)
                ctx->SetShaderResourceView(7, GetDepthSRV());

                // Bind sampler (DX11 only; DX12 uses static sampler s0)
                ctx->SetSampler(0, m_PostProcessSampler.get());
                // Bind comparison sampler for shadow mapping (DX11 s2; DX12 uses static sampler)
                ctx->SetSampler(2, m_ShadowSampler.get());

                // Update CB with lighting data
                UpdateDeferredLightingCB();

                // Upload and bind shadow uniform buffer at b2
                UploadShadowUB();
                ctx->SetConstantBuffer(2, m_ShadowCB.get());

                // Draw fullscreen triangle
                ctx->Draw(3, 0);

                // Unbind SRVs
                ctx->SetShaderResourceView(0, nullptr);
                ctx->SetShaderResourceView(1, nullptr);
                ctx->SetShaderResourceView(2, nullptr);
                ctx->SetShaderResourceView(3, nullptr); // Shadow atlas
                ctx->SetShaderResourceView(7, nullptr);

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

            // Transition depth buffer back to depth write for gizmo pass
            ctx->ResourceBarrier(GetDepthTexture(),
                RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RESOURCE_STATE_DEPTH_WRITE);

            // ==== PASS 3: Forward Gizmo Pass (on top of deferred result) ====
            ctx->BeginEvent("Gizmo Pass");
            m_PassTimer.Begin("Gizmo Pass");

            UploadViewUB(ctx);

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
            UploadViewUB(ctx);
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
        DrawViewModeButton();
        DrawCameraButton();
        DrawGizmoModeBar();
        DrawUI();
        DrawContentBrowser();
        DrawMaterialEditor();

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

        // ---- End frame (DX12: BackBuffer->Present barrier; DX11: no-op) ----
        ctx->EndFrame(swapChain);

        ctx->Flush();
    }

    // ============================================================
    // Scene Mesh Drawing
    // ============================================================

    // Helper: Upload per-frame ViewUniformBuffer (call once per frame before draw calls)
    void UploadViewUB(RHICommandContext* ctx)
    {
        Mat4 viewProj = m_ViewMatrix * m_ProjectionMatrix;
        Mat4 invViewProj = viewProj.Inverse();

        ViewUniformBuffer vub = {};
        memcpy(vub.ViewMatrix, m_ViewMatrix.m, sizeof(m_ViewMatrix.m));
        memcpy(vub.ProjectionMatrix, m_ProjectionMatrix.m, sizeof(m_ProjectionMatrix.m));
        memcpy(vub.ViewProjectionMatrix, viewProj.m, sizeof(viewProj.m));
        memcpy(vub.InvViewProjectionMatrix, invViewProj.m, sizeof(invViewProj.m));
        vub.CameraPos[0] = m_CameraPosition.x;
        vub.CameraPos[1] = m_CameraPosition.y;
        vub.CameraPos[2] = m_CameraPosition.z;
        vub.ViewPadding1 = 0.0f;
        vub.ScreenWidth = (float)GetWindow()->GetWidth();
        vub.ScreenHeight = (float)GetWindow()->GetHeight();
        vub.NearPlane = 0.1f;
        vub.FarPlane = 1000.0f;
        vub.NumLights = m_NumActiveLights;
        vub.ViewPadding2[0] = vub.ViewPadding2[1] = vub.ViewPadding2[2] = 0.0f;
        memcpy(vub.Lights, m_LightDataCache, sizeof(m_LightDataCache));

        void* mapped = m_ViewUB->Map();
        if (mapped) { memcpy(mapped, &vub, sizeof(vub)); m_ViewUB->Unmap(); }
        ctx->SetConstantBuffer(0, m_ViewUB.get());
    }

    // Helper: Fill and upload per-object ObjectUniformBuffer
    void UploadObjectUB(RHICommandContext* ctx, MeshComponent* meshComp)
    {
        Mat4 worldMatrix = meshComp->GetWorldMatrix();

        // Fetch material for PBR properties
        Material* mat = m_MaterialLibrary.GetMaterial(meshComp->MaterialName);
        Vec4  color     = mat ? mat->GetColor("_Color",     { 0.8f, 0.8f, 0.8f, 1.0f }) : Vec4{ 0.8f, 0.8f, 0.8f, 1.0f };
        float roughness = mat ? mat->GetFloat("_Roughness", 0.5f) : 0.5f;
        float metallic  = mat ? mat->GetFloat("_Metallic",  0.0f) : 0.0f;
        std::string baseColorTex = mat ? mat->GetTexture("_BaseColorTex") : "";
        std::string normalTex    = mat ? mat->GetTexture("_NormalTex")    : "";

        ObjectUniformBuffer oub = {};
        memcpy(oub.WorldMatrix, worldMatrix.m, sizeof(worldMatrix.m));
        oub.ObjectColor[0] = color.x;
        oub.ObjectColor[1] = color.y;
        oub.ObjectColor[2] = color.z;
        oub.ObjectColor[3] = color.w;
        oub.Selected = 0.0f;
        oub.Roughness = roughness;
        oub.Metallic  = metallic;
        oub.HasBaseColorTex = baseColorTex.empty() ? 0.0f : 1.0f;
        oub.HasNormalTex    = normalTex.empty()    ? 0.0f : 1.0f;
        oub.VisualizeMode = 0.0f;
        oub.ObjectPadding[0] = oub.ObjectPadding[1] = 0.0f;

        void* mapped = m_ObjectUB->Map();
        if (mapped) { memcpy(mapped, &oub, sizeof(oub)); m_ObjectUB->Unmap(); }
        ctx->SetConstantBuffer(1, m_ObjectUB.get());
    }

    // Helper: Bind material textures for the current object
    void BindMaterialTextures(RHICommandContext* ctx, MeshComponent* meshComp)
    {
        Material* mat = m_MaterialLibrary.GetMaterial(meshComp->MaterialName);
        std::string baseColorTex = mat ? mat->GetTexture("_BaseColorTex") : "";
        std::string normalTex    = mat ? mat->GetTexture("_NormalTex")    : "";
        std::string mrTex        = mat ? mat->GetTexture("_MetallicRoughnessTex") : "";

        // t4 = BaseColor texture
        if (!baseColorTex.empty())
        {
            GPUTexture* tex = m_TextureManager.GetTexture(baseColorTex);
            if (!tex) tex = m_TextureManager.LoadTexture(baseColorTex);
            if (tex && tex->SRV)
                ctx->SetShaderResourceView(4, tex->SRV.get());
            else
                ctx->SetShaderResourceView(4, m_TextureManager.GetWhiteTexture()->SRV.get());
        }
        else
        {
            if (m_TextureManager.GetWhiteTexture())
                ctx->SetShaderResourceView(4, m_TextureManager.GetWhiteTexture()->SRV.get());
        }

        // t5 = Normal map texture
        if (!normalTex.empty())
        {
            GPUTexture* tex = m_TextureManager.GetTexture(normalTex);
            if (!tex) tex = m_TextureManager.LoadTexture(normalTex);
            if (tex && tex->SRV)
                ctx->SetShaderResourceView(5, tex->SRV.get());
            else
                ctx->SetShaderResourceView(5, m_TextureManager.GetDefaultNormalTexture()->SRV.get());
        }
        else
        {
            if (m_TextureManager.GetDefaultNormalTexture())
                ctx->SetShaderResourceView(5, m_TextureManager.GetDefaultNormalTexture()->SRV.get());
        }

        // t6 = MetallicRoughness texture (optional; not yet sampled in shader but bound for future use)
        if (!mrTex.empty())
        {
            GPUTexture* tex = m_TextureManager.GetTexture(mrTex);
            if (!tex) tex = m_TextureManager.LoadTexture(mrTex);
            if (tex && tex->SRV)
                ctx->SetShaderResourceView(6, tex->SRV.get());
        }
    }

    // Deferred path: All objects rendered with G-Buffer shader (PSO already set)
    void DrawSceneMeshesDeferred(RHICommandContext* ctx)
    {
        // Upload per-frame ViewUB once before all draw calls
        UploadViewUB(ctx);

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

            UploadObjectUB(ctx, meshComp);

            // Bind material textures (t4 = BaseColor, t5 = NormalMap)
            BindMaterialTextures(ctx, meshComp);

            ctx->DrawIndexed(gpuMesh.IndexCount, 0, 0);
        }
    }

    // Forward path: Objects rendered with per-object shaders (or forced shader)
    void DrawSceneMeshesForward(RHICommandContext* ctx, const char* forceShaderName = nullptr)
    {
        // Upload per-frame ViewUB once before all draw calls
        UploadViewUB(ctx);

        std::string lastShaderName;
        for (const auto& renderItem : m_RenderList)
        {
            size_t i = renderItem.ObjectIndex;
            auto* meshComp = renderItem.MeshComp;
            if (!meshComp) continue;

            if (i >= m_GPUMeshes.size() || !m_GPUMeshes[i].VertexBuffer)
                continue;

            // --- Per-object shader switching ---
            std::string matShaderName;
            if (!forceShaderName)
            {
                Material* mat = m_MaterialLibrary.GetMaterial(meshComp->MaterialName);
                matShaderName = mat ? mat->ShaderName : "DefaultLit";
            }
            const std::string& shaderName = forceShaderName
                ? std::string(forceShaderName)
                : matShaderName;
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

            UploadObjectUB(ctx, meshComp);
            BindMaterialTextures(ctx, meshComp);
            ctx->DrawIndexed(gpuMesh.IndexCount, 0, 0);
        }
    }

    // Update CBs for deferred lighting fullscreen pass
    void UpdateDeferredLightingCB()
    {
        auto ctx = GetContext();

        // Upload ViewUniformBuffer (contains InvViewProj for position reconstruction)
        Mat4 viewProj = m_ViewMatrix * m_ProjectionMatrix;
        Mat4 invViewProj = viewProj.Inverse();

        ViewUniformBuffer vub = {};
        memcpy(vub.ViewMatrix, m_ViewMatrix.m, sizeof(m_ViewMatrix.m));
        memcpy(vub.ProjectionMatrix, m_ProjectionMatrix.m, sizeof(m_ProjectionMatrix.m));
        memcpy(vub.ViewProjectionMatrix, viewProj.m, sizeof(viewProj.m));
        memcpy(vub.InvViewProjectionMatrix, invViewProj.m, sizeof(invViewProj.m));
        vub.CameraPos[0] = m_CameraPosition.x;
        vub.CameraPos[1] = m_CameraPosition.y;
        vub.CameraPos[2] = m_CameraPosition.z;
        vub.ViewPadding1 = 0.0f;
        vub.ScreenWidth = (float)GetWindow()->GetWidth();
        vub.ScreenHeight = (float)GetWindow()->GetHeight();
        vub.NearPlane = 0.1f;
        vub.FarPlane = 1000.0f;
        vub.NumLights = m_NumActiveLights;
        vub.ViewPadding2[0] = vub.ViewPadding2[1] = vub.ViewPadding2[2] = 0.0f;
        memcpy(vub.Lights, m_LightDataCache, sizeof(m_LightDataCache));

        void* mapped = m_ViewUB->Map();
        if (mapped) { memcpy(mapped, &vub, sizeof(vub)); m_ViewUB->Unmap(); }
        ctx->SetConstantBuffer(0, m_ViewUB.get());

        // Upload ObjectUniformBuffer (identity world + no selection)
        ObjectUniformBuffer oub = {};
        Mat4 identity = Mat4::Identity();
        memcpy(oub.WorldMatrix, identity.m, sizeof(identity.m));
        oub.Selected = 0.0f;
        oub.ObjectPadding[0] = oub.ObjectPadding[1] = 0.0f;

        mapped = m_ObjectUB->Map();
        if (mapped) { memcpy(mapped, &oub, sizeof(oub)); m_ObjectUB->Unmap(); }
        ctx->SetConstantBuffer(1, m_ObjectUB.get());
    }

    // Update CBs for buffer visualization fullscreen pass
    void UpdateBufferVisualizationCB()
    {
        auto ctx = GetContext();

        // Upload ViewUniformBuffer
        Mat4 viewProj = m_ViewMatrix * m_ProjectionMatrix;
        Mat4 invViewProj = viewProj.Inverse();

        ViewUniformBuffer vub = {};
        memcpy(vub.ViewMatrix, m_ViewMatrix.m, sizeof(m_ViewMatrix.m));
        memcpy(vub.ProjectionMatrix, m_ProjectionMatrix.m, sizeof(m_ProjectionMatrix.m));
        memcpy(vub.ViewProjectionMatrix, viewProj.m, sizeof(viewProj.m));
        memcpy(vub.InvViewProjectionMatrix, invViewProj.m, sizeof(invViewProj.m));
        vub.CameraPos[0] = m_CameraPosition.x;
        vub.CameraPos[1] = m_CameraPosition.y;
        vub.CameraPos[2] = m_CameraPosition.z;
        vub.ViewPadding1 = 0.0f;
        vub.ScreenWidth = (float)GetWindow()->GetWidth();
        vub.ScreenHeight = (float)GetWindow()->GetHeight();
        vub.NearPlane = 0.1f;
        vub.FarPlane = 1000.0f;
        vub.NumLights = 0;
        vub.ViewPadding2[0] = vub.ViewPadding2[1] = vub.ViewPadding2[2] = 0.0f;

        void* mapped = m_ViewUB->Map();
        if (mapped) { memcpy(mapped, &vub, sizeof(vub)); m_ViewUB->Unmap(); }
        ctx->SetConstantBuffer(0, m_ViewUB.get());

        // Upload ObjectUniformBuffer with visualize mode
        ObjectUniformBuffer oub = {};
        Mat4 identity = Mat4::Identity();
        memcpy(oub.WorldMatrix, identity.m, sizeof(identity.m));
        oub.Selected = 0.0f;

        // Use g_VisualizeMode to pass the visualization mode
        switch (m_ViewMode)
        {
        case EViewMode::BaseColor: oub.VisualizeMode = 0.0f; break;
        case EViewMode::Roughness: oub.VisualizeMode = 1.0f; break;
        case EViewMode::Metallic:  oub.VisualizeMode = 2.0f; break;
        default:                   oub.VisualizeMode = 0.0f; break;
        }
        oub.ObjectPadding[0] = oub.ObjectPadding[1] = 0.0f;

        mapped = m_ObjectUB->Map();
        if (mapped) { memcpy(mapped, &oub, sizeof(oub)); m_ObjectUB->Unmap(); }
        ctx->SetConstantBuffer(1, m_ObjectUB.get());
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

    void UpdateWindowTitle()
    {
        std::string title = "Kiwi Engine - " + m_Scene.GetName();
        if (title != m_LastWindowTitle)
        {
            m_LastWindowTitle = title;
            std::wstring wtitle(title.begin(), title.end());
            SetWindowTextW(GetWindow()->GetHWND(), wtitle.c_str());
        }
    }

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
        m_ViewUB.reset();
        m_ObjectUB.reset();
        m_InputLayout.reset();
        m_PipelineState.reset();

        // Release all shaders via ShaderLibrary
        m_ShaderLibrary.ReleaseAll();
        m_TextureManager.ReleaseAll();

        // Release post-process resources
        ReleasePostProcessResources();

        // Release deferred rendering resources
        ReleaseGBufferResources();
        ReleaseDeferredShaders();

        // Release shadow map resources
        ReleaseShadowResources();

        // Release Gizmo GPU resources
        for (int i = 0; i < 3; i++)
        {
            m_GizmoVB[i].reset();
            m_GizmoIB[i].reset();
            m_GizmoRingVB[i].reset();
            m_GizmoRingIB[i].reset();
            m_GizmoScaleVB[i].reset();
            m_GizmoScaleIB[i].reset();
        }
        m_DirLightIndicatorVB.reset();
        m_DirLightIndicatorIB.reset();

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

        // Reinitialize texture manager
        m_TextureManager.Initialize(GetDevice(), GetContext());
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
        bool isGL = (device->GetApiType() == RHI_API_TYPE::OPENGL ||
                     device->GetApiType() == RHI_API_TYPE::VULKAN);
        const char* defaultVSSrc = isGL ? g_VertexShaderGLSL : g_VertexShaderHLSL;
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

        cbDesc.DebugName = "ObjectUniformBuffer";
        cbDesc.SizeInBytes = sizeof(ObjectUniformBuffer);
        m_ObjectUB = device->CreateBuffer(cbDesc);

        // Pipeline state (DX11: empty wrapper, DX12: managed per-shader)
        m_PipelineState = device->CreatePipelineState();

        // Initialize ShaderLibrary — GL uses GLShaders/ directory
        std::string shaderDir = isGL ? (m_ShaderDir + "\\..\\GLShaders") : m_ShaderDir;
        {
            namespace fs = std::filesystem;
            if (!fs::exists(shaderDir))
            {
                // Fallback: try source tree
                std::string fallback = m_ShaderDir + "\\..\\..\\..\\GLShaders";
                if (isGL && fs::exists(fallback)) shaderDir = fallback;
            }
        }
        m_ShaderLibrary.Initialize(shaderDir, device, m_InputLayout.get());

        // Initialize post-process resources
        InitPostProcessResources(device);

        // Initialize deferred rendering resources (DX11/DX12 only — GLSL deferred shaders not yet implemented)
        if (!isGL)
        {
            CompileDeferredShaders(device);
            CreateGBufferResources(device, GetWindow()->GetWidth(), GetWindow()->GetHeight());

            // Initialize shadow map resources
            InitShadowResources(device);
        }

        // Init ImGui backend
        device->InitImGui(GetWindow()->GetHWND());
    }

    // DX12 PSO creation is now handled by ShaderLibrary

    void InitPostProcessResources(RHIDevice* device)
    {
        bool isGL = (device->GetApiType() == RHI_API_TYPE::OPENGL ||
                     device->GetApiType() == RHI_API_TYPE::VULKAN);
        // PostProcess shader library
        m_PostProcessLibrary.Initialize(m_PostProcessShaderDir, device);

        // Post-process constant buffer
        BufferDesc ppCbDesc;
        ppCbDesc.SizeInBytes = sizeof(PostProcessCBData);
        ppCbDesc.BindFlags = BUFFER_USAGE_CONSTANT;
        ppCbDesc.Usage = EResourceUsage::Dynamic;
        ppCbDesc.DebugName = "PostProcessCB";
        m_PostProcessCB = device->CreateBuffer(ppCbDesc);

        // DX11 sampler for post-process (linear clamp)
        // DX12 uses static sampler in root signature, so this is only for DX11
        m_PostProcessSampler = device->CreateSampler();

        // Compile passthrough shader (for final blit from offscreen to backbuffer)
        const char* ppVSSrc = isGL ? g_PostProcessVS_GLSL : g_PostProcessVS;
        const char* ppPSSrc = isGL ? g_PostProcessPassthroughPS_GLSL : g_PostProcessPassthroughPS;
        m_PassthroughVS = device->CompileShader(
            EShaderType::Vertex, ppVSSrc, "VSMain", "vs_5_0");
        m_PassthroughPS = device->CompileShader(
            EShaderType::Pixel, ppPSSrc, "PSMain", "ps_5_0");
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
            rtDesc.DebugName = (i == 0) ? "OffscreenRT_0" : "OffscreenRT_1";

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

        // G-Buffer layout (UE5-inspired — position reconstructed from depth):
        // GBufferA: Normal Octahedron (RG) + Metallic (B) + ShadingModelID (A) — R8G8B8A8_UNORM
        // GBufferB: BaseColor (RGB) + Roughness (A) — R8G8B8A8_UNORM
        // GBufferC: Emissive (RGB) + Specular (A) — R8G8B8A8_UNORM
        // World position is reconstructed from hardware depth + inverse ViewProj matrix.
        EFormat gbufferFormats[GBUFFER_COUNT] = {
            EFormat::R8G8B8A8_UNORM,       // GBufferA: Normal + Metallic + ShadingModelID
            EFormat::R8G8B8A8_UNORM,       // GBufferB: BaseColor + Roughness
            EFormat::R8G8B8A8_UNORM,       // GBufferC: Emissive + Specular
        };

        const char* gbufferNames[GBUFFER_COUNT] = {
            "GBufferA_NormalMetallic",
            "GBufferB_BaseColorRoughness",
            "GBufferC_EmissiveSpecular",
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

    void ReleaseGBufferResources()
    {
        // Only release RT/RTV/SRV — shader/PSO are managed separately
        for (int i = 0; i < GBUFFER_COUNT; i++)
        {
            m_GBufferSRV[i].reset();
            m_GBufferRTV[i].reset();
            m_GBufferRT[i].reset();
        }
    }

    void ReleaseDeferredShaders()
    {
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
                // Create MRT PSO for G-Buffer (3 render targets, UE5-inspired layout)
                PipelineStateDesc gbufferPSODesc;
                gbufferPSODesc.NumRenderTargets = 3;
                gbufferPSODesc.RTVFormats[0] = EFormat::R8G8B8A8_UNORM; // GBufferA: Normal + Metallic
                gbufferPSODesc.RTVFormats[1] = EFormat::R8G8B8A8_UNORM; // GBufferB: BaseColor + Roughness
                gbufferPSODesc.RTVFormats[2] = EFormat::R8G8B8A8_UNORM; // GBufferC: Emissive + Specular
                gbufferPSODesc.DSVFormat = EFormat::D32_FLOAT;
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
    // Cascaded Shadow Map (CSM) Resources
    // ============================================================

    void InitShadowResources(RHIDevice* device)
    {
        // Create shadow constant buffer
        BufferDesc cbDesc;
        cbDesc.SizeInBytes = sizeof(ShadowUniformBuffer);
        cbDesc.BindFlags = BUFFER_USAGE_CONSTANT;
        cbDesc.Usage = EResourceUsage::Dynamic;
        cbDesc.DebugName = "ShadowCB";
        m_ShadowCB = device->CreateBuffer(cbDesc);

        // Create DX11 comparison sampler for shadow mapping
        // (DX12 uses static sampler s2 in root signature, returns nullptr)
        m_ShadowSampler = device->CreateComparisonSampler();

        // Compile shadow pass shader
        CompileShadowShader(device);

        // Create shadow maps based on first directional light settings
        CreateShadowMaps(device, 2048, 4);
    }

    void CompileShadowShader(RHIDevice* device)
    {
        std::string shadowPath = m_ShaderDir + "/ShadowPass.hlsl";
        std::string shadowSrc = ReadShaderFile(shadowPath);
        if (!shadowSrc.empty())
        {
            m_ShadowPassVS = device->CompileShader(
                EShaderType::Vertex, shadowSrc.c_str(), "VSMain", "vs_5_0");

            if (m_ShadowPassVS)
            {
                // Shadow pass PSO: depth-only, no color output
                PipelineStateDesc shadowPSODesc;
                shadowPSODesc.NumRenderTargets = 0;
                shadowPSODesc.RTVFormats[0] = EFormat::Unknown;
                shadowPSODesc.DSVFormat = EFormat::D32_FLOAT;
                shadowPSODesc.DepthEnabled = true;
                shadowPSODesc.DepthWrite = true;

                m_ShadowPassPSO = device->CreateGraphicsPipelineState(
                    m_ShadowPassVS.get(), nullptr, m_InputLayout.get(), shadowPSODesc);

                std::cout << "[Kiwi] Shadow Pass shader compiled successfully" << std::endl;
            }
            else
            {
                std::cerr << "[Kiwi] Failed to compile Shadow Pass shader" << std::endl;
            }
        }
    }

    void CreateShadowMaps(RHIDevice* device, uint32_t cascadeSize, int numCascades)
    {
        // Release existing
        ReleaseShadowMaps();

        m_ShadowCascadeSize = cascadeSize;
        uint32_t atlasSize = cascadeSize * 2; // 2x2 atlas layout

        TextureDesc desc;
        desc.Width = atlasSize;
        desc.Height = atlasSize;
        desc.Format = EFormat::R32_TYPELESS; // Typeless for DSV(D32_FLOAT) + SRV(R32_FLOAT)
        desc.BindFlags = TEXTURE_HINT_DEPTH_STENCIL | TEXTURE_BIND_SHADER_RESOURCE;
        desc.Usage = EResourceUsage::Default;
        desc.MipLevels = 1;
        desc.SampleCount = 1;
        desc.DebugName = "ShadowAtlas_CSM";

        m_ShadowAtlasRT = device->CreateTexture(desc);

        // DSV view: D32_FLOAT format (covers entire atlas)
        m_ShadowAtlasDSV = device->CreateTextureView(
            m_ShadowAtlasRT.get(), EDescriptorHeapType::DSV, EFormat::D32_FLOAT);

        // SRV view: R32_FLOAT format
        m_ShadowAtlasSRV = device->CreateTextureView(
            m_ShadowAtlasRT.get(), EDescriptorHeapType::CBV_SRV_UAV, EFormat::R32_FLOAT);

        std::cout << "[Kiwi] Shadow atlas created: " << numCascades << " cascades @ "
                  << cascadeSize << "x" << cascadeSize << " (atlas " << atlasSize << "x" << atlasSize << ")" << std::endl;
    }

    void ReleaseShadowMaps()
    {
        m_ShadowAtlasSRV.reset();
        m_ShadowAtlasDSV.reset();
        m_ShadowAtlasRT.reset();
    }

    void ReleaseShadowResources()
    {
        ReleaseShadowMaps();
        m_ShadowPassVS.reset();
        m_ShadowPassPSO.reset();
        m_ShadowCB.reset();
        m_ShadowSampler.reset();
    }

    // ============================================================
    // CSM: Cascade Split Calculation (PSSM — Practical Split Scheme)
    // ============================================================

    void CalculateCascadeSplits(float nearZ, float farZ, float shadowDistance, int numCascades, float lambda, float* outSplits)
    {
        float maxDist = std::min(farZ, shadowDistance);
        float range = maxDist - nearZ;

        for (int i = 0; i < numCascades; i++)
        {
            float p = (float)(i + 1) / (float)numCascades;

            // Logarithmic split
            float logSplit = nearZ * std::pow(maxDist / nearZ, p);
            // Uniform split
            float uniformSplit = nearZ + range * p;
            // PSSM blend
            outSplits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
        }
    }

    // ============================================================
    // CSM: Compute Light View-Projection Matrix for a Cascade
    // ============================================================

    Mat4 ComputeLightViewProjForCascade(
        const Vec3& lightDir,
        const Mat4& cameraView, const Mat4& cameraProj,
        float cascadeNear, float cascadeFar,
        float cameraNear, float cameraFar, float fovY, float aspect)
    {
        // 1. Compute the frustum corners for this cascade slice in world space
        float tanHalfFov = tanf(fovY * 0.5f);

        // Near and far plane dimensions
        float nearH = 2.0f * tanHalfFov * cascadeNear;
        float nearW = nearH * aspect;
        float farH = 2.0f * tanHalfFov * cascadeFar;
        float farW = farH * aspect;

        // Camera basis vectors (from view matrix — row-major, v*M convention)
        Vec3 camRight = { cameraView.m[0][0], cameraView.m[1][0], cameraView.m[2][0] };
        Vec3 camUp    = { cameraView.m[0][1], cameraView.m[1][1], cameraView.m[2][1] };
        Vec3 camFwd   = { cameraView.m[0][2], cameraView.m[1][2], cameraView.m[2][2] };

        // Camera position (inverse of translation in view matrix)
        Vec3 camPos;
        camPos.x = -(cameraView.m[3][0] * camRight.x + cameraView.m[3][1] * camUp.x + cameraView.m[3][2] * camFwd.x);
        camPos.y = -(cameraView.m[3][0] * camRight.y + cameraView.m[3][1] * camUp.y + cameraView.m[3][2] * camFwd.y);
        camPos.z = -(cameraView.m[3][0] * camRight.z + cameraView.m[3][1] * camUp.z + cameraView.m[3][2] * camFwd.z);

        // Near center and far center
        Vec3 nearCenter = camPos + camFwd * cascadeNear;
        Vec3 farCenter  = camPos + camFwd * cascadeFar;

        // 8 frustum corners
        Vec3 corners[8];
        // Near face
        corners[0] = nearCenter + camUp * (nearH * 0.5f) - camRight * (nearW * 0.5f); // top-left
        corners[1] = nearCenter + camUp * (nearH * 0.5f) + camRight * (nearW * 0.5f); // top-right
        corners[2] = nearCenter - camUp * (nearH * 0.5f) - camRight * (nearW * 0.5f); // bottom-left
        corners[3] = nearCenter - camUp * (nearH * 0.5f) + camRight * (nearW * 0.5f); // bottom-right
        // Far face
        corners[4] = farCenter + camUp * (farH * 0.5f) - camRight * (farW * 0.5f);
        corners[5] = farCenter + camUp * (farH * 0.5f) + camRight * (farW * 0.5f);
        corners[6] = farCenter - camUp * (farH * 0.5f) - camRight * (farW * 0.5f);
        corners[7] = farCenter - camUp * (farH * 0.5f) + camRight * (farW * 0.5f);

        // 2. Compute frustum center
        Vec3 center = { 0, 0, 0 };
        for (int i = 0; i < 8; i++)
        {
            center.x += corners[i].x;
            center.y += corners[i].y;
            center.z += corners[i].z;
        }
        center = center * (1.0f / 8.0f);

        // 3. Build light view matrix (looking along -lightDir at the center)
        Vec3 lightDirN = lightDir.Normalize();
        float radius = 0.0f;
        for (int i = 0; i < 8; i++)
        {
            float dist = (corners[i] - center).Length();
            radius = std::max(radius, dist);
        }

        // Snap to texel grid to reduce shimmer
        radius = std::ceil(radius * 16.0f) / 16.0f;

        Vec3 lightEye = center - lightDirN * radius;
        Vec3 lightTarget = center;
        Vec3 lightUp = { 0.0f, 1.0f, 0.0f };
        // If light is nearly vertical, use a different up vector
        if (std::abs(lightDirN.y) > 0.99f)
            lightUp = { 0.0f, 0.0f, 1.0f };

        Mat4 lightView = Mat4::LookAt(lightEye, lightTarget, lightUp);

        // 4. Build orthographic projection that encompasses the frustum
        Mat4 lightProj = Mat4::Orthographic(radius * 2.0f, radius * 2.0f, 0.0f, radius * 2.0f);

        return lightView * lightProj;
    }

    // ============================================================
    // CSM: Update Shadow CB and Compute Light VP Matrices
    // ============================================================

    void UpdateShadowData()
    {
        memset(&m_ShadowUBData, 0, sizeof(m_ShadowUBData));

        // Find the first shadow-casting directional light
        DirectionalLightComponent* shadowLight = nullptr;
        for (auto& obj : m_Scene.GetObjects())
        {
            auto* light = obj->GetComponent<LightComponent>();
            if (light && light->Enabled && light->AffectWorld &&
                light->GetLightType() == ELightType::Directional)
            {
                auto* dirLight = dynamic_cast<DirectionalLightComponent*>(light);
                if (dirLight && dirLight->CastShadow)
                {
                    shadowLight = dirLight;
                    break;
                }
            }
        }

        if (!shadowLight)
        {
            m_ShadowUBData.NumCascades = 0;
            return;
        }

        int numCascades = std::min(shadowLight->NumCascades, MAX_SHADOW_CASCADES);
        m_ShadowUBData.NumCascades = numCascades;
        m_ShadowUBData.ShadowBias = shadowLight->ShadowBias;
        m_ShadowUBData.NormalBias = shadowLight->NormalBias;
        m_ShadowUBData.ShadowStrength = shadowLight->ShadowStrength;
        m_ShadowUBData.ShadowMapSize = (float)(m_ShadowCascadeSize * 2); // Atlas total size

        // Recreate shadow maps if resolution changed
        auto device = GetDevice();
        if (m_ShadowCascadeSize != (uint32_t)shadowLight->ShadowMapResolution ||
            !m_ShadowAtlasRT)
        {
            CreateShadowMaps(device, (uint32_t)shadowLight->ShadowMapResolution, numCascades);
        }

        // Get camera parameters for frustum calculation
        auto* cam = m_Scene.GetActiveCamera();
        if (!cam) return;

        float fovY = DegToRad(cam->FieldOfView);
        float aspect = (float)GetWindow()->GetWidth() / (float)GetWindow()->GetHeight();
        float nearZ = cam->NearPlane;
        float farZ = cam->FarPlane;

        // Calculate cascade splits
        float splits[MAX_SHADOW_CASCADES];
        CalculateCascadeSplits(nearZ, farZ, shadowLight->ShadowDistance, numCascades,
            shadowLight->CascadeSplitLambda, splits);

        for (int i = 0; i < numCascades; i++)
        {
            m_ShadowUBData.CascadeSplits[i] = splits[i];
        }

        // Compute light VP matrices for each cascade
        Vec3 lightDir = shadowLight->GetForward(); // Direction the light shines toward

        float cascadeNear = nearZ;
        for (int i = 0; i < numCascades; i++)
        {
            float cascadeFar = splits[i];

            m_LightViewProjMatrices[i] = ComputeLightViewProjForCascade(
                lightDir, m_ViewMatrix, m_ProjectionMatrix,
                cascadeNear, cascadeFar, nearZ, farZ, fovY, aspect);

            memcpy(m_ShadowUBData.LightViewProj[i],
                m_LightViewProjMatrices[i].m, sizeof(float) * 16);

            cascadeNear = cascadeFar;
        }
    }

    void UploadShadowUB()
    {
        if (!m_ShadowCB) return;
        void* mapped = m_ShadowCB->Map();
        if (mapped)
        {
            memcpy(mapped, &m_ShadowUBData, sizeof(m_ShadowUBData));
            m_ShadowCB->Unmap();
        }
    }

    // ============================================================
    // Shadow Pass: Render scene depth from light perspective
    // ============================================================

    void RenderShadowPass(RHICommandContext* ctx)
    {
        if (!m_ShadowPassPSO || !m_ShadowPassVS || m_ShadowUBData.NumCascades <= 0)
            return;

        ctx->BeginEvent("Shadow Pass");
        m_PassTimer.Begin("Shadow Pass");

        int numCascades = m_ShadowUBData.NumCascades;

        // Set shadow pass pipeline state
        ctx->SetPipelineState(m_ShadowPassPSO.get());
        ctx->SetVertexShader(m_ShadowPassVS.get());
        ctx->SetPixelShader(nullptr);
        ctx->SetInputLayout(m_InputLayout.get());
        ctx->SetPrimitiveTopology(EPrimitiveTopology::TriangleList);

        // Transition atlas to depth write
        ctx->ResourceBarrier(m_ShadowAtlasRT.get(),
            RESOURCE_STATE_COMMON, RESOURCE_STATE_DEPTH_WRITE);

        // Set render target: depth only (no color RT), bind the whole atlas DSV
        RHITextureView* nullRTV = nullptr;
        ctx->SetRenderTargets(&nullRTV, 0, m_ShadowAtlasDSV.get());

        // Clear entire atlas depth
        uint32_t atlasSize = m_ShadowCascadeSize * 2;
        Viewport fullVP;
        fullVP.TopLeftX = 0; fullVP.TopLeftY = 0;
        fullVP.Width = (float)atlasSize; fullVP.Height = (float)atlasSize;
        fullVP.MinDepth = 0.0f; fullVP.MaxDepth = 1.0f;
        ctx->SetViewports(&fullVP, 1);
        ScissorRect fullSR;
        fullSR.Left = 0; fullSR.Top = 0;
        fullSR.Right = (int32_t)atlasSize; fullSR.Bottom = (int32_t)atlasSize;
        ctx->SetScissorRects(&fullSR, 1);

        ClearDepthStencilValue depthClear = { 1.0f, 0 };
        ctx->ClearDepthStencilView(m_ShadowAtlasDSV.get(), depthClear, 0x01);

        // Atlas 2x2 layout: [0]=top-left, [1]=top-right, [2]=bottom-left, [3]=bottom-right
        static const int cascadeOffsetX[4] = { 0, 1, 0, 1 };
        static const int cascadeOffsetY[4] = { 0, 0, 1, 1 };

        for (int cascade = 0; cascade < numCascades; cascade++)
        {
            // Set viewport/scissor for this cascade's region in the atlas
            float ox = (float)(cascadeOffsetX[cascade] * m_ShadowCascadeSize);
            float oy = (float)(cascadeOffsetY[cascade] * m_ShadowCascadeSize);

            Viewport shadowVP;
            shadowVP.TopLeftX = ox; shadowVP.TopLeftY = oy;
            shadowVP.Width = (float)m_ShadowCascadeSize;
            shadowVP.Height = (float)m_ShadowCascadeSize;
            shadowVP.MinDepth = 0.0f; shadowVP.MaxDepth = 1.0f;
            ctx->SetViewports(&shadowVP, 1);

            ScissorRect shadowSR;
            shadowSR.Left = (int32_t)ox; shadowSR.Top = (int32_t)oy;
            shadowSR.Right = (int32_t)(ox + m_ShadowCascadeSize);
            shadowSR.Bottom = (int32_t)(oy + m_ShadowCascadeSize);
            ctx->SetScissorRects(&shadowSR, 1);

            // Draw all mesh objects from light's perspective
            for (const auto& renderItem : m_RenderList)
            {
                size_t i = renderItem.ObjectIndex;
                auto* meshComp = renderItem.MeshComp;
                if (!meshComp) continue;
                if (i >= m_GPUMeshes.size() || !m_GPUMeshes[i].VertexBuffer) continue;

                auto& gpuMesh = m_GPUMeshes[i];

                // Upload ObjectUB with world matrix for this object
                Mat4 worldMatrix = meshComp->GetWorldMatrix();
                ObjectUniformBuffer oub = {};
                memcpy(oub.WorldMatrix, worldMatrix.m, sizeof(worldMatrix.m));
                oub.ObjectPadding[0] = oub.ObjectPadding[1] = 0.0f;

                void* mapped = m_ObjectUB->Map();
                if (mapped)
                {
                    memcpy(mapped, &oub, sizeof(oub));
                    m_ObjectUB->Unmap();
                }
                ctx->SetConstantBuffer(1, m_ObjectUB.get());

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

                ctx->DrawIndexed(gpuMesh.IndexCount, 0, 0);
            }
        }

        // Transition atlas to shader resource for lighting pass
        ctx->ResourceBarrier(m_ShadowAtlasRT.get(),
            RESOURCE_STATE_DEPTH_WRITE, RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        m_PassTimer.End();
        ctx->EndEvent();
    }

    // ============================================================
    // Menu Bar (fixed at top)
    // ============================================================

    void DrawMenuBar()
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Create Scene"))
                {
                    m_Scene.Clear();
                    m_GPUMeshes.clear();
                    m_Scene.SetName("New Scene");
                }

                // Open Scene — lists all .json files in Scenes/ directory
                if (ImGui::BeginMenu("Open Scene"))
                {
                    namespace fs = std::filesystem;
                    bool hasFiles = false;
                    if (fs::exists(m_ScenesDir))
                    {
                        for (auto& entry : fs::directory_iterator(m_ScenesDir))
                        {
                            if (entry.is_regular_file() && entry.path().extension() == ".json")
                            {
                                std::string filename = entry.path().stem().string();
                                if (ImGui::MenuItem(filename.c_str()))
                                {
                                    if (m_Scene.LoadFromFile(entry.path().string()))
                                    {
                                        RebuildAllGPUBuffers();
                                    }
                                }
                                hasFiles = true;
                            }
                        }
                    }
                    if (!hasFiles)
                    {
                        ImGui::TextDisabled("(no scene files)");
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::MenuItem("Save Scene"))
                {
                    m_ShowSaveDialog = true;
                    // Pre-fill with current scene name
                    std::string name = m_Scene.GetName();
                    strncpy(m_SaveSceneName, name.c_str(), sizeof(m_SaveSceneName) - 1);
                    m_SaveSceneName[sizeof(m_SaveSceneName) - 1] = '\0';
                }
                ImGui::EndMenu();
            }

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
                    if (ImGui::MenuItem("OpenGL", nullptr,
                        currentRHI == RHI_API_TYPE::OPENGL, currentRHI != RHI_API_TYPE::OPENGL))
                    {
                        m_PendingRHISwitch = true;
                        m_PendingRHIType = RHI_API_TYPE::OPENGL;
                    }

                    // Vulkan is incompatible with RenderDoc in-process hook (NVIDIA nvoglv64.dll conflict)
                    bool rdocLoaded = RenderDocIntegration::Get().IsAvailable();
                    bool canSwitchVulkan = !rdocLoaded && (currentRHI != RHI_API_TYPE::VULKAN);
                    if (ImGui::MenuItem("Vulkan", rdocLoaded ? "(RenderDoc active)" : nullptr,
                        currentRHI == RHI_API_TYPE::VULKAN, canSwitchVulkan))
                    {
                        m_PendingRHISwitch = true;
                        m_PendingRHIType = RHI_API_TYPE::VULKAN;
                    }

                    ImGui::EndMenu();
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Window"))
            {
                if (ImGui::MenuItem("Content Browser", "Ctrl+Space", m_ShowContentBrowser))
                {
                    m_ShowContentBrowser = !m_ShowContentBrowser;
                }
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        // Save Scene dialog (modal popup)
        if (m_ShowSaveDialog)
        {
            ImGui::OpenPopup("Save Scene##SaveDlg");
            m_ShowSaveDialog = false;
        }

        if (ImGui::BeginPopupModal("Save Scene##SaveDlg", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Scene name:");
            ImGui::SetNextItemWidth(300.0f);
            bool enterPressed = ImGui::InputText("##SceneName", m_SaveSceneName,
                sizeof(m_SaveSceneName), ImGuiInputTextFlags_EnterReturnsTrue);

            ImGui::Spacing();

            bool doSave = false;
            if (ImGui::Button("Save", ImVec2(120, 0)) || enterPressed)
                doSave = true;
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();

            if (doSave && m_SaveSceneName[0] != '\0')
            {
                namespace fs = std::filesystem;
                std::string name(m_SaveSceneName);
                m_Scene.SetName(name);
                std::string filepath = m_ScenesDir + "\\" + name + ".json";
                fs::create_directories(m_ScenesDir);
                m_Scene.SaveToFile(filepath);
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    // ============================================================
    // RenderDoc Capture Button (compact, top-right corner)
    // ============================================================

    void DrawRenderDocOverlay()
    {
        auto& rdoc = RenderDocIntegration::Get();
        if (!rdoc.IsAvailable()) return;
        // RenderDoc capture not supported in Vulkan mode (RenderDoc + NVIDIA Vulkan ICD conflict)
        if (GetCurrentRHIType() == RHI_API_TYPE::VULKAN) return;

        float menuBarHeight = ImGui::GetFrameHeight();
        float windowWidth = (float)GetWindow()->GetWidth();

        // Main viewport position for Multi-Viewport offset
        ImGuiViewport* vp = ImGui::GetMainViewport();
        float vpX = vp->Pos.x, vpY = vp->Pos.y;

        // Compact button size - just an icon button
        float btnSize = 32.0f;

        ImGui::SetNextWindowPos(
            ImVec2(vpX + windowWidth - btnSize - 12.0f, vpY + menuBarHeight + 6.0f),
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
        float btnGap = 6.0f;

        ImGuiViewport* mvp = ImGui::GetMainViewport();
        float vpX = mvp->Pos.x, vpY = mvp->Pos.y;

        // Right-to-left layout: RenderDoc | Stats | ViewMode
        float rdocBtnX = windowWidth - btnSize - 12.0f;
        float statsBtnX = rdocBtnX - btnSize - btnGap;

        ImGui::SetNextWindowPos(
            ImVec2(vpX + statsBtnX, vpY + menuBarHeight + 6.0f),
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

        ImGuiViewport* mvp = ImGui::GetMainViewport();
        float vpX = mvp->Pos.x, vpY = mvp->Pos.y;

        ImGui::SetNextWindowPos(
            ImVec2(vpX + windowWidth - panelWidth - 8.0f, vpY + menuBarHeight + 44.0f),
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
            auto rhiType = GetCurrentRHIType();
            const char* rhiName = (rhiType == RHI_API_TYPE::DX11) ? "DX11" :
                                  (rhiType == RHI_API_TYPE::DX12) ? "DX12" :
                                  (rhiType == RHI_API_TYPE::OPENGL) ? "OpenGL" :
                                  (rhiType == RHI_API_TYPE::VULKAN) ? "Vulkan" : "Unknown";
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
    // View Mode Button (top-right, left of Stats button)
    // ============================================================

    void DrawViewModeButton()
    {
        float menuBarHeight = ImGui::GetFrameHeight();
        float windowWidth = (float)GetWindow()->GetWidth();
        float btnSize = 32.0f;
        float btnGap = 6.0f;

        ImGuiViewport* mvp = ImGui::GetMainViewport();
        float vpX = mvp->Pos.x, vpY = mvp->Pos.y;

        // Right-to-left layout: RenderDoc | Stats | ViewMode | Camera
        float rdocBtnX = windowWidth - btnSize - 12.0f;
        float statsBtnX = rdocBtnX - btnSize - btnGap;
        float viewModeBtnX = statsBtnX - btnSize - btnGap;

        ImGui::SetNextWindowPos(
            ImVec2(vpX + viewModeBtnX, vpY + menuBarHeight + 6.0f),
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

        if (ImGui::Begin("##ViewModeBtn", nullptr, flags))
        {
            bool isLit = (m_ViewMode == EViewMode::Lit);

            // Non-Lit modes get a yellow/orange tint to indicate active override
            ImVec4 btnColor    = isLit ? ImVec4(0.25f, 0.32f, 0.38f, 0.95f)
                                       : ImVec4(0.55f, 0.45f, 0.10f, 0.95f);
            ImVec4 hoverColor  = isLit ? ImVec4(0.32f, 0.42f, 0.50f, 1.0f)
                                       : ImVec4(0.65f, 0.55f, 0.15f, 1.0f);
            ImVec4 activeColor = isLit ? ImVec4(0.18f, 0.25f, 0.30f, 1.0f)
                                       : ImVec4(0.45f, 0.35f, 0.08f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Button, btnColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

            if (ImGui::Button("##ViewModeIcon", ImVec2(btnSize, btnSize)))
            {
                ImGui::OpenPopup("ViewModePopup");
            }

            ImGui::PopStyleVar(1);  // FrameRounding
            ImGui::PopStyleColor(4);

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("View Mode: %s", GetViewModeName(m_ViewMode));
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Click to change view mode");
                ImGui::EndTooltip();
            }

            // Draw eye icon on the button
            ImVec2 btnMin = ImGui::GetItemRectMin();
            ImVec2 btnMax = ImGui::GetItemRectMax();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 center = ImVec2((btnMin.x + btnMax.x) * 0.5f, (btnMin.y + btnMax.y) * 0.5f);

            // Draw a simple eye shape
            ImU32 iconColor = isLit ? IM_COL32(200, 200, 200, 255) : IM_COL32(255, 220, 80, 255);
            float r = 7.0f;
            // Eye outline (horizontal ellipse)
            drawList->AddEllipse(center, ImVec2(r, r * 0.55f), iconColor, 0.0f, 0, 1.5f);
            // Pupil
            drawList->AddCircleFilled(center, 2.5f, iconColor);

            // Popup menu for view mode selection
            if (ImGui::BeginPopup("ViewModePopup"))
            {
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "View Mode");
                ImGui::Separator();

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

                ImGui::EndPopup();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    // ============================================================
    // Camera Settings Button (top-right, left of ViewMode button)
    // ============================================================

    void DrawCameraButton()
    {
        float menuBarHeight = ImGui::GetFrameHeight();
        float windowWidth = (float)GetWindow()->GetWidth();
        float btnSize = 32.0f;
        float btnGap = 6.0f;

        ImGuiViewport* mvp = ImGui::GetMainViewport();
        float vpX = mvp->Pos.x, vpY = mvp->Pos.y;

        // Right-to-left layout: RenderDoc | Stats | ViewMode | Camera
        float rdocBtnX = windowWidth - btnSize - 12.0f;
        float statsBtnX = rdocBtnX - btnSize - btnGap;
        float viewModeBtnX = statsBtnX - btnSize - btnGap;
        float cameraBtnX = viewModeBtnX - btnSize - btnGap;

        ImGui::SetNextWindowPos(
            ImVec2(vpX + cameraBtnX, vpY + menuBarHeight + 6.0f),
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

        if (ImGui::Begin("##CameraBtn", nullptr, flags))
        {
            ImVec4 btnColor    = ImVec4(0.25f, 0.32f, 0.38f, 0.95f);
            ImVec4 hoverColor  = ImVec4(0.32f, 0.42f, 0.50f, 1.0f);
            ImVec4 activeColor = ImVec4(0.18f, 0.25f, 0.30f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Button, btnColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

            if (ImGui::Button("##CameraIcon", ImVec2(btnSize, btnSize)))
            {
                ImGui::OpenPopup("CameraSettingsPopup");
            }

            ImGui::PopStyleVar(1);  // FrameRounding
            ImGui::PopStyleColor(4);

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Camera Settings");
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Move speed & FOV");
                ImGui::EndTooltip();
            }

            // Draw camera icon on the button
            ImVec2 btnMin = ImGui::GetItemRectMin();
            ImVec2 btnMax = ImGui::GetItemRectMax();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 center = ImVec2((btnMin.x + btnMax.x) * 0.5f, (btnMin.y + btnMax.y) * 0.5f);

            ImU32 iconColor = IM_COL32(200, 200, 200, 255);
            // Film camera side profile
            // Camera body
            drawList->AddRectFilled(
                ImVec2(center.x - 6.0f, center.y - 3.0f),
                ImVec2(center.x + 6.0f, center.y + 5.0f),
                iconColor, 2.0f);
            // Lens barrel (front)
            drawList->AddRectFilled(
                ImVec2(center.x + 6.0f, center.y - 1.0f),
                ImVec2(center.x + 10.0f, center.y + 3.0f),
                iconColor, 1.0f);
            // Film reel (top-left circle)
            drawList->AddCircleFilled(
                ImVec2(center.x - 3.0f, center.y - 6.0f), 4.0f, iconColor);
            drawList->AddCircleFilled(
                ImVec2(center.x - 3.0f, center.y - 6.0f), 1.5f,
                IM_COL32(40, 50, 60, 255));
            // Film reel (top-right circle, smaller)
            drawList->AddCircleFilled(
                ImVec2(center.x + 4.0f, center.y - 5.0f), 3.0f, iconColor);
            drawList->AddCircleFilled(
                ImVec2(center.x + 4.0f, center.y - 5.0f), 1.2f,
                IM_COL32(40, 50, 60, 255));

            // Popup with sliders
            if (ImGui::BeginPopup("CameraSettingsPopup"))
            {
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Camera Settings");
                ImGui::Separator();

                // Move speed slider
                float moveSpeed = m_EditorInput.GetCameraMoveSpeed();
                ImGui::SetNextItemWidth(180.0f);
                if (ImGui::SliderFloat("Move Speed", &moveSpeed, 0.5f, 50.0f, "%.1f"))
                {
                    m_EditorInput.SetCameraMoveSpeed(moveSpeed);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Camera movement speed when holding Right Mouse + WASD");

                // FOV slider (only for perspective cameras)
                auto* cam = m_Scene.GetActiveCamera();
                if (cam && cam->Projection == ECameraProjection::Perspective)
                {
                    ImGui::SetNextItemWidth(180.0f);
                    ImGui::SliderFloat("FOV", &cam->FieldOfView, 10.0f, 120.0f, "%.0f deg");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Camera field of view (degrees)");
                }

                ImGui::EndPopup();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    // ============================================================
    // Gizmo Mode Bar (top-left of viewport: W=Translate, E=Rotate, R=Scale)
    // ============================================================

    void DrawGizmoModeBar()
    {
        float menuBarHeight = ImGui::GetFrameHeight();
        ImGuiViewport* mvp = ImGui::GetMainViewport();
        float vpX = mvp->Pos.x, vpY = mvp->Pos.y;

        // W / E / R shortcut keys (only when ImGui doesn't want keyboard)
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureKeyboard)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_W, false) && !m_IsDragging)
                m_GizmoMode = EGizmoMode::Translate;
            if (ImGui::IsKeyPressed(ImGuiKey_E, false) && !m_IsDragging)
                m_GizmoMode = EGizmoMode::Rotate;
            if (ImGui::IsKeyPressed(ImGuiKey_R, false) && !m_IsDragging)
                m_GizmoMode = EGizmoMode::Scale;
        }

        // ---- Left toolbar (W/E/R text buttons) ----
        {
            float btnW = 36.0f, btnH = 26.0f, gap = 2.0f, padLeft = 8.0f;
            ImGui::SetNextWindowPos(
                ImVec2(vpX + padLeft, vpY + menuBarHeight + 6.0f),
                ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.75f);

            ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(gap, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

            if (ImGui::Begin("##GizmoModeBar", nullptr, flags))
            {
                struct ModeBtn { const char* label; EGizmoMode mode; const char* tip; };
                ModeBtn btns[] = {
                    { "W", EGizmoMode::Translate, "Translate (W)" },
                    { "E", EGizmoMode::Rotate,    "Rotate (E)"    },
                    { "R", EGizmoMode::Scale,     "Scale (R)"     },
                };

                for (int i = 0; i < 3; i++)
                {
                    bool active = (m_GizmoMode == btns[i].mode);
                    if (active)
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.85f, 1.0f));
                    else
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.28f, 0.34f, 0.95f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.60f, 0.90f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.45f, 0.75f, 1.0f));

                    if (ImGui::Button(btns[i].label, ImVec2(btnW, btnH)))
                        m_GizmoMode = btns[i].mode;

                    ImGui::PopStyleColor(3);

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(btns[i].tip);
                        ImGui::EndTooltip();
                    }

                    if (i < 2) ImGui::SameLine();
                }
            }
            ImGui::End();
            ImGui::PopStyleVar(4);
        }

        // ---- Right toolbar: 3 icon buttons, 26px, left of Camera button ----
        // Layout (right-to-left): RenderDoc(32) | Stats(32) | ViewMode(32) | Camera(32) | [Gizmo x3]
        {
            float bigBtn = 32.0f, bigGap = 6.0f;
            float windowWidth = (float)GetWindow()->GetWidth();
            float rdocBtnX     = windowWidth - bigBtn - 12.0f;
            float statsBtnX    = rdocBtnX  - bigBtn - bigGap;
            float viewModeBtnX = statsBtnX - bigBtn - bigGap;
            float cameraBtnX   = viewModeBtnX - bigBtn - bigGap;

            // Gizmo group: 3 buttons of 26px with 2px gap = 82px total
            float gizmoBtnSize = 26.0f;
            float gizmoGap     = 2.0f;
            float gizmoGroupW  = gizmoBtnSize * 3 + gizmoGap * 2;
            float gizmoBtnX    = cameraBtnX - gizmoGroupW - bigGap;

            ImGui::SetNextWindowPos(
                ImVec2(vpX + gizmoBtnX, vpY + menuBarHeight + 6.0f),
                ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.0f);

            ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2(gizmoGap, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,    6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

            if (ImGui::Begin("##GizmoModeTopRight", nullptr, flags))
            {
                struct GizmoBtn { EGizmoMode mode; const char* tip; };
                GizmoBtn btns[3] = {
                    { EGizmoMode::Translate, "Translate (W)" },
                    { EGizmoMode::Rotate,    "Rotate (E)"    },
                    { EGizmoMode::Scale,     "Scale (R)"     },
                };

                for (int i = 0; i < 3; i++)
                {
                    bool active = (m_GizmoMode == btns[i].mode);

                    ImVec4 btnCol    = active
                        ? ImVec4(0.18f, 0.50f, 0.82f, 1.0f)
                        : ImVec4(0.20f, 0.26f, 0.32f, 0.92f);
                    ImVec4 hoverCol  = ImVec4(0.28f, 0.58f, 0.90f, 1.0f);
                    ImVec4 activeCol = ImVec4(0.14f, 0.42f, 0.72f, 1.0f);

                    ImGui::PushStyleColor(ImGuiCol_Button,        btnCol);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverCol);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  activeCol);

                    if (ImGui::Button(("##gtr" + std::to_string(i)).c_str(),
                                      ImVec2(gizmoBtnSize, gizmoBtnSize)))
                        m_GizmoMode = btns[i].mode;

                    ImGui::PopStyleColor(3);

                    // Custom icon drawn over the button area
                    ImVec2 bMin = ImGui::GetItemRectMin();
                    ImVec2 bMax = ImGui::GetItemRectMax();
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImVec2 ctr = ImVec2((bMin.x + bMax.x) * 0.5f, (bMin.y + bMax.y) * 0.5f);
                    ImU32 iconCol = active
                        ? IM_COL32(255, 255, 255, 255)
                        : IM_COL32(160, 180, 200, 220);

                    if (btns[i].mode == EGizmoMode::Translate)
                    {
                        // Move: cross arrows icon
                        float arm = 7.0f, head = 3.0f;
                        // horizontal arrow
                        dl->AddLine(ImVec2(ctr.x - arm, ctr.y), ImVec2(ctr.x + arm, ctr.y), iconCol, 1.5f);
                        dl->AddTriangleFilled(
                            ImVec2(ctr.x + arm,       ctr.y),
                            ImVec2(ctr.x + arm - head, ctr.y - head * 0.6f),
                            ImVec2(ctr.x + arm - head, ctr.y + head * 0.6f), iconCol);
                        dl->AddTriangleFilled(
                            ImVec2(ctr.x - arm,       ctr.y),
                            ImVec2(ctr.x - arm + head, ctr.y - head * 0.6f),
                            ImVec2(ctr.x - arm + head, ctr.y + head * 0.6f), iconCol);
                        // vertical arrow
                        dl->AddLine(ImVec2(ctr.x, ctr.y - arm), ImVec2(ctr.x, ctr.y + arm), iconCol, 1.5f);
                        dl->AddTriangleFilled(
                            ImVec2(ctr.x,             ctr.y - arm),
                            ImVec2(ctr.x - head*0.6f, ctr.y - arm + head),
                            ImVec2(ctr.x + head*0.6f, ctr.y - arm + head), iconCol);
                        dl->AddTriangleFilled(
                            ImVec2(ctr.x,             ctr.y + arm),
                            ImVec2(ctr.x - head*0.6f, ctr.y + arm - head),
                            ImVec2(ctr.x + head*0.6f, ctr.y + arm - head), iconCol);
                    }
                    else if (btns[i].mode == EGizmoMode::Rotate)
                    {
                        // Rotate: circular arc with arrowhead
                        float r = 7.0f;
                        const int arcSegs = 20;
                        float startAngle = 0.3f;
                        float endAngle   = 2.0f * 3.14159f - 0.3f;
                        for (int s = 0; s < arcSegs; s++)
                        {
                            float a0 = startAngle + (endAngle - startAngle) * s / arcSegs;
                            float a1 = startAngle + (endAngle - startAngle) * (s + 1) / arcSegs;
                            dl->AddLine(
                                ImVec2(ctr.x + r * cosf(a0), ctr.y + r * sinf(a0)),
                                ImVec2(ctr.x + r * cosf(a1), ctr.y + r * sinf(a1)),
                                iconCol, 1.8f);
                        }
                        // Arrowhead at end
                        float ae = endAngle;
                        float tang = ae + 3.14159f * 0.5f;
                        ImVec2 tip(ctr.x + r * cosf(ae), ctr.y + r * sinf(ae));
                        float hs = 3.5f;
                        dl->AddTriangleFilled(
                            tip,
                            ImVec2(tip.x + hs * cosf(tang - 2.4f), tip.y + hs * sinf(tang - 2.4f)),
                            ImVec2(tip.x + hs * cosf(tang + 2.4f), tip.y + hs * sinf(tang + 2.4f)),
                            iconCol);
                    }
                    else // Scale
                    {
                        // Scale: 3 axis stubs with square end caps
                        float arm = 6.5f, sq = 2.2f;
                        // X axis (right, red-ish but use iconCol)
                        dl->AddLine(ImVec2(ctr.x, ctr.y), ImVec2(ctr.x + arm, ctr.y), iconCol, 1.5f);
                        dl->AddRectFilled(
                            ImVec2(ctr.x + arm - sq, ctr.y - sq),
                            ImVec2(ctr.x + arm + sq, ctr.y + sq), iconCol);
                        // Y axis (up)
                        dl->AddLine(ImVec2(ctr.x, ctr.y), ImVec2(ctr.x, ctr.y - arm), iconCol, 1.5f);
                        dl->AddRectFilled(
                            ImVec2(ctr.x - sq, ctr.y - arm - sq),
                            ImVec2(ctr.x + sq, ctr.y - arm + sq), iconCol);
                        // Z axis (diagonal hint)
                        float d = arm * 0.70f;
                        dl->AddLine(ImVec2(ctr.x, ctr.y), ImVec2(ctr.x - d, ctr.y + d), iconCol, 1.5f);
                        dl->AddRectFilled(
                            ImVec2(ctr.x - d - sq, ctr.y + d - sq),
                            ImVec2(ctr.x - d + sq, ctr.y + d + sq), iconCol);
                    }

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(btns[i].tip);
                        ImGui::EndTooltip();
                    }

                    if (i < 2) ImGui::SameLine();
                }
            }
            ImGui::End();
            ImGui::PopStyleVar(4);
        }
    }

    // ============================================================
    // UI
    // ============================================================

    void DrawUI()
    {
        float menuBarHeight = ImGui::GetFrameHeight();

        // Get main viewport position (for Multi-Viewport mode, this is the main window's screen position)
        ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImVec2 vpPos = mainViewport->Pos;
        ImVec2 vpSize = mainViewport->Size;

        // Side panel below menu bar — anchored to main window, not screen
        ImGui::SetNextWindowPos(ImVec2(vpPos.x, vpPos.y + menuBarHeight), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(320, vpSize.y - menuBarHeight), ImGuiCond_Always);

        ImGui::Begin("Scene Panel", nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        // Show current RHI
        auto rhiType = GetCurrentRHIType();
        const char* rhiName = (rhiType == RHI_API_TYPE::DX11) ? "Direct3D 11" :
                              (rhiType == RHI_API_TYPE::DX12) ? "Direct3D 12" :
                              (rhiType == RHI_API_TYPE::OPENGL) ? "OpenGL" :
                              (rhiType == RHI_API_TYPE::VULKAN) ? "Vulkan" : "Unknown";
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "RHI: %s", rhiName);
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

    // ============================================================
    // Content Browser — floating window showing all engine resources
    // ============================================================

    void DrawContentBrowser()
    {
        if (!m_ShowContentBrowser) return;

        namespace fs = std::filesystem;

        ImGui::SetNextWindowSize(ImVec2(720, 450), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Content Browser", &m_ShowContentBrowser))
        {
            ImGui::End();
            return;
        }

        // ---- Resource folders ----
        struct ContentFolder
        {
            const char* Name;
            const char* Icon;
            std::string Path;
        };

        ContentFolder folders[] = {
            { "Scenes",             "[S] ",  m_ScenesDir },
            { "Shaders",            "[SH] ", m_ShaderDir },
            { "GLShaders",          "[GL] ", m_GLShaderDir },
            { "PostProcessShaders", "[PP] ", m_PostProcessShaderDir },
            { "Textures",           "[TX] ", m_TexturesDir },
            { "Materials",          "[MT] ", m_MaterialsDir },
        };
        int folderCount = sizeof(folders) / sizeof(folders[0]);

        // If no folder selected, default to first
        if (m_ContentBrowserSelectedDir.empty())
            m_ContentBrowserSelectedDir = folders[0].Path;

        // ---- Left panel: folder tree ----
        ImGui::BeginChild("CB_FolderTree", ImVec2(180, 0), true);

        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Folders");
        ImGui::Separator();

        for (int i = 0; i < folderCount; i++)
        {
            auto& f = folders[i];
            if (f.Path.empty()) continue;

            bool selected = (m_ContentBrowserSelectedDir == f.Path);
            std::string label = std::string(f.Icon) + f.Name;
            if (ImGui::Selectable(label.c_str(), selected))
            {
                m_ContentBrowserSelectedDir = f.Path;
            }
        }

        ImGui::EndChild();

        ImGui::SameLine();

        // ---- Right panel: file list ----
        ImGui::BeginChild("CB_FileList", ImVec2(0, 0), true);

        // Find current folder name for display
        std::string currentFolderName = "Content";
        for (int i = 0; i < folderCount; i++)
        {
            if (folders[i].Path == m_ContentBrowserSelectedDir)
            {
                currentFolderName = folders[i].Name;
                break;
            }
        }

        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", currentFolderName.c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f);
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Path:");
        ImGui::Separator();

        // Small path display
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "%s", m_ContentBrowserSelectedDir.c_str());
        ImGui::Separator();

        // Enumerate files
        std::error_code ec;
        if (fs::exists(m_ContentBrowserSelectedDir, ec) && fs::is_directory(m_ContentBrowserSelectedDir, ec))
        {
            // Collect files first for sorting
            struct FileEntry
            {
                std::string Name;
                std::string Extension;
                std::string FullPath;
                uintmax_t Size;
            };
            std::vector<FileEntry> files;

            for (const auto& entry : fs::directory_iterator(m_ContentBrowserSelectedDir, ec))
            {
                if (!entry.is_regular_file()) continue;
                std::string name = entry.path().filename().string();
                std::string ext = entry.path().extension().string();
                // Skip README files
                if (name == "README.txt" || name == "README.md") continue;

                uintmax_t size = 0;
                std::error_code szEc;
                size = fs::file_size(entry.path(), szEc);

                files.push_back({ name, ext, entry.path().string(), size });
            }

            // Sort alphabetically
            std::sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b) {
                return a.Name < b.Name;
            });

            // Table display
            if (ImGui::BeginTable("ContentFiles", 3,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
            {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableHeadersRow();

                for (const auto& file : files)
                {
                    ImGui::TableNextRow();

                    // Name column (with icon)
                    ImGui::TableSetColumnIndex(0);
                    const char* icon = GetContentIcon(file.Extension);
                    ImVec4 iconColor = GetContentIconColor(file.Extension);
                    ImGui::TextColored(iconColor, "%s", icon);
                    ImGui::SameLine();

                    // Selectable filename
                    std::string stem = fs::path(file.Name).stem().string();
                    if (ImGui::Selectable(stem.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
                    {
                        if (ImGui::IsMouseDoubleClicked(0))
                        {
                            OnContentDoubleClick(file.FullPath, file.Extension);
                        }
                    }

                    // Drag-drop source: texture files only
                    bool isTexExt = (file.Extension == ".png" || file.Extension == ".jpg" || file.Extension == ".jpeg"
                                  || file.Extension == ".bmp" || file.Extension == ".tga"
                                  || file.Extension == ".PNG" || file.Extension == ".JPG"
                                  || file.Extension == ".BMP" || file.Extension == ".TGA");
                    if (isTexExt && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                    {
                        // payload = just the filename (relative to Textures/)
                        const std::string& fname = file.Name;
                        ImGui::SetDragDropPayload("KIWI_TEXTURE", fname.c_str(), fname.size() + 1);
                        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "[Tex] %s", stem.c_str());
                        ImGui::EndDragDropSource();
                    }

                    // Right-click context menu
                    if (ImGui::BeginPopupContextItem(("##ctx_" + file.Name).c_str()))
                    {
                        if (ImGui::MenuItem("Show In Explorer"))
                        {
                            // Open Explorer with file selected
                            std::string cmd = "explorer /select,\"" + file.FullPath + "\"";
                            system(cmd.c_str());
                        }
                        if (ImGui::MenuItem("Open File"))
                        {
                            // Open with default associated application
                            ShellExecuteA(nullptr, "open", file.FullPath.c_str(), nullptr, nullptr, SW_SHOW);
                        }
                        ImGui::EndPopup();
                    }

                    // Tooltip with full path
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::Text("%s", file.FullPath.c_str());
                        ImGui::EndTooltip();
                    }

                    // Type column
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", GetContentTypeName(file.Extension));

                    // Size column
                    ImGui::TableSetColumnIndex(2);
                    if (file.Size < 1024)
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%llu B", (unsigned long long)file.Size);
                    else if (file.Size < 1024 * 1024)
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%.1f KB", (float)file.Size / 1024.0f);
                    else
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%.1f MB", (float)file.Size / (1024.0f * 1024.0f));
                }

                ImGui::EndTable();
            }

            if (files.empty())
            {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(empty folder)");
            }
        }
        else
        {
            ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.4f, 1.0f), "Folder not found: %s", m_ContentBrowserSelectedDir.c_str());
        }

        ImGui::EndChild();
        ImGui::End();
    }

    // ---- Content Browser helpers ----

    static const char* GetContentIcon(const std::string& ext)
    {
        if (ext == ".json") return "[Scene]";
        if (ext == ".hlsl" || ext == ".HLSL") return "[HLSL]";
        if (ext == ".glsl" || ext == ".GLSL") return "[GLSL]";
        if (ext == ".mat") return "[Mat]";
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
            ext == ".bmp" || ext == ".tga" || ext == ".PNG" ||
            ext == ".JPG" || ext == ".BMP" || ext == ".TGA") return "[Tex]";
        return "[?]";
    }

    static ImVec4 GetContentIconColor(const std::string& ext)
    {
        if (ext == ".json") return ImVec4(0.3f, 0.9f, 0.5f, 1.0f); // green
        if (ext == ".hlsl" || ext == ".HLSL") return ImVec4(0.4f, 0.6f, 1.0f, 1.0f); // blue
        if (ext == ".glsl" || ext == ".GLSL") return ImVec4(0.6f, 0.4f, 1.0f, 1.0f); // purple
        if (ext == ".mat") return ImVec4(0.9f, 0.5f, 0.7f, 1.0f); // pink
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
            ext == ".bmp" || ext == ".tga" || ext == ".PNG" ||
            ext == ".JPG" || ext == ".BMP" || ext == ".TGA") return ImVec4(1.0f, 0.7f, 0.3f, 1.0f); // orange
        return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    }

    static const char* GetContentTypeName(const std::string& ext)
    {
        if (ext == ".json") return "Scene";
        if (ext == ".hlsl" || ext == ".HLSL") return "Shader";
        if (ext == ".glsl" || ext == ".GLSL") return "GL Shader";
        if (ext == ".mat") return "Material";
        if (ext == ".png") return "PNG";
        if (ext == ".jpg" || ext == ".jpeg") return "JPEG";
        if (ext == ".bmp") return "BMP";
        if (ext == ".tga") return "TGA";
        return "File";
    }

    void OnContentDoubleClick(const std::string& fullPath, const std::string& ext)
    {
        if (ext == ".json")
        {
            // Load scene
            if (m_Scene.LoadFromFile(fullPath))
            {
                RebuildAllGPUBuffers();
                std::cout << "[Kiwi] Content Browser: Loaded scene: " << fullPath << std::endl;
            }
        }
        else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
        {
            // Pre-load texture into TextureManager
            GPUTexture* tex = m_TextureManager.LoadTexture(fullPath);
            if (tex)
            {
                std::cout << "[Kiwi] Content Browser: Loaded texture: " << fullPath
                          << " (" << tex->Width << "x" << tex->Height << ")" << std::endl;
            }
        }
        else if (ext == ".mat")
        {
            // Open material editor
            namespace fs = std::filesystem;
            std::string matName = fs::path(fullPath).stem().string();
            // Ensure it's loaded in MaterialLibrary
            Material* mat = m_MaterialLibrary.GetMaterial(matName);
            if (!mat)
            {
                auto newMat = std::make_unique<Material>();
                if (newMat->LoadFromFile(fullPath))
                {
                    matName = newMat->Name.empty() ? matName : newMat->Name;
                    m_MaterialLibrary.AddMaterial(std::move(newMat));
                }
            }
            m_MaterialEditorTarget = matName;
            m_ShowMaterialEditor = true;
        }
    }

    // ============================================================
    // Material Editor Window
    // ============================================================

    // ---- Texture Picker Popup ----
    // Opens a modal listing all textures in Textures/ and lets the user pick one.
    void DrawTexturePicker()
    {
        if (!m_ShowTexturePicker) return;

        ImGui::OpenPopup("##KiwiTexPicker");
        m_ShowTexturePicker = false;
    }

    void DrawTexturePickerModal()
    {
        if (!ImGui::BeginPopupModal("##KiwiTexPicker", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
            return;

        ImGui::TextColored(ImVec4(0.9f, 0.7f, 1.0f, 1.0f), "Select Texture");
        ImGui::Separator();
        ImGui::Spacing();

        // List textures from Textures/ folder
        namespace fs = std::filesystem;
        static const char* kTexExts[] = { ".png", ".jpg", ".jpeg", ".bmp", ".tga",
                                           ".PNG", ".JPG", ".BMP", ".TGA", nullptr };
        auto isTexFile = [&](const std::string& ext) {
            for (int i = 0; kTexExts[i]; i++)
                if (ext == kTexExts[i]) return true;
            return false;
        };

        bool selected = false;
        if (fs::exists(m_TexturesDir))
        {
            for (auto& entry : fs::directory_iterator(m_TexturesDir))
            {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                if (!isTexFile(ext)) continue;

                std::string fname = entry.path().filename().string();
                std::string stem  = entry.path().stem().string();

                // Color swatch placeholder
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "[Tex]");
                ImGui::SameLine();
                if (ImGui::Selectable(fname.c_str(), false, 0, ImVec2(280, 0)))
                {
                    // Set the property
                    Material* mat = m_MaterialLibrary.GetMaterial(m_TexturePickerMatTarget);
                    if (mat)
                        mat->SetTexture(m_TexturePickerPropKey, fname);
                    m_TexturePickerMatTarget.clear();
                    m_TexturePickerPropKey.clear();
                    selected = true;
                    ImGui::CloseCurrentPopup();
                }
            }
        }

        if (!selected)
        {
            ImGui::Spacing();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // Helper: draw a single texture slot row
    // Returns true if value changed.
    // slotLabel: display label (e.g. "Base Color")
    // propKey: material property key (e.g. "_BaseColorTex")
    // uniqueId: unique string for ImGui ID disambiguation
    // mat: material to read/write
    void DrawTextureSlotRow(const std::string& slotLabel, const std::string& propKey,
                            const std::string& uniqueId, Material* mat)
    {
        std::string texPath = mat->GetTexture(propKey);

        // Slot label column (fixed width)
        ImGui::Text("%-11s", slotLabel.c_str());
        ImGui::SameLine();

        // Read-only display (grey input box)
        float pickBtnW = 52.0f;
        float clearBtnW = texPath.empty() ? 0.0f : 24.0f;
        float inputW = ImGui::GetContentRegionAvail().x - pickBtnW - clearBtnW - 8.0f;
        ImGui::SetNextItemWidth(inputW > 40 ? inputW : 40);

        // Display filename only (strip path)
        namespace fs = std::filesystem;
        std::string displayName = texPath.empty() ? "" : fs::path(texPath).filename().string();
        char buf[256] = {};
        strncpy(buf, displayName.c_str(), sizeof(buf) - 1);

        // Disabled read-only text field (acts as drop target display)
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
        ImGui::InputText(("##texslot_" + uniqueId).c_str(), buf, sizeof(buf),
                         ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();

        // Accept DragDrop from Content Browser
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("KIWI_TEXTURE"))
            {
                const char* droppedFile = static_cast<const char*>(payload->Data);
                mat->SetTexture(propKey, droppedFile);
            }
            ImGui::EndDragDropTarget();
        }

        // Pick button
        ImGui::SameLine();
        if (ImGui::Button(("Pick##pick_" + uniqueId).c_str(), ImVec2(pickBtnW, 0)))
        {
            m_TexturePickerMatTarget = mat->Name;
            m_TexturePickerPropKey   = propKey;
            m_ShowTexturePicker      = true;
        }

        // Clear (X) button
        if (!texPath.empty())
        {
            ImGui::SameLine();
            if (ImGui::SmallButton(("X##clr_" + uniqueId).c_str()))
                mat->SetTexture(propKey, "");
        }
    }

    void DrawMaterialEditor()
    {
        if (!m_ShowMaterialEditor) return;

        Material* mat = m_MaterialLibrary.GetMaterial(m_MaterialEditorTarget);
        if (!mat)
        {
            m_ShowMaterialEditor = false;
            return;
        }

        // Trigger texture picker popup if requested
        DrawTexturePicker();
        DrawTexturePickerModal();

        ImGuiIO& io = ImGui::GetIO();
        ImVec2 vp = ImGui::GetMainViewport()->Pos;

        // First-time position: center of main window
        ImGui::SetNextWindowSize(ImVec2(460.0f, 580.0f), ImGuiCond_Once);
        ImGui::SetNextWindowPos(
            ImVec2(vp.x + io.DisplaySize.x * 0.5f - 230.0f,
                   vp.y + io.DisplaySize.y * 0.5f - 290.0f),
            ImGuiCond_Once);

        std::string title = "Material Editor - " + mat->Name + "###KiwiMatEditor";
        bool open = true;
        if (ImGui::Begin(title.c_str(), &open,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking))
        {
            // ---- Header: material name ----
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 1.0f, 1.0f));
            ImGui::TextUnformatted(mat->Name.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 8);
            ImGui::TextDisabled("(.mat)");

            ImGui::Separator();

            // ---- Shader Selection ----
            ImGui::Text("Shader");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            const auto& shaderNames = m_ShaderLibrary.GetShaderNames();
            if (ImGui::BeginCombo("##MatEdShader", mat->ShaderName.c_str()))
            {
                for (const auto& sn : shaderNames)
                {
                    bool sel2 = (sn == mat->ShaderName);
                    if (ImGui::Selectable(sn.c_str(), sel2))
                        mat->ShaderName = sn;
                    if (sel2) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::Spacing();
            ImGui::Separator();

            // ---- Properties ----
            // Try to get @Properties from shader source for driven UI
            CompiledShader* cs = m_ShaderLibrary.GetShader(mat->ShaderName);
            std::vector<ShaderPropertyDef> propDefs;
            if (cs)
            {
                std::string shaderPath = m_ShaderDir + "\\" + mat->ShaderName + ".hlsl";
                std::ifstream sf(shaderPath);
                if (sf.is_open())
                {
                    std::string src((std::istreambuf_iterator<char>(sf)),
                                     std::istreambuf_iterator<char>());
                    propDefs = ParseShaderProperties(src);
                }
            }

            ImGui::Text("Properties");
            ImGui::Spacing();

            if (!propDefs.empty())
            {
                for (auto& def : propDefs)
                {
                    switch (def.Type)
                    {
                    case EShaderPropertyType::Float:
                    {
                        float v = mat->GetFloat(def.Name, def.DefaultFloat);
                        if (ImGui::DragFloat((def.DisplayName + "##me_f").c_str(), &v, 0.01f))
                            mat->SetFloat(def.Name, v);
                        break;
                    }
                    case EShaderPropertyType::Range:
                    {
                        float v = mat->GetFloat(def.Name, def.DefaultFloat);
                        if (ImGui::SliderFloat((def.DisplayName + "##me_r").c_str(), &v,
                                               def.RangeMin, def.RangeMax))
                            mat->SetFloat(def.Name, v);
                        break;
                    }
                    case EShaderPropertyType::Color:
                    {
                        Vec4 c = mat->GetColor(def.Name, def.DefaultColor);
                        if (ImGui::ColorEdit4((def.DisplayName + "##me_c").c_str(), &c.x))
                            mat->SetColor(def.Name, c);
                        break;
                    }
                    case EShaderPropertyType::Texture2D:
                        DrawTextureSlotRow(def.DisplayName, def.Name,
                                           "prop_" + def.Name, mat);
                        break;
                    }
                }
            }
            else
            {
                // Fallback: standard DefaultLit properties
                // Color
                {
                    Vec4 c = mat->GetColor("_Color", { 0.8f, 0.8f, 0.8f, 1.0f });
                    if (ImGui::ColorEdit4("Color##me_color", &c.x))
                        mat->SetColor("_Color", c);
                }

                // Roughness
                {
                    float v = mat->GetFloat("_Roughness", 0.5f);
                    if (ImGui::SliderFloat("Roughness##me_r", &v, 0.0f, 1.0f))
                        mat->SetFloat("_Roughness", v);
                }

                // Metallic
                {
                    float v = mat->GetFloat("_Metallic", 0.0f);
                    if (ImGui::SliderFloat("Metallic##me_m", &v, 0.0f, 1.0f))
                        mat->SetFloat("_Metallic", v);
                }

                ImGui::Spacing();
                ImGui::Text("Textures");
                ImGui::Spacing();

                DrawTextureSlotRow("Base Color", "_BaseColorTex", "fb_bc", mat);
                DrawTextureSlotRow("Normal Map", "_NormalTex",    "fb_nm", mat);
                DrawTextureSlotRow("MR Map",     "_MetallicRoughnessTex", "fb_mr", mat);
            }

            // ---- Footer: Save + Close ----
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            bool saved = false;
            if (ImGui::Button("Save##MatEdSave", ImVec2(120, 0)))
            {
                saved = m_MaterialLibrary.SaveMaterial(mat->Name);
            }
            if (saved)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Saved!");
            }

            ImGui::SameLine();
            if (ImGui::Button("Close##MatEdClose", ImVec2(120, 0)))
                open = false;

            // Color preview swatch
            ImGui::SameLine(0, 16);
            Vec4 col = mat->GetColor("_Color", {0.8f, 0.8f, 0.8f, 1.0f});
            ImVec2 swatchPos = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                swatchPos,
                ImVec2(swatchPos.x + 24, swatchPos.y + 24),
                IM_COL32((int)(col.x*255), (int)(col.y*255), (int)(col.z*255), 255),
                4.0f);
            ImGui::Dummy(ImVec2(24, 24));
        }
        ImGui::End();

        if (!open)
        {
            m_ShowMaterialEditor = false;
            m_MaterialEditorTarget.clear();
        }
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

                    // ---- Material Selection ----
                    ImGui::Separator();
                    ImGui::Text("Material");
                    {
                        auto matNames = m_MaterialLibrary.GetMaterialNames();
                        if (ImGui::BeginCombo(("##MaterialCombo" + std::to_string(ci)).c_str(), mesh.MaterialName.c_str()))
                        {
                            for (const auto& name : matNames)
                            {
                                bool isSelected = (name == mesh.MaterialName);
                                if (ImGui::Selectable(name.c_str(), isSelected))
                                    mesh.MaterialName = name;
                                if (isSelected) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                    }

                    // ---- Material Properties (direct, no double-write) ----
                    Material* activeMat = m_MaterialLibrary.GetMaterial(mesh.MaterialName);
                    if (activeMat)
                    {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Shader: %s", activeMat->ShaderName.c_str());

                        ImGui::Separator();
                        ImGui::Text("Properties");

                        // Color
                        {
                            Vec4 color = activeMat->GetColor("_Color", { 0.8f, 0.8f, 0.8f, 1.0f });
                            if (ImGui::ColorEdit4(("Color##mat" + std::to_string(ci)).c_str(), &color.x))
                                activeMat->SetColor("_Color", color);
                        }

                        // Roughness
                        {
                            float roughness = activeMat->GetFloat("_Roughness", 0.5f);
                            if (ImGui::SliderFloat(("Roughness##mat" + std::to_string(ci)).c_str(), &roughness, 0.0f, 1.0f))
                                activeMat->SetFloat("_Roughness", roughness);
                        }

                        // Metallic
                        {
                            float metallic = activeMat->GetFloat("_Metallic", 0.0f);
                            if (ImGui::SliderFloat(("Metallic##mat" + std::to_string(ci)).c_str(), &metallic, 0.0f, 1.0f))
                                activeMat->SetFloat("_Metallic", metallic);
                        }

                        ImGui::Spacing();
                        ImGui::Text("Textures");
                        ImGui::Spacing();

                        DrawTextureSlotRow("Base Color", "_BaseColorTex",
                                           "ins_bc_" + std::to_string(ci), activeMat);
                        DrawTextureSlotRow("Normal Map", "_NormalTex",
                                           "ins_nm_" + std::to_string(ci), activeMat);
                        DrawTextureSlotRow("MR Map",     "_MetallicRoughnessTex",
                                           "ins_mr_" + std::to_string(ci), activeMat);

                        // Save material button
                        ImGui::Spacing();
                        if (ImGui::SmallButton(("Save Material##" + std::to_string(ci)).c_str()))
                            m_MaterialLibrary.SaveMaterial(mesh.MaterialName);

                        // Quick open material editor
                        ImGui::SameLine();
                        if (ImGui::SmallButton(("Edit...##" + std::to_string(ci)).c_str()))
                        {
                            m_MaterialEditorTarget = mesh.MaterialName;
                            m_ShowMaterialEditor   = true;
                        }
                    }

                    ImGui::Separator();
                    ImGui::Text("Rendering");
                    ImGui::DragInt(("Sort Order##" + std::to_string(ci)).c_str(), &mesh.SortOrder, 0.5f, -1000, 1000);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Higher values are rendered first.\nObjects with same order are sorted back-to-front.");

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

                        auto& dirLight = static_cast<DirectionalLightComponent&>(light);

                        ImGui::Separator();
                        ImGui::Text("Shadow (CSM)");

                        ImGui::Checkbox(("Cast Shadow##" + std::to_string(ci)).c_str(), &dirLight.CastShadow);

                        if (dirLight.CastShadow)
                        {
                            ImGui::SliderInt(("Cascades##" + std::to_string(ci)).c_str(), &dirLight.NumCascades, 1, 4);
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Number of shadow map cascades.\nMore cascades = better quality at distance,\nbut more GPU cost.");

                            // Shadow map resolution dropdown
                            const int resolutions[] = { 512, 1024, 2048, 4096 };
                            const char* resLabels[] = { "512", "1024", "2048", "4096" };
                            int resIdx = 2; // default to 2048
                            for (int ri = 0; ri < 4; ri++)
                            {
                                if (resolutions[ri] == dirLight.ShadowMapResolution)
                                {
                                    resIdx = ri;
                                    break;
                                }
                            }
                            if (ImGui::Combo(("Resolution##shadow" + std::to_string(ci)).c_str(), &resIdx, resLabels, 4))
                            {
                                dirLight.ShadowMapResolution = resolutions[resIdx];
                            }
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Shadow map resolution per cascade.\nHigher = sharper shadows, more VRAM.");

                            ImGui::DragFloat(("Shadow Distance##" + std::to_string(ci)).c_str(), &dirLight.ShadowDistance, 0.5f, 1.0f, 500.0f);
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Maximum distance from camera\nwhere shadows are rendered.");

                            ImGui::SliderFloat(("Split Lambda##" + std::to_string(ci)).c_str(), &dirLight.CascadeSplitLambda, 0.0f, 1.0f);
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Cascade split scheme.\n0 = uniform splits\n1 = logarithmic splits\n0.75 is a good balance.");

                            ImGui::DragFloat(("Shadow Bias##" + std::to_string(ci)).c_str(), &dirLight.ShadowBias, 0.0001f, 0.0f, 0.05f, "%.4f");
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Depth bias to reduce shadow acne.\nToo high = peter panning.");

                            ImGui::DragFloat(("Normal Bias##" + std::to_string(ci)).c_str(), &dirLight.NormalBias, 0.001f, 0.0f, 0.1f, "%.3f");
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Normal offset bias.\nHelps with self-shadowing artifacts.");

                            ImGui::SliderFloat(("Shadow Strength##" + std::to_string(ci)).c_str(), &dirLight.ShadowStrength, 0.0f, 1.0f);
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Shadow darkness.\n0 = no shadow, 1 = full shadow.");
                        }
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

        // Rotation rings: ring normal = axis (X ring rotates around X, etc.)
        m_GizmoRingData[0] = CreateGizmoRing({ 1, 0, 0 }, { 1, 0.2f, 0.2f, 1 });
        m_GizmoRingData[1] = CreateGizmoRing({ 0, 1, 0 }, { 0.2f, 1, 0.2f, 1 });
        m_GizmoRingData[2] = CreateGizmoRing({ 0, 0, 1 }, { 0.2f, 0.4f, 1, 1 });

        // Scale axes: shaft + cube end cap
        m_GizmoScaleData[0] = CreateGizmoScaleAxis({ 1, 0, 0 }, { 1, 0.2f, 0.2f, 1 });
        m_GizmoScaleData[1] = CreateGizmoScaleAxis({ 0, 1, 0 }, { 0.2f, 1, 0.2f, 1 });
        m_GizmoScaleData[2] = CreateGizmoScaleAxis({ 0, 0, 1 }, { 0.2f, 0.4f, 1,   1 });

        // Direction indicator for directional lights (longer arrow, yellow)
        m_DirLightIndicator = CreateGizmoArrow({ 0, 0, 1 }, { 1, 1, 0, 1 }, 2.0f, 0.03f, 0.08f, 0.3f);
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

            static const char* gizmoAxisNames[] = { "GizmoVB_X", "GizmoVB_Y", "GizmoVB_Z" };
            static const char* gizmoAxisIBNames[] = { "GizmoIB_X", "GizmoIB_Y", "GizmoIB_Z" };

            BufferDesc vbDesc;
            vbDesc.SizeInBytes = m_GizmoVertexCount[i] * sizeof(Vertex);
            vbDesc.BindFlags = BUFFER_USAGE_VERTEX;
            vbDesc.Usage = EResourceUsage::Immutable;
            vbDesc.DebugName = (i < 3) ? gizmoAxisNames[i] : "GizmoVB";
            m_GizmoVB[i] = device->CreateBuffer(vbDesc, data.Vertices.data());

            BufferDesc ibDesc;
            ibDesc.SizeInBytes = m_GizmoIndexCount[i] * sizeof(uint32_t);
            ibDesc.BindFlags = BUFFER_USAGE_INDEX;
            ibDesc.Usage = EResourceUsage::Immutable;
            ibDesc.DebugName = (i < 3) ? gizmoAxisIBNames[i] : "GizmoIB";
            m_GizmoIB[i] = device->CreateBuffer(ibDesc, data.Indices.data());
        }

        // Build direction indicator GPU buffers
        {
            auto& data = m_DirLightIndicator;
            m_DirLightIndicatorVertexCount = (uint32_t)data.Vertices.size();
            m_DirLightIndicatorIndexCount = (uint32_t)data.Indices.size();

            if (m_DirLightIndicatorVertexCount > 0)
            {
                BufferDesc vbDesc;
                vbDesc.SizeInBytes = m_DirLightIndicatorVertexCount * sizeof(Vertex);
                vbDesc.BindFlags = BUFFER_USAGE_VERTEX;
                vbDesc.Usage = EResourceUsage::Immutable;
                vbDesc.DebugName = "GizmoVB_DirLight";
                m_DirLightIndicatorVB = device->CreateBuffer(vbDesc, data.Vertices.data());

                BufferDesc ibDesc;
                ibDesc.SizeInBytes = m_DirLightIndicatorIndexCount * sizeof(uint32_t);
                ibDesc.BindFlags = BUFFER_USAGE_INDEX;
                ibDesc.Usage = EResourceUsage::Immutable;
                ibDesc.DebugName = "GizmoIB_DirLight";
                m_DirLightIndicatorIB = device->CreateBuffer(ibDesc, data.Indices.data());
            }
        }

        // Build rotation ring GPU buffers
        {
            static const char* ringVBNames[] = { "GizmoRingVB_X", "GizmoRingVB_Y", "GizmoRingVB_Z" };
            static const char* ringIBNames[] = { "GizmoRingIB_X", "GizmoRingIB_Y", "GizmoRingIB_Z" };
            for (int i = 0; i < 3; i++)
            {
                auto& data = m_GizmoRingData[i];
                m_GizmoRingVertexCount[i] = (uint32_t)data.Vertices.size();
                m_GizmoRingIndexCount[i]  = (uint32_t)data.Indices.size();
                if (m_GizmoRingVertexCount[i] == 0) continue;

                BufferDesc vbDesc;
                vbDesc.SizeInBytes = m_GizmoRingVertexCount[i] * sizeof(Vertex);
                vbDesc.BindFlags = BUFFER_USAGE_VERTEX;
                vbDesc.Usage = EResourceUsage::Immutable;
                vbDesc.DebugName = ringVBNames[i];
                m_GizmoRingVB[i] = device->CreateBuffer(vbDesc, data.Vertices.data());

                BufferDesc ibDesc;
                ibDesc.SizeInBytes = m_GizmoRingIndexCount[i] * sizeof(uint32_t);
                ibDesc.BindFlags = BUFFER_USAGE_INDEX;
                ibDesc.Usage = EResourceUsage::Immutable;
                ibDesc.DebugName = ringIBNames[i];
                m_GizmoRingIB[i] = device->CreateBuffer(ibDesc, data.Indices.data());
            }
        }

        // Build scale axis GPU buffers
        {
            static const char* scaleVBNames[] = { "GizmoScaleVB_X", "GizmoScaleVB_Y", "GizmoScaleVB_Z" };
            static const char* scaleIBNames[] = { "GizmoScaleIB_X", "GizmoScaleIB_Y", "GizmoScaleIB_Z" };
            for (int i = 0; i < 3; i++)
            {
                auto& data = m_GizmoScaleData[i];
                m_GizmoScaleVertexCount[i] = (uint32_t)data.Vertices.size();
                m_GizmoScaleIndexCount[i]  = (uint32_t)data.Indices.size();
                if (m_GizmoScaleVertexCount[i] == 0) continue;

                BufferDesc vbDesc;
                vbDesc.SizeInBytes = m_GizmoScaleVertexCount[i] * sizeof(Vertex);
                vbDesc.BindFlags = BUFFER_USAGE_VERTEX;
                vbDesc.Usage = EResourceUsage::Immutable;
                vbDesc.DebugName = scaleVBNames[i];
                m_GizmoScaleVB[i] = device->CreateBuffer(vbDesc, data.Vertices.data());

                BufferDesc ibDesc;
                ibDesc.SizeInBytes = m_GizmoScaleIndexCount[i] * sizeof(uint32_t);
                ibDesc.BindFlags = BUFFER_USAGE_INDEX;
                ibDesc.Usage = EResourceUsage::Immutable;
                ibDesc.DebugName = scaleIBNames[i];
                m_GizmoScaleIB[i] = device->CreateBuffer(ibDesc, data.Indices.data());
            }
        }
    }

    // Compute gizmo scale factor so it maintains constant screen size regardless of camera distance
    float ComputeGizmoScale(const Vec3& gizmoPos) const
    {
        Vec3 diff = gizmoPos - m_CameraPosition;
        float dist = diff.Length();
        // Scale factor: at referenceDistance the gizmo is 1x size. Closer = smaller, farther = bigger.
        // This keeps it roughly the same pixel size on screen.
        const float referenceDistance = 8.0f;
        return std::max(0.1f, dist / referenceDistance);
    }

    void DrawGizmo(RHICommandContext* ctx)
    {
        SceneObject* sel = m_Scene.GetSelectedObject();
        if (!sel) return;

        // Skip gizmo for the active Main Camera (you're looking through it)
        auto* camComp = sel->GetComponent<CameraComponent>();
        if (camComp && camComp->IsMainCamera)
            return;

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
        float gizmoScale = ComputeGizmoScale(gizmoPos);

        Vec4 colors[3] = {
            { 1.0f, 0.2f, 0.2f, 1.0f }, // X - Red
            { 0.2f, 1.0f, 0.2f, 1.0f }, // Y - Green
            { 0.2f, 0.4f, 1.0f, 1.0f }, // Z - Blue
        };

        // Highlight the dragged/hovered axis
        if (m_IsDragging)
        {
            int axisIdx = (int)m_DragAxis - 1;
            if (axisIdx >= 0 && axisIdx < 3)
                colors[axisIdx] = { 1.0f, 1.0f, 0.3f, 1.0f }; // Yellow highlight
        }

        // Helper lambda: submit one piece of gizmo geometry
        auto submitMesh = [&](RHIBuffer* vb, RHIBuffer* ib,
                               uint32_t vtxCount, uint32_t idxCount,
                               const Mat4& world, const Vec4& color)
        {
            if (!vb || !ib || vtxCount == 0 || idxCount == 0) return;

            VertexBufferView vbView;
            vbView.BufferLocation = 0;
            vbView.SizeInBytes    = vtxCount * sizeof(Vertex);
            vbView.StrideInBytes  = sizeof(Vertex);
            RHIBuffer* vbPtr = vb;
            ctx->SetVertexBuffers(0, &vbPtr, &vbView, 1);

            IndexBufferView ibView;
            ibView.BufferLocation = 0;
            ibView.SizeInBytes    = idxCount * sizeof(uint32_t);
            ibView.Format         = EFormat::R32_UINT;
            ctx->SetIndexBuffer(ib, &ibView);

            ObjectUniformBuffer oub = {};
            memcpy(oub.WorldMatrix, world.m, sizeof(world.m));
            oub.ObjectColor[0] = color.x;
            oub.ObjectColor[1] = color.y;
            oub.ObjectColor[2] = color.z;
            oub.ObjectColor[3] = color.w;
            oub.Selected    = 2.0f; // Unlit/gizmo mode
            oub.ObjectPadding[0] = oub.ObjectPadding[1] = 0.0f;

            void* mapped = m_ObjectUB->Map();
            if (mapped) { memcpy(mapped, &oub, sizeof(oub)); m_ObjectUB->Unmap(); }
            ctx->SetConstantBuffer(1, m_ObjectUB.get());
            ctx->DrawIndexed(idxCount, 0, 0);
        };

        Mat4 scaleMat = Mat4::Scaling(gizmoScale, gizmoScale, gizmoScale);
        Mat4 transMat = Mat4::Translation(gizmoPos.x, gizmoPos.y, gizmoPos.z);
        Mat4 baseWorld = scaleMat * transMat; // scale then translate

        if (m_GizmoMode == EGizmoMode::Translate)
        {
            for (int i = 0; i < 3; i++)
                submitMesh(m_GizmoVB[i].get(), m_GizmoIB[i].get(),
                           m_GizmoVertexCount[i], m_GizmoIndexCount[i],
                           baseWorld, colors[i]);
        }
        else if (m_GizmoMode == EGizmoMode::Rotate)
        {
            for (int i = 0; i < 3; i++)
                submitMesh(m_GizmoRingVB[i].get(), m_GizmoRingIB[i].get(),
                           m_GizmoRingVertexCount[i], m_GizmoRingIndexCount[i],
                           baseWorld, colors[i]);
        }
        else if (m_GizmoMode == EGizmoMode::Scale)
        {
            for (int i = 0; i < 3; i++)
                submitMesh(m_GizmoScaleVB[i].get(), m_GizmoScaleIB[i].get(),
                           m_GizmoScaleVertexCount[i], m_GizmoScaleIndexCount[i],
                           baseWorld, colors[i]);
        }

        // ---- Directional Light Direction Indicator ----
        auto* dirLight = sel->GetComponent<DirectionalLightComponent>();
        if (dirLight && m_DirLightIndicatorVB && m_DirLightIndicatorIB)
        {
            Vec3 fwd = dirLight->GetForward();
            Vec3 up2 = dirLight->GetUp();
            Vec3 right = dirLight->GetRight();

            Mat4 rotMat = Mat4::Identity();
            rotMat.m[0][0] = right.x; rotMat.m[0][1] = right.y; rotMat.m[0][2] = right.z;
            rotMat.m[1][0] = up2.x;   rotMat.m[1][1] = up2.y;   rotMat.m[1][2] = up2.z;
            rotMat.m[2][0] = fwd.x;   rotMat.m[2][1] = fwd.y;   rotMat.m[2][2] = fwd.z;
            Mat4 worldDL = scaleMat * rotMat * transMat;

            submitMesh(m_DirLightIndicatorVB.get(), m_DirLightIndicatorIB.get(),
                       m_DirLightIndicatorVertexCount, m_DirLightIndicatorIndexCount,
                       worldDL, { 1.0f, 0.9f, 0.2f, 1.0f });
        }
    }


    // Project a world-space point to screen-space pixel coordinates
    Vec2 WorldToScreen(const Vec3& worldPos, uint32_t screenW, uint32_t screenH,
                       const Mat4& view, const Mat4& proj) const
    {
        // Transform to clip space: pos * View * Proj (row-major, left-multiply)
        Mat4 vp = view * proj;
        float x = worldPos.x * vp.m[0][0] + worldPos.y * vp.m[1][0] + worldPos.z * vp.m[2][0] + vp.m[3][0];
        float y = worldPos.x * vp.m[0][1] + worldPos.y * vp.m[1][1] + worldPos.z * vp.m[2][1] + vp.m[3][1];
        float w = worldPos.x * vp.m[0][3] + worldPos.y * vp.m[1][3] + worldPos.z * vp.m[2][3] + vp.m[3][3];
        if (std::abs(w) < 1e-6f) return { -1, -1 };
        float ndcX = x / w;
        float ndcY = y / w;
        float sx = (ndcX + 1.0f) * 0.5f * screenW;
        float sy = (1.0f - ndcY) * 0.5f * screenH;
        return { sx, sy };
    }

    // Compute distance from a 2D point to a 2D line segment (A->B)
    static float PointToSegmentDist2D(const Vec2& p, const Vec2& a, const Vec2& b)
    {
        Vec2 ab = b - a;
        Vec2 ap = p - a;
        float abLenSq = ab.x * ab.x + ab.y * ab.y;
        if (abLenSq < 1e-6f) // degenerate segment
        {
            return std::sqrt(ap.x * ap.x + ap.y * ap.y);
        }
        float t = (ap.x * ab.x + ap.y * ab.y) / abLenSq;
        t = std::max(0.0f, std::min(1.0f, t));
        Vec2 closest = { a.x + ab.x * t, a.y + ab.y * t };
        Vec2 diff = { p.x - closest.x, p.y - closest.y };
        return std::sqrt(diff.x * diff.x + diff.y * diff.y);
    }

    EGizmoAxis PickGizmoAxis(int mouseX, int mouseY)
    {
        SceneObject* sel = m_Scene.GetSelectedObject();
        if (!sel) return EGizmoAxis::None;

        uint32_t w = GetWindow()->GetWidth();
        uint32_t h = GetWindow()->GetHeight();

        Vec3 gizmoPos = sel->GetPosition();
        float gizmoScale = ComputeGizmoScale(gizmoPos);
        float gizmoLength = 1.2f * gizmoScale;
        float ringRadius  = 1.0f * gizmoScale;

        Vec2 originSS = WorldToScreen(gizmoPos, w, h, m_ViewMatrix, m_ProjectionMatrix);
        if (originSS.x < 0) return EGizmoAxis::None;

        Vec3 axisWorldDirs[3] = { {1,0,0}, {0,1,0}, {0,0,1} };
        EGizmoAxis axisTypes[3] = { EGizmoAxis::X, EGizmoAxis::Y, EGizmoAxis::Z };

        Vec2 mousePos = { (float)mouseX, (float)mouseY };
        const float pickThresh = 14.0f;

        float closestDist = 1e30f;
        EGizmoAxis result = EGizmoAxis::None;

        if (m_GizmoMode == EGizmoMode::Translate || m_GizmoMode == EGizmoMode::Scale)
        {
            // Line segment distance (same for both)
            for (int i = 0; i < 3; i++)
            {
                Vec3 tipWorld = gizmoPos + axisWorldDirs[i] * gizmoLength;
                Vec2 tipSS = WorldToScreen(tipWorld, w, h, m_ViewMatrix, m_ProjectionMatrix);
                if (tipSS.x < 0) continue;

                float dist = PointToSegmentDist2D(mousePos, originSS, tipSS);
                if (dist < pickThresh && dist < closestDist)
                {
                    closestDist = dist;
                    result = axisTypes[i];
                }
            }
        }
        else if (m_GizmoMode == EGizmoMode::Rotate)
        {
            // Sample ring circumference: project ~32 points, find closest to mouse
            for (int i = 0; i < 3; i++)
            {
                Vec3 N = axisWorldDirs[i];
                // Build ring plane basis
                Vec3 T;
                if (std::abs(N.y) < 0.99f) T = Vec3(0,1,0).Cross(N).Normalize();
                else                        T = Vec3(1,0,0).Cross(N).Normalize();
                Vec3 B = N.Cross(T).Normalize();

                const int kSamples = 40;
                float minDist = 1e30f;
                for (int s = 0; s < kSamples; s++)
                {
                    float phi = (float)s / kSamples * 2.0f * PI;
                    Vec3 pt = gizmoPos + (T * cosf(phi) + B * sinf(phi)) * ringRadius;
                    Vec2 ptSS = WorldToScreen(pt, w, h, m_ViewMatrix, m_ProjectionMatrix);
                    if (ptSS.x < 0) continue;
                    Vec2 diff = { mousePos.x - ptSS.x, mousePos.y - ptSS.y };
                    float d = sqrtf(diff.x*diff.x + diff.y*diff.y);
                    if (d < minDist) minDist = d;
                }
                if (minDist < pickThresh && minDist < closestDist)
                {
                    closestDist = minDist;
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

            // Debug names include object name for RenderDoc identification
            std::string vbName = "MeshVB_" + obj.Name;
            std::string ibName = "MeshIB_" + obj.Name;

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
        }
    }

    // ============================================================
    // Members
    // ============================================================

    Scene m_Scene;
    EditorInput m_EditorInput;
    std::vector<GPUMeshData> m_GPUMeshes;
    std::vector<RenderItem> m_RenderList; // Sorted visible objects from InitView()

    // Shader Library — manages all loaded shaders
    ShaderLibrary m_ShaderLibrary;
    TextureManager m_TextureManager;
    MaterialLibrary m_MaterialLibrary;
    std::string m_ShaderDir; // Path to Shaders/ folder
    std::string m_ScenesDir; // Path to Scenes/ folder
    std::string m_TexturesDir; // Path to Textures/ folder
    std::string m_MaterialsDir; // Path to Materials/ folder
    std::string m_PostProcessShaderDir;
    std::string m_GLShaderDir;

    // Content Browser state
    bool m_ShowContentBrowser = false;
    std::string m_ContentBrowserSelectedDir; // Currently selected folder in tree

    // Material Editor state
    bool m_ShowMaterialEditor = false;
    std::string m_MaterialEditorTarget;  // Name of material being edited

    // Texture picker popup state (used by material editor and inspector)
    bool m_ShowTexturePicker = false;
    std::string m_TexturePickerPropKey;   // Which material property to set (e.g. "_BaseColorTex")
    std::string m_TexturePickerMatTarget; // Which material to write to

    // Save Scene dialog state
    bool m_ShowSaveDialog = false;
    char m_SaveSceneName[128] = {};
    std::string m_LastWindowTitle; // Track to avoid redundant SetWindowText

    // RHI resources (shared interface)
    std::unique_ptr<RHIInputLayout>   m_InputLayout;
    std::unique_ptr<RHIBuffer>        m_ViewUB;      // b0: ViewUniformBuffer (per-frame)
    std::unique_ptr<RHIBuffer>        m_ObjectUB;    // b1: ObjectUniformBuffer (per-draw)
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
    EGizmoMode m_GizmoMode = EGizmoMode::Translate;     // Active gizmo mode

    GizmoMeshData m_GizmoMeshData[3];                    // CPU mesh data for 3 axes (translate)
    std::unique_ptr<RHIBuffer> m_GizmoVB[3];             // GPU vertex buffers
    std::unique_ptr<RHIBuffer> m_GizmoIB[3];             // GPU index buffers
    uint32_t m_GizmoVertexCount[3] = {};
    uint32_t m_GizmoIndexCount[3] = {};

    // Rotate gizmo rings (one per axis)
    GizmoMeshData m_GizmoRingData[3];
    std::unique_ptr<RHIBuffer> m_GizmoRingVB[3];
    std::unique_ptr<RHIBuffer> m_GizmoRingIB[3];
    uint32_t m_GizmoRingVertexCount[3] = {};
    uint32_t m_GizmoRingIndexCount[3] = {};

    // Scale gizmo axes
    GizmoMeshData m_GizmoScaleData[3];
    std::unique_ptr<RHIBuffer> m_GizmoScaleVB[3];
    std::unique_ptr<RHIBuffer> m_GizmoScaleIB[3];
    uint32_t m_GizmoScaleVertexCount[3] = {};
    uint32_t m_GizmoScaleIndexCount[3] = {};

    // Directional light direction indicator
    GizmoMeshData m_DirLightIndicator;                   // CPU mesh data (yellow arrow)
    std::unique_ptr<RHIBuffer> m_DirLightIndicatorVB;    // GPU vertex buffer
    std::unique_ptr<RHIBuffer> m_DirLightIndicatorIB;    // GPU index buffer
    uint32_t m_DirLightIndicatorVertexCount = 0;
    uint32_t m_DirLightIndicatorIndexCount = 0;

    // Dragging state
    bool m_IsDragging = false;
    EGizmoAxis m_DragAxis = EGizmoAxis::None;
    int m_DragStartMouseX = 0;
    int m_DragStartMouseY = 0;
    Vec3 m_DragStartPos;            // Translate: object position at drag start
    Vec3 m_DragStartScale;          // Scale: object scale at drag start
    Vec3 m_DragStartRotation;       // Rotate: object rotation (Euler) at drag start
    float m_DragStartAngle = 0.0f;  // Rotate: projected angle at drag start (radians)

    // ---- Post-Process Resources ----
    PostProcessShaderLibrary m_PostProcessLibrary;

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

    // ---- Cascaded Shadow Map (CSM) Resources — Single Atlas ----
    static constexpr int MAX_SHADOW_CASCADES = 4;
    std::unique_ptr<RHITexture>     m_ShadowAtlasRT;       // Single atlas texture (2*size x 2*size)
    std::unique_ptr<RHITextureView> m_ShadowAtlasDSV;      // DSV for the whole atlas
    std::unique_ptr<RHITextureView> m_ShadowAtlasSRV;      // SRV for sampling in lighting pass
    uint32_t m_ShadowCascadeSize = 0;                      // Per-cascade resolution (e.g. 2048)

    // Shadow pass shader and PSO
    std::unique_ptr<RHIShader> m_ShadowPassVS;
    std::unique_ptr<RHIPipelineState> m_ShadowPassPSO;

    // Shadow uniform buffer (b2)
    std::unique_ptr<RHIBuffer> m_ShadowCB;

    // Comparison sampler for DX11 shadow sampling
    std::unique_ptr<RHISampler> m_ShadowSampler;

    // Cached CSM data (computed each frame)
    ShadowUniformBuffer m_ShadowUBData = {};
    Mat4 m_LightViewProjMatrices[MAX_SHADOW_CASCADES];
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
        std::cout << "  RHI: DX11 / DX12 / OpenGL / Vulkan" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << std::endl;

        // Load engine configuration
        auto& config = Kiwi::EngineConfig::Get();
        config.LoadDefaultConfig();

        // Initialize RenderDoc BEFORE creating any graphics device
        // Note: RenderDoc's Vulkan hook conflicts with NVIDIA drivers (nvoglv64.dll)
        // when both OpenGL and Vulkan are used in the same process.
        // Skip RenderDoc if default RHI is Vulkan.
        std::string defaultRHI = config.GetString("Rendering", "DefaultRHI", "DX11");
        bool isVulkanDefault = (defaultRHI == "VULKAN" || defaultRHI == "Vulkan" || defaultRHI == "vulkan");

        auto& rdoc = Kiwi::RenderDocIntegration::Get();
        if (!isVulkanDefault)
        {
            bool rdocAvailable = rdoc.Initialize();
            if (rdocAvailable)
            {
                std::cout << "[Kiwi] RenderDoc attached - frame capture available." << std::endl;
                std::cout << "[Kiwi] Note: Vulkan backend disabled when RenderDoc is active." << std::endl;
            }
        }
        else
        {
            std::cout << "[Kiwi] Vulkan default RHI - RenderDoc skipped (incompatible)." << std::endl;
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
