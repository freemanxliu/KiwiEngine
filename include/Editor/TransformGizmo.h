#pragma once

#include "Editor/GizmoMesh.h"
#include "Math/Math.h"
#include "RHI/RHI.h"

#include <cstdint>
#include <memory>

namespace Kiwi
{

struct SceneObject;

enum class EGizmoAxis { None = 0, X, Y, Z };
enum class EGizmoMode { Translate = 0, Rotate, Scale };

struct GizmoViewInfo
{
    Mat4 View;
    Mat4 Projection;
    Vec3 CameraPosition;
    uint32_t ScreenWidth = 0;
    uint32_t ScreenHeight = 0;
};

// Translate / rotate / scale handles for the selected object.
// Owns the handle geometry and the drag state; the caller decides which object is the target.
class TransformGizmo
{
public:
    TransformGizmo();

    void CreateGPUResources(RHIDevice* device);
    void ReleaseGPUResources();

    EGizmoMode GetMode() const { return m_Mode; }
    void SetMode(EGizmoMode mode) { m_Mode = mode; }
    bool IsDragging() const { return m_IsDragging; }

    // Returns true if the click hit a handle and a drag started.
    bool TryBeginDrag(SceneObject& target, int mouseX, int mouseY, const GizmoViewInfo& view);
    void UpdateDrag(SceneObject& target, int mouseX, int mouseY, const GizmoViewInfo& view);
    void EndDrag();

    // Expects the gizmo shader, input layout and b0 view buffer to be bound already.
    void Draw(RHICommandContext* ctx, SceneObject& target, const Vec3& cameraPosition, RHIBuffer* objectUB) const;

private:
    struct GPUMesh
    {
        std::unique_ptr<RHIBuffer> VertexBuffer;
        std::unique_ptr<RHIBuffer> IndexBuffer;
        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
    };

    static GPUMesh UploadMesh(RHIDevice* device, const GizmoMeshData& data, const char* namePrefix, const char* nameSuffix);
    static float ComputeScale(const Vec3& gizmoPos, const Vec3& cameraPosition);
    EGizmoAxis PickAxis(const Vec3& gizmoPos, int mouseX, int mouseY, const GizmoViewInfo& view) const;

    EGizmoMode m_Mode = EGizmoMode::Translate;

    GizmoMeshData m_TranslateMeshes[3];
    GizmoMeshData m_RotateMeshes[3];
    GizmoMeshData m_ScaleMeshes[3];
    GizmoMeshData m_DirLightIndicatorMesh;

    GPUMesh m_TranslateGPU[3];
    GPUMesh m_RotateGPU[3];
    GPUMesh m_ScaleGPU[3];
    GPUMesh m_DirLightIndicatorGPU;

    bool m_IsDragging = false;
    EGizmoAxis m_DragAxis = EGizmoAxis::None;
    int m_DragStartMouseX = 0;
    int m_DragStartMouseY = 0;
    Vec3 m_DragStartPos;
    Vec3 m_DragStartRotation;
    Vec3 m_DragStartScale;
};

} // namespace Kiwi
